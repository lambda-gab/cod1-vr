// cod_vr: proxy opengl32.dll that turns on the dormant Quake 3 stereo path of CoDSP.exe 1.5
// and sends each eye to an OpenXR headset (phase 2), or shows both eyes side by side on the
// monitor when no headset is available (phase 1).
//
// How it works (addresses in ../codsp_addresses.h, findings in ../CoD_VR_RE.md):
//  * CoD loads "opengl32" with LoadLibrary, so this DLL in the game folder is picked up first.
//    Every export is forwarded to the system DLL (stubs.c), except glDrawBuffer, glReadBuffer
//    and wglMakeCurrent.
//  * With a headset the game draws one frame (one cgame pass) and every world scene is rendered
//    twice, once per eye, reusing the scene's entities. Without one, the Quake 3 stereo path
//    draws the screen twice (side by side on the monitor).
//  * re.BeginFrame / re.RenderScene in the client's renderer table are replaced:
//      - BeginFrame turns on glConfig.stereoEnabled (renderer + client copy), so SCR_UpdateScreen
//        draws the screen twice and RE_BeginFrame emits glDrawBuffer(GL_BACK_LEFT/RIGHT).
//        On the left eye it also starts the OpenXR frame and feeds the head angles to cl.viewangles.
//      - RenderScene places the camera at the tracked eye (orientation, position, IPD), or shifts
//        it by +-separation/2 in side-by-side mode.
//  * The only call to R_SetupProjection is redirected so each eye gets its asymmetric frustum.
//  * glDrawBuffer(GL_BACK_LEFT/RIGHT) is redirected to one framebuffer object per eye.
//  * In VR, the backend's 2D pass (RB_SetGL2D) and 3D views without a world (RB_BeginDrawingView
//    with RDF_NOWORLDMODEL) are drawn into a separate HUD buffer that becomes a flat OpenXR quad
//    layer (phase 3), instead of being stretched across the whole field of view.
//  * Controllers (phase 4): the dominant hand aims (cl.viewangles), the off-hand stick moves
//    (detour of CL_JoystickMove writes forward/right move), buttons run game commands through
//    Cbuf_AddText, and RF_DEPTHHACK entities (view weapon and hands) are moved from the game's
//    view to the hand when the world scene is rendered.
//  * The gdi32!SwapBuffers import of CoDSP.exe is hooked to submit the eyes and the HUD to the
//    headset (and mirror the left eye on the monitor), or to draw the eyes side by side.
#define _CRT_SECURE_NO_WARNINGS
#include "cod_vr.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../codsp_addresses.h"

extern FARPROC g_real[];
extern const char *g_realNames[];
extern const int g_realCount;

#define DEG(x)  ((x) * (180.0f / 3.14159265f))
#define RAD(x)  ((x) * (3.14159265f / 180.0f))
#define PITCH 0
#define YAW   1
#define MEM(type, addr) (*(type *)(addr))

gl_api_t gl;
char     g_dir[MAX_PATH];

// ---- configuration (cod_vr.ini next to CoDSP.exe) ----
static struct {
    int   enabled;      // 0 = pure passthrough
    float separation;   // side-by-side mode: camera distance between eyes, game units
    int   swapEyes;     // side-by-side mode: 1 = right eye on the left (cross-eyed viewing)
    int   vr;           // 1 = use an OpenXR headset when one is available
    float worldScale;   // game units per metre (1 unit = 1 inch)
    int   positional;   // 1 = head position moves the camera (6DoF), 0 = rotation only
    int   headAim;      // 1 = head yaw/pitch drive the player's view angles (aim where you look)
    int   hud;          // 1 = HUD and menus on a quad layer (VR only)
    vr_hud_t hudLayer;  // quad placement
    // controllers
    int   controllers;      // 1 = use the controllers (aim, move, buttons)
    int   rightHanded;      // 1 = gun in the right hand
    float snapTurn;         // degrees per flick, 0 = smooth turn
    float turnSpeed;        // smooth turn, degrees per second
    float converge;         // metres along the gun where the shots meet the barrel line
    int   weaponInHand;     // 1 = move the view weapon to the hand
    float weaponOffset[3];  // game units, view weapon position relative to the eye: forward, right, up
    int   twoHanded;        // 1 = off-hand grip near the gun steadies it (aim from hand to hand)
    float twoHandReach;     // metres between the hands within which the grip takes the gun
    float twoHandGripHeight; // metres the off hand holds below the barrel line
    char  bind[16][64];     // commands per button, see g_buttons
    // scope
    int   scope;            // 1 = while the game zooms in, show its zoomed view behind its overlay
    float scopeMaxFov;      // the game's fov below which that happens (degrees)
    float scopeWidth;       // width of that screen, metres
    float scopeDistance;    // metres in front of the eyes
    float menuPointer;      // 1 = point at menus with the gun hand
    int   logNear;          // diagnostics: log the entities near the view while firing
    float muzzleOffset[2];  // game units: where muzzle effects sit across the barrel, right and up
    int   roomScale;        // 1 = the player in the game walks to where your head is
    float weaponAngle[2];   // degrees: gun pitch (up) and yaw (left) relative to the controller
    float aimSmoothing;     // seconds: time constant of the gun direction filter, 0 = off
    int   autoAds;          // 1 = aim down the sights (+speed) while the gun is at the eye
} cfg = { 1, 2.5f, 0, 1, 39.37f, 1, 1, 1, { 0, 0, 0, 1, 1.5f, -0.05f, 1.0f, 1.8f },
          1, 1, 45.0f, 150.0f, 15.0f, 1, { 10.0f, 5.0f, -7.0f }, 1, 0.7f, 0.04f, { { 0 } },
          1, 35.0f, 1.4f, 0.7f, 1, 1, { 0.0f, 0.0f }, 1, { 0.0f, 0.0f }, 0.0f, 0 };

// Buttons -> game commands. "+cmd" is held while pressed ("-cmd" on release).
enum { BT_PTRIGGER, BT_PGRIP, BT_STRIGGER, BT_SGRIP, BT_A, BT_B, BT_X, BT_Y, BT_LCLICK, BT_RCLICK,
       BT_MENU, BT_STICKUP, BT_STICKDOWN, BT_COUNT };
static const struct { const char *key, *def; } g_buttons[BT_COUNT] = {
    { "PrimaryTrigger", "+attack" },        // dominant hand
    { "PrimaryGrip", "+activate" },         // use / pick up
    { "SecondaryTrigger", "+speed" },       // off hand: aim down the sights (zoom with scopes)
    { "SecondaryGrip", "" },                // off hand: holds the gun with both hands (TwoHanded)
    { "A", "+gostand" },
    { "B", "+reload" },
    { "X", "weapnext" },
    { "Y", "+melee" },
    { "LeftStickClick", "+scores" },        // objectives
    { "RightStickClick", "toggleprone" },
    { "Menu", "togglemenu" },
    { "TurnStickUp", "raisestance" },
    { "TurnStickDown", "lowerstance" },
};

static HMODULE g_realDll;
static FILE   *g_log;

