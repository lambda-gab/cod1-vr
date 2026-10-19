// Post-processing of the eye images: the game draws with fixed-function OpenGL, but its context
// runs GLSL fine, so after each frame the two eye images get, in place:
//   * bloom: the bright parts, at a quarter of the size, blurred and added back softly;
//   * sharpening: contrast-adaptive (the idea of AMD's CAS), which gives the old textures their
//     definition back without halos;
//   * a colour style: saturation, contrast and a tint.
//   * ambient occlusion (SSAO): contact shadows where surfaces meet, from each eye's depth.
// Every GL state touched is saved and restored; if the shaders cannot be built, nothing is done.
#define _CRT_SECURE_NO_WARNINGS
#include "cod_vr.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef GLuint (WINAPI *PFN_CreateShader)(GLenum);
typedef void   (WINAPI *PFN_ShaderSource)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void   (WINAPI *PFN_CompileShader)(GLuint);
typedef void   (WINAPI *PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void   (WINAPI *PFN_GetInfoLog)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (WINAPI *PFN_CreateProgram)(void);
typedef void   (WINAPI *PFN_AttachShader)(GLuint, GLuint);
typedef void   (WINAPI *PFN_LinkProgram)(GLuint);
typedef void   (WINAPI *PFN_UseProgram)(GLuint);
typedef GLint  (WINAPI *PFN_GetUniformLocation)(GLuint, const char *);
typedef void   (WINAPI *PFN_Uniform1i)(GLint, GLint);
typedef void   (WINAPI *PFN_Uniform1f)(GLint, GLfloat);
typedef void   (WINAPI *PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void   (WINAPI *PFN_Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);

static struct {
    PFN_CreateShader CreateShader; PFN_ShaderSource ShaderSource; PFN_CompileShader CompileShader;
    PFN_GetShaderiv GetShaderiv; PFN_GetInfoLog GetShaderInfoLog; PFN_CreateProgram CreateProgram;
    PFN_AttachShader AttachShader; PFN_LinkProgram LinkProgram; PFN_GetShaderiv GetProgramiv;
    PFN_GetInfoLog GetProgramInfoLog; PFN_UseProgram UseProgram; PFN_GetUniformLocation GetUniformLocation;
    PFN_Uniform1i Uniform1i; PFN_Uniform1f Uniform1f; PFN_Uniform2f Uniform2f; PFN_Uniform3f Uniform3f;
} sh;

enum { PROG_FINAL, PROG_BRIGHT, PROG_BLUR, PROG_AO, PROG_AOBLUR, PROG_AOMUL, PROG_COUNT };
static GLuint g_prog[PROG_COUNT];
static HGLRC  g_progCtx, g_bufCtx;
static int    g_failed;
static GLuint g_fbo[3], g_tex[3];   // 0 = copy of the eye (full size), 1 / 2 = bloom (quarter size)
static int    g_w, g_h;

static const char *VS =
    "varying vec2 uv;\n"
    "void main() { uv = gl_Vertex.xy * 0.5 + 0.5; gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0); }\n";

// bright parts, sampled 4 times from the full image into the quarter-size one
static const char *FS_BRIGHT =
    "uniform sampler2D img; uniform vec2 texel; uniform float thr; varying vec2 uv;\n"
    "vec3 bright(vec2 p) {\n"
    "  vec3 c = texture2D(img, p).rgb; float l = max(c.r, max(c.g, c.b));\n"
    "  return c * (max(l - thr, 0.0) / max(1.0 - thr, 0.001)) / max(l, 0.001);\n"
    "}\n"
    "void main() {\n"
    "  vec3 c = bright(uv + texel * vec2(-1.0, -1.0)) + bright(uv + texel * vec2(1.0, -1.0))\n"
    "         + bright(uv + texel * vec2(-1.0, 1.0)) + bright(uv + texel * vec2(1.0, 1.0));\n"
    "  gl_FragColor = vec4(c * 0.25, 1.0);\n"
    "}\n";

// 9-tap gaussian along dir (one texel of the quarter-size image)
static const char *FS_BLUR =
    "uniform sampler2D img; uniform vec2 dir; varying vec2 uv;\n"
    "void main() {\n"
    "  vec3 c = texture2D(img, uv).rgb * 0.2270;\n"
    "  c += (texture2D(img, uv + dir * 1.3846).rgb + texture2D(img, uv - dir * 1.3846).rgb) * 0.3162;\n"
    "  c += (texture2D(img, uv + dir * 3.2308).rgb + texture2D(img, uv - dir * 3.2308).rgb) * 0.0703;\n"
    "  gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

// sharpening (contrast adaptive), bloom, colour
static const char *FS_FINAL =
    "uniform sampler2D img; uniform sampler2D bloom; uniform vec2 texel;\n"
    "uniform float sharp, bloomAmt, contrast, sat; uniform vec3 tint; varying vec2 uv;\n"
    "void main() {\n"
    "  vec3 c = texture2D(img, uv).rgb;\n"
    "  if (sharp > 0.0) {\n"
    "    vec3 n = texture2D(img, uv + vec2(0.0, texel.y)).rgb, s = texture2D(img, uv - vec2(0.0, texel.y)).rgb;\n"
    "    vec3 e = texture2D(img, uv + vec2(texel.x, 0.0)).rgb, w = texture2D(img, uv - vec2(texel.x, 0.0)).rgb;\n"
    "    vec3 mn = min(c, min(min(n, s), min(e, w))), mx = max(c, max(max(n, s), max(e, w)));\n"
    "    vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0));\n"
    "    vec3 wt = amp * (-1.0 / mix(8.0, 5.0, sharp));\n"
    "    c = clamp((c + (n + s + e + w) * wt) / (1.0 + 4.0 * wt), 0.0, 1.0);\n"
    "  }\n"
    "  c += texture2D(bloom, uv).rgb * bloomAmt;\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  c = mix(vec3(l), c, sat);\n"
    "  c = (c - 0.5) * contrast + 0.5;\n"
    "  gl_FragColor = vec4(clamp(c * tint, 0.0, 1.0), 1.0);\n"
    "}\n";

// ---- ambient occlusion. Depth d (0..1, the world drawn with glDepthRange(0, 1); the view weapon
// uses 0..0.1 and is left out) to distance with the world's projection: P14 / (2d - 1 + P10).
static const char *AO_COMMON =
    "uniform sampler2D depth; uniform vec4 fov; uniform vec2 proj;\n"
    "float dist(float d) { return proj.y / (2.0 * d - 1.0 + proj.x); }\n"
    "vec3 viewPos(vec2 p, float d) { float z = dist(d); return vec3(mix(fov.x, fov.y, p.x) * z, mix(fov.z, fov.w, p.y) * z, z); }\n";

// half size: 8 samples in the hemisphere around the normal, the pattern turned per pixel in 4x4
static const char *FS_AO =
    "uniform float radius; varying vec2 uv;\n"
    "void main() {\n"
    "  float d = texture2D(depth, uv).r;\n"
    "  if (d > 0.9999 || d < 0.1) { gl_FragColor = vec4(1.0); return; }\n"
    "  vec3 P = viewPos(uv, d);\n"
    "  if (P.z > 2400.0) { gl_FragColor = vec4(1.0); return; }\n"
    "  vec3 N = normalize(cross(dFdx(P), dFdy(P)));\n"
    "  if (dot(N, P) > 0.0) N = -N;\n"
    "  vec2 ip = mod(floor(gl_FragCoord.xy), 4.0);\n"
    "  float rot = (ip.x * 4.0 + ip.y) * 0.3927 + 0.2;\n"
    "  float occ = 0.0;\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    float fi = float(i), z = (fi + 0.5) / 8.0, r = sqrt(1.0 - z * z), a = fi * 2.39996 + rot;\n"
    "    float len = mix(0.2, 1.0, (fi + 1.0) * (fi + 1.0) / 64.0);\n"
    "    vec3 v = vec3(cos(a) * r, sin(a) * r, z) * len;\n"
    "    if (dot(v, N) < 0.0) v = -v;\n"
    "    vec3 S = P + v * radius;\n"
    "    if (S.z <= 1.0) continue;\n"
    "    vec2 su = vec2((S.x / S.z - fov.x) / (fov.y - fov.x), (S.y / S.z - fov.z) / (fov.w - fov.z));\n"
    "    float sd = texture2D(depth, su).r;\n"
    "    if (sd < 0.1) continue;\n"
    "    float sz = dist(sd);\n"
    "    float range = clamp(radius / max(abs(P.z - sz), 0.001), 0.0, 1.0);\n"
    "    if (sz < S.z - 0.5 - S.z * 0.002) occ += range;\n"
    "  }\n"
    // contact shadows are a close-range thing; far away the 24-bit depth is too coarse (the sky
    // and the horizon came out black): fade out from about 30 m to 60 m
    "  float fade = 1.0 - smoothstep(1200.0, 2400.0, P.z);\n"
    "  gl_FragColor = vec4(vec3(1.0 - occ / 8.0 * fade), 1.0);\n"
    "}\n";

// half size: 4x4 average that keeps to one surface (weights fall with the depth difference)
static const char *FS_AOBLUR =
    "uniform sampler2D ao; uniform vec2 texel; varying vec2 uv;\n"
    "void main() {\n"
    "  float c = dist(texture2D(depth, uv).r), sum = 0.0, wsum = 0.0;\n"
    "  for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) {\n"
    "    vec2 p = uv + (vec2(float(x), float(y)) - 1.5) * texel;\n"
    "    float z = dist(texture2D(depth, p).r);\n"
    "    float w = 1.0 / (1.0 + abs(z - c) / max(c, 1.0) * 40.0);\n"
    "    sum += texture2D(ao, p).r * w; wsum += w;\n"
    "  }\n"
    "  gl_FragColor = vec4(vec3(sum / wsum), 1.0);\n"
    "}\n";

// full size, multiplied over the eye image by blending. The half-size result is brought up with
// the 4 nearest values weighted by how close their depth is to this pixel's, so it does not bleed
// across edges (a light outline around trees against the sky).
static const char *FS_AOMUL =
    "uniform sampler2D ao; uniform vec2 htexel; uniform float strength; varying vec2 uv;\n"
    "void main() {\n"
    "  float d = texture2D(depth, uv).r;\n"
    "  if (d > 0.9999 || d < 0.1) { gl_FragColor = vec4(1.0); return; }\n"
    "  float c = dist(d), sum = 0.0, wsum = 0.0;\n"
    "  vec2 f = uv / htexel - 0.5, base = (floor(f) + 0.5) * htexel, t = fract(f);\n"
    "  for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {\n"
    "    vec2 p = base + vec2(float(i), float(j)) * htexel;\n"
    "    float bw = (i == 0 ? 1.0 - t.x : t.x) * (j == 0 ? 1.0 - t.y : t.y);\n"
    "    float z = dist(texture2D(depth, p).r);\n"
    "    float w = (bw + 0.001) / (1.0 + abs(z - c) / max(c, 1.0) * 80.0);\n"
    "    sum += texture2D(ao, p).r * w; wsum += w;\n"
    "  }\n"
    "  gl_FragColor = vec4(vec3(mix(1.0, sum / wsum, strength)), 1.0);\n"
    "}\n";

static PROC GetProc(const char *name) { return gl.GetProcAddress(name); }

static GLuint Compile(GLenum type, const char *src)
{
    GLuint s = sh.CreateShader(type);
    GLint ok = 0;
    sh.ShaderSource(s, 1, &src, NULL);
    sh.CompileShader(s);
    sh.GetShaderiv(s, 0x8B81, &ok);   // GL_COMPILE_STATUS
    if (!ok) {
        char log[512] = "";
        sh.GetShaderInfoLog(s, sizeof(log), NULL, log);
        Log("postfx: shader does not compile: %s", log);
        return 0;
    }
    return s;
}

static GLuint Link2(const char *common, const char *fs);

static GLuint Link(const char *fs) { return Link2(NULL, fs); }

static GLuint Link2(const char *common, const char *fs)
{
    char src[8192];
    GLuint v, f, p;
    _snprintf(src, sizeof(src), "%s%s", common ? common : "", fs);
    src[sizeof(src) - 1] = 0;
    v = Compile(0x8B31, VS); f = Compile(0x8B30, src);   // vertex, fragment
    GLint ok = 0;
    if (!v || !f) return 0;
    p = sh.CreateProgram();
    sh.AttachShader(p, v);
    sh.AttachShader(p, f);
    sh.LinkProgram(p);
    sh.GetProgramiv(p, 0x8B82, &ok);   // GL_LINK_STATUS
    if (!ok) {
        char log[512] = "";
        sh.GetProgramInfoLog(p, sizeof(log), NULL, log);
        Log("postfx: program does not link: %s", log);
        return 0;
    }
    return p;
}

static int Init(void)
{
    HGLRC ctx = gl.GetCurrentContext();
    if (g_failed) return 0;
    if (g_prog[0] && ctx == g_progCtx) return 1;
#define GET(field, name) if (!(sh.field = (void *)GetProc(name))) { Log("postfx: %s missing, off", name); g_failed = 1; return 0; }
    GET(CreateShader, "glCreateShader") GET(ShaderSource, "glShaderSource") GET(CompileShader, "glCompileShader")
    GET(GetShaderiv, "glGetShaderiv") GET(GetShaderInfoLog, "glGetShaderInfoLog") GET(CreateProgram, "glCreateProgram")
    GET(AttachShader, "glAttachShader") GET(LinkProgram, "glLinkProgram") GET(GetProgramiv, "glGetProgramiv")
    GET(GetProgramInfoLog, "glGetProgramInfoLog") GET(UseProgram, "glUseProgram")
    GET(GetUniformLocation, "glGetUniformLocation") GET(Uniform1i, "glUniform1i") GET(Uniform1f, "glUniform1f")
    GET(Uniform2f, "glUniform2f") GET(Uniform3f, "glUniform3f")
#undef GET
    if (!gl.ActiveTexture) { Log("postfx: no multitexture, off"); g_failed = 1; return 0; }
    g_prog[PROG_FINAL] = Link(FS_FINAL);
    g_prog[PROG_BRIGHT] = Link(FS_BRIGHT);
    g_prog[PROG_BLUR] = Link(FS_BLUR);
    g_prog[PROG_AO] = Link2(AO_COMMON, FS_AO);
    g_prog[PROG_AOBLUR] = Link2(AO_COMMON, FS_AOBLUR);
    g_prog[PROG_AOMUL] = Link2(AO_COMMON, FS_AOMUL);
    if (!g_prog[PROG_FINAL] || !g_prog[PROG_BRIGHT] || !g_prog[PROG_BLUR] ||
        !g_prog[PROG_AO] || !g_prog[PROG_AOBLUR] || !g_prog[PROG_AOMUL]) {
        g_prog[0] = 0;
        g_failed = 1;
        return 0;
    }
    g_progCtx = ctx;
    Log("postfx: shaders ready");
    return 1;
}

static void InitTex(GLuint t, int w, int h)
{
    gl.BindTexture(GL_TEXTURE_2D, t);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

static int Buffers(int w, int h)
{
    HGLRC ctx = gl.GetCurrentContext();
    int i, ok = 1;
    GLint prevTex = 0;
    if (g_fbo[0] && ctx == g_bufCtx && w == g_w && h == g_h) return 1;
    // the game caches which texture is bound: give it back its binding afterwards
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    if (g_fbo[0] && ctx == g_bufCtx) { gl.DeleteFramebuffers(3, g_fbo); gl.DeleteTextures(3, g_tex); }
    gl.GenTextures(3, g_tex);
    gl.GenFramebuffers(3, g_fbo);
    for (i = 0; i < 3; i++) {
        InitTex(g_tex[i], i ? (w + 3) / 4 : w, i ? (h + 3) / 4 : h);
        gl.BindFramebuffer(GL_FRAMEBUFFER, g_fbo[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex[i], 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (!ok) {
        gl.DeleteFramebuffers(3, g_fbo); gl.DeleteTextures(3, g_tex);
        g_fbo[0] = 0;
        Log("postfx: buffers incomplete, off");
        g_failed = 1;
        return 0;
    }
    g_w = w; g_h = h; g_bufCtx = ctx;
    return 1;
}

static void Quad(void)
{
    gl.Begin(0x0007);   // GL_QUADS
    gl.Vertex3f(-1, -1, 0); gl.Vertex3f(1, -1, 0); gl.Vertex3f(1, 1, 0); gl.Vertex3f(-1, 1, 0);
    gl.End();
}

static void Bind(int unit, GLuint tex)
{
    gl.ActiveTexture(0x84C0 + unit);   // GL_TEXTURE0 + unit
    gl.BindTexture(GL_TEXTURE_2D, tex);
}

void PostFX_Apply(const GLuint eyeFbo[2], const GLuint eyeTex[2], int w, int h, const postfx_t *fx)
{
    GLint prog = 0, active = 0x84C0;
    int eye, i, qw = (w + 3) / 4, qh = (h + 3) / 4;
    int bloom = fx->bloom > 0.001f;
    (void)eyeTex;
    if (fx->sharpen <= 0.001f && !bloom && fx->contrast == 1.0f && fx->saturation == 1.0f &&
        fx->tint[0] == 1.0f && fx->tint[1] == 1.0f && fx->tint[2] == 1.0f)
        return;
    if (!Init() || !Buffers(w, h)) return;

    gl.GetIntegerv(0x8B8D, &prog);       // GL_CURRENT_PROGRAM: the game draws with none
    gl.GetIntegerv(0x84E0, &active);     // GL_ACTIVE_TEXTURE
    gl.PushAttrib(0x000FFFFF);           // GL_ALL_ATTRIB_BITS (bindings, enables, viewport...)
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(0x0BE2); gl.Disable(0x0B71); gl.Disable(0x0BC0); gl.Disable(0x0B44);   // blend, depth, alpha test, cull
    gl.Disable(0x0B60); gl.Disable(0x0B50); gl.Disable(0x0B90);                       // fog, lighting, stencil
    for (i = 3; i >= 0; i--) { gl.ActiveTexture(0x84C0 + i); gl.Disable(GL_TEXTURE_2D); }
    gl.ColorMask(1, 1, 1, 1);
    gl.DepthMask(0);

    for (eye = 0; eye < 2; eye++) {
        GL_Blit(eyeFbo[eye], w, h, g_fbo[0], w, h);   // a copy to read from
        if (bloom) {
            sh.UseProgram(g_prog[PROG_BRIGHT]);
            sh.Uniform1i(sh.GetUniformLocation(g_prog[PROG_BRIGHT], "img"), 0);
            sh.Uniform2f(sh.GetUniformLocation(g_prog[PROG_BRIGHT], "texel"), 1.0f / w, 1.0f / h);
            sh.Uniform1f(sh.GetUniformLocation(g_prog[PROG_BRIGHT], "thr"), fx->bloomThreshold);
            gl.BindFramebuffer(GL_FRAMEBUFFER, g_fbo[1]);
            gl.Viewport(0, 0, qw, qh);
            Bind(0, g_tex[0]);
            Quad();
            sh.UseProgram(g_prog[PROG_BLUR]);
            sh.Uniform1i(sh.GetUniformLocation(g_prog[PROG_BLUR], "img"), 0);
            for (i = 0; i < 2; i++) {   // horizontal into 2, vertical back into 1
                sh.Uniform2f(sh.GetUniformLocation(g_prog[PROG_BLUR], "dir"), i ? 0.0f : 1.0f / qw, i ? 1.0f / qh : 0.0f);
                gl.BindFramebuffer(GL_FRAMEBUFFER, g_fbo[i ? 1 : 2]);
                Bind(0, g_tex[i ? 2 : 1]);
                Quad();
            }
        }
        sh.UseProgram(g_prog[PROG_FINAL]);
        sh.Uniform1i(sh.GetUniformLocation(g_prog[PROG_FINAL], "img"), 0);
        sh.Uniform1i(sh.GetUniformLocation(g_prog[PROG_FINAL], "bloom"), 1);
        sh.Uniform2f(sh.GetUniformLocation(g_prog[PROG_FINAL], "texel"), 1.0f / w, 1.0f / h);
        sh.Uniform1f(sh.GetUniformLocation(g_prog[PROG_FINAL], "sharp"), fx->sharpen);
        sh.Uniform1f(sh.GetUniformLocation(g_prog[PROG_FINAL], "bloomAmt"), bloom ? fx->bloom : 0.0f);
        sh.Uniform1f(sh.GetUniformLocation(g_prog[PROG_FINAL], "contrast"), fx->contrast);
        sh.Uniform1f(sh.GetUniformLocation(g_prog[PROG_FINAL], "sat"), fx->saturation);
        sh.Uniform3f(sh.GetUniformLocation(g_prog[PROG_FINAL], "tint"), fx->tint[0], fx->tint[1], fx->tint[2]);
        gl.BindFramebuffer(GL_FRAMEBUFFER, eyeFbo[eye]);
        gl.Viewport(0, 0, w, h);
        Bind(1, g_tex[1]);
        Bind(0, g_tex[0]);
        Quad();
    }

    sh.UseProgram((GLuint)prog);
    gl.PopAttrib();
    gl.ActiveTexture((GLenum)active);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// Ambient occlusion
// ---------------------------------------------------------------------------
static GLuint g_depthTex[2], g_depthFbo[2], g_aoTex[2], g_aoFbo[2];
static int    g_dw, g_dh;
static HGLRC  g_depthCtx;
static int    g_depthOk;

static int DepthBuffers(int w, int h)
{
    HGLRC ctx = gl.GetCurrentContext();
    int i, ok = 1, qw = (w + 1) / 2, qh = (h + 1) / 2;
    if (g_depthFbo[0] && ctx == g_depthCtx && w == g_dw && h == g_dh) return g_depthOk;
    if (g_depthFbo[0] && ctx == g_depthCtx) {
        gl.DeleteFramebuffers(2, g_depthFbo); gl.DeleteTextures(2, g_depthTex);
        gl.DeleteFramebuffers(2, g_aoFbo); gl.DeleteTextures(2, g_aoTex);
    }
    gl.GenTextures(2, g_depthTex); gl.GenFramebuffers(2, g_depthFbo);
    gl.GenTextures(2, g_aoTex); gl.GenFramebuffers(2, g_aoFbo);
    for (i = 0; i < 2; i++) {
        gl.BindTexture(GL_TEXTURE_2D, g_depthTex[i]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2600);   // GL_NEAREST
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, 0x2600);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, 0x884C, 0);                        // GL_TEXTURE_COMPARE_MODE: none
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, 0x84F9, 0x84FA, NULL);   // DEPTH_STENCIL, UNSIGNED_INT_24_8
        gl.BindFramebuffer(GL_FRAMEBUFFER, g_depthFbo[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, 0x821A, GL_TEXTURE_2D, g_depthTex[i], 0);   // DEPTH_STENCIL_ATTACHMENT
        gl.DrawBuffer(0); gl.ReadBuffer(0);                                 // no colour
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
        InitTex(g_aoTex[i], qw, qh);
        gl.BindFramebuffer(GL_FRAMEBUFFER, g_aoFbo[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_aoTex[i], 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    g_dw = w; g_dh = h; g_depthCtx = ctx; g_depthOk = ok;
    if (!ok) Log("postfx: depth buffers incomplete, ambient occlusion off");
    return ok;
}

// Copies the depth of an eye (its drawing buffer, multisampled or not) before anything clears it.
// srcFbo stays bound afterwards.
void PostFX_CaptureDepth(int eye, GLuint srcFbo, int w, int h)
{
    GLboolean scissor;
    GLint tex = 0;
    if (eye < 0 || eye > 1 || g_failed) return;
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    if (!DepthBuffers(w, h)) { gl.BindTexture(GL_TEXTURE_2D, (GLuint)tex); gl.BindFramebuffer(GL_FRAMEBUFFER, srcFbo); return; }
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)tex);
    scissor = gl.IsEnabled(GL_SCISSOR_TEST);
    if (scissor) gl.Disable(GL_SCISSOR_TEST);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, g_depthFbo[eye]);
    gl.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, 0x2600);   // GL_NEAREST
    if (scissor) gl.Enable(GL_SCISSOR_TEST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, srcFbo);
}

void PostFX_SSAO(const GLuint eyeFbo[2], int w, int h, const float tans[2][4], const float proj[2],
                 float strength, float radius)
{
    GLint prog = 0, active = 0x84C0;
    int eye, i, qw = (w + 1) / 2, qh = (h + 1) / 2;
    if (strength <= 0.01f || !g_depthOk || g_dw != w || g_dh != h) return;
    if (!Init()) return;
    gl.GetIntegerv(0x8B8D, &prog);
    gl.GetIntegerv(0x84E0, &active);
    gl.PushAttrib(0x000FFFFF);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(0x0BE2); gl.Disable(0x0B71); gl.Disable(0x0BC0); gl.Disable(0x0B44);
    gl.Disable(0x0B60); gl.Disable(0x0B50); gl.Disable(0x0B90);
    for (i = 3; i >= 0; i--) { gl.ActiveTexture(0x84C0 + i); gl.Disable(GL_TEXTURE_2D); }
    gl.ColorMask(1, 1, 1, 1);
    gl.DepthMask(0);
    for (eye = 0; eye < 2; eye++) {
        const float *t = tans[eye];   // left, right, down, up
        GLuint p;
        // occlusion, half size
        p = g_prog[PROG_AO];
        sh.UseProgram(p);
        sh.Uniform1i(sh.GetUniformLocation(p, "depth"), 0);
        sh.Uniform1f(sh.GetUniformLocation(p, "radius"), radius);
        gl.BindFramebuffer(GL_FRAMEBUFFER, g_aoFbo[0]);
        gl.Viewport(0, 0, qw, qh);
        Bind(0, g_depthTex[eye]);
        {
            GLint loc = sh.GetUniformLocation(p, "fov");
            typedef void (WINAPI *PFN_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
            static PFN_Uniform4f u4;
            if (!u4) u4 = (PFN_Uniform4f)GetProc("glUniform4f");
            if (u4) u4(loc, t[0], t[1], t[2], t[3]);
            sh.Uniform2f(sh.GetUniformLocation(p, "proj"), proj[0], proj[1]);
        }
        Quad();
        // blur, half size
        p = g_prog[PROG_AOBLUR];
        sh.UseProgram(p);
        sh.Uniform1i(sh.GetUniformLocation(p, "depth"), 0);
        sh.Uniform1i(sh.GetUniformLocation(p, "ao"), 1);
        sh.Uniform2f(sh.GetUniformLocation(p, "texel"), 1.0f / qw, 1.0f / qh);
        sh.Uniform2f(sh.GetUniformLocation(p, "proj"), proj[0], proj[1]);
        gl.BindFramebuffer(GL_FRAMEBUFFER, g_aoFbo[1]);
        Bind(1, g_aoTex[0]);
        Bind(0, g_depthTex[eye]);
        Quad();
        // multiply over the eye
        p = g_prog[PROG_AOMUL];
        sh.UseProgram(p);
        sh.Uniform1i(sh.GetUniformLocation(p, "ao"), 1);
        sh.Uniform1i(sh.GetUniformLocation(p, "depth"), 0);
        sh.Uniform2f(sh.GetUniformLocation(p, "htexel"), 1.0f / qw, 1.0f / qh);
        sh.Uniform2f(sh.GetUniformLocation(p, "proj"), proj[0], proj[1]);
        sh.Uniform1f(sh.GetUniformLocation(p, "strength"), strength > 1 ? 1 : strength);
        gl.BindFramebuffer(GL_FRAMEBUFFER, eyeFbo[eye]);
        gl.Viewport(0, 0, w, h);
        gl.Enable(0x0BE2);
        gl.BlendFunc(0, 0x0300);   // GL_ZERO, GL_SRC_COLOR: dst * src
        Bind(1, g_aoTex[1]);
        Bind(0, g_depthTex[eye]);
        Quad();
        gl.Disable(0x0BE2);
    }
    sh.UseProgram((GLuint)prog);
    gl.PopAttrib();
    gl.ActiveTexture((GLenum)active);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}
