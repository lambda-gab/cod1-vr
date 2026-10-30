// Shared declarations between the GL proxy (cod_vr.c) and the OpenXR backend (vr_xr.c).
#pragma once
#include <windows.h>

// ---- minimal GL declarations (gl.h would clash with our exports) ----
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned char GLboolean;

#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_BACK_LEFT                0x0402
#define GL_CW                     0x0900
#define GL_CCW                    0x0901
#define GL_BACK_RIGHT               0x0403
#define GL_FRONT_LEFT               0x0400
#define GL_FRONT_RIGHT              0x0401
#define GL_TEXTURE_2D               0x0DE1
#define GL_RGBA                     0x1908
#define GL_RGBA8                    0x8058
#define GL_SRGB8_ALPHA8             0x8C43
#define GL_UNSIGNED_BYTE            0x1401
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_LINEAR                   0x2601
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_SCISSOR_TEST             0x0C11
#define GL_COLOR_BUFFER_BIT         0x00004000
#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_STENCIL_BUFFER_BIT       0x00000400
#define GL_SCISSOR_BIT              0x00080000
#define GL_TEXTURE_BINDING_2D       0x8069
#define GL_FRAMEBUFFER              0x8D40
#define GL_READ_FRAMEBUFFER         0x8CA8
#define GL_DRAW_FRAMEBUFFER         0x8CA9
#define GL_RENDERBUFFER             0x8D41
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_DEPTH_ATTACHMENT         0x8D00
#define GL_STENCIL_ATTACHMENT       0x8D20
#define GL_DEPTH24_STENCIL8         0x88F0
#define GL_FRAMEBUFFER_COMPLETE     0x8CD5