void Log(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---- eye framebuffers ----
static HGLRC  fb_ctx;
static int    fb_w, fb_h, fb_ok, fb_failed;
static GLuint fb_fbo[2], fb_tex[2], fb_depth[2];
// HUD buffer (the left eye's 2D) and a tiny buffer that swallows the right eye's identical 2D,
// which would otherwise be blended twice.
static GLuint hud_fbo[2], hud_tex[2], hud_depth;
static int    g_backEye = -1;          // eye the backend is drawing (from glDrawBuffer), -1 mono
static int    g_hudActive;             // this frame sends 2D to the HUD buffer
static int    g_worldDrawn;            // the left eye drew the 3D world this frame
static int    g_eye = -1;              // eye currently being rendered (0 left, 1 right), -1 mono
static int    g_boundFbo;              // nonzero while an eye FBO is bound
static int    g_stereoFrameDrawn;      // set once the right eye drew this frame
static int    g_stereoActive;          // we turned on stereoEnabled
static int       g_singlePass;            // headset frame: one cgame pass, both eyes per world scene
static int       g_hudCleared;            // the HUD buffer was cleared this frame
static int       g_nearLogFrames, g_nearLogLines;   // diagnostics budget
static float     g_lastViewOrg[3], g_lastViewAxis[3][3];
// full-screen 2D effects (damage tint, fades, flashes) are drawn into both eyes instead of the HUD
#define MAX_FULLSCREEN 16
static struct { float rect[8]; int shader; float color[4]; int hasColor; } g_fs[MAX_FULLSCREEN];
static int       g_fsCount;
static float     g_curColor[4];
static int       g_curColorSet;
static int       g_fxTarget;              // backend: 1/2 = 2D goes to the left/right eye
static int       g_statScenes, g_statFlags, g_statMaxEnt, g_statMaxPoly;
static DWORD     g_statTick;

// ---- VR frame state ----
static int       g_vrFrame;            // this frame renders with headset poses
static vr_pose_t g_eyes[2], g_head;
static int       g_projOverride = -1;  // eye whose frustum R_SetupProjection must use, -1 = none
static float     g_headYawCmd;         // aim yaw (head or hand) already contained in the game's view this frame
static float     g_headYawPrev;        // aim yaw written to cl.viewangles last
static int       g_headYawInit;
static int       g_aimFromHand;        // aim source of the last write (switching resets the yaw delta)
static vr_input_t g_in;                // controllers, read at the left eye's BeginFrame
static int       g_btnDown[BT_COUNT];
static int       g_turnLatched, g_stanceLatched;
static DWORD     g_lastInputTick;
static signed char g_moveForward, g_moveRight;   // added to the next usercmd
static int       g_crosshairOff;
static vr_pose_t g_gun;                // gun pose (dominant hand, or both hands), tracking space
static int       g_gunValid;
static int       g_twoHand;            // off hand is holding the gun
static DWORD     g_gunSeen;            // last tick the gun hand was tracked
static float     g_gunFwdSmooth[3], g_gunUpSmooth[3];
static int       g_gunSmoothInit;
// roomscale: the game's player follows the head. anchor = where the head was when the player
// was last caught up (tracking space, horizontal), so the camera offset is head - anchor.
static float     g_anchor[2];
static int       g_anchorInit;
static float     g_lastGameOrg[3];
static int       g_lastGameOrgValid;
static float     g_roomFrac;           // share of the last usercmd's movement that was roomscale
static signed char g_roomForward, g_roomRight;
static int       g_statTypes[16];
static float     g_lodRatio = 1.0f;    // game fov / culling fov, applied to the model LOD scale
static int       g_autoAdsOn;
static int       g_scopeActive;        // this frame the zoomed view is drawn into the HUD buffer
static int       g_lastHudWorld = 1;   // last frame showed the world (HUD size) or a menu (menu size)
// menus
static int       g_menuMode;           // the UI catches the keys
static int       g_menuKeyDown[6];     // mouse1, escape, enter, up, down, menu
static float     g_cursor[2];          // where we last put the UI cursor (640x480)
static DWORD     g_cursorSync;
// view weapon entities held back until the world scene, when the game's view is known
#define MAX_HELD 32
static BYTE      g_held[MAX_HELD][REFENT_SIZE];
static int       g_heldParam[MAX_HELD], g_heldCount;
static float     g_clPitchCmd;         // cl.viewangles pitch the current frame was predicted with
static float     g_pitchOffset;        // game pitch - cl.viewangles pitch (delta_angles), filtered
static int       g_pitchOffsetInit;
static int       g_pitchSampled;       // first world scene of the frame measured the offset

static void StartVR(void);
static signed char ClampMove(float v);
static void (__cdecl *Detour(DWORD target, const BYTE *prologue, int len, void *hook))(void);
static void FlushHeldEntities(const cod_refdef_t *fd);
static void ProcessControllers(void);
static void UpdateAutoAds(void);
static void BindHud(void);
static void BindEye(int eye);
static void BindTarget(GLuint fbo);

// ---------------------------------------------------------------------------
// Small math helpers (Quake 3 conventions: axis = forward, left, up; pitch > 0 looks down)
// ---------------------------------------------------------------------------
static float AngleNormalize180(float a)
{
    a = fmodf(a + 180.0f, 360.0f);
    if (a < 0) a += 360.0f;
    return a - 180.0f;
}

static float YawOf(const float fwd[3])   { return DEG(atan2f(fwd[1], fwd[0])); }
static float PitchOf(const float fwd[3])
{
    float z = fwd[2] < -1 ? -1 : fwd[2] > 1 ? 1 : fwd[2];
    return -DEG(asinf(z));
}

static void RotateZ(const float v[3], float yawDeg, float out[3])
{
    float s = sinf(RAD(yawDeg)), c = cosf(RAD(yawDeg));
    out[0] = v[0] * c - v[1] * s;
    out[1] = v[0] * s + v[1] * c;
    out[2] = v[2];
}

// ---------------------------------------------------------------------------
// Renderer table hooks
// ---------------------------------------------------------------------------
typedef void (__cdecl *PFN_BeginFrame)(int stereoFrame);
typedef void (__cdecl *PFN_RenderScene)(cod_refdef_t *fd);
typedef void (__cdecl *PFN_Void)(void);
static PFN_BeginFrame  orig_BeginFrame;
static PFN_RenderScene orig_RenderScene;

// Head orientation -> cl.viewangles, so the game aims where the player looks. Yaw is applied as
// a delta (the mouse keeps turning the body); pitch is absolute, corrected by the measured
// offset between cl.viewangles and the predicted view (the server's delta_angles).
// A tracked point relative to the player in the game (metres, tracking axes): relative to the
// roomscale anchor, or to the head when only rotation is tracked.
static void BodyOffset(const float p[3], float out[3])
{
    if (!cfg.positional) {
        out[0] = p[0] - g_head.pos[0]; out[1] = p[1] - g_head.pos[1]; out[2] = p[2] - g_head.pos[2];
    } else if (cfg.roomScale && g_anchorInit) {
        out[0] = p[0] - g_anchor[0]; out[1] = p[1] - g_anchor[1]; out[2] = p[2];
    } else {
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }
}

// Roomscale, once per frame on the main scene: account for how far the game moved the player
// because of the last roomscale command, then ask it to walk to the head again. Walls stop the
// player as usual; the camera then stays where the head is.
static void UpdateRoomScale(const cod_refdef_t *fd)
{
    float body = YawOf(fd->viewaxis[0]) - g_headYawCmd, c[3], d[3], dl[3], len;
    int i;

    g_roomForward = g_roomRight = 0;
    if (!cfg.roomScale || !cfg.positional) return;
    if (g_moveForward || g_moveRight) {
        // Walking with the stick: the game moves the player alone. Mixing both commands made
        // the share of each in the measured movement unreliable (stutter, "invisible walls").
        g_lastGameOrgValid = 0;
        return;
    }
    if (!g_anchorInit) { g_anchor[0] = g_head.pos[0]; g_anchor[1] = g_head.pos[1]; g_anchorInit = 1; }

    if (g_lastGameOrgValid) {
        for (i = 0; i < 2; i++) d[i] = fd->vieworg[i] - g_lastGameOrg[i];
        d[2] = 0;
        if (d[0] * d[0] + d[1] * d[1] > 60.0f * 60.0f) {
            // teleport, level change, cutscene: start over from the current head position
            g_anchor[0] = g_head.pos[0]; g_anchor[1] = g_head.pos[1];
        } else if (g_roomFrac > 0) {
            RotateZ(d, -body, dl);
            g_anchor[0] += dl[0] / cfg.worldScale * g_roomFrac;
            g_anchor[1] += dl[1] / cfg.worldScale * g_roomFrac;
        }
    }
    memcpy(g_lastGameOrg, fd->vieworg, sizeof(g_lastGameOrg));
    g_lastGameOrgValid = 1;

    c[0] = g_head.pos[0] - g_anchor[0];
    c[1] = g_head.pos[1] - g_anchor[1];
    c[2] = 0;
    len = sqrtf(c[0] * c[0] + c[1] * c[1]);
    if (len > 1.5f) {   // recentered or walked far without the game following: catch up at once
        g_anchor[0] = g_head.pos[0]; g_anchor[1] = g_head.pos[1];
        return;
    }
    if (len < 0.03f) return;
    {
        // The usercmd moves relative to the game's view: rotate from tracking into it.
        float v[3], m, speed = len * cfg.worldScale / 0.2f, amp;   // close the gap in ~0.2 s
        RotateZ(c, -g_headYawCmd, v);
        v[0] /= len; v[1] /= len;
        m = max(fabsf(v[0]), fabsf(v[1]));
        amp = 127.0f * speed / 190.0f / m;   // pmove: speed = g_speed (190) * max(|f|,|r|) / 127
        if (amp > 127.0f / m) amp = 127.0f / m;
        g_roomForward = ClampMove(v[0] * amp / 127.0f);
        g_roomRight = ClampMove(-v[1] * amp / 127.0f);
    }
}

static void UpdateViewAngles(float yaw, float pitch, int fromHand)
{
    float *va = (float *)ADDR_cl_viewangles;

    if (!g_headYawInit || fromHand != g_aimFromHand) { g_headYawPrev = yaw; g_headYawInit = 1; g_aimFromHand = fromHand; }
    g_headYawCmd = g_headYawPrev;          // what this frame's view was built from
    g_clPitchCmd = va[PITCH];
    if (!cfg.headAim) return;

    va[YAW] += AngleNormalize180(yaw - g_headYawPrev);
    g_headYawPrev = yaw;
    if (g_pitchOffsetInit) va[PITCH] = pitch - g_pitchOffset;
}

// Cbuf_AddText takes the text in EAX.
static void Cbuf(const char *text)
{
    __asm {
        mov eax, text
        mov ecx, ADDR_Cbuf_AddText
        call ecx
    }
}

static void RunBind(int b, int down)
{
    const char *cmd = cfg.bind[b];
    char line[80];
    if (!cmd[0]) return;
    if (cmd[0] == '+') _snprintf(line, sizeof(line), "%c%s\n", down ? '+' : '-', cmd + 1);
    else if (down) _snprintf(line, sizeof(line), "%s\n", cmd);
    else return;
    Cbuf(line);
}

static void SetButton(int b, int down)
{
    if (down == g_btnDown[b]) return;
    g_btnDown[b] = down;
    RunBind(b, down);
}

// Analog buttons with hysteresis so a resting finger does not chatter.
static int Pressed(float v, int b) { return g_btnDown[b] ? v > 0.35f : v > 0.65f; }

static void ReleaseAllButtons(void)
{
    int b;
    for (b = 0; b < BT_COUNT; b++) SetButton(b, 0);
}

static signed char ClampMove(float v)
{
    v *= 127.0f;
    return (signed char)(v > 127.0f ? 127 : v < -127.0f ? -127 : (int)v);
}

static void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float Normalize(float v[3])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
    return len;
}

// The gun follows the dominant hand. While the off-hand grip is held near the gun, the barrel
// points from the dominant hand to the off hand, like holding a rifle by the handguard.
static void UpdateGunPose(int dom, int off)
{
    const vr_pose_t *d = &g_in.hand[dom], *o = &g_in.hand[off];
    int gripDown = g_btnDown[BT_SGRIP];

    DWORD now = GetTickCount();
    if (!g_in.handValid[dom]) {
        // Tracking dropped out (occlusion): keep the last pose for a moment rather than letting
        // the game draw the gun in front of the face. After that the gun is hidden.
        g_twoHand = 0;
        g_gunValid = g_gunSeen && now - g_gunSeen < 1000;
        return;
    }
    g_gunSeen = now;
    g_gunValid = 1;
    g_gun = *d;

    if (!cfg.twoHanded || !g_in.handValid[off] || !gripDown) { g_twoHand = 0; return; }
    {
        float f[3], dist, up[3], l[3];
        int i;
        // the off hand holds the handguard below the barrel line
        for (i = 0; i < 3; i++) f[i] = o->pos[i] + d->axis[2][i] * cfg.twoHandGripHeight - d->pos[i];
        dist = Normalize(f);
        // take hold only when the off hand grabs in front of the gun hand, within reach
        if (!g_twoHand && (dist > cfg.twoHandReach || f[0] * d->axis[0][0] + f[1] * d->axis[0][1] + f[2] * d->axis[0][2] < 0.3f))
            return;
        if (dist < 0.05f) return;
        g_twoHand = 1;
        // keep the dominant hand's roll: up = its up vector made perpendicular to the barrel
        Cross(d->axis[2], f, l);          // left = up x forward
        if (Normalize(l) < 1e-3f) return;
        Cross(f, l, up);                  // up = forward x left
        for (i = 0; i < 3; i++) { g_gun.axis[0][i] = f[i]; g_gun.axis[1][i] = l[i]; g_gun.axis[2][i] = up[i]; }
    }
}

// Puts an event in the engine's queue; Com_EventLoop delivers it at the start of the next frame,
// outside the renderer (a click can start a level).
static void QueueEvent(int type, int value, int value2)
{
    int head = MEM(int, ADDR_eventHead), *ev;
    if (head - MEM(int, ADDR_eventTail) >= 256) return;   // queue full
    ev = (int *)(ADDR_eventQue + (head & 0xff) * 24);
    ev[0] = 0; ev[1] = type; ev[2] = value; ev[3] = value2; ev[4] = 0; ev[5] = 0;
    MEM(int, ADDR_eventHead) = head + 1;
}

static void MenuKey(int slot, int key, int down)
{
    if (down == g_menuKeyDown[slot]) return;
    g_menuKeyDown[slot] = down;
    QueueEvent(SE_KEY, key, down);
}

