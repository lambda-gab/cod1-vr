// Exercises vr_xr.c outside the game: GL context on a hidden window, OpenXR session on it,
// left eye red / right eye blue, head pose printed once a second. Runs for N seconds (arg 1).
#define _CRT_SECURE_NO_WARNINGS
#include "../cod_vr.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

gl_api_t gl;
char     g_dir[MAX_PATH];

void Log(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n'); fflush(stdout);
}

void GL_BlitRect(GLuint srcFbo, int sx, int sy, int sw, int sh, GLuint dstFbo, int dw, int dh)
{
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    gl.BlitFramebuffer(sx, sy, sx + sw, sy + sh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void GL_Blit(GLuint srcFbo, int w, int h, GLuint dstFbo, int dw, int dh) { GL_BlitRect(srcFbo, 0, 0, w, h, dstFbo, dw, dh); }

#define R(f, n) gl.f = (void *)GetProcAddress(ogl, n)
#define X(f, n) gl.f = (void *)gl.GetProcAddress(n)

int main(int argc, char **argv)
{
    HMODULE ogl = LoadLibraryA("opengl32.dll");
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32, 0,0,0,0,0,0, 8, 0, 0,0,0,0,0, 24, 8, 0, PFD_MAIN_PLANE };
    HWND wnd; HDC dc; HGLRC rc;
    GLuint fbo[3], tex[3];
    void (WINAPI *ClearColor)(float, float, float, float) = (void *)GetProcAddress(ogl, "glClearColor");
    void (WINAPI *Clear)(GLbitfield) = (void *)GetProcAddress(ogl, "glClear");
    HGLRC (WINAPI *CreateContext)(HDC) = (void *)GetProcAddress(ogl, "wglCreateContext");
    int seconds = argc > 1 ? atoi(argv[1]) : 10, i, frames = 0, sent = 0;
    DWORD start, lastPrint = 0;
    char *slash;

    GetModuleFileNameA(NULL, g_dir, sizeof(g_dir));
    if ((slash = strrchr(g_dir, '\\')) != NULL) *slash = 0;

    R(MakeCurrent, "wglMakeCurrent"); R(GetProcAddress, "wglGetProcAddress");
    R(GetCurrentContext, "wglGetCurrentContext"); R(GetCurrentDC, "wglGetCurrentDC");
    R(Enable, "glEnable"); R(Disable, "glDisable"); R(IsEnabled, "glIsEnabled");
    R(GenTextures, "glGenTextures"); R(BindTexture, "glBindTexture"); R(TexImage2D, "glTexImage2D");
    R(TexParameteri, "glTexParameteri"); R(GetError, "glGetError"); R(GetIntegerv, "glGetIntegerv");

    wnd = CreateWindowA("STATIC", "xrtest", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    dc = GetDC(wnd);
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    rc = CreateContext(dc);
    gl.MakeCurrent(dc, rc);

    X(GenFramebuffers, "glGenFramebuffers"); X(DeleteFramebuffers, "glDeleteFramebuffers");
    X(BindFramebuffer, "glBindFramebuffer"); X(FramebufferTexture2D, "glFramebufferTexture2D");
    X(BlitFramebuffer, "glBlitFramebuffer");

    gl.GenTextures(3, tex);
    gl.GenFramebuffers(3, fbo);
    for (i = 0; i < 3; i++) {
        gl.BindTexture(GL_TEXTURE_2D, tex[i]);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
    }

    start = GetTickCount();
    while (!XR_Init(640, 480, 0)) {   // waits for the headset like the game does
        if (GetTickCount() - start > (DWORD)seconds * 1000) { printf("RESULT: XR_Init failed\n"); return 1; }
        Sleep(100);
    }
    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000) {
        vr_pose_t eyes[2], head;
        int render = XR_BeginFrame(eyes, &head);
        if (!XR_FrameOpen()) { Sleep(10); continue; }
        frames++;
        for (i = 0; i < 2; i++) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
            ClearColor(i == 0 ? 0.8f : 0, 0, i == 1 ? 0.8f : 0, 1);
            Clear(GL_COLOR_BUFFER_BIT);
        }
        // HUD: half-transparent green, premultiplied like the game's blended 2D
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo[2]);
        ClearColor(0, 0.4f, 0, 0.5f);
        Clear(GL_COLOR_BUFFER_BIT);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        {
            vr_hud_t hud = { 0 };
            hud.world = 1; hud.headLocked = 1; hud.distance = 1.5f; hud.offsetY = -0.05f; hud.width = 1.0f; hud.menuWidth = 1.8f;
            hud.fbo = fbo[2];
            XR_EndFrame(render ? fbo : NULL, 640, 480, &hud, NULL);
        }
        if (render) sent++;
        if (render && GetTickCount() - lastPrint > 1000) {
            vr_input_t in;
            XR_GetInput(&in);
            printf("input active=%d  L%s pos %.2f %.2f %.2f trig %.2f grip %.2f stick %.2f %.2f  R%s pos %.2f %.2f %.2f fwd %.2f %.2f %.2f trig %.2f grip %.2f stick %.2f %.2f  buttons 0x%02x\n",
                   in.active, in.handValid[0] ? "" : "(lost)", in.hand[0].pos[0], in.hand[0].pos[1], in.hand[0].pos[2],
                   in.trigger[0], in.grip[0], in.stick[0][0], in.stick[0][1],
                   in.handValid[1] ? "" : "(lost)", in.hand[1].pos[0], in.hand[1].pos[1], in.hand[1].pos[2],
                   in.hand[1].axis[0][0], in.hand[1].axis[0][1], in.hand[1].axis[0][2],
                   in.trigger[1], in.grip[1], in.stick[1][0], in.stick[1][1], in.buttons);
            lastPrint = GetTickCount();
            printf("head pos %.2f %.2f %.2f  fwd %.2f %.2f %.2f  eye0 tan L%.2f R%.2f U%.2f D%.2f  IPD %.1f mm\n",
                   head.pos[0], head.pos[1], head.pos[2], head.axis[0][0], head.axis[0][1], head.axis[0][2],
                   eyes[0].tanLeft, eyes[0].tanRight, eyes[0].tanUp, eyes[0].tanDown,
                   1000.0f * sqrtf((eyes[0].pos[0] - eyes[1].pos[0]) * (eyes[0].pos[0] - eyes[1].pos[0]) +
                                   (eyes[0].pos[1] - eyes[1].pos[1]) * (eyes[0].pos[1] - eyes[1].pos[1]) +
                                   (eyes[0].pos[2] - eyes[1].pos[2]) * (eyes[0].pos[2] - eyes[1].pos[2])));
        }
    }
    printf("RESULT: %d frames, %d sent to the headset in %d s, GL error 0x%x\n", frames, sent, seconds, gl.GetError());
    XR_Shutdown();
    return 0;
}