typedef struct {
    void      (WINAPI *DrawBuffer)(GLenum);
    void      (WINAPI *ReadBuffer)(GLenum);
    BOOL      (WINAPI *MakeCurrent)(HDC, HGLRC);
    PROC      (WINAPI *GetProcAddress)(LPCSTR);
    HGLRC     (WINAPI *GetCurrentContext)(void);
    HDC       (WINAPI *GetCurrentDC)(void);
    void      (WINAPI *Enable)(GLenum);
    void      (WINAPI *Disable)(GLenum);
    GLboolean (WINAPI *IsEnabled)(GLenum);
    void      (WINAPI *GenTextures)(GLsizei, GLuint *);
    void      (WINAPI *DeleteTextures)(GLsizei, const GLuint *);
    void      (WINAPI *BindTexture)(GLenum, GLuint);
    void      (WINAPI *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void      (WINAPI *TexParameteri)(GLenum, GLenum, GLint);
    void      (WINAPI *TexParameterf)(GLenum, GLenum, GLfloat);
    void      (WINAPI *GetFloatv)(GLenum, GLfloat *);
    void      (WINAPI *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    void      (WINAPI *GetIntegerv)(GLenum, GLint *);
    GLenum    (WINAPI *GetError)(void);
    void      (WINAPI *Clear)(GLbitfield);
    void      (WINAPI *ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void      (WINAPI *PushAttrib)(GLbitfield);
    void      (WINAPI *PopAttrib)(void);
    void      (WINAPI *ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void      (WINAPI *DepthMask)(GLboolean);
    void      (WINAPI *MatrixMode)(GLenum);
    void      (WINAPI *LoadMatrixf)(const GLfloat *);
    void      (WINAPI *PushMatrix)(void);
    void      (WINAPI *PopMatrix)(void);
    void      (WINAPI *Begin)(GLenum);
    void      (WINAPI *End)(void);
    void      (WINAPI *Vertex3f)(GLfloat, GLfloat, GLfloat);
    void      (WINAPI *Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void      (WINAPI *LineWidth)(GLfloat);
    void      (WINAPI *PointSize)(GLfloat);
    void      (WINAPI *Viewport)(GLint, GLint, GLsizei, GLsizei);
    void      (WINAPI *DepthRange)(double, double);
    void      (WINAPI *Scissor)(GLint, GLint, GLsizei, GLsizei);
    void      (WINAPI *BlendFunc)(GLenum, GLenum);
    void      (WINAPI *FrontFace)(GLenum);
    void      (WINAPI *ActiveTexture)(GLenum);   // may be NULL
    // framebuffer objects (core 3.0 / ARB / EXT), resolved once a context exists
    void      (WINAPI *GenFramebuffers)(GLsizei, GLuint *);
    void      (WINAPI *DeleteFramebuffers)(GLsizei, const GLuint *);
    void      (WINAPI *BindFramebuffer)(GLenum, GLuint);
    void      (WINAPI *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void      (WINAPI *GenRenderbuffers)(GLsizei, GLuint *);
    void      (WINAPI *DeleteRenderbuffers)(GLsizei, const GLuint *);
    void      (WINAPI *BindRenderbuffer)(GLenum, GLuint);
    void      (WINAPI *RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void      (WINAPI *RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);   // may be NULL
    void      (WINAPI *FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    GLenum    (WINAPI *CheckFramebufferStatus)(GLenum);
    void      (WINAPI *BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
} gl_api_t;

extern gl_api_t gl;
extern char     g_dir[MAX_PATH];
void Log(const char *fmt, ...);

// Copies a w x h color region from one framebuffer to a dw x dh region of another, with scissor off.
void GL_Blit(GLuint srcFbo, int w, int h, GLuint dstFbo, int dw, int dh);
void GL_BlitRect(GLuint srcFbo, int sx, int sy, int sw, int sh, GLuint dstFbo, int dw, int dh);

// postfx.c: sharpening, bloom and a colour style on the eye images
typedef struct {
    float sharpen;          // 0..1
    float bloom;            // strength, 0 = off
    float bloomThreshold;   // brightness where bloom starts, 0..1
    float contrast, saturation, tint[3];
} postfx_t;
void PostFX_Apply(const GLuint eyeFbo[2], const GLuint eyeTex[2], int w, int h, const postfx_t *fx);
void PostFX_CaptureDepth(int eye, GLuint srcFbo, int w, int h);   // leaves srcFbo bound
void PostFX_SSAO(const GLuint eyeFbo[2], int w, int h, const float tans[2][4], const float proj[2],
                 float strength, float radius);

// ---- OpenXR backend (vr_xr.c) ----
// One eye (or the head) in tracking space, converted to Quake 3 conventions:
// x forward, y left, z up, metres. fov tangents follow OpenGL eye space (left/down negative).
typedef struct {
    float pos[3];
    float axis[3][3];   // forward, left, up
    float tanLeft, tanRight, tanUp, tanDown;
} vr_pose_t;

typedef struct {
    int    opaque;       // the image covers the whole quad (zoomed scope view behind the overlay)
    GLuint fbo;          // HUD/menu image (window-sized, transparent where nothing was drawn), 0 = none
    int    world;        // the 3D world was drawn this frame (0 = menu or loading screen)
    int    headLocked;   // 1 = screen follows the head, 0 = fixed in the room
    float  distance;     // metres in front of the eyes
    float  offsetY;      // metres up (negative = down)
    float  width;        // metres, while playing
    float  menuWidth;    // metres, in menus (no world drawn)
    int    posed;        // 1 = placed at pose (tracking space) instead of headLocked / distance / offsetY
    vr_pose_t pose;      // axis[0] points away from the viewer, axis[2] is the screen's up
} vr_hud_t;

// Scope lens: the magnified view rendered into a sub-rectangle of fbo, shown as a small quad.
typedef struct {
    GLuint    fbo;
    int       x, y, size;   // source rectangle (GL coordinates, square)
    int       visible;
    vr_pose_t lens;         // tracking space, Quake 3 conventions; the lens faces back along -forward
    float     width;        // metres
} vr_scope_t;

int  XR_Init(int hudW, int hudH, int scopeSize);      // needs the game's GL context current
void XR_SetRenderScale(float s);                     // eye resolution = the runtime's recommended size x s
int  XR_EyeSize(int *w, int *h);                     // eye swapchain size, 0 before XR_Init
void XR_RecommendedSize(int *w, int *h, int *maxW, int *maxH);   // the runtime's, 0 before XR_Init
void XR_Shutdown(void);                               // same; called before the context goes away
int  XR_Ready(void);                                  // instance + session created
int  XR_BeginFrame(vr_pose_t eyes[2], vr_pose_t *head); // 1 = this frame goes to the headset
void XR_EndFrame(const GLuint eyeFbo[2], int w, int h, int hudW, int hudH, const vr_hud_t *hud, const vr_scope_t *scope); // copies and submits
int  XR_FrameOpen(void);                              // XR_BeginFrame succeeded, XR_EndFrame pending

// ---- controllers ----
#define VRB_A       0x01
#define VRB_B       0x02
#define VRB_X       0x04
#define VRB_Y       0x08
#define VRB_LSTICK  0x10   // left thumbstick click
#define VRB_RSTICK  0x20   // right thumbstick click
#define VRB_MENU    0x40

typedef struct {
    int       active;        // input was read this frame (session focused)
    int       handValid[2];  // 0 = left, 1 = right
    vr_pose_t hand[2];       // aim pose (pointing ray) in tracking space, Quake 3 conventions
    float     trigger[2];    // 0..1
    float     grip[2];       // 0..1
    float     stick[2][2];   // [hand][x right, y forward], -1..1
    unsigned  buttons;       // VRB_* bits
} vr_input_t;

void XR_GetInput(vr_input_t *in);   // after a successful XR_BeginFrame

// Menu pointer: a beam from 'from' to 'to' (tracking space, Quake 3 conventions, metres), drawn
// on top of everything this frame. Cleared by XR_BeginFrame.
void XR_SetLaser(const float from[3], const float to[3]);
// Any polyline (up to 64 points, same space and conventions), with an optional ring of 'ring'
// metres lying flat around the last point. Used for the grenade trajectory.
void XR_SetLine(const float (*pts)[3], int n, const float rgba[4], float ring);
// Three axes of 'len' metres from a point: fwd red, right green, up blue (the in-game menu's weapon adjustment).
void XR_SetAxes(const float c[3], const float fwd[3], const float right[3], const float up[3], float len);
void XR_Haptic(int hand, float amplitude, int ms);   // 0 = left, 1 = right
// crosshair styles: 1 cross, 2 dot, 3 circle, 4 circle + dot, 5 cross + dot
enum { RETICLE_NONE, RETICLE_CROSS, RETICLE_DOT, RETICLE_CIRCLE, RETICLE_CIRCLE_DOT, RETICLE_CROSS_DOT, RETICLE_COUNT };
void XR_RequestDump(const char *dir);
// a progress bar in the world (tracking space, Quake axes): centre, half-width and half-height vectors, 0..1
void XR_SetProgress(const float center[3], const float right[3], const float up[3], float fraction);   // diagnostics: save the left eye as rendered and as sent, once
void XR_SetReticle(int style, const float center[3], const float right[3], const float up[3], const float rgba[4]);
void XR_SetPanel(GLuint fbo, int w, int h, const float pos[3], const float axis[3][3], float width);   // a flat image panel in the room for this frame (beside the menu)