// Menus: the gun hand points at the menu screen (moves the UI cursor), either trigger clicks,
// B / Menu go back, A confirms, the turn stick scrolls.
static void MenuControls(int dom)
{
    static const int keys[6] = { K_MOUSE1, K_ESCAPE, K_ENTER, K_UPARROW, K_DOWNARROW, K_ESCAPE };
    const vr_hud_t *hl = &cfg.hudLayer;
    float sy = g_in.stick[dom][1];
    DWORD now = GetTickCount();

    if (!g_menuMode) {
        g_menuMode = 1;
        g_cursorSync = 0;
    }

    if (cfg.menuPointer && g_in.handValid[dom]) {
        const vr_pose_t *h = &g_in.hand[dom];
        float width = g_lastHudWorld ? hl->width : hl->menuWidth, height = width * fb_h / (float)fb_w;
        float c[3], n[3], d[3], denom, t, hit[3], u, v, tx, ty;
        int i;
        // the menu screen: head-locked, distance in front, offsetY up
        for (i = 0; i < 3; i++) {
            n[i] = g_head.axis[0][i];
            c[i] = g_head.pos[i] + g_head.axis[0][i] * hl->distance + g_head.axis[2][i] * hl->offsetY;
            d[i] = c[i] - h->pos[i];
        }
        denom = h->axis[0][0] * n[0] + h->axis[0][1] * n[1] + h->axis[0][2] * n[2];
        if (denom < -1e-3f || denom > 1e-3f) {
            t = (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]) / denom;
            if (t > 0) {
                for (i = 0; i < 3; i++) hit[i] = h->pos[i] + h->axis[0][i] * t - c[i];
                u = -(hit[0] * g_head.axis[1][0] + hit[1] * g_head.axis[1][1] + hit[2] * g_head.axis[1][2]);  // right
                v = hit[0] * g_head.axis[2][0] + hit[1] * g_head.axis[2][1] + hit[2] * g_head.axis[2][2];     // up
                {
                    float end[3];
                    for (i = 0; i < 3; i++) end[i] = h->pos[i] + h->axis[0][i] * t;
                    XR_SetLaser(h->pos, end);
                }
                tx = (u / width + 0.5f) * 640.0f;
                ty = (0.5f - v / height) * 480.0f;
                tx = tx < 0 ? 0 : tx > 639 ? 639 : tx;
                ty = ty < 0 ? 0 : ty > 479 ? 479 : ty;
                // The UI cursor only takes relative moves: park it in the corner now and then
                // to stay in sync, otherwise move by the difference.
                if (!g_cursorSync || now - g_cursorSync > 2000) {
                    QueueEvent(SE_MOUSE, -4000, -4000);
                    g_cursor[0] = g_cursor[1] = 0;
                    g_cursorSync = now ? now : 1;
                }
                if ((int)tx != (int)g_cursor[0] || (int)ty != (int)g_cursor[1]) {
                    QueueEvent(SE_MOUSE, (int)tx - (int)g_cursor[0], (int)ty - (int)g_cursor[1]);
                    g_cursor[0] = tx;
                    g_cursor[1] = ty;
                }
            }
        }
    }

    MenuKey(0, keys[0], g_in.trigger[0] > 0.6f || g_in.trigger[1] > 0.6f ||
                        (g_menuKeyDown[0] && (g_in.trigger[0] > 0.4f || g_in.trigger[1] > 0.4f)));
    MenuKey(1, keys[1], (g_in.buttons & VRB_B) != 0);
    MenuKey(2, keys[2], (g_in.buttons & VRB_A) != 0);
    MenuKey(3, keys[3], sy > 0.7f || (g_menuKeyDown[3] && sy > 0.3f));
    MenuKey(4, keys[4], sy < -0.7f || (g_menuKeyDown[4] && sy < -0.3f));
    MenuKey(5, keys[5], (g_in.buttons & VRB_MENU) != 0);
}

static void LeaveMenuMode(void)
{
    static const int keys[6] = { K_MOUSE1, K_ESCAPE, K_ENTER, K_UPARROW, K_DOWNARROW, K_ESCAPE };
    int i;
    if (!g_menuMode) return;
    for (i = 0; i < 6; i++) MenuKey(i, keys[i], 0);
    g_menuMode = 0;
}

// Weapon angle relative to the controller (one hand only) and an optional low-pass filter on
// the gun direction, then rebuilds an orthonormal frame.
static void AdjustGunPose(float dt)
{
    float f[3], u[3], l[3], cp, sp, cy, sy, t[3];
    int i;

    memcpy(f, g_gun.axis[0], sizeof(f));
    memcpy(l, g_gun.axis[1], sizeof(l));
    memcpy(u, g_gun.axis[2], sizeof(u));
    if (!g_twoHand && (cfg.weaponAngle[0] != 0 || cfg.weaponAngle[1] != 0)) {
        cp = cosf(RAD(cfg.weaponAngle[0])); sp = sinf(RAD(cfg.weaponAngle[0]));
        for (i = 0; i < 3; i++) { t[i] = f[i] * cp + u[i] * sp; u[i] = u[i] * cp - f[i] * sp; f[i] = t[i]; }
        cy = cosf(RAD(cfg.weaponAngle[1])); sy = sinf(RAD(cfg.weaponAngle[1]));
        for (i = 0; i < 3; i++) { t[i] = f[i] * cy + l[i] * sy; l[i] = l[i] * cy - f[i] * sy; f[i] = t[i]; }
    }
    if (cfg.aimSmoothing > 0 && dt > 0) {
        float a = 1.0f - expf(-dt / cfg.aimSmoothing);
        if (!g_gunSmoothInit) { memcpy(g_gunFwdSmooth, f, sizeof(f)); memcpy(g_gunUpSmooth, u, sizeof(u)); g_gunSmoothInit = 1; }
        for (i = 0; i < 3; i++) {
            g_gunFwdSmooth[i] += (f[i] - g_gunFwdSmooth[i]) * a;
            g_gunUpSmooth[i] += (u[i] - g_gunUpSmooth[i]) * a;
        }
        memcpy(f, g_gunFwdSmooth, sizeof(f));
        memcpy(u, g_gunUpSmooth, sizeof(u));
    }
    Normalize(f);
    Cross(u, f, l);          // left = up x forward
    if (Normalize(l) < 1e-3f) return;
    Cross(f, l, u);          // up = forward x left
    memcpy(g_gun.axis[0], f, sizeof(f));
    memcpy(g_gun.axis[1], l, sizeof(l));
    memcpy(g_gun.axis[2], u, sizeof(u));
}

// Aim down the sights while the gun is raised to the eye and lined up with the view, so the
// game uses its aimed accuracy (hip fire spreads much more). Hysteresis avoids flicker.
static void UpdateAutoAds(void)
{
    float d[3], dist, cosAng;
    int i, on;

    if (!cfg.autoAds || !g_gunValid) on = 0;
    else {
        for (i = 0; i < 3; i++) d[i] = g_gun.pos[i] - g_head.pos[i];
        dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        cosAng = g_gun.axis[0][0] * g_head.axis[0][0] + g_gun.axis[0][1] * g_head.axis[0][1] + g_gun.axis[0][2] * g_head.axis[0][2];
        if (g_autoAdsOn) on = dist < 0.40f && cosAng > 0.94f;   // ~20 degrees
        else on = dist < 0.32f && cosAng > 0.97f;               // ~14 degrees
    }
    if (on == g_autoAdsOn) return;
    g_autoAdsOn = on;
    // Leave +speed alone while the button that also sends it is held.
    if (!(g_btnDown[BT_STRIGGER] && !strcmp(cfg.bind[BT_STRIGGER], "+speed")))
        Cbuf(on ? "+speed\n" : "-speed\n");
}

// Reads the controllers once per frame (left eye): buttons, turning, movement and aim.
static void ProcessControllers(void)
{
    float *va = (float *)ADDR_cl_viewangles;
    int dom = cfg.rightHanded ? 1 : 0, off = 1 - dom;
    float headYaw = YawOf(g_head.axis[0]), headPitch = PitchOf(g_head.axis[0]);
    float aimYaw = headYaw, aimPitch = headPitch, dt;
    int fromHand = 0;
    DWORD now = GetTickCount();

    dt = g_lastInputTick ? (now - g_lastInputTick) * 0.001f : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    g_lastInputTick = now;

    if (cfg.controllers) XR_GetInput(&g_in);
    else memset(&g_in, 0, sizeof(g_in));

    if (g_in.active && (MEM(int, ADDR_cls_keyCatchers) & 2)) {   // a menu is open
        ReleaseAllButtons();
        g_moveForward = g_moveRight = 0;
        MenuControls(dom);
        return;
    }
    LeaveMenuMode();

    if (!g_in.active) {
        ReleaseAllButtons();
        g_gunValid = g_twoHand = 0;
        g_moveForward = g_moveRight = 0;
    } else {
        float sx = g_in.stick[dom][0], sy = g_in.stick[dom][1];
        float mx = g_in.stick[off][0], my = g_in.stick[off][1];

        if (!g_btnDown[BT_PTRIGGER] && Pressed(g_in.trigger[dom], BT_PTRIGGER) && cfg.logNear) {
            g_nearLogFrames = 6;   // log a few frames after the shot
            Log("---- trigger pressed: entities near the view ----");
        } else if (g_nearLogFrames > 0) {
            g_nearLogFrames--;
        }
        SetButton(BT_PTRIGGER, Pressed(g_in.trigger[dom], BT_PTRIGGER));
        SetButton(BT_PGRIP, Pressed(g_in.grip[dom], BT_PGRIP));
        SetButton(BT_STRIGGER, Pressed(g_in.trigger[off], BT_STRIGGER));
        SetButton(BT_SGRIP, Pressed(g_in.grip[off], BT_SGRIP));
        SetButton(BT_A, (g_in.buttons & VRB_A) != 0);
        SetButton(BT_B, (g_in.buttons & VRB_B) != 0);
        SetButton(BT_X, (g_in.buttons & VRB_X) != 0);
        SetButton(BT_Y, (g_in.buttons & VRB_Y) != 0);
        SetButton(BT_LCLICK, (g_in.buttons & VRB_LSTICK) != 0);
        SetButton(BT_RCLICK, (g_in.buttons & VRB_RSTICK) != 0);
        SetButton(BT_MENU, (g_in.buttons & VRB_MENU) != 0);
        UpdateGunPose(dom, off);
        if (g_in.handValid[dom]) AdjustGunPose(dt);

        // Turn stick: left/right turns the body, up/down raises or lowers the stance.
        if (cfg.snapTurn > 0) {
            if (!g_turnLatched && fabsf(sx) > 0.7f) { va[YAW] -= (sx > 0 ? 1 : -1) * cfg.snapTurn; g_turnLatched = 1; }
            else if (fabsf(sx) < 0.3f) g_turnLatched = 0;
        } else if (fabsf(sx) > 0.15f) {
            va[YAW] -= sx * cfg.turnSpeed * dt;
        }
        if (!g_stanceLatched && fabsf(sy) > 0.7f && fabsf(sy) > fabsf(sx)) {
            SetButton(sy > 0 ? BT_STICKUP : BT_STICKDOWN, 1);
            SetButton(sy > 0 ? BT_STICKUP : BT_STICKDOWN, 0);
            g_stanceLatched = 1;
        } else if (fabsf(sy) < 0.3f) g_stanceLatched = 0;

        // Aim: from the eye through a point ConvergeDistance metres along the gun, so the
        // shots (which leave from the eye) cross the barrel line there.
        if (g_gunValid) {
            const vr_pose_t *h = &g_gun;
            float t[3], len;
            int i;
            for (i = 0; i < 3; i++) t[i] = h->pos[i] + h->axis[0][i] * cfg.converge - g_head.pos[i];
            len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            if (len > 0.01f) {
                for (i = 0; i < 3; i++) t[i] /= len;
                aimYaw = YawOf(t);
                aimPitch = PitchOf(t);
                fromHand = 1;
            }
        }

        // Move stick, relative to where the head looks. The game moves relative to its view
        // (the aim), so rotate the stick by the angle between head and aim.
        if (mx * mx + my * my > 0.15f * 0.15f) {
            float d = RAD(AngleNormalize180(headYaw - aimYaw));
            float f = my, l = -mx;
            float f2 = f * cosf(d) - l * sinf(d), l2 = f * sinf(d) + l * cosf(d);
            g_moveForward = ClampMove(f2);
            g_moveRight = ClampMove(-l2);
        } else {
            g_moveForward = g_moveRight = 0;
        }

        if (fromHand && !g_crosshairOff) { Cbuf("cg_drawCrosshair 0\n"); g_crosshairOff = 1; }
        UpdateAutoAds();
    }
    UpdateViewAngles(aimYaw, aimPitch, fromHand);
}

