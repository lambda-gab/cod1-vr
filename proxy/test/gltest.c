// Loads ..\out\opengl32.dll the way CoD's QGL does (LoadLibrary + GetProcAddress), creates a GL
// context on a hidden window and calls a few forwarded and hooked entry points.
#include <windows.h>
#include <stdio.h>

typedef const unsigned char *(WINAPI *PFN_GetString)(unsigned int);
typedef HGLRC (WINAPI *PFN_Create)(HDC);
typedef BOOL  (WINAPI *PFN_Make)(HDC, HGLRC);
typedef BOOL  (WINAPI *PFN_Delete)(HGLRC);
typedef void  (WINAPI *PFN_Enum)(unsigned int);
typedef unsigned int (WINAPI *PFN_GetError)(void);
typedef PROC  (WINAPI *PFN_GPA)(LPCSTR);

int main(int argc, char **argv)
{
    HMODULE gl = LoadLibraryA(argv[1]);
    char path[MAX_PATH];
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32, 0,0,0,0,0,0, 8, 0, 0,0,0,0,0, 24, 8, 0, PFD_MAIN_PLANE };
    HWND wnd; HDC dc; HGLRC rc; int pf;

    if (!gl) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    GetModuleFileNameA(gl, path, sizeof(path));
    printf("loaded %s\n", path);

    wnd = CreateWindowA("STATIC", "t", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    dc = GetDC(wnd);
    pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);
    rc = ((PFN_Create)GetProcAddress(gl, "wglCreateContext"))(dc);
    if (!rc || !((PFN_Make)GetProcAddress(gl, "wglMakeCurrent"))(dc, rc)) { printf("context failed\n"); return 2; }

    printf("GL_VENDOR   %s\n", ((PFN_GetString)GetProcAddress(gl, "glGetString"))(0x1F00));
    printf("GL_RENDERER %s\n", ((PFN_GetString)GetProcAddress(gl, "glGetString"))(0x1F01));
    ((PFN_Enum)GetProcAddress(gl, "glDrawBuffer"))(0x0405);   // GL_BACK
    ((PFN_Enum)GetProcAddress(gl, "glReadBuffer"))(0x0405);
    printf("glGetError after DrawBuffer/ReadBuffer: 0x%x\n", ((PFN_GetError)GetProcAddress(gl, "glGetError"))());
    printf("glGenFramebuffers %s\n", ((PFN_GPA)GetProcAddress(gl, "wglGetProcAddress"))("glGenFramebuffers") ? "available" : "MISSING");
    SwapBuffers(dc);

    ((PFN_Make)GetProcAddress(gl, "wglMakeCurrent"))(NULL, NULL);
    ((PFN_Delete)GetProcAddress(gl, "wglDeleteContext"))(rc);
    printf("ok\n");
    return 0;
}