// Adds the stick movement to the usercmd built by CL_CreateCmd.
static void __cdecl AddVrMove(BYTE *cmd)
{
    int f = (signed char)cmd[USERCMD_OFS_forwardmove] + g_moveForward + g_roomForward;
    int r = (signed char)cmd[USERCMD_OFS_rightmove] + g_moveRight + g_roomRight;
    float room = sqrtf((float)(g_roomForward * g_roomForward + g_roomRight * g_roomRight));
    float stick = sqrtf((float)(g_moveForward * g_moveForward + g_moveRight * g_moveRight));
    g_roomFrac = room > 0 ? room / (room + stick) : 0.0f;
    cmd[USERCMD_OFS_forwardmove] = (BYTE)(signed char)(f > 127 ? 127 : f < -127 ? -127 : f);
    cmd[USERCMD_OFS_rightmove] = (BYTE)(signed char)(r > 127 ? 127 : r < -127 ? -127 : r);
}

typedef void (__cdecl *PFN_Tramp)(void);
static PFN_Tramp tramp_JoystickMove;

// CL_JoystickMove(usercmd_t *cmd /* ECX */)
__declspec(naked) static void hk_JoystickMove(void)
{
    __asm {
        push ecx
        call dword ptr [tramp_JoystickMove]
        mov ecx, [esp]
        push ecx
        call AddVrMove
        add esp, 4
        pop ecx
        ret
    }
}

typedef void (__cdecl *PFN_AddRefEntity)(void *ent, int param);
static PFN_AddRefEntity orig_AddRefEntity;

// Diagnostics: while the trigger is held, logs what is added close to the game's view, in view
// coordinates (forward, left, up), to find effects that are not tied to the view weapon.
static void LogNearEntity(const BYTE *ent, int param, int held)
{
    const float *o = (const float *)(ent + REFENT_OFS_origin);
    float d[3], l[3];
    int i, k;
    for (i = 0; i < 3; i++) d[i] = o[i] - g_lastViewOrg[i];
    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 120.0f * 120.0f) return;
    for (k = 0; k < 3; k++) l[k] = d[0] * g_lastViewAxis[k][0] + d[1] * g_lastViewAxis[k][1] + d[2] * g_lastViewAxis[k][2];
    Log("near entity: reType %d renderfx 0x%x hModel %d param %d %s view(%.1f %.1f %.1f)",
        *(const int *)ent, *(const int *)(ent + REFENT_OFS_renderfx), *(const int *)(ent + 8), param,
        held ? "HELD" : "free", l[0], l[1], l[2]);
    g_nearLogLines++;
}

static void __cdecl hk_AddRefEntity(void *ent, int param)
{
    int hold = g_vrFrame && g_eye == 0 && cfg.weaponInHand && g_gunValid && g_heldCount < MAX_HELD &&
               (*(int *)((BYTE *)ent + REFENT_OFS_renderfx) & RF_DEPTHHACK);
    if (cfg.logNear && g_nearLogFrames > 0 && g_nearLogLines < 400 && g_eye == 0) LogNearEntity(ent, param, hold);
    if (g_vrFrame && g_eye == 0 && cfg.weaponInHand && !g_gunValid &&
        (*(int *)((BYTE *)ent + REFENT_OFS_renderfx) & RF_DEPTHHACK))
        return;   // gun hand not tracked: hide the view weapon rather than draw it at the face
    if (cfg.logNear) g_statTypes[*(int *)ent & 15]++;
    if (hold) {
        memcpy(g_held[g_heldCount], ent, REFENT_SIZE);
        g_heldParam[g_heldCount++] = param;
        return;
    }
    orig_AddRefEntity(ent, param);
}

// Re-expresses a point given in the game's view frame (gameOrg, gameAxis) in the hand frame.
// kind: 0 = direction, 1 = point of the view weapon model, 2 = point of a muzzle effect.
static void ViewToHand(float *p, const cod_refdef_t *fd, const float handOrg[3], float handAxis[3][3], int kind)
{
    float local[3], d[3];
    int i, k;
    int isPoint = kind != 0;
    for (i = 0; i < 3; i++) d[i] = p[i] - (isPoint ? fd->vieworg[i] : 0.0f);
    for (k = 0; k < 3; k++) local[k] = d[0] * fd->viewaxis[k][0] + d[1] * fd->viewaxis[k][1] + d[2] * fd->viewaxis[k][2];
    if (isPoint) {
        // the gun sits at weaponOffset from the eye in the game; put that spot in the hand
        local[0] -= cfg.weaponOffset[0];
        local[1] += cfg.weaponOffset[1];   // right -> -left
        local[2] -= cfg.weaponOffset[2];
    }
    if (kind == 2) {
        // The flat game draws muzzle flashes near the screen centre line, not at the hip gun's
        // muzzle: keep their distance along the barrel and put them on the barrel axis.
        local[1] = -cfg.muzzleOffset[0];
        local[2] = cfg.muzzleOffset[1];
    }
    for (i = 0; i < 3; i++)
        p[i] = (isPoint ? handOrg[i] : 0.0f) + local[0] * handAxis[0][i] + local[1] * handAxis[1][i] + local[2] * handAxis[2][i];
}

// Moves the held view-weapon entities from the game's view to the dominant hand and gives
// them to the renderer. fd is the game's own view for this scene (before the eye pose).
static void FlushHeldEntities(const cod_refdef_t *fd)
{
    int n, i, k;
    const vr_pose_t *h = &g_gun;
    float body = YawOf(fd->viewaxis[0]) - g_headYawCmd;
    float rel[3], handOrg[3], handAxis[3][3];

    if (!g_heldCount) return;
    BodyOffset(h->pos, rel);
    for (i = 0; i < 3; i++) rel[i] *= cfg.worldScale;
    RotateZ(rel, body, handOrg);
    for (i = 0; i < 3; i++) handOrg[i] += fd->vieworg[i];
    for (k = 0; k < 3; k++) RotateZ(h->axis[k], body, handAxis[k]);

    for (n = 0; n < g_heldCount; n++) {
        BYTE *e = g_held[n];
        float *axis = (float *)(e + REFENT_OFS_axis);
        int kind = *(int *)e == REFENT_TYPE_MODEL ? 1 : 2;   // models vs sprites/beams (flash)
        ViewToHand((float *)(e + REFENT_OFS_origin), fd, handOrg, handAxis, kind);
        ViewToHand((float *)(e + REFENT_OFS_oldorigin), fd, handOrg, handAxis, kind);
        if (*(int *)(e + REFENT_OFS_renderfx) & RF_LIGHTING_ORIGIN)
            ViewToHand((float *)(e + REFENT_OFS_lightingOrigin), fd, handOrg, handAxis, 1);
        for (k = 0; k < 3; k++) ViewToHand(axis + 3 * k, fd, handOrg, handAxis, 0);
        orig_AddRefEntity(e, g_heldParam[n]);
    }
    g_heldCount = 0;
}

static void __cdecl hk_BeginFrame(int stereoFrame)
{
    if (!cfg.enabled) { orig_BeginFrame(stereoFrame); return; }

    if (stereoFrame != 2) {   // start of a frame
        g_heldCount = 0;
        g_pitchSampled = 0;
        if (fb_ok) StartVR();
        g_vrFrame = cfg.vr && XR_Ready() && XR_BeginFrame(g_eyes, &g_head);
        if (g_vrFrame) ProcessControllers();
        else { g_moveForward = g_moveRight = 0; }
        g_hudActive = g_vrFrame && cfg.hud && hud_fbo[0];
        g_singlePass = XR_FrameOpen();
    }

    if (g_singlePass) {
        // One pass per frame: keep the stereo flags off so SCR_UpdateScreen draws once. A frame
        // that was already set up for two passes (switching over) throws its second pass away.
        g_eye = (stereoFrame == 2) ? 1 : 0;
        MEM(int, ADDR_r_glConfig_stereo) = stereoFrame != 0;   // what RE_BeginFrame checks
        orig_BeginFrame(stereoFrame);
        MEM(int, ADDR_r_glConfig_stereo) = 0;
        MEM(int, ADDR_cls_glconfig_stereo) = 0;
        return;
    }

    if (stereoFrame == 0) {
        // Mono frame. Either stereo is not on yet (first frame), or a caller that never
        // passes a stereo frame (loading screen at 0x410937). RE_BeginFrame raises a fatal
        // error for stereoFrame 0 while stereo is enabled, so hide the flag for this call.
        g_eye = -1;
        MEM(int, ADDR_r_glConfig_stereo) = 0;
        orig_BeginFrame(0);
        // From the next SCR_UpdateScreen on, both copies say stereo.
        MEM(int, ADDR_r_glConfig_stereo) = 1;
        MEM(int, ADDR_cls_glconfig_stereo) = 1;
        if (!g_stereoActive) { g_stereoActive = 1; Log("stereo enabled"); }
        return;
    }

    g_eye = (stereoFrame == 1) ? 0 : 1;
    MEM(int, ADDR_r_glConfig_stereo) = 1;
    orig_BeginFrame(stereoFrame);
}

// Places the camera at the tracked eye. The game's own view gives the body: its yaw minus the
// head yaw it was built from. Head pitch and roll come straight from the headset.
static void ApplyEyePose(cod_refdef_t *fd, const vr_pose_t *eye, float out_axis[3][3], float out_org[3])
{
    float gameYaw = YawOf(fd->viewaxis[0]);
    float body = gameYaw - g_headYawCmd;
    float rel[3], p[3];
    int i;

    for (i = 0; i < 3; i++) RotateZ(eye->axis[i], body, out_axis[i]);

    // Eye position relative to the head (IPD), plus the head itself when 6DoF is on.
    BodyOffset(eye->pos, rel);
    RotateZ(rel, body, p);
    for (i = 0; i < 3; i++) out_org[i] = fd->vieworg[i] + p[i] * cfg.worldScale;
}

static void SamplePitchOffset(const cod_refdef_t *fd)
{
    float raw = AngleNormalize180(PitchOf(fd->viewaxis[0]) - g_clPitchCmd);
    // Weapon kick and the mouse add short-lived noise; a big jump means delta_angles changed
    // (spawn, teleport, cutscene end), which is taken at once.
    if (!g_pitchOffsetInit || fabsf(raw - g_pitchOffset) > 15.0f) { g_pitchOffset = raw; g_pitchOffsetInit = 1; }
    else g_pitchOffset += (raw - g_pitchOffset) * 0.05f;
}

// While the game zooms in (scoped ADS, binoculars), renders the game's own view - from the
// player's eye along the aim, with the zoom fov - into the HUD buffer, under the 2D the cgame
// draws next (the scope overlay). The HUD screen then shows exactly the flat game's scope, and
// its reticle is where the shots go. Reuses this scene's entities by restoring RE_RenderScene's
// first-item counters; runs before the view weapon is added. The backend recognises the view by
// x = 1 (the eyes use x = 0).
static void RenderScopeView(cod_refdef_t *fd)
{
    int saved[3], x = fd->x, w = fd->width;

    g_scopeActive = 0;
    if (!cfg.scope || !g_hudActive || fd->fov_x >= cfg.scopeMaxFov || fd->x != 0) return;
    fd->x = 1;
    fd->width = w - 1;

    saved[0] = MEM(int, ADDR_r_firstSceneEntity);
    saved[1] = MEM(int, ADDR_r_firstSceneDlight);
    saved[2] = MEM(int, ADDR_r_firstScenePoly);
    g_projOverride = -1;
    orig_RenderScene(fd);
    MEM(int, ADDR_r_firstSceneEntity) = saved[0];
    MEM(int, ADDR_r_firstSceneDlight) = saved[1];
    MEM(int, ADDR_r_firstScenePoly) = saved[2];

    fd->x = x;
    fd->width = w;
    g_scopeActive = 1;
}

// Headset frame: the scope view (when zoomed), the view weapon in the hand, then the scene from
// each eye. The right eye reuses the scene's entities, lights and polys (first-item counters
// restored) and is marked for the backend by x + 2.
static void RenderBothEyes(cod_refdef_t *fd)
{
    float savedOrg[3], savedAxis[3][3], savedFov[2], cullFov[2];
    int saved[3], x = fd->x, y = fd->y, w = fd->width, h = fd->height, eye;

    // A sky portal scene (rdflags bit 3) has its own camera: the aim, the scope and the view
    // weapon belong to the main scene.
    if (!(fd->rdflags & 8)) {
        if (!g_pitchSampled) { SamplePitchOffset(fd); g_pitchSampled = 1; }
        memcpy(g_lastViewOrg, fd->vieworg, sizeof(g_lastViewOrg));
        memcpy(g_lastViewAxis, fd->viewaxis, sizeof(g_lastViewAxis));
        UpdateRoomScale(fd);
        RenderScopeView(fd);
        FlushHeldEntities(fd);
    }

    memcpy(savedOrg, fd->vieworg, sizeof(savedOrg));
    memcpy(savedAxis, fd->viewaxis, sizeof(savedAxis));
    savedFov[0] = fd->fov_x;
    savedFov[1] = fd->fov_y;
    saved[0] = MEM(int, ADDR_r_firstSceneEntity);
    saved[1] = MEM(int, ADDR_r_firstSceneDlight);
    saved[2] = MEM(int, ADDR_r_firstScenePoly);

    {
        // Both eyes cull with the same frustum, the union of the two plus a margin. With
        // per-eye frustums a model at the edge of the view could be skipped by the first eye
        // (so its skeleton is not updated this frame) and drawn by the second: it collapses.
        float tx = 0, ty = 0;
        int k;
        for (k = 0; k < 2; k++) {
            tx = max(tx, max(fabsf(g_eyes[k].tanLeft), fabsf(g_eyes[k].tanRight)));
            ty = max(ty, max(fabsf(g_eyes[k].tanUp), fabsf(g_eyes[k].tanDown)));
        }
        cullFov[0] = 2.0f * DEG(atanf(tx)) + 4.0f;
        cullFov[1] = 2.0f * DEG(atanf(ty)) + 4.0f;
        // The model LOD scale grows with the fov: keep the game's own detail level.
        g_lodRatio = tanf(RAD(savedFov[0] * 0.5f)) / tanf(RAD(cullFov[0] * 0.5f));
    }

    for (eye = 0; eye < 2; eye++) {
        const vr_pose_t *e = &g_eyes[eye];
        // The eyes always fill their buffer: the game's letterbox (cinematic bars) would
        // otherwise be stretched over the headset image.
        fd->x = eye ? 2 : 0;
        fd->y = 0;
        fd->width = fb_w - fd->x;
        fd->height = fb_h;
        if (eye == 1) {
            MEM(int, ADDR_r_firstSceneEntity) = saved[0];
            MEM(int, ADDR_r_firstSceneDlight) = saved[1];
            MEM(int, ADDR_r_firstScenePoly) = saved[2];
        }
        ApplyEyePose(fd, e, fd->viewaxis, fd->vieworg);
        // Symmetric fov for the culling planes (R_SetupFrustum); the exact asymmetric frustum
        // is put into the projection matrix by hk_SetupProjection.
        fd->fov_x = cullFov[0];
        fd->fov_y = cullFov[1];
        g_projOverride = eye;
        orig_RenderScene(fd);
        g_projOverride = -1;
        memcpy(fd->vieworg, savedOrg, sizeof(savedOrg));
        memcpy(fd->viewaxis, savedAxis, sizeof(savedAxis));
        fd->fov_x = savedFov[0];
        fd->fov_y = savedFov[1];
    }
    fd->x = x;
    fd->y = y;
    fd->width = w;
    fd->height = h;
}

static void __cdecl hk_RenderScene(cod_refdef_t *fd)
{
    g_statScenes++;
    g_statFlags |= fd->rdflags;
    float savedOrg[3], savedAxis[3][3], savedFov[2];
    int i;

    // 2D scenes (HUD models, menus) keep the original camera.
    if (!cfg.enabled || g_eye < 0 || (fd->rdflags & 1)) { orig_RenderScene(fd); return; }

    if (g_singlePass) {
        if (g_eye == 1) return;            // thrown-away pass while switching to one pass
        if (!g_vrFrame) { orig_RenderScene(fd); return; }   // no poses this frame: left eye only
        RenderBothEyes(fd);
        return;
    }

    memcpy(savedOrg, fd->vieworg, sizeof(savedOrg));
    memcpy(savedAxis, fd->viewaxis, sizeof(savedAxis));
    savedFov[0] = fd->fov_x;
    savedFov[1] = fd->fov_y;

    {
        // viewaxis[1] points to the left of the view in Quake 3 conventions.
        float shift = (g_eye == 0 ? 0.5f : -0.5f) * cfg.separation;
        for (i = 0; i < 3; i++) fd->vieworg[i] += fd->viewaxis[1][i] * shift;
    }

    orig_RenderScene(fd);
    g_projOverride = -1;

    memcpy(fd->vieworg, savedOrg, sizeof(savedOrg));
    memcpy(fd->viewaxis, savedAxis, sizeof(savedAxis));
    fd->fov_x = savedFov[0];
    fd->fov_y = savedFov[1];
}

typedef void (__cdecl *PFN_SetColor)(const float *rgba);
typedef void (__cdecl *PFN_StretchPic)(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader);
typedef void (__cdecl *PFN_EndFrame)(int *frontMsec, int *backMsec);
static PFN_SetColor   orig_SetColor;
static PFN_StretchPic orig_StretchPic;
static PFN_EndFrame   orig_EndFrame;

static void __cdecl hk_SetColor(const float *rgba)
{
    g_curColorSet = rgba != NULL;
    if (rgba) memcpy(g_curColor, rgba, sizeof(g_curColor));
    orig_SetColor(rgba);
}

// A picture over the whole screen during play (damage tint, fade, flash) would be a small
// rectangle on the HUD screen: keep it for DrawFullscreenEffects.
static void __cdecl hk_StretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader)
{
    if (g_singlePass && g_hudActive && !g_scopeActive && g_fsCount < MAX_FULLSCREEN &&
        !(MEM(int, ADDR_cls_keyCatchers) & 2) &&
        x <= 1 && y <= 1 && x + w >= fb_w - 1 && y + h >= fb_h - 1) {
        float r[8] = { x, y, w, h, s1, t1, s2, t2 };
        memcpy(g_fs[g_fsCount].rect, r, sizeof(r));
        g_fs[g_fsCount].shader = shader;
        g_fs[g_fsCount].hasColor = g_curColorSet;
        memcpy(g_fs[g_fsCount].color, g_curColor, sizeof(g_curColor));
        g_fsCount++;
        return;
    }
    orig_StretchPic(x, y, w, h, s1, t1, s2, t2, shader);
}

// Draws the kept full-screen pictures once per eye, at the end of the frame. An empty 3D scene
// without world in front of each copy tells the backend which eye the 2D goes to (x = 3 left,
// x = 7 right); it only clears the depth buffer, which nothing uses after the 2D.
static void DrawFullscreenEffects(void)
{
    static BYTE ref[1024];
    cod_refdef_t *rd = (cod_refdef_t *)ref;
    int eye, i;

    for (eye = 0; eye < 2; eye++) {
        memset(ref, 0, sizeof(ref));
        rd->x = eye ? 7 : 3;
        rd->y = 0;
        rd->width = fb_w - rd->x;
        rd->height = fb_h;
        rd->fov_x = 90; rd->fov_y = 74;
        rd->viewaxis[0][0] = rd->viewaxis[1][1] = rd->viewaxis[2][2] = 1;
        rd->rdflags = 1;   // RDF_NOWORLDMODEL
        orig_RenderScene(rd);
        for (i = 0; i < g_fsCount; i++) {
            orig_SetColor(g_fs[i].hasColor ? g_fs[i].color : NULL);
            orig_StretchPic(g_fs[i].rect[0], g_fs[i].rect[1], g_fs[i].rect[2], g_fs[i].rect[3],
                            g_fs[i].rect[4], g_fs[i].rect[5], g_fs[i].rect[6], g_fs[i].rect[7], g_fs[i].shader);
        }
    }
    orig_SetColor(g_curColorSet ? g_curColor : NULL);
    g_fsCount = 0;
}

static void __cdecl hk_EndFrame(int *frontMsec, int *backMsec)
{
    if (g_fsCount) {
        if (g_singlePass && g_eye == 0) DrawFullscreenEffects();
        g_fsCount = 0;
    }
    if (cfg.logNear) {
        DWORD now = GetTickCount();
        if (MEM(int, ADDR_r_numEntities) > g_statMaxEnt) g_statMaxEnt = MEM(int, ADDR_r_numEntities);
        if (MEM(int, ADDR_r_numPolys) > g_statMaxPoly) g_statMaxPoly = MEM(int, ADDR_r_numPolys);
        if (now - g_statTick > 1000) {
            char types[200] = "";
            int t, n = 0;
            for (t = 0; t < 16; t++)
                if (g_statTypes[t]) n += _snprintf(types + n, sizeof(types) - n, " %d:%d", t, g_statTypes[t]);
            memset(g_statTypes, 0, sizeof(g_statTypes));
            Log("stats/s: scenes %d rdflags 0x%x, max entities %d, max polys %d, dlights %d, added by reType%s",
                g_statScenes, g_statFlags, g_statMaxEnt, g_statMaxPoly, MEM(int, ADDR_r_numDlights), types);
            g_statTick = now;
            g_statScenes = g_statFlags = g_statMaxEnt = g_statMaxPoly = 0;
        }
    }
    orig_EndFrame(frontMsec, backMsec);
}

// Replaces the frustum terms of both projection matrices (world and view weapon) with the
// eye's asymmetric fov. Same formulas as R_SetupProjection with xmin/xmax/ymin/ymax unequal.
static void __cdecl hk_SetupProjection(void)
{
    ((PFN_Void)ADDR_R_SetupProjection)();
    if (g_projOverride >= 0) {
        const vr_pose_t *e = &g_eyes[g_projOverride];
        float *m[2] = { (float *)ADDR_projectionMatrix, (float *)ADDR_projectionMatrixGun };
        int k;
        for (k = 0; k < 2; k++) {
            m[k][0] = 2.0f / (e->tanRight - e->tanLeft);
            m[k][8] = (e->tanRight + e->tanLeft) / (e->tanRight - e->tanLeft);
            m[k][5] = 2.0f / (e->tanUp - e->tanDown);
            m[k][9] = (e->tanUp + e->tanDown) / (e->tanUp - e->tanDown);
        }
        MEM(float, ADDR_viewParms_lodScale) *= g_lodRatio;
    }
}

static void PatchPointer(DWORD addr, void *value)
{
    DWORD old;
    VirtualProtect((void *)addr, 4, PAGE_EXECUTE_READWRITE, &old);
    MEM(void *, addr) = value;
    VirtualProtect((void *)addr, 4, old, &old);
}

static void PatchCall(DWORD site, void *target)
{
    DWORD old;
    VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old);
    MEM(DWORD, site + 1) = (DWORD)target - (site + 5);
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);
}

typedef BOOL (WINAPI *PFN_SwapBuffers)(HDC);
static PFN_SwapBuffers orig_SwapBuffers;
static BOOL WINAPI hk_SwapBuffers(HDC dc);

// ---------------------------------------------------------------------------
// Backend detours: entry jump + trampoline that replays the copied prologue
// ---------------------------------------------------------------------------
static PFN_Tramp tramp_SetGL2D, tramp_BeginDrawingView;

static PFN_Tramp Detour(DWORD target, const BYTE *prologue, int len, void *hook)
{
    BYTE *t, *p = (BYTE *)target;
    DWORD old;
    int i;

    if (memcmp(p, prologue, len) != 0) { Log("detour at %08x: unexpected bytes, not hooking", target); return NULL; }
    t = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, p, len);                          // the prologue only uses absolute addresses
    t[len] = 0xE9;
    *(DWORD *)(t + len + 1) = (target + len) - ((DWORD)t + len + 5);

    VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD *)(p + 1) = (DWORD)hook - (target + 5);
    for (i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return (PFN_Tramp)t;
}

static void __cdecl hk_SetGL2D(void)
{
    if (g_fxTarget) BindEye(g_fxTarget - 1);   // full-screen effect pass for one eye
    else if (g_hudActive && g_backEye >= 0) BindHud();
    else if (g_singlePass && g_backEye == 0) BindEye(0);   // no HUD layer: 2D on the left eye
    tramp_SetGL2D();
}

static void __cdecl hk_BeginDrawingView(void)
{
    int world = !(MEM(BYTE, ADDR_backEnd_refdef_rdflags) & 1);   // RDF_NOWORLDMODEL
    const int *rd = (const int *)ADDR_backEnd_refdef;             // x, y, width, height
    if (g_hudActive && world && g_backEye == 0 && (rd[0] & 3) == 1 && rd[1] == 0) {   // the zoomed scope view
        BindTarget(hud_fbo[0]);
        tramp_BeginDrawingView();
        return;
    }
    g_fxTarget = 0;
    if (g_singlePass && g_backEye == 0 && !world && (rd[0] & 3) == 3) {   // see DrawFullscreenEffects
        g_fxTarget = 1 + ((rd[0] >> 2) & 1);
        BindEye(g_fxTarget - 1);
        tramp_BeginDrawingView();
        return;
    }
    if (g_singlePass && g_backEye == 0) {
        if (world) {
            int eye = (rd[0] & 3) == 2 ? 1 : 0;   // RenderBothEyes marks the right eye by x + 2
            BindEye(eye);
            if (eye == 0) g_worldDrawn = 1;
        } else if (g_hudActive) {
            BindHud();
        } else {
            BindEye(0);
        }
        tramp_BeginDrawingView();
        return;
    }
    if (g_hudActive && g_backEye >= 0) {
        if (world) BindEye(g_backEye);
        else BindHud();
    }
    if (world && g_backEye == 0) g_worldDrawn = 1;
    tramp_BeginDrawingView();
}

static void InstallDetours(void)
{
    // mov dword ptr [0x1549b50], 1
    static const BYTE gl2d[] = { 0xC7, 0x05, 0x50, 0x9B, 0x54, 0x01, 0x01, 0x00, 0x00, 0x00 };
    // push ecx; mov eax, dword ptr [0x15346a8]
    static const BYTE bdv[] = { 0x51, 0xA1, 0xA8, 0x46, 0x53, 0x01 };
    // mov eax, dword ptr [0x1423c70]
    static const BYTE joy[] = { 0xA1, 0x70, 0x3C, 0x42, 0x01 };
    if (cfg.vr && cfg.controllers && !tramp_JoystickMove) {
        tramp_JoystickMove = Detour(ADDR_CL_JoystickMove, joy, sizeof(joy), (void *)hk_JoystickMove);
        if (tramp_JoystickMove) Log("hooked CL_JoystickMove");
    }
    if (!cfg.vr || !cfg.hud || tramp_SetGL2D) return;
    tramp_SetGL2D = Detour(ADDR_RB_SetGL2D, gl2d, sizeof(gl2d), (void *)hk_SetGL2D);
    if (!tramp_SetGL2D) { cfg.hud = 0; return; }
    tramp_BeginDrawingView = Detour(ADDR_RB_BeginDrawingView, bdv, sizeof(bdv), (void *)hk_BeginDrawingView);
    if (!tramp_BeginDrawingView) { Log("RB_BeginDrawingView not hooked: HUD disabled"); cfg.hud = 0; return; }
    Log("hooked RB_SetGL2D and RB_BeginDrawingView");
}

static int IsKnownExe(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    return (DWORD_PTR)base == 0x400000 && nt->FileHeader.TimeDateStamp == CODSP_TIMESTAMP;
}

// Installs the hooks if they are missing. Runs at load and on every wglMakeCurrent, because
// vid_restart copies a fresh renderer table into the client (CL_InitRef).
static void EnsureHooks(void)
{
    DWORD rs = ADDR_re_client + RE_OFS_RenderScene, bf = ADDR_re_client + RE_OFS_BeginFrame;
    DWORD callTarget;
    void *cur;

    if (!cfg.enabled) return;

    cur = MEM(void *, rs);
    if (cur != (void *)hk_RenderScene) {
        if (cur != (void *)ADDR_RE_RenderScene) { Log("re.RenderScene = %p, expected %08x: unknown CoDSP.exe, not hooking", cur, ADDR_RE_RenderScene); return; }
        orig_RenderScene = (PFN_RenderScene)cur;
        PatchPointer(rs, (void *)hk_RenderScene);
        Log("hooked re.RenderScene");
    }
    cur = MEM(void *, bf);
    if (cur != (void *)hk_BeginFrame) {
        if (cur != (void *)ADDR_RE_BeginFrame) { Log("re.BeginFrame = %p, expected %08x: not hooking", cur, ADDR_RE_BeginFrame); return; }
        orig_BeginFrame = (PFN_BeginFrame)cur;
        PatchPointer(bf, (void *)hk_BeginFrame);
        Log("hooked re.BeginFrame");
    }
    cur = MEM(void *, ADDR_IAT_SwapBuffers);
    if (cur != (void *)hk_SwapBuffers) {
        orig_SwapBuffers = (PFN_SwapBuffers)cur;
        PatchPointer(ADDR_IAT_SwapBuffers, (void *)hk_SwapBuffers);
        Log("hooked SwapBuffers import");
    }
    callTarget = ADDR_CALL_SetupProjection + 5 + MEM(DWORD, ADDR_CALL_SetupProjection + 1);
    if (MEM(BYTE, ADDR_CALL_SetupProjection) == 0xE8 && callTarget == ADDR_R_SetupProjection) {
        PatchCall(ADDR_CALL_SetupProjection, (void *)hk_SetupProjection);
        Log("hooked R_SetupProjection call");
    }
    if (cfg.vr && cfg.hud) {
        cur = MEM(void *, ADDR_re_client + RE_OFS_SetColor);
        if (cur != (void *)hk_SetColor && cur == (void *)ADDR_RE_SetColor) {
            orig_SetColor = (PFN_SetColor)cur;
            PatchPointer(ADDR_re_client + RE_OFS_SetColor, (void *)hk_SetColor);
        }
        cur = MEM(void *, ADDR_re_client + RE_OFS_DrawStretchPic);
        if (cur != (void *)hk_StretchPic && cur == (void *)ADDR_RE_DrawStretchPic) {
            orig_StretchPic = (PFN_StretchPic)cur;
            PatchPointer(ADDR_re_client + RE_OFS_DrawStretchPic, (void *)hk_StretchPic);
        }
        cur = MEM(void *, ADDR_re_client + RE_OFS_EndFrame);
        if (cur != (void *)hk_EndFrame && cur == (void *)ADDR_RE_EndFrame) {
            orig_EndFrame = (PFN_EndFrame)cur;
            PatchPointer(ADDR_re_client + RE_OFS_EndFrame, (void *)hk_EndFrame);
            Log("hooked re.SetColor, re.DrawStretchPic and re.EndFrame");
        }
    }
    cur = MEM(void *, ADDR_re_client + RE_OFS_AddRefEntityToScene);
    if (cfg.vr && cfg.controllers && cur != (void *)hk_AddRefEntity && cur == (void *)ADDR_RE_AddRefEntityToScene) {
        orig_AddRefEntity = (PFN_AddRefEntity)cur;
        PatchPointer(ADDR_re_client + RE_OFS_AddRefEntityToScene, (void *)hk_AddRefEntity);
        Log("hooked re.AddRefEntityToScene");
    }
    InstallDetours();
}

// ---------------------------------------------------------------------------
// Eye framebuffers
// ---------------------------------------------------------------------------
static PROC GetGLProc(const char *core, const char *ext)
{
    PROC p = gl.GetProcAddress(core);
    if (!p && ext) p = gl.GetProcAddress(ext);
    return p;
}

static void DestroyFramebuffers(void)
{
    if (fb_ok && gl.GetCurrentContext() == fb_ctx) {
        gl.DeleteFramebuffers(2, fb_fbo);
        gl.DeleteRenderbuffers(2, fb_depth);
        gl.DeleteTextures(2, fb_tex);
        if (hud_fbo[0]) {
            gl.DeleteFramebuffers(2, hud_fbo);
            gl.DeleteRenderbuffers(1, &hud_depth);
            gl.DeleteTextures(2, hud_tex);
        }
    }
    fb_ok = 0;
    hud_fbo[0] = hud_fbo[1] = 0;
}

static int ResolveFramebufferFuncs(void)
{
    if (!gl.GenFramebuffers) {
        gl.GenFramebuffers         = (void *)GetGLProc("glGenFramebuffers", "glGenFramebuffersEXT");
        gl.DeleteFramebuffers      = (void *)GetGLProc("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
        gl.BindFramebuffer         = (void *)GetGLProc("glBindFramebuffer", "glBindFramebufferEXT");
        gl.FramebufferTexture2D    = (void *)GetGLProc("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
        gl.GenRenderbuffers        = (void *)GetGLProc("glGenRenderbuffers", "glGenRenderbuffersEXT");
        gl.DeleteRenderbuffers     = (void *)GetGLProc("glDeleteRenderbuffers", "glDeleteRenderbuffersEXT");
        gl.BindRenderbuffer        = (void *)GetGLProc("glBindRenderbuffer", "glBindRenderbufferEXT");
        gl.RenderbufferStorage     = (void *)GetGLProc("glRenderbufferStorage", "glRenderbufferStorageEXT");
        gl.FramebufferRenderbuffer = (void *)GetGLProc("glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT");
        gl.CheckFramebufferStatus  = (void *)GetGLProc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
        gl.BlitFramebuffer         = (void *)GetGLProc("glBlitFramebuffer", "glBlitFramebufferEXT");
        gl.ActiveTexture           = (void *)GetGLProc("glActiveTexture", "glActiveTextureARB");
    }
    return gl.GenFramebuffers && gl.BindFramebuffer && gl.FramebufferTexture2D && gl.GenRenderbuffers &&
           gl.RenderbufferStorage && gl.FramebufferRenderbuffer && gl.CheckFramebufferStatus && gl.BlitFramebuffer;
}

// The headset needs the game's context, so this only runs while it is current. XR_Init is
// rate limited when no headset is connected yet.
static void StartVR(void)
{
    if (!cfg.vr || XR_Ready() || !gl.GetCurrentContext()) return;
    if (XR_Init(fb_w, fb_h, 0)) {
        // The headset paces the frames: don't wait for the monitor's vsync as well.
        BOOL (WINAPI *swapInterval)(int) = (void *)gl.GetProcAddress("wglSwapIntervalEXT");
        if (swapInterval) swapInterval(0);
    }
}

static void InitColorTexture(GLuint tex, int w, int h)
{
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

// HUD: window-sized with depth/stencil (3D models in menus); trash: 16x16, everything outside is clipped.
static int CreateHudBuffers(int w, int h)
{
    int i, ok = 1;
    if (!cfg.vr || !cfg.hud) return 1;
    gl.GenTextures(2, hud_tex);
    gl.GenFramebuffers(2, hud_fbo);
    gl.GenRenderbuffers(1, &hud_depth);
    InitColorTexture(hud_tex[0], w, h);
    InitColorTexture(hud_tex[1], 16, 16);
    gl.BindRenderbuffer(GL_RENDERBUFFER, hud_depth);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    for (i = 0; i < 2; i++) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, hud_fbo[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hud_tex[i], 0);
        if (i == 0) {
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hud_depth);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hud_depth);
        }
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
    }
    if (!ok) {
        gl.DeleteFramebuffers(2, hud_fbo);
        gl.DeleteRenderbuffers(1, &hud_depth);
        gl.DeleteTextures(2, hud_tex);
        hud_fbo[0] = hud_fbo[1] = 0;
    }
    return ok;
}

static int EnsureFramebuffers(void)
{
    HGLRC ctx = gl.GetCurrentContext();
    int w = MEM(int, ADDR_r_glConfig_vidWidth), h = MEM(int, ADDR_r_glConfig_vidHeight);
    int i;
    GLint prevTex = 0;

    if (fb_failed || !ctx || w <= 0 || h <= 0) return 0;
    if (fb_ok && ctx == fb_ctx && w == fb_w && h == fb_h) return 1;
    if (ctx != fb_ctx) fb_ok = 0;   // old context is gone with its objects
    DestroyFramebuffers();

    if (!ResolveFramebufferFuncs()) {
        Log("framebuffer objects unavailable: stereo off");
        fb_failed = 1;
        return 0;
    }

    // The renderer caches which texture each unit has bound; put the binding back afterwards.
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    gl.GenTextures(2, fb_tex);
    gl.GenFramebuffers(2, fb_fbo);
    gl.GenRenderbuffers(2, fb_depth);
    for (i = 0; i < 2; i++) {
        InitColorTexture(fb_tex[i], w, h);

        gl.BindRenderbuffer(GL_RENDERBUFFER, fb_depth[i]);
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

        gl.BindFramebuffer(GL_FRAMEBUFFER, fb_fbo[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_tex[i], 0);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb_depth[i]);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb_depth[i]);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Log("eye framebuffer %d incomplete: stereo off", i);
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
            fb_ok = 1; fb_ctx = ctx;
            DestroyFramebuffers();
            fb_failed = 1;
            return 0;
        }
    }
    if (!CreateHudBuffers(w, h)) Log("HUD buffer incomplete: HUD stays in the eyes");
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    gl.BindRenderbuffer(GL_RENDERBUFFER, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.GetError();

    fb_ok = 1; fb_ctx = ctx; fb_w = w; fb_h = h;
    Log("eye framebuffers %dx%d ready", w, h);

    StartVR();
    return 1;
}

void GL_BlitRect(GLuint srcFbo, int sx, int sy, int sw, int sh, GLuint dstFbo, int dw, int dh)
{
    GLboolean scissor = gl.IsEnabled(GL_SCISSOR_TEST);   // the only state that clips a blit
    if (scissor) gl.Disable(GL_SCISSOR_TEST);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    gl.BlitFramebuffer(sx, sy, sx + sw, sy + sh, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    if (scissor) gl.Enable(GL_SCISSOR_TEST);
}

void GL_Blit(GLuint srcFbo, int w, int h, GLuint dstFbo, int dw, int dh)
{
    GL_BlitRect(srcFbo, 0, 0, w, h, dstFbo, dw, dh);
}

static void BindTarget(GLuint fbo)
{
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    g_boundFbo = 1;
}

// 2D and world-less 3D of the current eye go to the HUD (left) or are discarded (right).
static void BindHud(void)
{
    BindTarget(hud_fbo[g_backEye == 0 ? 0 : 1]);
}

static void ClearHud(void)
{
    BindTarget(hud_fbo[0]);
    gl.PushAttrib(GL_COLOR_BUFFER_BIT | GL_SCISSOR_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    gl.Disable(GL_SCISSOR_TEST);
    gl.ColorMask(1, 1, 1, 1);
    gl.DepthMask(1);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    gl.PopAttrib();
}

static void BindEye(int eye)
{
    gl.BindFramebuffer(GL_FRAMEBUFFER, fb_fbo[eye]);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    g_boundFbo = 1;
}

static void BindWindow(GLenum mode)
{
    if (g_boundFbo) { gl.BindFramebuffer(GL_FRAMEBUFFER, 0); g_boundFbo = 0; }
    gl.DrawBuffer(mode);
}

// ---------------------------------------------------------------------------
// Hooked exports
// ---------------------------------------------------------------------------
void WINAPI hook_glDrawBuffer(GLenum mode)
{
    g_fxTarget = 0;
    if (cfg.enabled && g_singlePass && (mode == GL_BACK || mode == GL_BACK_LEFT || mode == GL_BACK_RIGHT) && EnsureFramebuffers()) {
        if (mode == GL_BACK_RIGHT) {          // thrown-away second pass while switching over
            g_backEye = 1;
            BindTarget(hud_fbo[1] ? hud_fbo[1] : fb_fbo[1]);
            return;
        }
        g_backEye = 0;
        if (g_hudActive && !g_hudCleared) { ClearHud(); g_hudCleared = 1; }
        BindEye(0);
        g_stereoFrameDrawn = 1;
        return;
    }
    if (cfg.enabled && (mode == GL_BACK_LEFT || mode == GL_BACK_RIGHT) && EnsureFramebuffers()) {
        int eye = (mode == GL_BACK_RIGHT);
        g_backEye = eye;
        if (eye == 0 && g_hudActive) ClearHud();
        BindEye(eye);
        if (eye == 1) g_stereoFrameDrawn = 1;
        return;
    }
    g_backEye = -1;
    if (mode == GL_BACK_LEFT || mode == GL_BACK_RIGHT) mode = GL_BACK;  // no stereo pixel format
    BindWindow(mode);
}

void WINAPI hook_glReadBuffer(GLenum mode)
{
    if (g_boundFbo && (mode == GL_BACK || mode == GL_FRONT || mode == GL_BACK_LEFT || mode == GL_BACK_RIGHT ||
                       mode == GL_FRONT_LEFT || mode == GL_FRONT_RIGHT))
        mode = GL_COLOR_ATTACHMENT0;
    gl.ReadBuffer(mode);
}

BOOL WINAPI hook_wglMakeCurrent(HDC dc, HGLRC rc)
{
    BOOL r;
    // The context is about to go away (vid_restart, quit): the session and its swapchains are
    // bound to it, so release them while it is still current.
    if (!rc && XR_Ready() && gl.GetCurrentContext()) { XR_Shutdown(); g_vrFrame = 0; }
    r = gl.MakeCurrent(dc, rc);
    if (!rc) { fb_ok = 0; g_boundFbo = 0; }
    EnsureHooks();
    return r;
}

static BOOL WINAPI hk_SwapBuffers(HDC dc)
{
    if (XR_FrameOpen()) {
        int drawn = g_stereoFrameDrawn && fb_ok;
        vr_hud_t hud = cfg.hudLayer;
        hud.fbo = g_hudActive ? hud_fbo[0] : 0;
        // Without the HUD layer everything is in the eyes, so the eyes are always shown.
        hud.world = g_worldDrawn || !hud.fbo;
        // Scope: only the game's scope screen, close to the face, nothing around it
        if (g_scopeActive && hud.fbo) {
            hud.opaque = 1;
            hud.world = 0;
            hud.width = hud.menuWidth = cfg.scopeWidth;
            hud.distance = cfg.scopeDistance;
            hud.offsetY = 0;
        }
        if (!g_scopeActive) g_lastHudWorld = hud.world;
        if (g_boundFbo) BindWindow(GL_BACK);
        XR_EndFrame(drawn && g_vrFrame ? fb_fbo : NULL, fb_w, fb_h, &hud, NULL);
        // Monitor: the left eye while playing, the HUD buffer (menus) otherwise.
        if (drawn) GL_Blit(hud.world ? fb_fbo[0] : hud.fbo, fb_w, fb_h, 0, fb_w, fb_h);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (g_stereoFrameDrawn && fb_ok) {
        int i;
        if (g_boundFbo) BindWindow(GL_BACK);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        for (i = 0; i < 2; i++) {
            // half-width side by side
            GLboolean scissor = gl.IsEnabled(GL_SCISSOR_TEST);
            int x0 = ((i ^ cfg.swapEyes) ? fb_w / 2 : 0);
            if (scissor) gl.Disable(GL_SCISSOR_TEST);
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb_fbo[i]);
            gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            gl.BlitFramebuffer(0, 0, fb_w, fb_h, x0, 0, x0 + fb_w / 2, fb_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            if (scissor) gl.Enable(GL_SCISSOR_TEST);
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (g_boundFbo) {
        BindWindow(GL_BACK);
    }
    g_stereoFrameDrawn = 0;
    g_worldDrawn = 0;
    g_hudCleared = 0;
    g_scopeActive = 0;
    while (gl.GetError()) {}
    return orig_SwapBuffers(dc);
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
static float IniFloat(const char *sec, const char *key, float def, const char *ini)
{
    char buf[64], d[64];
    sprintf(d, "%g", def);
    GetPrivateProfileStringA(sec, key, d, buf, sizeof(buf), ini);
    return (float)atof(buf);
}

static void LoadConfig(void)
{
    char ini[MAX_PATH];
    _snprintf(ini, sizeof(ini), "%s\\cod_vr.ini", g_dir);
    cfg.enabled    = GetPrivateProfileIntA("Stereo", "Enabled", cfg.enabled, ini);
    cfg.swapEyes   = GetPrivateProfileIntA("Stereo", "SwapEyes", cfg.swapEyes, ini);
    cfg.separation = IniFloat("Stereo", "Separation", cfg.separation, ini);
    cfg.vr         = GetPrivateProfileIntA("VR", "Enabled", cfg.vr, ini);
    cfg.worldScale = IniFloat("VR", "WorldScale", cfg.worldScale, ini);
    cfg.positional = GetPrivateProfileIntA("VR", "PositionalTracking", cfg.positional, ini);
    cfg.headAim    = GetPrivateProfileIntA("VR", "HeadAim", cfg.headAim, ini);
    cfg.hud                 = GetPrivateProfileIntA("HUD", "Enabled", cfg.hud, ini);
    cfg.hudLayer.headLocked = GetPrivateProfileIntA("HUD", "HeadLocked", cfg.hudLayer.headLocked, ini);
    cfg.hudLayer.distance   = IniFloat("HUD", "Distance", cfg.hudLayer.distance, ini);
    cfg.hudLayer.offsetY    = IniFloat("HUD", "OffsetY", cfg.hudLayer.offsetY, ini);
    cfg.hudLayer.width      = IniFloat("HUD", "Width", cfg.hudLayer.width, ini);
    cfg.hudLayer.menuWidth  = IniFloat("HUD", "MenuWidth", cfg.hudLayer.menuWidth, ini);
    cfg.controllers     = GetPrivateProfileIntA("Controllers", "Enabled", cfg.controllers, ini);
    cfg.rightHanded     = GetPrivateProfileIntA("Controllers", "RightHanded", cfg.rightHanded, ini);
    cfg.snapTurn        = IniFloat("Controllers", "SnapTurn", cfg.snapTurn, ini);
    cfg.turnSpeed       = IniFloat("Controllers", "SmoothTurnSpeed", cfg.turnSpeed, ini);
    cfg.converge        = IniFloat("Controllers", "ConvergeDistance", cfg.converge, ini);
    cfg.weaponInHand    = GetPrivateProfileIntA("Controllers", "WeaponInHand", cfg.weaponInHand, ini);
    cfg.weaponOffset[0] = IniFloat("Controllers", "WeaponOffsetForward", cfg.weaponOffset[0], ini);
    cfg.weaponOffset[1] = IniFloat("Controllers", "WeaponOffsetRight", cfg.weaponOffset[1], ini);
    cfg.weaponOffset[2] = IniFloat("Controllers", "WeaponOffsetUp", cfg.weaponOffset[2], ini);
    cfg.twoHanded       = GetPrivateProfileIntA("Controllers", "TwoHanded", cfg.twoHanded, ini);
    cfg.twoHandReach    = IniFloat("Controllers", "TwoHandReach", cfg.twoHandReach, ini);
    cfg.scope           = GetPrivateProfileIntA("Scope", "Enabled", cfg.scope, ini);
    cfg.scopeMaxFov     = IniFloat("Scope", "MaxFov", cfg.scopeMaxFov, ini);
    cfg.scopeWidth      = IniFloat("Scope", "Width", cfg.scopeWidth, ini);
    cfg.scopeDistance   = IniFloat("Scope", "Distance", cfg.scopeDistance, ini);
    cfg.twoHandGripHeight = IniFloat("Controllers", "TwoHandGripHeight", cfg.twoHandGripHeight, ini);
    cfg.logNear         = GetPrivateProfileIntA("Debug", "LogNearEntities", cfg.logNear, ini);
    cfg.muzzleOffset[0] = IniFloat("Controllers", "MuzzleOffsetRight", cfg.muzzleOffset[0], ini);
    cfg.muzzleOffset[1] = IniFloat("Controllers", "MuzzleOffsetUp", cfg.muzzleOffset[1], ini);
    cfg.roomScale       = GetPrivateProfileIntA("VR", "RoomScale", cfg.roomScale, ini);
    cfg.weaponAngle[0]  = IniFloat("Controllers", "WeaponPitch", cfg.weaponAngle[0], ini);
    cfg.weaponAngle[1]  = IniFloat("Controllers", "WeaponYaw", cfg.weaponAngle[1], ini);
    cfg.aimSmoothing    = IniFloat("Controllers", "AimSmoothing", cfg.aimSmoothing, ini);
    cfg.autoAds         = GetPrivateProfileIntA("Controllers", "AutoADS", cfg.autoAds, ini);
    cfg.menuPointer     = (float)GetPrivateProfileIntA("Controllers", "MenuPointer", (int)cfg.menuPointer, ini);
    {
        int b;
        for (b = 0; b < BT_COUNT; b++)
            GetPrivateProfileStringA("Buttons", g_buttons[b].key, g_buttons[b].def, cfg.bind[b], sizeof(cfg.bind[b]), ini);
    }
    if (cfg.enabled && !IsKnownExe()) {
        Log("not CoDSP.exe 1.5 (timestamp %08x expected): passthrough only", CODSP_TIMESTAMP);
        cfg.enabled = 0;
    }
    if (!cfg.enabled) cfg.vr = 0;
}

#define RESOLVE(field, name) gl.field = (void *)GetProcAddress(g_realDll, name)

static BOOL LoadRealOpenGL(void)
{
    char path[MAX_PATH];
    int i;

    GetSystemDirectoryA(path, sizeof(path));   // SysWOW64 for a 32-bit process
    strcat(path, "\\opengl32.dll");
    g_realDll = LoadLibraryA(path);
    if (!g_realDll) { Log("cannot load %s", path); return FALSE; }
    for (i = 0; i < g_realCount; i++) g_real[i] = GetProcAddress(g_realDll, g_realNames[i]);

    RESOLVE(DrawBuffer, "glDrawBuffer");         RESOLVE(ReadBuffer, "glReadBuffer");
    RESOLVE(MakeCurrent, "wglMakeCurrent");      RESOLVE(GetProcAddress, "wglGetProcAddress");
    RESOLVE(GetCurrentContext, "wglGetCurrentContext");
    RESOLVE(GetCurrentDC, "wglGetCurrentDC");
    RESOLVE(Enable, "glEnable");                 RESOLVE(Disable, "glDisable");
    RESOLVE(IsEnabled, "glIsEnabled");
    RESOLVE(GenTextures, "glGenTextures");       RESOLVE(DeleteTextures, "glDeleteTextures");
    RESOLVE(BindTexture, "glBindTexture");       RESOLVE(TexImage2D, "glTexImage2D");
    RESOLVE(TexParameteri, "glTexParameteri");   RESOLVE(GetIntegerv, "glGetIntegerv");
    RESOLVE(GetError, "glGetError");
    RESOLVE(Clear, "glClear");                   RESOLVE(ClearColor, "glClearColor");
    RESOLVE(PushAttrib, "glPushAttrib");         RESOLVE(PopAttrib, "glPopAttrib");
    RESOLVE(ColorMask, "glColorMask");           RESOLVE(DepthMask, "glDepthMask");
    RESOLVE(MatrixMode, "glMatrixMode");         RESOLVE(LoadMatrixf, "glLoadMatrixf");
    RESOLVE(PushMatrix, "glPushMatrix");         RESOLVE(PopMatrix, "glPopMatrix");
    RESOLVE(Begin, "glBegin");                   RESOLVE(End, "glEnd");
    RESOLVE(Vertex3f, "glVertex3f");             RESOLVE(Color4f, "glColor4f");
    RESOLVE(LineWidth, "glLineWidth");           RESOLVE(PointSize, "glPointSize");
    RESOLVE(Viewport, "glViewport");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    char logPath[MAX_PATH], *slash;
    HMODULE self;

    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    // Stay loaded across vid_restart (QGL_Shutdown frees opengl32) so hooked pointers never dangle.
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)DllMain, &self);

    GetModuleFileNameA(NULL, g_dir, sizeof(g_dir));
    if ((slash = strrchr(g_dir, '\\')) != NULL) *slash = 0;
    _snprintf(logPath, sizeof(logPath), "%s\\cod_vr.log", g_dir);
    g_log = fopen(logPath, "w");
    if (!g_log) {
        GetTempPathA(sizeof(logPath), logPath);
        strcat(logPath, "cod_vr.log");
        g_log = fopen(logPath, "w");
    }

    LoadConfig();
    Log("cod_vr phase 4 loaded: enabled=%d vr=%d separation=%.2f worldScale=%.2f positional=%d headAim=%d "
        "hud=%d (distance %.2f, width %.2f, menu %.2f, headLocked %d)",
        cfg.enabled, cfg.vr, cfg.separation, cfg.worldScale, cfg.positional, cfg.headAim,
        cfg.hud, cfg.hudLayer.distance, cfg.hudLayer.width, cfg.hudLayer.menuWidth, cfg.hudLayer.headLocked);
    Log("controllers=%d rightHanded=%d snapTurn=%.0f converge=%.1f weaponInHand=%d offset=(%.1f %.1f %.1f)",
        cfg.controllers, cfg.rightHanded, cfg.snapTurn, cfg.converge, cfg.weaponInHand,
        cfg.weaponOffset[0], cfg.weaponOffset[1], cfg.weaponOffset[2]);
    if (!LoadRealOpenGL()) return FALSE;
    EnsureHooks();
    return TRUE;
}
