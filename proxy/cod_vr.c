// cod_vr: proxy opengl32.dll that turns on the dormant Quake 3 stereo path of CoDSP.exe 1.3
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
#include <ctype.h>

#include "../codsp_addresses.h"

addr_t g_A = {
#define X(n, sp, uo) sp,
#include "../addr_values.h"
#undef X
};
static const addr_t g_addrUO = {
#define X(n, sp, uo) uo,
#include "../addr_values.h"
#undef X
};
int g_uo;
#define GAME_DLL  (g_uo ? "uo_gamex86.dll" : "gamex86.dll")
#define CGAME_DLL (g_uo ? "uo_cgamex86.dll" : "cgamex86.dll")

// CoDSP.exe (1.3) or CoDUOSP.exe (United Offensive): which set of addresses to use.
static void SelectGame(void)
{
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp == 0x412653f0) { memcpy(&g_A, &g_addrUO, sizeof(g_A)); g_uo = 1; }
}

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
    int   grenadeArc;       // 0 = off, 1 = while the trigger is held with a grenade, 2 = whenever a grenade is in hand
    float gravity;          // game units / s^2 (g_gravity)
    float grenadeOpacity;   // 0..1, trajectory line
    float scopeSmoothing;   // seconds: gun direction filter while the scope is up (0 = AimSmoothing)
    int   traceMask;        // contents the trajectory collides with
    float worldFlashLength; // scale of the spread of world flash sprites along the barrel
    int   realistic;        // 0 = Arcade (gun always in the dominant hand), 1 = Realistic (draw, holster, drop)
    int   dropOnRelease;    // Realistic: letting go of the gun away from a holster drops it on the floor
    float backHeight;       // Realistic, metres from the eyes: hands above this and behind the head reach the back
    float hipHeight;        // height of the waist holsters
    float lowerBackHeight;  // height of the holster behind the waist
    int   hipAccuracy;      // 1 = shots without the game's aim-down-sights spread like aimed ones
    int   mirrorLeft;       // 1 = a gun in the left hand is drawn mirrored
    int   drawTime;         // Realistic: msec to put a weapon away / raise one (0 = the game's animations)
    int   zoneAct[8];       // Holsters: what each body zone (ZONE_*) holds (ACT_*)
    float subtitleSize;     // HUD: how big the subtitles are drawn, 1 = the game's size
    float shakeKeep;        // comfort: share of the camera shake (earthquakes, explosions) that is kept, 1 = all of it
    float vehicleSmooth;    // comfort: seconds the view takes to follow a vehicle's turns (longer = gentler)
    float turretOffset[3];  // Turret: where the player stands relative to a mounted MG42, in cm along the gun (X forward, Y left, Z up)
    int   turretNoCollision;// Turret: the gun is not solid for the player while mounted (it pushed the player when aimed down)
    float waistReach;       // Holsters: metres from the body (seen from above) within which a hand is at a waist holster
    int   menuFollowsHead;  // 1 = menus and loading screens follow the head, 0 = fixed in front of you
    int   objOnHand;        // 1 = the objectives screen shows above the left hand while it is held
    int   hudMode;          // HUD_* : what of the game's HUD stays on screen (all of it while Objectives is held)
    int   infiniteAmmo;     // MODS: 1 = the reserve stays full, 2 = the clip too (no reloading)
    int   difficulty;       // MODS: 0 normal, 1 real life (one hit kills), 2 perma death, 3 both, 4 god mode
    int   gore;             // MODS: 0 no blood, 1 the game's blood, 2 gore (Main\pakz_vr_gore.pk3)
    int   modernFont;       // 1 = the text in Segoe UI (Main\pakz_vr_font.pk3, made by the installer)
    int   logHud;           // diagnostics: log the HUD's pictures
    float objWidth;         // metres
    float objHeight;        // metres above the hand
    int   emptyPointer;     // 1 = with empty hands the right hand aims, with a line to where it points
    float renderScale;      // eye resolution, times the headset's recommended size
    int   vignette;         // comfort: narrow the view while moving with the stick or turning smoothly
    float vehicleVignette;  // comfort: its strength while a vehicle carries the player (0 = off)
    float vignetteStrength; // 0..1, how far it closes
    float vignetteSpeed;    // game units per second where it starts closing (walking is about 190)
    float vignetteTurn;     // degrees per second where it starts closing
    float haptics;          // controller vibration strength, 0 = off (default of the three below)
    float hapShots;         // vibration strength of the shots
    float hapDamage;        // ... of being hit
    float hapHolster;       // ... of the holsters and of taking, putting away, dropping weapons
    int   physCrouch;       // 1 = crouching for real crouches the soldier
    float crouchDepth;      // metres the head goes below its standing height to crouch
    float heightOffset;     // metres added to the head height (taller / shorter in the game)
    float twoHandStab;      // 0..1: aim steadiness while the gun is held with both hands
    float twoHandKick;      // 0..1: share of the recoil taken away while the gun is held with both hands
    float objBackground;    // 0..1: opacity of the dark panel behind the objectives on the hand
    float objMargin;        // share of the screen on each side between the panel's edge and the objectives
    int   msaa;             // samples per pixel in the eye buffers (1 = off)
    float anisotropy;       // texture filtering level forced on every mipmapped texture (1 = off)
    postfx_t postfx;        // sharpening, bloom, colour style of the eye images
    float ssao;             // ambient occlusion strength, 0 = off
    float ssaoRadius;       // game units
    float cullMargin;       // degrees added on each side of the view for what the game draws
    int   swingMelee;       // 1 = swinging the gun into someone is a melee hit (the game's own)
    float meleeSpeed;       // m/s the moving part of the gun needs
    int   crosshair;        // crosshair style where the gun is aimed (RETICLE_*, 0 = none)
    float crosshairSize;    // degrees across
    int   rightEyeFirst;    // diagnostics: render the right eye's scene before the left one
    int   dumpEyes;         // diagnostics: save the left eye (rendered and sent) 15 s into a level
    int   swapSticks;       // older setting: 1 = moving on the gun hand's stick, turning on the other
    int   moveStick;        // 0 = the left stick moves and the right one turns, 1 = the other way round, -1 = by hand (swapSticks)
    int   headCollision;    // 1 = the camera stops a little before walls when the head leans or walks into them
    int   holsterHum;       // 1 = a soft continuous vibration while a hand is in a holster it can use
    int   objGesture;       // 1 = the objectives show while the back of the off hand faces the eyes (WatchGesture)
    float objWatchWidth;    // metres: the objectives screen lying on the back of the off hand, as a watch
    char  shortcut[4][4][64];   // [A, B, X, Y held][A, B, X, Y pressed]: the shortcut's command ([Buttons] ShortcutXA ...); only the two buttons of the hand that does not hold the gun are held ones
    float turretSeatLift;   // Turret: game units added to the seat of a gun that makes the soldier crouch (the bomber's tail gun)
    float turretSeat[2][3]; // Turret: extra X / Y / Z (cm, the gun's own directions) for the bomber's dorsal [0] and tail [1] guns
    int   moveMode;         // Controllers: 0 Blink (aim with the move stick, the screen blinks, teleport), 1 Shift (glide there), 2 Continuous
} cfg = { 1, 2.5f, 0, 1, 39.37f, 1, 1, 1, { 0, 0, 0, 1, 1.5f, -0.05f, 1.0f, 1.8f },
          1, 1, 45.0f, 150.0f, 15.0f, 1, { 10.0f, 5.0f, -7.0f }, 1, 0.7f, 0.04f, { { 0 } },
          1, 35.0f, 1.4f, 0.7f, 1, 1, { 0.0f, 0.0f }, 1, { 0.0f, 0.0f }, 0.0f, 0, 1, 800.0f, 0.6f, 0.15f, CONTENTS_SOLID };

// Buttons -> game commands. "+cmd" is held while pressed ("-cmd" on release).
enum { BT_PTRIGGER, BT_PGRIP, BT_STRIGGER, BT_SGRIP, BT_A, BT_B, BT_X, BT_Y, BT_LCLICK, BT_RCLICK,
       BT_MENU, BT_STICKUP, BT_STICKDOWN, BT_STICKLEFT, BT_STICKRIGHT, BT_COUNT };
static const struct { const char *key, *def; } g_buttons[BT_COUNT] = {
    { "PrimaryTrigger", "+attack" },        // dominant hand
    { "PrimaryGrip", "+activate" },         // use / pick up
    { "SecondaryTrigger", "+speed" },       // off hand: aim down the sights (zoom with scopes)
    { "SecondaryGrip", "" },                // off hand: holds the gun with both hands (TwoHanded)
    { "A", "+gostand" },                    // the buttons of the dominant hand run their own binds at once (reload while walking)
    { "B", "+reload" },
    { "X", "" },                            // the other hand's two (X and Y for the right-handed, A and B for the left-handed) are the
    { "Y", "" },                            // wildcard buttons (UpdateFaceButtons): tapped, their own bind; held, with another button, a shortcut
    { "LeftStickClick", "vr:recenter" },    // stand up straight and press: height and roomscale centre
    { "RightStickClick", "toggleprone" },
    { "Menu", "key:ESCAPE" },              // the game opens its menu on the Escape key
    { "TurnStickUp", "raisestance" },
    { "TurnStickDown", "lowerstance" },
    { "TurnStickLeft", "vr:turnleft" },     // the stick that does not move: its four directions run these
    { "TurnStickRight", "vr:turnright" },
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
static int    fb_w, fb_h, fb_ok, fb_failed;   // the game's window size (HUD, 2D, refdef coordinates)
// The eye buffers are the headset's size, not the window's: the game sets its viewport and
// scissor in window pixels, and hook_glViewport / hook_glScissor scale them while an eye
// buffer is bound (the game never notices, its pixels are just smaller).
static int    eye_w, eye_h, g_eyeBound;
static GLint  g_vp[4], g_sc[4];
static int    g_vpSet, g_scSet;
static GLuint fb_fbo[2], fb_tex[2], fb_depth[2];
// MSAA: the eyes are drawn into multisampled buffers and resolved into fb_tex at the end of the frame
static GLuint fb_ms[2], fb_msColor[2], fb_msDepth[2];
static float  g_worldProj[2] = { -1.0f, -8.0f };   // projection [10], [14] of the eyes' world view
static int    g_depthCaptured[2];                 // this frame's eye depth is kept for the SSAO
static int    g_msaaOn;   // samples, 0 = off
// HUD buffer (the left eye's 2D) and a tiny buffer that swallows the right eye's identical 2D,
// which would otherwise be blended twice.
static GLuint hud_fbo[2], hud_tex[2], hud_depth;
static int    g_backEye = -1;          // eye the backend is drawing (from glDrawBuffer), -1 mono
static int    g_hudActive;             // this frame sends 2D to the HUD buffer
static int    g_worldDrawn;            // the left eye drew the 3D world this frame
// A script's briefing (slides over a black picture over the whole screen, "fire to skip") is a
// 2D picture stack, not a view: it goes to the screen in front of the player like a menu, and
// the trigger skips it. g_slideShow is what the frame before showed (the controls read it).
static int    g_slideFrame, g_slideShow;
static int    g_fullShader = -1;       // diagnostics: the shader of the last full-screen picture drawn (see the "screen:" log line)
static float  g_fullAlpha;
static int    g_eye = -1;              // eye currently being rendered (0 left, 1 right), -1 mono
static int    g_boundFbo;              // nonzero while an eye FBO is bound
static int    g_stereoFrameDrawn;      // set once the right eye drew this frame
static int    g_stereoActive;          // we turned on stereoEnabled
static int       g_singlePass;            // headset frame: one cgame pass, both eyes per world scene
static int       g_hudCleared;            // the HUD buffer was cleared this frame
static int       g_nearLogFrames, g_nearLogLines;   // diagnostics budget
static float     g_lastViewOrg[3], g_lastViewAxis[3][3];
static float     g_rawViewOrg[3];      // the game's view origin before FilterShake: what its view-weapon entities are placed from
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
static int       g_linkedScene;        // the player is linked to something that is not a vehicle (a scripted scene: the head is free on its view)
static int       g_viewLocked;         // the game holds the view (a scripted scene): it does not follow what we write
static float     g_headYawCmd;         // aim yaw (head or hand) already contained in the game's view this frame
// In a vehicle (the player linked to another entity: truck, car, plane) the game turns and clamps
// the view with the vehicle and shakes it with every bump. The body then follows the vehicle's
// own yaw, smoothed, and the head looks around freely.
static int       g_inVehicle;
static int       g_gunVehicle;        // the vehicle is a gun (Flak 88, a tank): it fires, no ride hints
static int       g_rideGun;           // United Offensive: the vehicle with a gun (the jeep) is moving, so it is a ride too: vignette strength and skip-mission work on it
static int       g_noRideAids;        // the vehicle is the boat of "ship" or "stalingrad": no vignette, no skip-mission hold and no ride hints (the mission needs the player to crouch)
static int       g_tankMode;          // driving a tank: left stick drives, right stick turns the gun, the head only looks
static float     g_tankStick[2], g_tankDt;
static float     g_vehicleBody, g_vehicleTurnRate, g_vehicleSpeed;
static float     g_vigStrength;
// Turrets (MG42, 30 cal, Flak 88): the player on one, or next to a free one
static int       g_onTurret, g_nearTurret, g_turretGrab, g_turretTarget, g_turretUseIn, g_turretInView;
static float     g_turretPivot[3], g_turretRestPitch;   // tracking space, fixed while on it
static float     g_turretPivotWorld[3], g_turretYaw;    // the gun's pivot in the world, where it faces
static DWORD     g_turretOnAt;                           // when the player got on
static float     g_turretOrgBase[3];  // the eyes' place as the game put them, before the Turret offsets
static int       g_turretEnt, g_turretContentsOrig, g_turretContentsZeroed;   // the mounted gun's collision (see UpdateTurret)
static int       g_turretViaGrips;                       // our own start put him on (not the game's use)
static int       g_turretScripted;                       // a mission script put him on (UO's bomber: dorsal turret `useby`)
static float     g_turretCanon[3];                       // the seat: the game's gunner eye minus the pivot, world axes, fixed at mount
static float     g_turretCanonYaw, g_turretYawNow;       // the gun base's yaw when the seat was taken, and now (the plane turns)
static int       g_turretIdx;                            // entity number of the gun he is on
static int       g_turretLift;                         // the game crouches the gunner (tail gun): the seat is lifted (cfg.turretSeatLift)
static int       g_turretPivotOk, g_turretRestSet;
static float     g_turretBody;        // the body's yaw while on a turret: where it was when getting on
static float     g_turretOrg[3];      // and the eyes' place (the game swings the player around the pivot)
static int       g_turretOrgSet;
static DWORD     g_turretReleasedAt;
static int       g_vehicleRecenter;   // the next frame faces the vehicle's front again
static int       g_vehAimInit;        // the vehicle aim has a last frame to compare with
static DWORD     g_vignetteShownAt;   // when the vehicle vignette was last changed (its bar shows)
static float     g_vigAmount;         // (defined with the vignette below)
static float     g_tpFade;            // Blink: 0..1, how black both eyes are (see UpdateTeleport)
static int       g_ladderMove;        // on a ladder or right at one: the move stick climbs (no teleport aiming)
static char      g_userIni[MAX_PATH]; // (and the player settings with the config)
static float BodyYaw(const float viewFwd[3]);
static int MapLoaded(void);
static BYTE *PlayerClient(void);
static void GamePrintBold(const char *text);
static int CurrentMap(char *out, int len);
static void PatchPointer(DWORD addr, void *value);
static float     g_headYawPrev;        // aim yaw written to cl.viewangles last
static int       g_headYawInit;
static int       g_aimFromHand;        // aim source of the last write
static vr_input_t g_in;                // controllers, read at the left eye's BeginFrame
static int       g_btnDown[BT_COUNT];
static int       g_turnLatched, g_stanceLatched, g_sideLatched;
static LARGE_INTEGER g_lastInputTick;
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
static float     g_aimDir[3];          // the shot direction written to cl.viewangles (tracking space)
static int       g_scopeWasActive;     // the last frame showed the scope view
static float     g_lastSmoothing;      // filter time constant used last frame
static int       g_scopeActive;        // this frame the zoomed view is drawn into the HUD buffer
static float     g_scopeFov;           // the fov of that zoomed view
static int       g_scopeOverlay;       // this frame the game drew its scope overlay over it (DrawScopeMask is not needed)
static int       g_lastHudWorld = 1;   // last frame showed the world (HUD size) or a menu (menu size)
static int       g_menuQuadValid;      // menus / loading screens: the screen stays where it appeared
static float     g_menuRefZ;           // the head height it was placed at
static vr_pose_t g_menuQuad;
static int       g_objOnHand;          // the objectives screen is held up above the left hand
static int       g_pointerOn;          // empty hands: the right hand aims, a line shows where
// menus
static int       g_menuMode;           // the UI catches the keys
static int       g_menuKeyDown[6];     // mouse1, escape, enter, up, down, menu
static float     g_cursor[2];          // where we last put the UI cursor (640x480)
static DWORD     g_cursorSync;
// view weapon entities held back until the world scene, when the game's view is known
#define MAX_HELD 32
static BYTE      g_held[MAX_HELD][0x100];   // refEntity_t is REFENT_SIZE (0x9c) bytes
static int       g_heldParam[MAX_HELD], g_heldWorldFlash[MAX_HELD], g_heldCount;
static float     g_clPitchCmd;         // cl.viewangles pitch the current frame was predicted with
static float     g_pitchOffset;        // game pitch - cl.viewangles pitch (delta_angles), filtered
static int       g_pitchOffsetInit;
static int       g_pitchSampled;       // first world scene of the frame measured the offset

static void StartVR(void);
static signed char ClampMove(float v);
static void (__cdecl *Detour(DWORD target, const BYTE *prologue, int len, void *hook))(void);
static void FlushHeldEntities(const cod_refdef_t *fd);
static void ProcessControllers(void);
static void Recenter(void);
static void Buzz(int h, float amp, int ms, float strength);
static const char *WeaponName(const BYTE *def);
static void InitColorTexture(GLuint tex, int w, int h);
static void UpdateAutoAds(void);
static void UpdateGrenadeArc(void);
static void UpdateHudMode(int objectivesHeld);
static void QueueEvent(int type, int value, int value2);
static void HookKeyBindingText(void);
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
// Height: the tracking origin is where the head was when the headset started, which is the
// game's eye height. Recenter moves that reference to the head's current height; while the
// player crouches for real and the game crouches the soldier for it, g_crouchCorr gives back the
// game's own drop of the eye, so the camera stays at the real head.
static float g_heightRef, g_crouchCorr;

static float HeightShift(void) { return -g_heightRef + cfg.heightOffset + g_crouchCorr; }

static void BodyOffset(const float p[3], float out[3])
{
    if (!cfg.positional) {
        out[0] = p[0] - g_head.pos[0]; out[1] = p[1] - g_head.pos[1]; out[2] = p[2] - g_head.pos[2];
    } else if (cfg.roomScale && g_anchorInit) {
        out[0] = p[0] - g_anchor[0]; out[1] = p[1] - g_anchor[1]; out[2] = p[2] + HeightShift();
    } else {
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2] + HeightShift();
    }
}

// Roomscale, once per frame on the main scene: account for how far the game moved the player
// because of the last roomscale command, then ask it to walk to the head again. Walls stop the
// player as usual; the camera then stays where the head is.
static void UpdateRoomScale(const cod_refdef_t *fd)
{
    float body = BodyYaw(fd->viewaxis[0]), c[3], d[3], dl[3], len;
    int i;

    g_roomForward = g_roomRight = 0;
    if (!cfg.roomScale || !cfg.positional) return;
    if (g_inVehicle || g_onTurret || g_linkedScene) {
        // carried by a vehicle, swung around a turret's pivot, or linked by a script (UO's Noville
        // starts on a tank through a script_origin: not a vehicle) the player cannot walk, and that
        // movement is not the player's: no catching up with the head (it pulled the view along)
        g_roomFrac = 0;
        g_lastGameOrgValid = 0;
        return;
    }
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

// The body's yaw: where the game's view points minus the aim already in it, or the vehicle's.
static float BodyYaw(const float viewFwd[3])
{
    if (g_onTurret) return g_turretBody;
    return g_inVehicle ? g_vehicleBody : YawOf(viewFwd) - g_headYawCmd;
}

// Where a seat faces, from the vehicle's front, where it is not the front: the car at the end of
// Dawnville is got into from the side, looking at the driver's window.
static float SeatTurn(void)
{
    char map[64];
    if (CurrentMap(map, sizeof(map)) && !_stricmp(map, "bastogne1")) return 90.0f;   // United Offensive's first truck: counter-clockwise
    if (CurrentMap(map, sizeof(map)) && !_stricmp(map, "dawnville")) return -90.0f;   // clockwise
    if (CurrentMap(map, sizeof(map)) && !_strnicmp(map, "tankdrive", 9)) return 90.0f;   // counter-clockwise
    return 0.0f;
}

// Once per frame: linked to a vehicle or not, and the smoothed vehicle yaw.
static void TankDiagnostics(void)
{
    static DWORD next;
    char map[64];
    BYTE *gm, *pc;
    if (GetTickCount() < next) return;
    next = GetTickCount() + 2000;
    if (!CurrentMap(map, sizeof(map)) || _strnicmp(map, "tankdrive", 9)) return;
    gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    pc = PlayerClient();
    __try {
        BYTE *me = gm + G_OFS_entities;
        const BYTE *tag = *(const BYTE **)(me + GENT_OFS_tagInfo), *parent = tag ? *(const BYTE **)tag : NULL;
        Log("tank diag: in vehicle %d (tank %d), tag %p parent %p (vehicle %p, weapon %d), pm_type %d pm_flags %x,"
            " ps view %.1f %.1f, camera %.1f, turret %d",
            g_inVehicle, g_tankMode, tag, parent, parent ? *(void **)(parent + GENT_OFS_vehicle) : NULL,
            parent ? parent[GENT_OFS_weapon] : -1, pc ? *(int *)(pc + 4) : -1, pc ? *(int *)(pc + GCL_OFS_pmFlags) : -1,
            pc ? *(float *)(pc + GCL_OFS_viewangles) : 0, pc ? *(float *)(pc + GCL_OFS_viewangles + 4) : 0,
            YawOf(g_lastViewAxis[0]), pc ? *(int *)(pc + 0x360) : -1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void UpdateVehicle(float dt)
{
    TankDiagnostics();
    static float offset, lastYaw, lastOrg[3];
    static int haveLast;
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    const BYTE *parent = NULL;
    float yaw, target, d;
    int scene = 0;
    if (gm && MapLoaded()) {
        __try {
            const BYTE *tag = *(const BYTE **)(gm + G_OFS_entities + GENT_OFS_tagInfo);
            if (tag) parent = *(const BYTE **)tag;
            if (parent && *(void **)(parent + GENT_OFS_turretInfo)) parent = NULL;   // a turret, not a vehicle
            // linked to a script model that is no vehicle (the queue for ammo in Stalingrad: the
            // script carries the player along a line): nothing sits in a seat, so the game's own
            // view stays and the head looks freely over it
            if (parent && !*(void **)(parent + GENT_OFS_vehicle)) { parent = NULL; scene = 1; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            parent = NULL;
        }
    }
    if (g_onTurret) { parent = NULL; scene = 0; }
    if (scene != g_linkedScene) {
        g_linkedScene = scene;
        Log("linked scene: %s", scene ? "the head is free over the game's view" : "over");
        if (!scene) g_viewLocked = 0;
    }
    if (!parent) {
        if (g_inVehicle) { Log("vehicle: left"); g_vigAmount = 0; }
        g_gunVehicle = g_tankMode = g_noRideAids = g_rideGun = 0;
        g_inVehicle = 0;
        g_vehicleTurnRate = g_vehicleSpeed = 0;
        haveLast = 0;
        return;
    }
    __try {
        const float *ang = (const float *)(parent + GENT_OFS_currentAngles);
        const float *org = (const float *)(parent + GENT_OFS_currentOrigin);
        yaw = ang[1];
        if (haveLast && dt > 0) {
            float dx = org[0] - lastOrg[0], dy = org[1] - lastOrg[1];
            g_vehicleSpeed += (sqrtf(dx * dx + dy * dy) / dt - g_vehicleSpeed) * (1.0f - expf(-dt / 0.3f));
            if (g_vehicleSpeed > 3000.0f) g_vehicleSpeed = 0;
        }
        memcpy(lastOrg, org, sizeof(lastOrg));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (g_uo && g_gunVehicle && !g_rideGun && g_vehicleSpeed > 60.0f) { g_rideGun = 1; Log("vehicle gun: it drives, so the ride aids (vignette setting, skip) are on"); }
    if (!g_inVehicle) {
        char map[64];
        __try {
            g_gunVehicle = *(void **)(parent + GENT_OFS_vehicle) && parent[GENT_OFS_weapon];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_gunVehicle = 0;
        }
        g_tankMode = g_gunVehicle && CurrentMap(map, sizeof(map)) && !_strnicmp(map, "tankdrive", 9);
        // the boat of "ship" (the crossing to Stalingrad) and the one Stalingrad starts in keep the free head and the
        // vehicle-aligned view, but they are not rides to skip or to feel sick in, and the missions need the player to crouch
        g_noRideAids = CurrentMap(map, sizeof(map)) && (!_stricmp(map, "ship") || !_strnicmp(map, "stalingrad", 10));
    }
    if (!g_inVehicle || g_vehicleRecenter) {
        // getting in (or recentering): the way the head looks now is the vehicle's front; on a
        // gun, where the game points it (the barrel)
        g_vehicleBody = AngleNormalize180((g_gunVehicle ? YawOf(g_lastViewAxis[0]) : yaw) + SeatTurn() - YawOf(g_head.axis[0]));
        offset = AngleNormalize180(g_vehicleBody - yaw);
        if (g_vehicleRecenter) { g_vehicleRecenter = 0; Log("vehicle: recentered"); }
        g_inVehicle = 1;
        g_vehicleTurnRate = 0;
        lastYaw = yaw;
        Log("vehicle: in (entity %d), yaw %.1f%s", (int)((parent - (gm + G_OFS_entities)) / G_ENTITY_SIZE), yaw,
            g_tankMode ? ", a tank" : g_gunVehicle ? ", a gun" : "");
        if (g_tankMode) GamePrintBold("Left stick drives the tank, the gun follows your head.");
        else if (g_gunVehicle && !g_vehicleRecenter) GamePrintBold(g_uo ? "Point the controller of your gun hand to aim." : "Move your head to aim at the target.");
    }
    // follows the vehicle's turns without its jolts (a gun turns with its own aim: the body
    // stays, unless the whole thing is driving)
    if (g_tankMode || (g_gunVehicle && g_vehicleSpeed < 30.0f)) { offset = AngleNormalize180(g_vehicleBody - yaw); lastYaw = yaw; haveLast = 1; return; }
    target = yaw + offset;
    d = AngleNormalize180(target - g_vehicleBody);
    if (dt > 0) {
        // the body follows the vehicle's turns; a slow follow with a speed limit keeps a sharp bend
        // from swinging the whole view round at once, which is what makes people sick
        float step = d * (1.0f - expf(-dt / cfg.vehicleSmooth)), most = 50.0f * dt;
        if (cfg.vehicleSmooth > 0.2f) step = step > most ? most : step < -most ? -most : step;
        g_vehicleBody = AngleNormalize180(g_vehicleBody + step);
        g_vehicleTurnRate += (fabsf(AngleNormalize180(yaw - lastYaw)) / dt - g_vehicleTurnRate) * (1.0f - expf(-dt / 0.3f));
    }
    lastYaw = yaw;
    haveLast = 1;
}

static void UpdateViewAngles(float yaw, float pitch, int fromHand)
{
    float *va = (float *)ADDR_cl_viewangles;

    // A change of aim source (head <-> hand) is a turn of the game's view like any other: the
    // world stays where it is. (Starting over from the new yaw would turn the world by the angle
    // between head and hand: 180 degrees when drawing a gun from the back.)
    if (!g_headYawInit) { g_headYawPrev = yaw; g_headYawInit = 1; }
    g_aimFromHand = fromHand;
    g_headYawCmd = g_headYawPrev;          // what this frame's view was built from
    if (g_viewLocked) g_headYawCmd = 0;    // a held view has none of our aim in it: the head turns freely on top
    g_clPitchCmd = va[PITCH];
    if (!cfg.headAim) return;

    if (g_tankMode) {
        // A tank's gun is turned by the mouse, so the head sends the game mouse moves
        // (about 0.11 degrees a count) and the gun, which follows the view, turns with them.
        static float fx, fy;
        int ix, iy;
        // The gun follows where the head looks: its turn rate is the angle it is off by.
        float ey = AngleNormalize180(g_vehicleBody + YawOf(g_head.axis[0]) - YawOf(g_lastViewAxis[0]));
        float ep = PitchOf(g_head.axis[0]) - PitchOf(g_lastViewAxis[0]);
        float ry = fabsf(ey) < 2.0f ? 0 : ey * 3.0f, rp = fabsf(ep) < 1.5f ? 0 : ep * 3.0f;
        if (ry > 90.0f) ry = 90.0f;
        if (ry < -90.0f) ry = -90.0f;
        if (rp > 50.0f) rp = 50.0f;
        if (rp < -50.0f) rp = -50.0f;
        fx -= ry * g_tankDt / 0.11f;
        fy += rp * g_tankDt / 0.11f;
        ix = (int)fx; iy = (int)fy;
        fx -= ix; fy -= iy;
        if (ix || iy) QueueEvent(SE_MOUSE, ix, iy);
        g_headYawPrev = yaw;
        return;
    }
    if (g_inVehicle) {
        // The body follows the vehicle, and the camera the game draws follows the seat it is
        // linked to: neither is where the shots go. Those leave along the player's own view angles
        // (ps.viewangles), so those are steered to where the gun points, yaw and pitch: the aim's
        // own turn goes in at once, what they still miss of last frame's aim is closed by half.
        static float lastWanted[2], lastSeen[2];
        BYTE *pc = PlayerClient();
        float wanted[2], seen[2] = { 0, 0 }, err[2];
        int k, valid = 0;
        wanted[0] = pitch;
        wanted[1] = AngleNormalize180(g_vehicleBody + yaw);
        __try {
            if (pc) {
                seen[0] = AngleNormalize180(*(float *)(pc + GCL_OFS_viewangles));
                seen[1] = AngleNormalize180(*(float *)(pc + GCL_OFS_viewangles + 4));
                valid = 1;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        // United Offensive: the game owns the view angles on a vehicle's gun too (the jeep's machine gun: cl_viewangles
        // is rewritten every frame, the log showed the shots stuck at one angle while the aim moved), so it is
        // aimed with the mouse like a car; the Flak 88 of 1.3 keeps the view-angle method that works there.
        if ((!g_gunVehicle || g_uo) && valid) {
            // Edits of the view angles do not reach the shots in a car or a truck, but the mouse does
            // (the tank's gun is turned that way): the game gets the mouse moves that close what is
            // missing between where the gun points and where the shots go, a part of it a frame
            // (about 0.11 degrees a count). Not while the game stands still, or the moves pile up.
            static float fx, fy;
            int ix, iy;
            if (MEM(int, ADDR_eventHead) - MEM(int, ADDR_eventTail) < 3) {
                float ey = AngleNormalize180(wanted[1] - seen[1]), ep = wanted[0] - seen[0];
                fx -= ey * 0.3f / 0.11f;
                fy += ep * 0.3f / 0.11f;
                if (fx > 400.0f) fx = 400.0f;
                if (fx < -400.0f) fx = -400.0f;
                if (fy > 400.0f) fy = 400.0f;
                if (fy < -400.0f) fy = -400.0f;
                ix = (int)fx; iy = (int)fy;
                fx -= ix; fy -= iy;
                if (ix || iy) QueueEvent(SE_MOUSE, ix, iy);
            }
            g_vehAimInit = 1;
            goto vehicleAimLog;
        }
        for (k = 0; k < 2; k++) {
            int axis = k ? YAW : PITCH;
            err[k] = g_vehAimInit ? AngleNormalize180(lastWanted[k] - seen[k]) : 0;
            if (!valid) continue;
            if (!g_vehAimInit) va[axis] += AngleNormalize180(wanted[k] - seen[k]);   // getting in: straight there
            else {
                // while the view does not move at all (a load, a pause) its error means nothing
                va[axis] += AngleNormalize180(wanted[k] - lastWanted[k]) + (seen[k] != lastSeen[k] ? err[k] * 0.5f : 0);
            }
            lastSeen[k] = seen[k];
            lastWanted[k] = wanted[k];
        }
        if (va[PITCH] > 85.0f) va[PITCH] = 85.0f;
        if (va[PITCH] < -85.0f) va[PITCH] = -85.0f;
        g_vehAimInit = valid;
    vehicleAimLog:
        {
            static DWORD nextLog;
            if (GetTickCount() >= nextLog) {
                nextLog = GetTickCount() + 1000;
                Log("vehicle aim: wanted yaw %.1f pitch %.1f, shots go yaw %.1f pitch %.1f (camera %.1f)",
                    wanted[1], wanted[0], seen[1], seen[0], YawOf(g_lastViewAxis[0]));
            }
        }
        g_headYawPrev = yaw;
        return;
    }
    g_vehAimInit = 0;

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

// "key:NAME" binds send a key to the game instead of a command (some actions, like opening the
// menu with Escape, are only handled as keys).
static int KeyByName(const char *name)
{
    static const struct { const char *name; int key; } keys[] = {
        { "ESCAPE", K_ESCAPE }, { "ENTER", K_ENTER }, { "TAB", 9 }, { "SPACE", 32 }, { "BACKSPACE", 127 },
        { "UPARROW", K_UPARROW }, { "DOWNARROW", K_DOWNARROW }, { "LEFTARROW", 156 }, { "RIGHTARROW", 157 },
        { "MOUSE1", K_MOUSE1 }, { "MOUSE2", K_MOUSE1 + 1 }, { "MOUSE3", K_MOUSE1 + 2 },
    };
    int i;
    for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
        if (!_stricmp(keys[i].name, name)) return keys[i].key;
    if (name[0] && !name[1]) return (unsigned char)tolower((unsigned char)name[0]);   // a letter or digit
    return 0;
}

static void SwitchLongGuns(void);
static void CycleVignette(void);

// Runs a button's command (its bind, or a shortcut's) on a press or a release.
static void RunCommand(int b, const char *cmd, int down)
{
    char line[80];
    if (!cmd[0]) return;
    // in a vehicle "use" leans out of the window (the car ride: from inside, the shots hit the car).
    // The grips hold the gun, so there it is on A (jumping does nothing in a vehicle).
    if (g_inVehicle && !g_gunVehicle && b == BT_A) cmd = "+activate";
    else if (g_inVehicle && down && !_stricmp(cmd, "+activate")) return;
    if ((g_onTurret || g_turretInView) && !_stricmp(cmd, "+activate")) return;   // turrets: both grips (UpdateTurret)
    if (!_stricmp(cmd, "vr:recenter")) {
        if (down) Recenter();
        return;
    }
    if (!_stricmp(cmd, "vr:switchweapon")) {   // weapon 1 <-> weapon 2 without reaching for the shoulders
        if (down) SwitchLongGuns();
        return;
    }
    if (!_stricmp(cmd, "vr:vignette")) {       // the comfort vignette's next strength
        if (down) CycleVignette();
        return;
    }
    if (!_strnicmp(cmd, "vr:turn", 7)) return;   // turning is done by the stick code (smooth or snap), not by a press
    if (!_strnicmp(cmd, "key:", 4)) {
        int key = KeyByName(cmd + 4);
        if (key) QueueEvent(SE_KEY, key, down);
        return;
    }
    if (cmd[0] == '+') _snprintf(line, sizeof(line), "%c%s\n", down ? '+' : '-', cmd + 1);
    else if (down) _snprintf(line, sizeof(line), "%s\n", cmd);
    else return;
    Cbuf(line);
}

static void RunBind(int b, int down) { RunCommand(b, cfg.bind[b], down); }

static void SetButton(int b, int down)
{
    if (down == g_btnDown[b]) return;
    g_btnDown[b] = down;
    RunBind(b, down);
}

// Analog buttons with hysteresis so a resting finger does not chatter.
static int Pressed(float v, int b) { return g_btnDown[b] ? v > 0.35f : v > 0.65f; }

// Face buttons. The two buttons of the hand that does NOT hold the gun are wildcards: X and Y for the right-handed, A and B for the
// left-handed (the dominant hand's own two buttons run their binds at once: reload while the thumb stays on the move stick).
// A wildcard tapped on its own runs its own bind (a click); held, the next face button pressed (any other) runs that pair's
// shortcut (cfg.shortcut[wildcard][pressed]) instead of its own action, for as long as it is held.
enum { FACE_A, FACE_B, FACE_X, FACE_Y };
static const int g_faceBt[4] = { BT_A, BT_B, BT_X, BT_Y };
static int g_faceDown[4];      // the buttons as read
static int g_faceShortcut[4];  // 1 + the wildcard button a held button's shortcut belongs to (0 when none)
static int g_shortcutHeld = -1;   // the wildcard held first
static int g_shortcutUsed;        // something was pressed while it was held (so it is not a tap)

static int IsWildcard(int f) { return cfg.rightHanded ? (f == FACE_X || f == FACE_Y) : (f == FACE_A || f == FACE_B); }
static const char *ShortcutOf(int mod, int f) { return cfg.shortcut[mod][f]; }

static void UpdateFaceButtons(const int down[4])
{
    static const char *const names[4] = { "A", "B", "X", "Y" };
    int f;
    for (f = 0; f < 4; f++) {
        if (down[f] == g_faceDown[f]) continue;
        g_faceDown[f] = down[f];
        if (down[f]) {
            if (g_shortcutHeld >= 0 && f != g_shortcutHeld) {
                const char *cmd = ShortcutOf(g_shortcutHeld, f);
                g_faceShortcut[f] = 1 + g_shortcutHeld;
                g_shortcutUsed = 1;
                Log("shortcut %s + %s: %s", names[g_shortcutHeld], names[f], cmd[0] ? cmd : "(nothing)");
                RunCommand(g_faceBt[f], cmd, 1);
            } else if (IsWildcard(f)) {
                g_shortcutHeld = f;
                g_shortcutUsed = 0;
            } else {
                SetButton(g_faceBt[f], 1);
            }
        } else {
            if (g_faceShortcut[f]) {
                RunCommand(g_faceBt[f], cfg.shortcut[g_faceShortcut[f] - 1][f], 0);
                g_faceShortcut[f] = 0;
            } else if (IsWildcard(f)) {
                if (f == g_shortcutHeld) {
                    if (!g_shortcutUsed) { RunBind(g_faceBt[f], 1); RunBind(g_faceBt[f], 0); }   // a tap: its own action
                    g_shortcutHeld = -1;
                }
            } else {
                SetButton(g_faceBt[f], 0);
            }
        }
    }
}

// Is a held shortcut running this command (the objectives screen, held with +scores)?
static int ShortcutHolds(const char *cmd)
{
    int f;
    for (f = 0; f < 4; f++)
        if (g_faceShortcut[f] && !_stricmp(cfg.shortcut[g_faceShortcut[f] - 1][f], cmd)) return 1;
    return 0;
}
static void ReleaseAllButtons(void)
{
    static const int none[4] = { 0, 0, 0, 0 };
    int b;
    UpdateFaceButtons(none);   // a held shortcut lets go too
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

// Would the off hand at o take the gun held at d by the handguard: in front of the gun hand, within TwoHandReach, and close to
// the barrel's line (20 cm). Anywhere else the off hand's grip is "use" (planting explosives, doors): a hand reaching out
// to a bomb in front of the soldier is in front of the gun hand too, but not on the barrel.
static int OffHandOnGun(const vr_pose_t *d, const vr_pose_t *o)
{
    float f[3], dist, dot;
    int i;
    for (i = 0; i < 3; i++) f[i] = o->pos[i] + d->axis[2][i] * cfg.twoHandGripHeight - d->pos[i];
    dist = Normalize(f);
    dot = f[0] * d->axis[0][0] + f[1] * d->axis[0][1] + f[2] * d->axis[0][2];
    if (dist > cfg.twoHandReach || dot < 0.3f) return 0;
    return dist * sqrtf(1.0f - (dot > 1.0f ? 1.0f : dot * dot)) <= 0.2f;
}

// The gun follows the dominant hand. While the off-hand grip is held near the gun, the barrel
// points from the dominant hand to the off hand, like holding a rifle by the handguard.
static void UpdateGunPose(int dom, int off, int gripDown)
{
    const vr_pose_t *d = &g_in.hand[dom], *o = &g_in.hand[off];

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
        if (!g_twoHand && !OffHandOnGun(d, o))
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
// Switching between play and a menu happens while the button that caused it is still held
// (Menu opens the menu, B or Menu closes it): ignore everything held at the switch until all of
// it is released, otherwise the same press would act again on the other side.
static int g_ignoreHeld;

static int AnyInputHeld(void)
{
    return g_in.buttons || g_in.trigger[0] > 0.3f || g_in.trigger[1] > 0.3f || g_in.grip[0] > 0.3f || g_in.grip[1] > 0.3f;
}

static void MenuControls(int dom)
{
    static const int keys[6] = { K_MOUSE1, K_ESCAPE, K_ENTER, K_UPARROW, K_DOWNARROW, K_ESCAPE };
    const vr_hud_t *hl = &cfg.hudLayer;
    float sy = g_in.stick[dom][1];
    DWORD now = GetTickCount();

    if (!g_menuMode) {
        g_menuMode = 1;
        g_cursorSync = 0;
        g_ignoreHeld = AnyInputHeld();
    }

    if (cfg.menuPointer && g_in.handValid[dom]) {
        const vr_pose_t *h = &g_in.hand[dom];
        float width = g_lastHudWorld ? hl->width : hl->menuWidth, height = width * fb_h / (float)fb_w;
        float c[3], n[3], d[3], rt[3], upv[3], denom, t, hit[3], u, v, tx, ty;
        int i;
        // the menu screen: fixed where it appeared, or head-locked at distance, offsetY up
        for (i = 0; i < 3; i++) {
            if (g_menuQuadValid) {
                n[i] = g_menuQuad.axis[0][i];
                c[i] = g_menuQuad.pos[i];
                rt[i] = -g_menuQuad.axis[1][i];
                upv[i] = g_menuQuad.axis[2][i];
            } else {
                n[i] = g_head.axis[0][i];
                c[i] = g_head.pos[i] + g_head.axis[0][i] * hl->distance + g_head.axis[2][i] * hl->offsetY;
                rt[i] = -g_head.axis[1][i];
                upv[i] = g_head.axis[2][i];
            }
            d[i] = c[i] - h->pos[i];
        }
        denom = h->axis[0][0] * n[0] + h->axis[0][1] * n[1] + h->axis[0][2] * n[2];
        if (denom < -1e-3f || denom > 1e-3f) {
            t = (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]) / denom;
            if (t > 0) {
                for (i = 0; i < 3; i++) hit[i] = h->pos[i] + h->axis[0][i] * t - c[i];
                u = hit[0] * rt[0] + hit[1] * rt[1] + hit[2] * rt[2];
                v = hit[0] * upv[0] + hit[1] * upv[1] + hit[2] * upv[2];
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

    if (g_ignoreHeld) return;
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
    g_ignoreHeld = AnyInputHeld();
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
    {
        // A steadier filter while looking through the scope: hand tremor is magnified there.
        // It stays on for a moment after the zoom ends, so a flickering zoom does not switch it.
        static float scopeHold;
        float tau;
        if (g_scopeWasActive) scopeHold = 0.3f;
        else if (scopeHold > 0) scopeHold -= dt;
        tau = scopeHold > 0 && cfg.scopeSmoothing > 0 ? cfg.scopeSmoothing : cfg.aimSmoothing;
        if (g_twoHand && cfg.twoHandStab > 0) {
            // Held with both hands: an adaptive filter. Small motions (the hands' tremor) are
            // damped a lot; the further the gun turns from where it is steadied, the quicker it
            // follows, so a deliberate swing is not left behind.
            float ang = 0, k, t2;
            if (g_gunSmoothInit) {
                float c = f[0] * g_gunFwdSmooth[0] + f[1] * g_gunFwdSmooth[1] + f[2] * g_gunFwdSmooth[2];
                ang = c >= 1.0f ? 0.0f : DEG(acosf(c < -1.0f ? -1.0f : c));
            }
            k = ang / 6.0f;
            if (k > 1) k = 1;
            t2 = cfg.twoHandStab * 0.18f * (1.0f - k) + 0.01f * k;
            if (t2 > tau) tau = t2;
        }
        if (tau != g_lastSmoothing && g_lastSmoothing == 0) g_gunSmoothInit = 0;   // start from the current pose
        g_lastSmoothing = tau;
        if (tau <= 0) goto unfiltered;
    }
    {
        // dt can be 0 (two updates in one clock tick): then the filter just holds its value.
        float a = dt > 0 ? 1.0f - expf(-dt / g_lastSmoothing) : 0.0f;
        if (!g_gunSmoothInit) { memcpy(g_gunFwdSmooth, f, sizeof(f)); memcpy(g_gunUpSmooth, u, sizeof(u)); g_gunSmoothInit = 1; }
        for (i = 0; i < 3; i++) {
            g_gunFwdSmooth[i] += (f[i] - g_gunFwdSmooth[i]) * a;
            g_gunUpSmooth[i] += (u[i] - g_gunUpSmooth[i]) * a;
        }
        memcpy(f, g_gunFwdSmooth, sizeof(f));
        memcpy(u, g_gunUpSmooth, sizeof(u));
    }
unfiltered:
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

// ---------------------------------------------------------------------------
// Key hints: "Press [%s] to ..." shows the controller button instead of the keyboard key
// ---------------------------------------------------------------------------
// Is the stick that does not move you the right one? (Its directions are the BT_STICK* buttons.)
static int FreeStickIsRight(void)
{
    int dom = cfg.rightHanded ? 1 : 0;
    if (cfg.moveStick >= 0) return cfg.moveStick == 0;
    return cfg.swapSticks ? 1 - dom : dom;
}

// +1 = the action turns right, -1 = turns left, 0 = an ordinary action
static int TurnOf(const char *cmd)
{
    if (!_stricmp(cmd, "vr:turnright")) return 1;
    if (!_stricmp(cmd, "vr:turnleft")) return -1;
    return 0;
}

static const char *ButtonName(int b)
{
    int right = FreeStickIsRight();
    switch (b) {
    case BT_PTRIGGER:  return cfg.rightHanded ? "RIGHT TRIGGER" : "LEFT TRIGGER";
    case BT_PGRIP:     if (g_turretInView) return "BOTH GRIPS";   // turrets are held with both hands
                       return cfg.rightHanded ? "RIGHT GRIP" : "LEFT GRIP";
    case BT_STRIGGER:  return cfg.rightHanded ? "LEFT TRIGGER" : "RIGHT TRIGGER";
    case BT_SGRIP:     return cfg.rightHanded ? "LEFT GRIP" : "RIGHT GRIP";
    case BT_A:         return "A";
    case BT_B:         return "B";
    case BT_X:         return "X";
    case BT_Y:         return "Y";
    case BT_LCLICK:    return "LEFT STICK CLICK";
    case BT_RCLICK:    return "RIGHT STICK CLICK";
    case BT_MENU:      return "MENU";
    case BT_STICKUP:   return right ? "RIGHT STICK UP" : "LEFT STICK UP";
    case BT_STICKDOWN: return right ? "RIGHT STICK DOWN" : "LEFT STICK DOWN";
    case BT_STICKLEFT: return right ? "RIGHT STICK LEFT" : "LEFT STICK LEFT";
    case BT_STICKRIGHT: return right ? "RIGHT STICK RIGHT" : "LEFT STICK RIGHT";
    }
    return NULL;
}

// The name to show for a game command, or NULL to let the game show its key. Buttons follow
// [Buttons] in cod_vr.ini; the rest are things the mod does with the sticks or the body.
static const char *VrKeyName(const char *cmd)
{
    static const struct { const char *cmd; int button; const char *text; } extra[] = {
        { "+forward", -1, "MOVE STICK" }, { "+back", -1, "MOVE STICK" },
        { "+moveleft", -1, "MOVE STICK" }, { "+moveright", -1, "MOVE STICK" },
        { "+leanleft", -1, "LEAN YOUR BODY" }, { "+leanright", -1, "LEAN YOUR BODY" },
        { "gocrouch", BT_STICKDOWN, NULL }, { "togglecrouch", BT_STICKDOWN, NULL }, { "+movedown", BT_STICKDOWN, NULL },
        { "goprone", BT_RCLICK, NULL }, { "+prone", BT_RCLICK, NULL },
        { "+moveup", BT_STICKUP, NULL },
        { "weaponslot primary", BT_X, NULL }, { "weaponslot primaryb", BT_X, NULL },
        { "weaponslot pistol", BT_X, NULL }, { "weaponslot grenade", BT_X, NULL }, { "weapprev", BT_X, NULL },
    };
    int b, i;

    if (!cmd || !cfg.vr || !cfg.controllers) return NULL;
    for (b = 0; b < BT_COUNT; b++)
        if (cfg.bind[b][0] && !_stricmp(cfg.bind[b], cmd)) return ButtonName(b);
    for (i = 0; i < (int)(sizeof(extra) / sizeof(extra[0])); i++)
        if (!_stricmp(extra[i].cmd, cmd)) return extra[i].text ? extra[i].text : ButtonName(extra[i].button);
    return NULL;
}

typedef void (__cdecl *PFN_Tramp)(void);
static PFN_Tramp tramp_KeyBindingText;
static HMODULE   g_keyHookModule;
static int       g_keyHookKnown;
static const char g_emptyName[1] = "";

// cgame KeyBindingText(cmd): char** out in EAX (first key) and ECX (second key).
__declspec(naked) static void hk_KeyBindingText(void)
{
    __asm {
        push ecx
        push eax
        mov edx, [esp + 12]       // cmd (above the two saves and the return address)
        push edx
        call VrKeyName
        add esp, 4
        test eax, eax
        jz passthrough
        mov edx, eax
        pop eax
        pop ecx
        mov [eax], edx            // first key = the controller button
        mov dword ptr [ecx], offset g_emptyName
        mov eax, 1
        ret
    passthrough:
        pop eax
        pop ecx
        jmp dword ptr [tramp_KeyBindingText]
    }
}

// The level scripts ask the engine which key does a command (getKeyBinding, for hints like
// "Press &&1 to strafe left" in the training): keyboard keys, unreadable on another layout and
// wrong in VR. The game's engine calls go through one pointer; that one call gets the controller.
static DWORD orig_GameSyscall;

static int __cdecl GameKeyBinding(const char *cmd, const char **key1, const char **key2)
{
    const char *name = NULL;
    __try {
        name = VrKeyName(cmd);
        if (!name) return -1;   // not ours: the engine answers
        *key1 = name;
        *key2 = g_emptyName;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return 1;
}

__declspec(naked) static int hk_GameSyscall(void)
{
    __asm {
        cmp dword ptr [esp + 4], G_TRAP_KeyBinding
        jne pass
        push dword ptr [esp + 16]     // key2
        push dword ptr [esp + 16]     // key1
        push dword ptr [esp + 16]     // cmd
        call GameKeyBinding
        add esp, 12
        test eax, eax
        jl pass
        ret
    pass:
        jmp dword ptr [orig_GameSyscall]
    }
}

static void HookGameKeyBinding(void)
{
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    DWORD cur;
    if (!gm || !cfg.vr || !cfg.controllers) return;
    cur = *(DWORD *)(gm + G_OFS_syscall);
    if (!cur || cur == (DWORD)hk_GameSyscall) return;
    orig_GameSyscall = cur;
    PatchPointer((DWORD)(gm + G_OFS_syscall), (void *)hk_GameSyscall);
    Log("hooked game key binding names");
}

// The cgame is loaded again for every level: patch each new copy.
static void HookKeyBindingText(void)
{
    // push ebp; mov ebp, dword ptr [esp + 8]
    static const BYTE prologue[] = { 0x55, 0x8B, 0x6C, 0x24, 0x08 };
    HMODULE m = GetModuleHandleA(CGAME_DLL);
    IMAGE_NT_HEADERS *nt;
    BYTE *target;

    if (!m || !cfg.vr || !cfg.controllers) return;
    if (m != g_keyHookModule) {   // check the version once per module handle
        nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
        g_keyHookModule = m;
        g_keyHookKnown = nt->FileHeader.TimeDateStamp == CGAME_TIMESTAMP;
    }
    if (!g_keyHookKnown) return;
    target = (BYTE *)m + CG_OFS_KeyBindingText;
    // A reloaded cgame usually comes back at the same address with the original bytes.
    if (memcmp(target, prologue, sizeof(prologue)) != 0) return;   // patched already (or unknown)
    tramp_KeyBindingText = Detour((DWORD)target, prologue, sizeof(prologue), (void *)hk_KeyBindingText);
    if (tramp_KeyBindingText) Log("hooked cgame key binding names");
}

// weaponDef_t of the weapon in the player's hand: the selected weapon index the client sends
// in each usercmd, looked up in cgamex86.dll's weaponDefs array (NULL if unknown). The cgame's
// own "current weapon def" pointer is only filled while it draws the crosshair, which the mod
// turns off.
static const BYTE *CurrentWeaponDef(void)
{
    static HMODULE checked;
    static int known;
    HMODULE m = GetModuleHandleA(CGAME_DLL);
    const BYTE *def = NULL;
    if (!m) return NULL;
    if (m != checked) {   // the cgame is reloaded with each level
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
        known = nt->FileHeader.TimeDateStamp == CGAME_TIMESTAMP;
        if (!known) Log("cgamex86.dll timestamp %08x unknown: no grenade trajectory", nt->FileHeader.TimeDateStamp);
        checked = m;
    }
    if (!known) return NULL;
    __try {
        int index = MEM(BYTE, ADDR_cl_cgameUserCmdValue);
        int count = *(const int *)((const BYTE *)m + CG_OFS_numWeaponDefs);
        const BYTE * const *defs = *(const BYTE * const * const *)((const BYTE *)m + CG_OFS_weaponDefs);
        if (defs && index > 0 && index <= count) def = defs[index];
        if (def) (void)*(volatile const int *)(def + WEAPDEF_OFS_weaponType);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        def = NULL;
    }
    return def;
}

// A map is loaded (traces need one: without it CM_BoxTrace raises a fatal error).
static int MapLoaded(void) { return MEM(int, ADDR_cm_numSubModels) > 0; }

// World trace (CM_BoxTrace against the map): fraction of start->end before hitting something.
static float WorldTrace(const float *start, const float *end)
{
    BYTE tr[256];   // trace_t; only its first field (fraction) is read
    static const float zero[3] = { 0, 0, 0 };
    const float *z = zero;
    int mask = cfg.traceMask;
    if (!MapLoaded()) return 1.0f;
    memset(tr, 0, sizeof(tr));
    __try {
        __asm {
            push 0              // model: the world
            push z              // maxs
            push z              // mins
            push end
            push start
            lea eax, tr
            mov edx, mask
            xor ecx, ecx        // a box, not a capsule
            mov esi, ADDR_CM_BoxTrace
            call esi
            add esp, 20
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1.0f;
    }
    return *(float *)tr;
}

// A ladder right ahead of the head (the game looks for one along its view, which in VR is the
// gun's aim): the same short trace, along where the head looks.
static int LadderAhead(float worldYaw)
{
    BYTE tr[256];
    static const float zero[3] = { 0, 0, 0 };
    const float *z = zero, *start = g_lastViewOrg;
    float end[3];
    int mask = CONTENTS_SOLID | 0x10000;   // and player clip (ladders are often clip brushes)
    if (!MapLoaded()) return 0;
    end[0] = start[0] + cosf(RAD(worldYaw)) * 30.0f;
    end[1] = start[1] + sinf(RAD(worldYaw)) * 30.0f;
    end[2] = start[2] - 20.0f;   // a little low: the trace is from the eyes
    memset(tr, 0, sizeof(tr));
    __try {
        __asm {
            push 0
            push z
            push z
            lea eax, end
            push eax
            push start
            lea eax, tr
            mov edx, mask
            xor ecx, ecx
            mov esi, ADDR_CM_BoxTrace
            call esi
            add esp, 20
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return *(float *)tr < 1.0f && (*(int *)(tr + TRACE_OFS_surfaceFlags) & SURF_LADDER);
}

// A point in the game world to tracking space, the inverse of the eyes' placement.
static void WorldToTracking(const float w[3], float body, float out[3])
{
    float d[3], r[3];
    int i;
    const float *org = g_onTurret && g_turretOrgSet ? g_turretOrg : g_lastViewOrg;   // the eyes' base
    for (i = 0; i < 3; i++) d[i] = (w[i] - org[i]) / cfg.worldScale;
    RotateZ(d, -body, r);
    if (!cfg.positional) {
        for (i = 0; i < 3; i++) out[i] = r[i] + g_head.pos[i];
    } else if (cfg.roomScale && g_anchorInit) {
        out[0] = r[0] + g_anchor[0]; out[1] = r[1] + g_anchor[1]; out[2] = r[2] - HeightShift();
    } else {
        out[0] = r[0]; out[1] = r[1]; out[2] = r[2] - HeightShift();
    }
}

// Grenade trajectory, computed like the game throws it: from the player's eye (the game view)
// along the game's aim, at projectileSpeed plus projectileSpeedUp upwards, under gravity. Each
// 0.05 s step is traced against the map; the line stops where it first hits (a wall above the
// window, or the floor inside it) and a ring marks that spot. Drawn from 0.1 s after the throw
// so it does not start inside the face. Bounces are not simulated.
static void UpdateGrenadeArc(void)
{
    float color[4] = { 1.0f, 1.0f, 1.0f, 0.6f };
    const BYTE *def;
    float pts[64][3], fuse, t;
    int speed = 0, up = 0, fuseMs = 0, type = -1, n = 0;

    static DWORD lastDiag;
    int diag = 0;

    if (!cfg.grenadeArc || (cfg.grenadeArc == 1 && !g_btnDown[BT_PTRIGGER])) return;
    if (g_onTurret || (g_inVehicle && g_gunVehicle)) return;   // the weapon index is stale on a mounted gun
    if (cfg.logNear && GetTickCount() - lastDiag > 1000) { lastDiag = GetTickCount(); diag = 1; }
    def = CurrentWeaponDef();
    if (diag) Log("grenade diag: cgame module %p, weapon index %d, weaponDef %p",
                  (void *)GetModuleHandleA(CGAME_DLL), MEM(BYTE, ADDR_cl_cgameUserCmdValue), (void *)def);
    if (def == NULL) return;
    __try {
        type = *(const int *)(def + WEAPDEF_OFS_weaponType);
        speed = *(const int *)(def + WEAPDEF_OFS_projectileSpeed);
        up = *(const int *)(def + WEAPDEF_OFS_projectileSpeedUp);
        fuseMs = *(const int *)(def + WEAPDEF_OFS_fuseTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (diag) Log("grenade diag: weaponType %d, projectileSpeed %d, speedUp %d, fuse %d ms, aimDir %.2f %.2f %.2f",
                  type, speed, up, fuseMs, g_aimDir[0], g_aimDir[1], g_aimDir[2]);
    if (type != WEAPTYPE_GRENADE || speed <= 0) return;

    fuse = fuseMs > 0 && fuseMs < 20000 ? fuseMs * 0.001f : 4.0f;
    color[3] = cfg.grenadeOpacity < 0.05f ? 0.05f : cfg.grenadeOpacity > 1 ? 1 : cfg.grenadeOpacity;
    {
        const float *org = g_lastViewOrg, *fwd = g_lastViewAxis[0];
        float body = BodyYaw(fwd), wv[3], p0[3], p1[3], frac;
        int k, hit = 0;
        for (k = 0; k < 3; k++) wv[k] = fwd[k] * speed;
        wv[2] += up;
        memcpy(p0, org, sizeof(p0));
        for (t = 0.05f; t <= fuse + 1e-4f && n < 64 && !hit; t += 0.05f) {
            for (k = 0; k < 3; k++) p1[k] = org[k] + wv[k] * t;
            p1[2] -= 0.5f * cfg.gravity * t * t;
            frac = WorldTrace(p0, p1);
            if (frac < 1.0f) {
                if (frac < 0) frac = 0;
                for (k = 0; k < 3; k++) p1[k] = p0[k] + (p1[k] - p0[k]) * frac;
                hit = 1;
            }
            if (t >= 0.1f || hit) WorldToTracking(p1, body, pts[n++]);
            memcpy(p0, p1, sizeof(p0));
        }
    }
    if (diag) Log("grenade diag: %d arc points, last %.2f %.2f %.2f (head %.2f %.2f %.2f)", n,
                  n ? pts[n - 1][0] : 0, n ? pts[n - 1][1] : 0, n ? pts[n - 1][2] : 0, g_head.pos[0], g_head.pos[1], g_head.pos[2]);
    if (n >= 2) XR_SetLine((const float (*)[3])pts, n, color, 0.3f);
}

// ---------------------------------------------------------------------------
// Realistic mode: the hands start empty. The grip over the shoulder (hand behind the head)
// draws a long gun (each shoulder has its own); at the hip the pistol; behind the lower back a
// grenade (away from the floor, where an empty hand picks weapons up). Letting go of the grip at a holster puts the weapon back; anywhere else
// the gun falls to the floor (the game's own weapon drop, the one it uses when a picked-up
// weapon takes the place of another). An empty hand's grip is "use": pick a weapon up (it goes
// to that hand), open doors. The off hand's grip near the gun holds it with both hands.
// The game always has some weapon selected: with empty hands it is hidden and does not fire.
// ---------------------------------------------------------------------------
enum { ZONE_NONE, ZONE_SHOULDER_L, ZONE_SHOULDER_R, ZONE_CHEST, ZONE_WAIST_L, ZONE_WAIST_R, ZONE_WAIST_F, ZONE_WAIST_B, ZONE_COUNT };
// what a zone holds: weapon 1, weapon 2, or whichever long gun was not drawn last (the zone gives
// back one and takes the other), the pistol, a grenade
enum { ACT_NONE, ACT_W1, ACT_W2, ACT_SWITCH, ACT_PISTOL, ACT_GRENADE };
enum { HELD_LONG, HELD_PISTOL, HELD_GRENADE };
static int   g_gunHand = -1;       // hand holding the weapon, -1 = both hands empty
static int   g_heldKind;
static int   g_heldWeapon;         // the game's weapon in that hand, 0 while the switch is under way
static int   g_wantWeapon;         // weapon asked for by the last draw
static DWORD g_wantTime;
static int   g_lastGameWeapon;
static int   g_lastSlots[6], g_slotsKnown;   // the gun slots last frame (pickups)
static int   g_gripDown[2], g_useHand = -1, g_realSync = 1;
// The view weapon is drawn mirrored this frame (gun in the left hand). Its triangles then wind
// the other way, so hook_glDepthRange flips the front face while the weapon is drawn.
static int   g_mirrorGun;
static int   g_gunFlashed;       // the weapon in the hand showed a muzzle flash since the last check

// The player's gclient_t in gamex86.dll, or NULL (another build, no level).
static BYTE *PlayerClient(void)
{
    static HMODULE checked;
    static int known;
    HMODULE m = GetModuleHandleA(GAME_DLL);
    BYTE *cl = NULL;
    if (!m) return NULL;
    if (m != checked) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
        known = nt->FileHeader.TimeDateStamp == GAME_TIMESTAMP;
        if (!known) Log("gamex86.dll timestamp %08x unknown: Realistic mode cannot read the weapons", nt->FileHeader.TimeDateStamp);
        checked = m;
    }
    if (!known) return NULL;
    __try {
        cl = *(BYTE **)((BYTE *)m + G_OFS_entities + GENT_OFS_client);
        if (cl) (void)*(volatile int *)(cl + GCL_OFS_slots);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cl = NULL;
    }
    return cl;
}

static int SlotWeapon(const BYTE *cl, int slot) { return cl ? cl[GCL_OFS_slots + slot] : 0; }
static int GameWeapon(const BYTE *cl) { return cl ? *(const int *)(cl + GCL_OFS_weapon) : 0; }

// ---------------------------------------------------------------------------
// Movement modes ([Controllers] MoveMode, launcher VR > Movement): Blink (0, beta) and Shift (1, beta) move by
// aiming with the move stick: pushed forward, an arc from the dominant hand (the grenade line's way) shows where the
// soldier will stand, letting go moves him there; Continuous (2, the default) is the stick walking, as before. The destination is
// the floor under where the arc lands, and the soldier's box has to fit there. Blink fades both eyes to black, moves
// him, and fades back in; Shift glides him there in a quarter of a second along a straight line that must be clear
// (steps up to 18 units are walked over). The player state's origin is written, as the turret mount does.
// ---------------------------------------------------------------------------
enum { MOVE_BLINK, MOVE_SHIFT, MOVE_CONTINUOUS };
enum { TP_IDLE, TP_FADE_OUT, TP_FADE_IN, TP_GLIDE };
#define TP_SPEED     640.0f   // launch speed of the arc (game units per second): about 13 m at 45 degrees
#define TP_STEP      0.04f
#define TP_STEP_UP   18.0f
static int    g_tpState, g_tpAiming, g_tpAimOk;
static float  g_tpDest[3], g_tpFrom[3], g_tpT;

// Like WorldTrace, with a box (the soldier's) and the player-clip contents too.
static float WorldTraceBox(const float *start, const float *end, const float *mins, const float *maxs)
{
    BYTE tr[256];
    int mask = cfg.traceMask | 0x10000;
    if (!MapLoaded()) return 1.0f;
    memset(tr, 0, sizeof(tr));
    __try {
        __asm {
            push 0              // model: the world
            push maxs
            push mins
            push end
            push start
            lea eax, tr
            mov edx, mask
            xor ecx, ecx        // a box, not a capsule
            mov esi, ADDR_CM_BoxTrace
            call esi
            add esp, 20
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1.0f;
    }
    return *(float *)tr;
}

// The soldier's box fits at the origin o (feet): 30 wide, 56 high.
static int HullFits(const float o[3])
{
    static const float mins[3] = { -15, -15, 0 }, maxs[3] = { 15, 15, 56 };
    float a[3] = { o[0], o[1], o[2] + 1.0f }, b[3] = { o[0], o[1], o[2] + 2.0f };
    return WorldTraceBox(a, b, mins, maxs) >= 1.0f;
}

// A point in tracking space to the game world (the inverse of WorldToTracking).
static void TrackingToWorld(const float p[3], float body, float out[3])
{
    float rel[3], w[3];
    int i;
    BodyOffset(p, rel);
    RotateZ(rel, body, w);
    for (i = 0; i < 3; i++) out[i] = g_lastViewOrg[i] + w[i] * cfg.worldScale;
}

// The arc from start along dir (world), as tracking-space points for the line; returns 1 when it ends on a floor where the
// soldier fits, with the origin to stand at in dest.
static int TeleportArc(const float start[3], const float dir[3], float body, float pts[64][3], int *count, float dest[3])
{
    float p0[3], p1[3], t, frac;
    int k, n = 0, hit = 0;
    memcpy(p0, start, sizeof(p0));
    memcpy(p1, start, sizeof(p1));
    for (t = TP_STEP; t < 2.5f && n < 63 && !hit; t += TP_STEP) {
        for (k = 0; k < 3; k++) p1[k] = start[k] + dir[k] * TP_SPEED * t;
        p1[2] -= 0.5f * cfg.gravity * t * t;
        frac = WorldTrace(p0, p1);
        if (frac < 1.0f) {
            if (frac < 0) frac = 0;
            for (k = 0; k < 3; k++) p1[k] = p0[k] + (p1[k] - p0[k]) * frac;
            hit = 1;
        }
        WorldToTracking(p1, body, pts[n++]);
        memcpy(p0, p1, sizeof(p0));
    }
    *count = n;
    if (!hit) return 0;
    {
        // the floor within 48 units under the landing point (a wall hit a little above the floor counts)
        float top[3] = { p1[0], p1[1], p1[2] + 12.0f }, bot[3] = { p1[0], p1[1], p1[2] - 48.0f };
        frac = WorldTrace(top, bot);
        if (frac >= 1.0f) return 0;
        dest[0] = top[0]; dest[1] = top[1]; dest[2] = top[2] + (bot[2] - top[2]) * frac + 0.25f;
    }
    return HullFits(dest);
}

static float *PlayerOrigin(void)
{
    BYTE *cl = PlayerClient();
    return cl ? (float *)(cl + GCL_OFS_origin) : NULL;
}

// The game's player is put at o (feet), at rest.
static void SetPlayerOrigin(const float o[3])
{
    float *me = PlayerOrigin();
    if (!me) return;
    __try {
        me[0] = o[0]; me[1] = o[1]; me[2] = o[2];
        me[3] = me[4] = me[5] = 0;   // its velocity
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_lastGameOrgValid = 0;   // not a roomscale step
}

// ---------------------------------------------------------------------------
// Using something with the FREE hand while a gun is in the other (planting explosives, switches, doors). The game uses
// what the aim points at, and with a gun in hand the aim is the gun's line. When the free hand reaches into something
// usable, its grip is "use" (never the handguard grab) and, for as long as it is held, the aim goes to that thing's centre
// (from the eyes), so the game uses it whatever the gun points at. Nothing changes until the grip is pressed: no aim switch
// by gaze (it made the aim drop out whenever the head met a trigger).
// "Usable" is read from the game's entities, by their boxes (absmin/absmax = currentOrigin - 0x18 / - 0x0c, contents at
// - 0x1c): the usable bit (0x200000: items, the turrets' 0x200004) or a small trigger (0x40000000, up to 160 units across:
// a bomb's use trigger, not an area). The hand has to be inside the box grown by 24 units. The log names what was found
// (`use: hand N reaches entity E (contents ..)`), which tells whether a bomb's trigger fits this rule.
// ---------------------------------------------------------------------------
#define USE_REACH        24.0f
#define CONTENTS_USABLE  0x200000
#define CONTENTS_TRIGGER_BIT 0x40000000
static int g_useEnt;   // the entity the free hand's grip is using (0 = none: the aim is that hand's own pointing)

static const float *EntBox(const BYTE *e, const float **mx)
{
    const float *org = (const float *)(e + GENT_OFS_currentOrigin);
    *mx = (const float *)((const BYTE *)org - 0x0c);
    return (const float *)((const BYTE *)org - 0x18);
}

// The usable entity whose box holds hand h (world position), the nearest by centre; 0 if none.
static int UsableAtHand(int h)
{
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    float hand[3], best = 1e9f;
    int i, n, found = 0;
    if (!gm || !MapLoaded() || !PlayerClient() || !g_in.handValid[h]) return 0;
    TrackingToWorld(g_in.hand[h].pos, BodyYaw(g_lastViewAxis[0]), hand);
    __try {
        n = *(int *)(gm + G_OFS_numEntities);
        if (n > 1024) n = 1024;
        for (i = 1; i < n; i++) {
            BYTE *e = gm + G_OFS_entities + i * G_ENTITY_SIZE;
            const float *mx, *mn = EntBox(e, &mx);
            int contents = *(const int *)((BYTE *)(e + GENT_OFS_currentOrigin) - 0x1c);
            float size = 0, d = 0;
            int k;
            if (!e[GENT_OFS_inuse] || *(void **)(e + GENT_OFS_turretInfo)) continue;   // turrets have their own mount
            if (!(contents & CONTENTS_USABLE) && !(contents & CONTENTS_TRIGGER_BIT)) continue;
            for (k = 0; k < 3; k++) if (mx[k] - mn[k] > size) size = mx[k] - mn[k];
            if (size <= 0 || size > 5000.0f) continue;
            if (!(contents & CONTENTS_USABLE) && size > 160.0f) continue;   // an area trigger, not something to use
            for (k = 0; k < 3; k++) {
                float c = (mn[k] + mx[k]) * 0.5f;
                if (hand[k] < mn[k] - USE_REACH || hand[k] > mx[k] + USE_REACH) break;
                d += (hand[k] - c) * (hand[k] - c);
            }
            if (k == 3 && d < best) { best = d; found = i; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (found) {
        const BYTE *e = gm + G_OFS_entities + found * G_ENTITY_SIZE;
        const float *mx, *mn = EntBox(e, &mx);
        Log("use: hand %d reaches entity %d (contents %08x, box %.0f %.0f %.0f to %.0f %.0f %.0f)", h, found,
            *(const int *)((const BYTE *)(e + GENT_OFS_currentOrigin) - 0x1c), mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
    }
    return found;
}

// The centre of an entity's box, in the world; 0 when it is gone.
static int UseTargetCenter(int ent, float out[3])
{
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    if (!gm || ent <= 0) return 0;
    __try {
        const BYTE *e = gm + G_OFS_entities + ent * G_ENTITY_SIZE;
        const float *mx, *mn = EntBox(e, &mx);
        int k;
        if (!e[GENT_OFS_inuse]) return 0;
        for (k = 0; k < 3; k++) out[k] = (mn[k] + mx[k]) * 0.5f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 1;
}

// Once per frame on foot (not in a vehicle, on a gun or in a scripted scene): the move stick (mx, my) aims and moves.
// active = 0 resets everything (and opens the screen).
static void UpdateTeleport(int active, float dt, float mx, float my, int dom, int off)   // dom = the aiming hand (the move stick's), off = the fallback
{
    if (!active || cfg.moveMode == MOVE_CONTINUOUS) {
        g_tpState = TP_IDLE; g_tpAiming = 0;
        if (g_tpFade > 0) g_tpFade = 0;
        return;
    }
    (void)mx;
    if (g_tpState == TP_FADE_OUT) {
        g_tpFade += dt / 0.12f;
        if (g_tpFade >= 1.0f) { g_tpFade = 1.0f; SetPlayerOrigin(g_tpDest); g_tpState = TP_FADE_IN; Log("move: blink to %.0f %.0f %.0f", g_tpDest[0], g_tpDest[1], g_tpDest[2]); }
        return;
    }
    if (g_tpState == TP_FADE_IN) {
        g_tpFade -= dt / 0.22f;
        if (g_tpFade <= 0) { g_tpFade = 0; g_tpState = TP_IDLE; }
        return;
    }
    if (g_tpState == TP_GLIDE) {
        float o[3];
        int k;
        g_tpT += dt / 0.25f;
        if (g_tpT >= 1.0f) { g_tpT = 1.0f; g_tpState = TP_IDLE; }
        for (k = 0; k < 3; k++) o[k] = g_tpFrom[k] + (g_tpDest[k] - g_tpFrom[k]) * g_tpT;
        SetPlayerOrigin(o);
        return;
    }
    if (my > 0.7f) {
        // aiming: the hand of the move stick (the caller's first hand), else the other, else the head
        const vr_pose_t *h = g_in.handValid[dom] ? &g_in.hand[dom] : g_in.handValid[off] ? &g_in.hand[off] : &g_head;
        float body = BodyYaw(g_lastViewAxis[0]), s[3], d[3], pts[64][3], dest[3], color[4];
        int n = 0, ok;
        TrackingToWorld(h->pos, body, s);
        RotateZ(h->axis[0], body, d);
        ok = TeleportArc(s, d, body, pts, &n, dest);
        color[0] = ok ? 0.2f : 1.0f; color[1] = ok ? 1.0f : 0.25f; color[2] = ok ? 0.4f : 0.2f; color[3] = 0.85f;
        if (n >= 2) XR_SetLine((const float (*)[3])pts, n, color, ok ? 0.35f : 0.0f);
        g_tpAiming = 1;
        if (ok) memcpy(g_tpDest, dest, sizeof(g_tpDest));
        g_tpAimOk = ok;
    } else if (g_tpAiming && my < -0.6f) {
        g_tpAiming = 0;   // pulled back: no move
    } else if (g_tpAiming && my < 0.35f) {
        // let go: move if the last arc landed somewhere he fits
        float *me = PlayerOrigin();
        g_tpAiming = 0;
        if (!g_tpAimOk || !me) return;
        if (cfg.moveMode == MOVE_BLINK) {
            g_tpState = TP_FADE_OUT;
        } else {
            // Shift: the straight line must be clear (steps are walked over)
            static const float mins[3] = { -15, -15, 0 }, maxs[3] = { 15, 15, 56 };
            float a[3] = { me[0], me[1], me[2] + TP_STEP_UP }, b[3] = { g_tpDest[0], g_tpDest[1], g_tpDest[2] + TP_STEP_UP };
            if (WorldTraceBox(a, b, mins, maxs) < 1.0f) { Log("move: shift blocked on the way"); return; }
            memcpy(g_tpFrom, me, sizeof(g_tpFrom));
            g_tpT = 0;
            g_tpState = TP_GLIDE;
            Log("move: shift to %.0f %.0f %.0f", g_tpDest[0], g_tpDest[1], g_tpDest[2]);
        }
        XR_Haptic(dom, 0.4f, 40);
    }
}

// Where a hand is relative to the body (the head's yaw stands in for the torso).
static int HandZone(int h)
{
    const float *p = g_in.hand[h].pos;
    float yaw = RAD(YawOf(g_head.axis[0])), c = cosf(yaw), s = sinf(yaw);
    float dx = p[0] - g_head.pos[0], dy = p[1] - g_head.pos[1], up = p[2] - g_head.pos[2];
    float fwd = dx * c + dy * s, left = -dx * s + dy * c;
    float reach = sqrtf(fwd * fwd + left * left);   // how far the hand is from the body: reaching out for something on the floor is not a holster
    if (up > cfg.backHeight && fwd < 0.05f && fabsf(left) < 0.5f) return left > 0 ? ZONE_SHOULDER_L : ZONE_SHOULDER_R;
    // the chest: in front of the body, between the shoulders and the waist
    if (up < cfg.backHeight && up > cfg.hipHeight + 0.2f && fwd > 0.05f && fwd < 0.45f && fabsf(left) < 0.25f) return ZONE_CHEST;
    // behind the body from the waist line down to the lower back
    if (fabsf(up - cfg.lowerBackHeight) < 0.32f && fabsf(left) < 0.4f && fwd < -0.03f && fwd > -0.7f && reach < cfg.waistReach + 0.1f) return ZONE_WAIST_B;
    if (fabsf(up - cfg.hipHeight) < 0.2f && fwd > -0.15f && fwd < 0.5f && reach < cfg.waistReach) {
        if (left > 0.12f && left < 0.5f && fwd < 0.2f) return ZONE_WAIST_L;
        if (left < -0.12f && left > -0.5f && fwd < 0.2f) return ZONE_WAIST_R;
        if (fabsf(left) < 0.12f && fwd > 0.1f) return ZONE_WAIST_F;
    }
    return ZONE_NONE;
}

static void WeaponSlotCmd(int slot)
{
    static const char *names[6] = { "", "primary", "primaryb", "pistol", "grenade", "smokegrenade" };
    char line[48];
    _snprintf(line, sizeof(line), "weaponslot %s\n", names[slot]);
    Cbuf(line);
}

static int g_lastLong;   // the long gun slot drawn last: a Switch holster gives the other one

// The slot a zone gives (0 = nothing there) and what kind of weapon it is.
static int ZoneSlot(const BYTE *cl, int zone, int *kind)
{
    int act = cfg.zoneAct[zone], slot = 0;
    *kind = HELD_LONG;
    if (act == ACT_W1 || act == ACT_W2 || act == ACT_SWITCH) {
        // with only one long gun, any such zone gives it
        int want = act == ACT_W1 ? 1 : act == ACT_W2 ? 2 : g_lastLong == 1 ? 2 : 1, other = want == 1 ? 2 : 1;
        slot = SlotWeapon(cl, want) ? want : SlotWeapon(cl, other) ? other : 0;
    } else if (act == ACT_PISTOL) {
        slot = SlotWeapon(cl, 3) ? 3 : 0;
        *kind = HELD_PISTOL;
    } else if (act == ACT_GRENADE) {
        slot = SlotWeapon(cl, 4) ? 4 : SlotWeapon(cl, 5) ? 5 : 0;
        *kind = HELD_GRENADE;
    }
    return slot;
}

// Does a weapon of this kind go back into this zone? Grenades are thrown, they go anywhere.
static int ZoneHome(int zone, int kind)
{
    int act = cfg.zoneAct[zone];
    return kind == HELD_LONG ? act == ACT_W1 || act == ACT_W2 || act == ACT_SWITCH
         : kind == HELD_PISTOL ? act == ACT_PISTOL : 1;
}

// A weapon taken from or stowed in a holster: a sharp double click ("tum-tum") that cuts through
// the holster's hum (UpdateHaptics plays the second click and holds the hum back meanwhile).
static DWORD g_clickTwoAt[2], g_humPausedUntil[2];

static void HolsterClick(int h)
{
    if (h < 0 || h > 1) return;
    Buzz(h, 0.85f, 30, cfg.hapHolster);
    g_clickTwoAt[h] = GetTickCount() + 90;
    g_humPausedUntil[h] = GetTickCount() + 220;
}

static void DrawWeapon(int h, int zone)
{
    BYTE *cl = PlayerClient();
    int slot = 0, kind;
    if (!cl) return;
    slot = ZoneSlot(cl, zone, &kind);
    if (kind == HELD_LONG && slot) g_lastLong = slot;
    if (!slot) return;   // nothing in that holster
    g_wantWeapon = SlotWeapon(cl, slot);
    g_wantTime = GetTickCount();
    if (GameWeapon(cl) != g_wantWeapon) WeaponSlotCmd(slot);
    g_gunHand = h;
    g_heldKind = kind;
    g_heldWeapon = 0;
    HolsterClick(h);
    Log("realistic: hand %d draws slot %d (weapon %d)", h, slot, g_wantWeapon);
}

// The shortcut "Switch weapon 1 & 2": the other long gun than the one drawn last, as a Switch
// holster gives, straight into the hand that holds the gun (the dominant one when it is empty).
static void SwitchLongGuns(void)
{
    BYTE *cl = PlayerClient();
    int want = g_lastLong == 1 ? 2 : 1, other = want == 1 ? 2 : 1, slot, h;
    if (!cl) return;
    slot = SlotWeapon(cl, want) ? want : SlotWeapon(cl, other) ? other : 0;
    if (!slot) return;   // no long gun
    g_lastLong = slot;
    if (!cfg.realistic) { WeaponSlotCmd(slot); return; }
    h = g_gunHand >= 0 ? g_gunHand : cfg.rightHanded ? 1 : 0;
    g_wantWeapon = SlotWeapon(cl, slot);
    g_wantTime = GetTickCount();
    if (GameWeapon(cl) != g_wantWeapon) WeaponSlotCmd(slot);
    g_gunHand = h;
    g_heldKind = HELD_LONG;
    g_heldWeapon = 0;
    HolsterClick(h);
    Log("shortcut: hand %d switches to slot %d (weapon %d)", h, slot, g_wantWeapon);
}

// The shortcut "Comfort vignette": Off, Low, Medium, High (the launcher's steps), then Off again;
// kept for next time in the player's settings.
static void CycleVignette(void)
{
    static const float levels[4] = { 0.0f, 0.35f, 0.6f, 0.9f };
    static const char *const names[4] = { "Off", "Low", "Medium", "High" };
    char v[16], line[64];
    int i, cur = 0;
    if (cfg.vignette)
        for (i = 1; i < 4; i++)
            if (fabsf(cfg.vignetteStrength - levels[i]) < fabsf(cfg.vignetteStrength - levels[cur]) || cur == 0) cur = i;
    cur = (cur + 1) % 4;
    cfg.vignette = cur > 0;
    if (cur > 0) cfg.vignetteStrength = levels[cur];
    if (g_userIni[0]) {
        WritePrivateProfileStringA("Comfort", "Vignette", cur > 0 ? "1" : "0", g_userIni);
        _snprintf(v, sizeof(v), "%.2f", levels[cur]);
        if (cur > 0) WritePrivateProfileStringA("Comfort", "VignetteStrength", v, g_userIni);
    }
    _snprintf(line, sizeof(line), "Comfort vignette: %s", names[cur]);
    GamePrintBold(line);
    Log("shortcut: comfort vignette %s", names[cur]);
}

// The hand's position in the game world (the inverse of the eyes' placement).
static void HandWorld(int h, float out[3])
{
    float rel[3], r[3], body = BodyYaw(g_lastViewAxis[0]);
    int i;
    BodyOffset(g_in.hand[h].pos, rel);
    for (i = 0; i < 3; i++) rel[i] *= cfg.worldScale;
    RotateZ(rel, body, r);
    for (i = 0; i < 3; i++) out[i] = g_lastViewOrg[i] + r[i];
}

// The game module's vm_t: its traps reach the engine through currentVM, so calling game code
// (or a game trap) from outside the game's own frame needs currentVM set to it meanwhile.
static BYTE *GameVM(void)
{
    static BYTE *gameVM;
    int k;
    if (!gameVM)
        for (k = 0; k < VM_COUNT; k++) {
            BYTE *vm = (BYTE *)ADDR_vmTable + k * VM_SIZE;
            const char *name = (const char *)vm + VM_OFS_name;
            if (*(void **)vm && (!_stricmp(name, "game") || !_stricmp(name, "qagame"))) gameVM = vm;
        }
    return gameVM;
}

// A configstring of the running game (server side), through the game's own trap.
static int GameConfigString(int index, char *buf, int size);

// A message in the middle of the screen, as the game's own iprintlnbold.
static void GamePrintBold(const char *text)
{
    BYTE *vm = GameVM(), *prev;
    char cmd[256];
    if (!vm || !GetModuleHandleA(GAME_DLL)) return;
    _snprintf(cmd, sizeof(cmd), "gmb \"%s\"", text);
    cmd[sizeof(cmd) - 1] = 0;
    prev = MEM(BYTE *, ADDR_currentVM);
    MEM(BYTE *, ADDR_currentVM) = vm;
    __try {
        __asm {
            lea eax, cmd
            push eax
            push -1
            push G_TRAP_SendServerCommand
            mov eax, ADDR_VM_DllSyscall
            call eax
            add esp, 12
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    MEM(BYTE *, ADDR_currentVM) = prev;
}

// The mission after each one (from the game's own missionSuccess / changelevel calls).
static const char *NextMission(const char *map)
{
    static const char *next[][2] = {
        { "training", "us_intro" }, { "us_intro", "pathfinder" }, { "pathfinder", "burnville" },
        { "burnville", "dawnville" }, { "dawnville", "carride" }, { "carride", "brecourt" },
        { "brecourt", "us_mid" }, { "us_mid", "chateau" }, { "chateau", "powcamp" }, { "powcamp", "uk_6ab" },
        { "uk_6ab", "pegasusnight" }, { "pegasusnight", "pegasusday" }, { "pegasusday", "uk_sas" },
        { "uk_sas", "dam" }, { "dam", "truckride" }, { "truckride", "airfield" }, { "airfield", "ship" },
        { "ship", "ru_stalingrad" }, { "ru_stalingrad", "stalingrad" }, { "stalingrad", "redsquare" },
        { "redsquare", "trainstation" }, { "trainstation", "sewer" }, { "sewer", "pavlov" },
        { "pavlov", "factory" }, { "factory", "railyard" }, { "railyard", "tankdrivecountry" },
        { "tankdrivecountry", "tankdrivetown" }, { "tankdrivetown", "allied_start" },
        { "allied_start", "hurtgen" }, { "hurtgen", "rocket" }, { "rocket", "berlin" }, { "berlin", "credits" },
        // United Offensive (from its mission scripts)
        { "bastogne1", "bastogne2" }, { "bastogne2", "foy" }, { "foy", "noville" }, { "noville", "gmi_uk_intro" },
        { "gmi_uk_intro", "bomber" }, { "trainbridge", "gmi_uk_mid" }, { "gmi_uk_mid", "sicily1" }, { "sicily1", "sicily2" },
        { "ponyri", "kursk" }, { "kursk", "kharkov1" }, { "kharkov1", "kharkov2" }, { "kharkov2", "credits" },
        { "gmi_ru_intro", "trenches" },
    };
    int i;
    for (i = 0; i < (int)(sizeof(next) / sizeof(next[0])); i++)
        if (!_stricmp(map, next[i][0])) return next[i][1];
    return NULL;
}

// The map being played, from the server info ("\mapname\carride\...").
static int CurrentMap(char *out, int len)
{
    char info[1024], *p, *e;
    out[0] = 0;
    if (!GameConfigString(0, info, sizeof(info))) return 0;
    if (!(p = strstr(info, "\\mapname\\"))) return 0;
    p += 9;
    e = strchr(p, '\\');
    if (!e) e = p + strlen(p);
    if (e - p >= len) return 0;
    memcpy(out, p, e - p);
    out[e - p] = 0;
    return 1;
}

// In a vehicle, holding both triggers (and nothing else) for 4 seconds skips to the next
// mission: for players the ride makes sick. Said on screen when the ride starts.
// A vehicle's gun (the Flak 88) is held with a grip, as the turrets: letting go gets off.
static void UpdateGunVehicleGrip(void)
{
    static DWORD releasedAt;
    static int sent;
    if (!g_inVehicle || !g_gunVehicle) { releasedAt = 0; sent = 0; return; }
    if (g_in.grip[0] > 0.3f || g_in.grip[1] > 0.3f) { releasedAt = 0; sent = 0; return; }
    if (!releasedAt) releasedAt = GetTickCount();
    else if (!sent && GetTickCount() - releasedAt > 300) {
        Cbuf("+activate\n");
        Cbuf("-activate\n");
        sent = 1;
        Log("vehicle gun: let go");
    }
}

static void UpdateSkipMission(float dt)
{
    static int wasInVehicle, told;
    static float held;
    int others = g_in.buttons != 0 || (!g_rideGun && (g_in.grip[0] > 0.3f || g_in.grip[1] > 0.3f)) ||   // (the grips hold a vehicle's gun)
                 fabsf(g_in.stick[0][0]) > 0.3f || fabsf(g_in.stick[0][1]) > 0.3f ||
                 fabsf(g_in.stick[1][0]) > 0.3f || fabsf(g_in.stick[1][1]) > 0.3f;
    static DWORD toldAt;
    if (g_inVehicle && !wasInVehicle) told = 0;
    wasInVehicle = g_inVehicle;
    if (!g_inVehicle || (g_gunVehicle && !g_rideGun) || g_noRideAids) { held = 0; return; }
    {
        // only in the missions that are a ride, where there is a next one to go to (not a short scripted
        // transport at the start of a level)
        char map[64];
        if (!CurrentMap(map, sizeof(map)) || !NextMission(map)) { held = 0; return; }
    }
    if (!told) {
        GamePrintBold("Feeling sick? Hold both triggers for 4 seconds to skip this mission.");
        GamePrintBold("Push the right stick up or down to change the vignette.");
        told = 1;
        toldAt = GetTickCount();
    } else if (told == 1 && GetTickCount() - toldAt > 8500) {   // as the first ones go (con_boldgamemessagetime 8)
        char map[64];
        if (CurrentMap(map, sizeof(map)) && !_stricmp(map, "carride"))   // only that mission has a window to lean out of
            GamePrintBold("Press A to lean out of the window.");
        told = 2;
    }
    if (g_in.trigger[0] > 0.8f && g_in.trigger[1] > 0.8f && !others) {
        if (held == 0) GamePrintBold("Going to the next mission...");
        held += dt;
    } else {
        held = held < 0 ? held : 0;
    }
    if (held > 0 && held < 4.0f) {
        // the bar, 1.2 m ahead of the eyes and a little low, level, facing them
        float f[3] = { g_head.axis[0][0], g_head.axis[0][1], 0 }, c[3], r[3], u[3] = { 0, 0, 0.025f };
        float len = sqrtf(f[0] * f[0] + f[1] * f[1]);
        int i;
        if (len > 1e-3f) {
            f[0] /= len; f[1] /= len;
            for (i = 0; i < 3; i++) c[i] = g_head.pos[i] + f[i] * 1.2f;
            c[2] -= 0.12f;
            r[0] = f[1] * 0.25f; r[1] = -f[0] * 0.25f; r[2] = 0;   // to the right, 50 cm wide
            XR_SetProgress(c, r, u, held / 4.0f);
        }
    }
    if (held >= 4.0f) {
        char map[64], cmd[96];
        const char *next;
        held = -1000.0f;   // once
        if (CurrentMap(map, sizeof(map)) && (next = NextMission(map))) {
            Log("skipping mission %s -> %s", map, next);
            GamePrintBold("Skipping to the next mission...");
            _snprintf(cmd, sizeof(cmd), "map %s\n", next);
            Cbuf(cmd);
        } else {
            Log("skip mission: no next mission known for '%s'", map);
            GamePrintBold("This mission cannot be skipped.");
        }
    }
}

static int GameConfigString(int index, char *buf, int size)
{
    BYTE *vm = GameVM(), *prev;
    int ok = 1, len = size;   // "size" is an assembler keyword
    buf[0] = 0;
    if (!vm || !GetModuleHandleA(GAME_DLL)) return 0;
    prev = MEM(BYTE *, ADDR_currentVM);
    MEM(BYTE *, ADDR_currentVM) = vm;
    __try {
        __asm {
            push len
            push buf
            push index
            push G_TRAP_GetConfigstring
            mov eax, ADDR_VM_DllSyscall
            call eax
            add esp, 16
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    MEM(BYTE *, ADDR_currentVM) = prev;
    buf[size - 1] = 0;
    return ok;
}

// Value of key in an info string ("\\key\\value\\key\\value"), copied to out.
static void InfoValue(const char *info, const char *key, char *out, int size)
{
    size_t kl = strlen(key);
    out[0] = 0;
    while (*info == '\\') {
        const char *k = info + 1, *v = strchr(k, '\\'), *e;
        if (!v) return;
        e = strchr(v + 1, '\\');
        if (!e) e = v + 1 + strlen(v + 1);
        if ((size_t)(v - k) == kl && !_strnicmp(k, key, kl)) {
            int n = (int)(e - v - 1);
            if (n >= size) n = size - 1;
            memcpy(out, v + 1, n);
            out[n] = 0;
            return;
        }
        info = e;
    }
}

// The empty-hand pointer is wanted: always (EmptyHandPointer=2), or on the training map until
// the first objective (pointing at the signs) is done (1).
static int PointerWanted(void)
{
    static DWORD last;
    static int wanted;
    if (cfg.emptyPointer >= 2) return 1;
    if (cfg.emptyPointer <= 0) return 0;
    if (GetTickCount() - last > 500) {
        char cs[2048], v[64];
        int i, done = 0;
        last = GetTickCount();
        wanted = 0;
        if (GameConfigString(CS_SERVERINFO, cs, sizeof(cs))) {
            InfoValue(cs, "mapname", v, sizeof(v));
            if (!_stricmp(v, "training")) {
                for (i = 0; i < 16 && !done; i++)
                    if (GameConfigString(CS_OBJECTIVES + i, cs, sizeof(cs))) {
                        InfoValue(cs, "state", v, sizeof(v));
                        if (v[0] && atoi(v) == OBJST_DONE) done = 1;
                    }
                wanted = !done;
            }
        }
    }
    return wanted;
}

static int DropFilter(EXCEPTION_POINTERS *e, HMODULE m)
{
    BYTE *at = (BYTE *)e->ExceptionRecord->ExceptionAddress;
    Log("realistic: weapon drop failed: exception %08x at %p (gamex86 + %x), reading %p",
        e->ExceptionRecord->ExceptionCode, at, (unsigned)(at - (BYTE *)m),
        e->ExceptionRecord->NumberParameters > 1 ? (void *)e->ExceptionRecord->ExceptionInformation[1] : NULL);
    return EXCEPTION_EXECUTE_HANDLER;
}

#ifdef COD_VR_TEST
static BYTE *g_testItem;
#endif
// Drops the weapon in use on the floor below the hand, with its ammo, as a pickup.
static void DropHeldWeapon(int h)
{
    HMODULE m = GetModuleHandleA(GAME_DLL);
    BYTE *cl = PlayerClient(), *player, *item = NULL, *fn, *prevVM;
    int weapon = GameWeapon(cl), i;
    float pos[3], frac;
    if (!m || !cl || weapon <= 0) return;
    player = (BYTE *)m + G_OFS_entities;
    fn = (BYTE *)m + G_OFS_DropWeapon;
    {
        // the weapon's pickup item (bg_itemlist): LaunchItem reads its model name
        const DWORD *it = (const DWORD *)((BYTE *)m + G_OFS_itemlist + weapon * G_ITEM_SIZE);
        __try {
            Log("realistic: dropping weapon %d, item %08x %08x %08x type %d", weapon, it[0], it[1], it[2], it[8]);
            if (!it[2]) { Log("realistic: weapon %d has no pickup model, put away instead", weapon); return; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
    }
    {
        // the drop's traps reach the engine through currentVM: make it the game's while it runs
        BYTE *gameVM = GameVM();
        int k;
        if (!gameVM) {
            for (k = 0; k < VM_COUNT; k++)
                Log("realistic: vm %d '%.16s'", k, (const char *)ADDR_vmTable + k * VM_SIZE + VM_OFS_name);
            Log("realistic: game VM not found, the weapon is put away instead");
            return;
        }
        prevVM = MEM(BYTE *, ADDR_currentVM);
        MEM(BYTE *, ADDR_currentVM) = gameVM;
    }
    __try {
        __asm {
            push 0              // no tag: launched from the player
            push weapon
            mov eax, player
            mov ecx, fn
            call ecx
            add esp, 8
            mov item, eax
        }
    } __except (DropFilter(GetExceptionInformation(), m)) {
        MEM(BYTE *, ADDR_currentVM) = prevVM;
        return;
    }
    MEM(BYTE *, ADDR_currentVM) = prevVM;
    if (item) {
        // let it fall from the hand, on this side of any wall between the eye and the hand
        HandWorld(h, pos);
        frac = WorldTrace(g_lastViewOrg, pos);
        if (frac < 1.0f)
            for (i = 0; i < 3; i++) pos[i] = g_lastViewOrg[i] + (pos[i] - g_lastViewOrg[i]) * frac * 0.9f;
        __try {
            memcpy(item + GENT_OFS_posBase, pos, sizeof(pos));
            memset(item + GENT_OFS_posDelta, 0, 3 * sizeof(float));
            memcpy(item + GENT_OFS_currentOrigin, pos, sizeof(pos));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    Log("realistic: weapon %d dropped (item %p)", weapon, item);
#ifdef COD_VR_TEST
    g_testItem = item;
#endif
    // the game still has the dropped weapon selected: change to one it has (hidden, empty hands); with the
    // dropped one still selected the game does not take the same weapon up from the floor. Grenades count
    // too: someone who only had a rifle and a grenade (United Offensive's start) could not pick it up again.
    for (i = 1; i <= 5; i++)
        if (SlotWeapon(cl, i)) { WeaponSlotCmd(i); break; }
}

// A hand closing on a weapon lying on the floor: the game's own "use" takes it (the gun goes into that hand
// through the weapon slots). The game picks what to use by the way the head looks; with a hand that is the
// wrong test, so the nearest dropped weapon within reach of the hand is handed to the game's use function.
// Returns 1 when one was taken.
static int HandPickup(int h)
{
    HMODULE m = GetModuleHandleA(GAME_DLL);
    BYTE *gm = (BYTE *)m, *player, *best = NULL, *prevVM, *gameVM;
    int n, i;
    float hand[3], bestD = 90.0f * 90.0f;
    if (!m || !PlayerClient() || !MapLoaded()) return 0;
    HandWorld(h, hand);
    player = gm + G_OFS_entities;
    __try {
        n = *(int *)(gm + G_OFS_numEntities);
        if (n > 1024) n = 1024;
        for (i = 1; i < n; i++) {
            BYTE *e = gm + G_OFS_entities + i * G_ENTITY_SIZE;
            const BYTE *item;
            const float *o;
            float d[3], dd;
            if (!e[GENT_OFS_inuse] || e[0] != 2) continue;               // an item
            item = *(const BYTE **)(e + GENT_OFS_item);
            if (!item || *(const int *)(item + GITEM_OFS_giType) != 1) continue;   // a weapon
            if (!*(const int *)(e + GENT_OFS_useIdx)) continue;
            o = (const float *)(e + GENT_OFS_currentOrigin);
            d[0] = o[0] - hand[0]; d[1] = o[1] - hand[1]; d[2] = o[2] - hand[2];
            dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (dd < bestD) { bestD = dd; best = e; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!best) return 0;
    gameVM = GameVM();
    if (!gameVM) return 0;
    prevVM = MEM(BYTE *, ADDR_currentVM);
    MEM(BYTE *, ADDR_currentVM) = gameVM;
    __try {
        typedef void (__cdecl *PFN_Use)(BYTE *item, BYTE *user, int arg);
        PFN_Use use = ((PFN_Use *)(gm + G_OFS_useTable))[*(int *)(best + GENT_OFS_useIdx)];
        Log("realistic: hand %d takes the weapon item %d from the floor", h, (int)((best - (gm + G_OFS_entities)) / G_ENTITY_SIZE));
        use(best, player, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("realistic: taking the weapon from the floor failed");
    }
    MEM(BYTE *, ADDR_currentVM) = prevVM;
    return 1;
}

// Controller vibration, scaled by Haptics (0.6 = the strengths written here).
// Controller vibration of one kind (HapticsShots / Damage / Holsters; 0.6 = the strengths here).
static void Buzz(int h, float amp, int ms, float strength)
{
    if (strength > 0) XR_Haptic(h, amp * strength / 0.6f, ms);
}

// Rounds in the clip of weapon w, or -1.
static int GameClip(const BYTE *cl, int w)
{
    HMODULE gm = GetModuleHandleA(GAME_DLL);
    int clip = -1;
    if (!cl || !gm || w <= 0) return -1;
    __try {
        BYTE **defs = *(BYTE ***)((BYTE *)gm + G_OFS_weaponDefs);
        if (defs && defs[w]) clip = *(const int *)(cl + GCL_OFS_ammoclip + *(int *)(defs[w] + WEAPDEF_OFS_clipIndex) * 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        clip = -1;
    }
    return clip;
}

// A hand reaching a holster it can use: an empty hand where a weapon is, or the weapon's own place.
static int ZoneUseful(int h, int zone)
{
    BYTE *cl = PlayerClient();
    if (zone == ZONE_NONE || !cl) return 0;
    if (h == g_gunHand)
        return g_heldKind == HELD_GRENADE ? cfg.zoneAct[zone] == ACT_GRENADE : ZoneHome(zone, g_heldKind);
    if (g_gunHand >= 0) return 0;   // one weapon at a time
    switch (cfg.zoneAct[zone]) {
    case ACT_W1: case ACT_W2: case ACT_SWITCH: return SlotWeapon(cl, 1) || SlotWeapon(cl, 2);
    case ACT_PISTOL: return SlotWeapon(cl, 3) != 0;
    case ACT_GRENADE: return SlotWeapon(cl, 4) || SlotWeapon(cl, 5);
    }
    return 0;
}

// Every frame: a tick when a hand reaches a usable holster, a kick on each shot (seen by the
// muzzle flash of the weapon in the hand, at most every 70 ms, so automatic fire rattles), and
// both hands shake when the player is hit, harder for more damage.
static int shot_clip(int w, int lastW, int clip, int lastClip)
{
    return w == lastW && clip >= 0 && lastClip >= 0 && clip < lastClip;
}

// The kick of a mounted gun: a machine gun rattles in both hands, a cannon thumps harder and longer.
static void MountedKick(int cannon)
{
    if (cannon) {
        Buzz(0, 1.0f, 140, cfg.hapShots);
        Buzz(1, 1.0f, 140, cfg.hapShots);
    } else {
        Buzz(0, 0.8f, 45, cfg.hapShots);
        Buzz(1, 0.8f, 45, cfg.hapShots);
    }
}

static void UpdateHaptics(void)
{
    static int lastZone[2], lastHealth = -1, lastWeapon, lastClip = -1, wasMounted, clipSeen;
    static DWORD lastShot, mountedAt;
    HMODULE gm = GetModuleHandleA(GAME_DLL);
    DWORD now = GetTickCount();
    BYTE *cl = PlayerClient();
    int mounted = g_onTurret || (g_inVehicle && g_gunVehicle);
    int h, health = -1, w = GameWeapon(cl), clip = GameClip(cl, w), shot;
    for (h = 0; h < 2; h++) {
        static DWORD enteredAt[2];
        int zone = g_in.handValid[h] ? HandZone(h) : ZONE_NONE, useful = ZoneUseful(h, zone);
        // Reaching a holster it can use: one vibration of a second (HolsterHum; else just a tick), not again for 1.5 s
        // so a hand shivering on the edge does not keep buzzing. It used to hum for as long as the hand stayed there,
        // which in a walking posture (hands at the waist) was all the time.
        if (zone != lastZone[h] && useful) {
            if (!cfg.holsterHum) Buzz(h, 0.25f, 25, cfg.hapHolster);
            else if (now - enteredAt[h] > 1500) { Buzz(h, 0.2f, 1000, cfg.hapHolster); enteredAt[h] = now; }
        }
        lastZone[h] = zone;
        if (g_clickTwoAt[h] && now >= g_clickTwoAt[h]) {   // the second click of a take or stow
            Buzz(h, 0.85f, 30, cfg.hapHolster);
            g_clickTwoAt[h] = 0;
        }
    }
    // a shot: a round left the clip of the same weapon (scoped rifles show no flash), or the
    // weapon in the hand drew its muzzle flash
    shot = g_gunFlashed || (w == lastWeapon && clip >= 0 && lastClip >= 0 && clip < lastClip);
    if (mounted) {
        // turrets and vehicle guns: both hands hold the gun. The clip tells a shot when the game
        // counts rounds for it; otherwise (a tank cannon, unlimited ammo) the trigger stands in
        // for it, paced like the gun.
        int trig = Pressed(g_in.trigger[0], BT_PTRIGGER) || Pressed(g_in.trigger[1], BT_PTRIGGER);
        if (!wasMounted) { clipSeen = 0; mountedAt = now; }
        if (w == lastWeapon && clip >= 0 && lastClip >= 0 && clip < lastClip) clipSeen = 1;
        if (shot_clip(w, lastWeapon, clip, lastClip) && now - lastShot >= 60) {
            lastShot = now;
            MountedKick(g_tankMode);
        } else if (trig && !clipSeen && now - mountedAt > 400 && now - lastShot >= (g_tankMode ? 900u : 70u)) {
            lastShot = now;
            MountedKick(g_tankMode);
        }
    } else if (shot && g_gunHand >= 0 && now - lastShot >= 70) {
        const char *wn = WeaponName(CurrentWeaponDef());
        lastShot = now;
        if (wn && (strstr(wn, "panzerfaust") || strstr(wn, "Panzerfaust"))) {
            // the rocket leaves the tube with one hard push on both hands
            Buzz(g_gunHand, 1.0f, 160, cfg.hapShots);
            Buzz(1 - g_gunHand, 0.8f, 160, cfg.hapShots);
        } else {
            Buzz(g_gunHand, 0.85f, 40, cfg.hapShots);
            if (g_twoHand) Buzz(1 - g_gunHand, 0.5f, 40, cfg.hapShots);
        }
    }
    wasMounted = mounted;
    g_gunFlashed = 0;
    lastWeapon = w;
    lastClip = clip;
    if (gm && PlayerClient()) {
        __try {
            health = *(const int *)((BYTE *)gm + G_OFS_entities + GENT_OFS_health);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            health = -1;
        }
    }
    if (health >= 0 && lastHealth > 0 && health < lastHealth) {
        int dmg = lastHealth - health;
        float amp = 0.45f + dmg / 60.0f;
        int ms = 90 + dmg * 3;
        if (health <= 0) { amp = 1.0f; ms = 600; }   // killed
        if (amp > 1.0f) amp = 1.0f;
        if (ms > 300 && health > 0) ms = 300;
        Buzz(0, amp, ms, cfg.hapDamage);
        Buzz(1, amp, ms, cfg.hapDamage);
    }
    lastHealth = health;
}

// Puts the eyes at the gun's seat (g_turretCanon from the pivot, turned with the gun's base) for the head as it is now:
// the roomscale centre is taken at the head's place and the head's offset from it is cancelled, so the eyes are at the
// seat whatever the head did before. On a gun the roomscale anchor is not kept (UpdateRoomScale lets go while
// mounted), so the head's place was measured from a stale one. The body's yaw stays: the aim and the view do not jump.
static void TurretReseat(void)
{
    float rel[3], p[3], canon[3];
    int i;
    if (cfg.roomScale && cfg.positional) { g_anchor[0] = g_head.pos[0]; g_anchor[1] = g_head.pos[1]; g_anchorInit = 1; }
    BodyOffset(g_head.pos, rel);
    RotateZ(rel, g_turretBody, p);
    RotateZ(g_turretCanon, g_turretYawNow - g_turretCanonYaw, canon);   // the seat turns with the plane
    for (i = 0; i < 3; i++) g_turretOrgBase[i] = g_turretPivotWorld[i] + canon[i] - p[i] * cfg.worldScale;
}

// Stand up straight and press the recenter button: this is the soldier's eye height, and the
// roomscale centre is here.
static void Recenter(void)
{
    if (g_inVehicle) g_vehicleRecenter = 1;
    g_heightRef = g_head.pos[2];
    g_menuQuadValid = 0;   // a menu on screen comes back in front of the head
    g_anchorInit = 0;
    g_crouchCorr = 0;
    if (g_onTurret && g_turretOrgSet) {
        if (g_turretScripted) g_turretBody = YawOf(g_lastViewAxis[0]) - g_headYawCmd;   // the game's body, not a stale one
        TurretReseat();
        Log("turret: recentered at the seat");
    }
    Log("recentered: head height %.2f", g_heightRef);
    Buzz(0, 0.5f, 60, cfg.hapHolster);
    Buzz(1, 0.5f, 60, cfg.hapHolster);
}

// Physical crouch: the head going CrouchDepth below its standing height crouches the soldier,
// standing up stands him. While crouched for real the camera keeps the real head height (the
// game's own lower eye is given back), so crouching behind cover looks the way it feels.
static int g_crouchOwned;   // the game crouched because the player did (camera corrected)

// The game's eye height above the player's origin: 60 standing, 40 crouched, 11 prone.
static float GameViewHeight(const float *vieworg)
{
    HMODULE gm = GetModuleHandleA(GAME_DLL);
    float vh = 60.0f;
    if (!gm || !PlayerClient()) return vh;
    __try {
        vh = vieworg[2] - ((const float *)((BYTE *)gm + G_OFS_entities + GENT_OFS_currentOrigin))[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        vh = 60.0f;
    }
    return vh;
}

// Called with the game's own view of the frame, before the eyes and the gun are placed: gives
// back exactly how far the game has lowered its eye, in the same frame (a correction measured on
// the previous frame, or smoothed, lagged behind the game's crouch and the view dipped).
static void SetCrouchCorrection(const float *vieworg)
{
    float c = 0;
    if (g_crouchOwned) {
        c = (60.0f - GameViewHeight(vieworg)) / cfg.worldScale;
        if (c < 0) c = 0;
        if (c > 0.8f) c = 0.8f;
    }
    g_crouchCorr = c;
}

static void UpdatePhysicalCrouch(float dt)
{
    static int crouched;
    float drop = g_heightRef - g_head.pos[2], vh;
    int stance;
    (void)dt;
    if (!cfg.physCrouch || !cfg.positional || !MapLoaded() || !g_lastHudWorld || !PlayerClient()) {
        crouched = g_crouchOwned = 0;
        return;
    }
    vh = GameViewHeight(g_lastViewOrg);
    stance = vh > 50 ? 0 : vh > 25 ? 1 : 2;
    if (!crouched && drop > cfg.crouchDepth) {
        crouched = 1;
        if (stance == 0) { Cbuf("lowerstance\n"); g_crouchOwned = 1; }
    } else if (crouched && drop < cfg.crouchDepth - 0.08f) {
        crouched = 0;
        if (stance == 1 && g_crouchOwned) Cbuf("raisestance\n");
    }
    if (g_crouchOwned && stance == 2) g_crouchOwned = 0;               // went prone with the stick
    if (!crouched && g_crouchOwned && vh >= 59.5f) g_crouchOwned = 0;  // stood up again
}

static void GripPressed(int h)
{
    int zone = HandZone(h);
    if (g_gunHand < 0) {
        if (zone != ZONE_NONE) {
            DrawWeapon(h, zone);
            if (g_gunHand == h) return;
        }
        g_useHand = h;   // grabbing nothing: use (pick up, doors)
        g_useEnt = 0;
        if (g_uo || g_inVehicle) HandPickup(h);   // in a vehicle the game's own use is blocked (it leans out)
    } else if (h != g_gunHand) {
        // the off hand: on the gun it holds it with both hands (UpdateGunPose), anywhere else use (plant a bomb)
        // (the hand reaching into something usable, a bomb's trigger: always use, wherever it is from the gun)
        int ent = UsableAtHand(h);
        if (ent || !OffHandOnGun(&g_in.hand[g_gunHand], &g_in.hand[h])) {
            g_useHand = h;
            g_useEnt = ent;
            Log("realistic: hand %d uses while hand %d holds the gun%s", h, g_gunHand, ent ? " (on something usable)" : "");
        }
    }
}

static void GripReleased(int h)
{
    if (h == g_useHand) { g_useHand = -1; g_useEnt = 0; }
    if (h != g_gunHand) return;
    {
        // a weapon goes back only where it belongs: long guns over a shoulder, the pistol at the
        // hip, grenades at the lower back (a gun held low at the side is not being holstered)
        int zone = HandZone(h);
        int home = ZoneHome(zone, g_heldKind);
        if (!home && cfg.dropOnRelease && g_in.handValid[h] && !g_inVehicle) {   // in a vehicle it would fall out
            DropHeldWeapon(h);
            Buzz(h, 0.7f, 80, cfg.hapHolster);
        } else {
            if (home) HolsterClick(h);   // stowed in its holster
            else Buzz(h, 0.45f, 50, cfg.hapHolster);
            Log("realistic: hand %d puts the weapon away", h);
        }
    }
    g_gunHand = -1;
}

// Follows what the game does with the weapon: the switch asked for, a grenade thrown (the
// game goes back to another weapon), a weapon picked up with an empty hand.
static void WatchHeldWeapon(void)
{
    BYTE *cl = PlayerClient();
    int w = GameWeapon(cl);
    if (!cl) return;
    if (g_gunHand >= 0) {
        if (!g_heldWeapon) {
            if (w == g_wantWeapon) g_heldWeapon = w;
            else if (GetTickCount() - g_wantTime > 1500) {   // the game did not change weapon
                if (g_heldKind == HELD_GRENADE) g_gunHand = -1;
                else g_heldWeapon = w;
            }
        } else if (w != g_heldWeapon) {
            if (g_heldKind == HELD_GRENADE) g_gunHand = -1;   // thrown
            else g_heldWeapon = w;                           // picked up, out of ammo, next weapon
        }
    } else if (g_useHand >= 0 && g_slotsKnown) {
        // A weapon picked up with the empty hand goes to that hand. The game puts it in a slot
        // (an empty one, or in place of the weapon it swaps out) but does not always change to
        // it, so watch the gun slots and take whatever arrives.
        int slot;
        for (slot = 1; slot <= 3; slot++) {
            int now = SlotWeapon(cl, slot);
            if (!now || now == g_lastSlots[slot]) continue;
            g_wantWeapon = now;
            g_wantTime = GetTickCount();
            if (w != now) WeaponSlotCmd(slot);
            g_gunHand = g_useHand;
            g_heldKind = slot == 3 ? HELD_PISTOL : HELD_LONG;
            if (slot < 3) g_lastLong = slot;
            g_heldWeapon = 0;
            g_useHand = -1;
            Buzz(g_gunHand, 0.5f, 50, cfg.hapHolster);
            Log("realistic: hand %d picked up weapon %d (slot %d)", g_gunHand, now, slot);
            break;
        }
    }
    if (g_uo) {
        // United Offensive: the weapon slots, whenever one changes (has the game taken a weapon?)
        static int shown[8];
        int slot, changed = 0;
        for (slot = 1; slot < 8; slot++) if (SlotWeapon(cl, slot) != shown[slot]) changed = 1;
        if (changed) {
            for (slot = 1; slot < 8; slot++) shown[slot] = SlotWeapon(cl, slot);
            Log("slots: %d %d %d %d %d %d %d, weapon in use %d", shown[1], shown[2], shown[3], shown[4], shown[5], shown[6], shown[7], w);
        }
    }
    {
        int slot;
        for (slot = 1; slot < 6; slot++) g_lastSlots[slot] = SlotWeapon(cl, slot);
        g_slotsKnown = 1;
    }
    if (g_uo && w != g_lastGameWeapon) Log("game weapon in use: %d -> %d", g_lastGameWeapon, w);
    g_lastGameWeapon = w;
}

static void RealisticHands(void)
{
    int h;
    for (h = 0; h < 2; h++) {
        int was = g_gripDown[h];
        int down = was ? g_in.grip[h] > 0.35f : g_in.grip[h] > 0.65f;
        g_gripDown[h] = down;
        if (g_realSync) continue;   // back from a menu: take the grips as they are, no actions
        if (down && !was) GripPressed(h);
        else if (!down && was) GripReleased(h);
    }
    if (g_realSync) { g_useHand = -1; g_realSync = 0; }
    {
        // In a vehicle something held out to the player (Waters offers the Panzerfaust in the
        // truck) can be taken with a trigger of an empty hand too, as with the grip: with nothing
        // in the hands the trigger has nothing to fire.
        static int trigWas[2];
        static DWORD useUntil;
        for (h = 0; h < 2; h++) {
            int down = trigWas[h] ? g_in.trigger[h] > 0.35f : g_in.trigger[h] > 0.65f;
            if (down && !trigWas[h] && g_inVehicle && !g_gunVehicle && g_gunHand < 0 && !g_gripDown[h] && HandPickup(h)) {
                g_useHand = h;
                useUntil = GetTickCount() + 800;
            }
            trigWas[h] = down;
        }
        if (useUntil && GetTickCount() > useUntil) {
            useUntil = 0;
            if (!g_gripDown[0] && !g_gripDown[1]) g_useHand = -1;
        }
    }
    WatchHeldWeapon();
    UpdateHaptics();
    SetButton(BT_PGRIP, g_useHand >= 0);
    SetButton(BT_SGRIP, 0);
    {
        // Either trigger fires (the gun is aimed with its own sights, the game's aim-down-sights
        // mode is not needed); with a scoped weapon the other hand's trigger zooms the scope. United
        // Offensive's LMGs (.30 cal, MG34, DP28) are set up on their bipod with aim down the sights
        // ("Press [..] to setup your weapon"), held for as long as it stays set up: there a press of
        // the other hand's trigger sets it up and the next one takes it down.
        static int lmgSetUp, lmgTrigger;
        const BYTE *def = CurrentWeaponDef();
        int scoped = 0, lmg = 0, other = 1 - g_gunHand;
        int fire;
        __try {
            const char *ov = def ? *(const char * const *)(def + WEAPDEF_OFS_adsOverlayShader) : NULL;
            const char *dep = def && WEAPDEF_OFS_lmgDeployAnim ? *(const char * const *)(def + WEAPDEF_OFS_lmgDeployAnim) : NULL;
            scoped = ov && ov[0];
            lmg = dep && dep[0];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            scoped = lmg = 0;
        }
        if (lmg && g_gunHand >= 0) {
            float v = g_in.trigger[other];
            int down = lmgTrigger ? v > 0.35f : v > 0.65f;
            if (down && !lmgTrigger) {
                lmgSetUp = !lmgSetUp;
                Log("LMG: %s", lmgSetUp ? "set up (aim down the sights held)" : "taken down");
            }
            lmgTrigger = down;
            scoped = 1;   // the other hand's trigger does not fire
        } else {
            lmgSetUp = lmgTrigger = 0;   // another weapon, or none in the hand
        }
        fire = g_gunHand >= 0 && (Pressed(g_in.trigger[g_gunHand], BT_PTRIGGER) ||
                                  (!scoped && Pressed(g_in.trigger[other], BT_PTRIGGER)));
        if (g_slideShow) fire = Pressed(g_in.trigger[0], BT_PTRIGGER) || Pressed(g_in.trigger[1], BT_PTRIGGER);   // "fire to skip"

        SetButton(BT_STRIGGER, lmg ? lmgSetUp : g_gunHand >= 0 && scoped && Pressed(g_in.trigger[other], BT_STRIGGER));
        SetButton(BT_PTRIGGER, fire);
    }
}

// In VR the player aims with the gun's real sights, but the game only uses its aimed accuracy
// while it is in its aim-down-sights mode (whose animation takes the gun away from the hands):
// a shot "from the hip" spreads several degrees whatever the gun points at. Make the hip spread
// of every weapon the aimed one, in the game (which shoots) and in the cgame (prediction).
static void PatchSpread(HMODULE m, int ofsDefs, int ofsCount, const char *what)
{
    int count, i, n = 0, t = 0;
    BYTE **defs;
    __try {
        count = *(int *)((BYTE *)m + ofsCount);
        defs = *(BYTE ***)((BYTE *)m + ofsDefs);
        if (!defs || count <= 0 || count > 256) return;
        for (i = 1; i <= count; i++) {
            float *hip, ads;
            int k;
            if (!defs[i]) continue;
            hip = (float *)(defs[i] + WEAPDEF_OFS_hipSpreadStandMin);
            ads = *(float *)(defs[i] + WEAPDEF_OFS_adsSpread);
            if (cfg.hipAccuracy)
                for (k = 0; k < 4; k++) if (hip[k] != ads) { hip[k] = ads; n++; }
            if (cfg.realistic && cfg.drawTime > 0) {
                // the hand takes the weapon: no lowering / raising animation (M1A1 stock unfolding)
                int *times = (int *)(defs[i] + WEAPDEF_OFS_dropTime);
                for (k = 0; k < 4; k++) if (times[k] > cfg.drawTime) { times[k] = cfg.drawTime; t++; }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (n) Log("%s: hip fire spread set to the aimed spread (%d values)", what, n);
    if (t) Log("%s: weapon switch times set to %d ms (%d values)", what, cfg.drawTime, t);
}

static void UpdateHipAccuracy(void)
{
    static DWORD last;
    static HMODULE gChecked, cgChecked;
    static int gKnown, cgKnown;
    HMODULE g = GetModuleHandleA(GAME_DLL), cg = GetModuleHandleA(CGAME_DLL);
    if ((!cfg.hipAccuracy && !(cfg.realistic && cfg.drawTime > 0)) || GetTickCount() - last < 1000)
        return;   // weapons load as the level uses them
    last = GetTickCount();
    if (g && g != gChecked) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)g + ((IMAGE_DOS_HEADER *)g)->e_lfanew);
        gKnown = nt->FileHeader.TimeDateStamp == GAME_TIMESTAMP;
        gChecked = g;
    }
    if (cg && cg != cgChecked) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cg + ((IMAGE_DOS_HEADER *)cg)->e_lfanew);
        cgKnown = nt->FileHeader.TimeDateStamp == CGAME_TIMESTAMP;
        cgChecked = cg;
    }
    if (g && gKnown) PatchSpread(g, G_OFS_weaponDefs, G_OFS_numWeaponDefs, "game");
    if (cg && cgKnown) PatchSpread(cg, CG_OFS_weaponDefs, CG_OFS_numWeaponDefs, "cgame");
}

// Empty hands: a thin line from the hand to where the game's aim meets the map (the aim leaves
// from the eye through a point ConvergeDistance along the hand, so it is drawn to that spot).
static void DrawPointer(int h)
{
    static const float color[4] = { 1.0f, 1.0f, 1.0f, 0.45f };
    float body = BodyYaw(g_lastViewAxis[0]), dir[3], end[3], hit[3], pts[2][3], frac;
    int i;
    if (!MapLoaded() || !g_lastHudWorld) return;   // loading or no level: nothing to point at
    RotateZ(g_aimDir, body, dir);
    for (i = 0; i < 3; i++) end[i] = g_lastViewOrg[i] + dir[i] * 8000.0f;
    frac = WorldTrace(g_lastViewOrg, end);
    for (i = 0; i < 3; i++) hit[i] = g_lastViewOrg[i] + (end[i] - g_lastViewOrg[i]) * frac;
    memcpy(pts[0], g_in.hand[h].pos, sizeof(pts[0]));
    WorldToTracking(hit, body, pts[1]);
    XR_SetLine((const float (*)[3])pts, 2, color, 0);
}

// Optional crosshair (off by default: in VR you aim with the sights): the game's own is drawn at
// the centre of the head-locked HUD, not where the gun points, so this one is put in the world
// where the shot goes (the map along the aim, at most 30 m away), facing the eyes, the same size
// whatever the distance. Not with a scope or a grenade.
static void UpdateCrosshair(void)
{
    static const float color[4] = { 1.0f, 1.0f, 1.0f, 0.8f };
    float body, dir[3], end[3], hit[3], c[3], f[3], l[3], u[3], up[3] = { 0, 0, 1 }, d, frac, len;
    const BYTE *def;
    int i;
    int onGun = g_onTurret || (g_inVehicle && g_gunVehicle), rocket = 0;
    if (g_onTurret) return;   // the turret has its own sight: no reticle of ours (a vehicle's gun keeps it)
    def = onGun ? NULL : CurrentWeaponDef();
    if (def && g_gunValid) {
        // the Panzerfaust is aimed over its tube, hard to line up in VR: a sight even without the crosshair
        const char *wn = WeaponName(def);
        rocket = wn && (strstr(wn, "panzerfaust") || strstr(wn, "Panzerfaust"));
    }
    if (!onGun && !rocket && (!cfg.crosshair || !g_gunValid)) return;
    if (g_pointerOn || g_scopeActive || !MapLoaded() || !g_lastHudWorld) return;
    __try {
        if (def && *(const int *)(def + WEAPDEF_OFS_weaponType) == WEAPTYPE_GRENADE) return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    body = BodyYaw(g_lastViewAxis[0]);
    RotateZ(g_aimDir, body, dir);
    for (i = 0; i < 3; i++) end[i] = g_lastViewOrg[i] + dir[i] * 30.0f * cfg.worldScale;
    frac = WorldTrace(g_lastViewOrg, end);
    for (i = 0; i < 3; i++) hit[i] = g_lastViewOrg[i] + (end[i] - g_lastViewOrg[i]) * frac * 0.98f;
    WorldToTracking(hit, body, c);
    for (i = 0; i < 3; i++) f[i] = c[i] - g_head.pos[i];
    d = Normalize(f);
    if (d < 0.3f) return;
    Cross(up, f, l);
    if (Normalize(l) < 1e-3f) return;
    Cross(f, l, u);
    len = d * tanf(RAD(cfg.crosshairSize * 0.5f));
    for (i = 0; i < 3; i++) { l[i] *= -len; u[i] *= len; }   // right = -left
    XR_SetReticle((onGun || rocket) && !cfg.crosshair ? RETICLE_CROSS_DOT : cfg.crosshair, c, l, u, color);
}

// Melee by swinging the gun: any part of it (stock to muzzle) moving fast enough into someone
// starts the game's own melee (damage, sound, blood, the enemy's reaction), aimed at whoever was
// hit until the blow lands. Only real hand motion counts (tracking space: walking does not), and
// only the part of the motion going into the target, so resting the gun on someone does nothing.
#define SWING_POINTS 6
static float g_swingPrev[SWING_POINTS][3];
static int   g_swingPrevValid, g_meleeTarget = -1, g_meleeRelease;
static DWORD g_meleeUntil, g_meleeNext;

// Feet and head of entity i in tracking space (a standing soldier), 0 if it is not someone to hit.
static int MeleeCandidate(BYTE *gm, int i, float body, float feet[3], float top[3])
{
    BYTE *e = gm + G_OFS_entities + i * G_ENTITY_SIZE;
    float w[3];
    const float *org;
    if (!e[GENT_OFS_inuse] || *(int *)(e + GENT_OFS_health) <= 0) return 0;
    org = (const float *)(e + GENT_OFS_currentOrigin);
    WorldToTracking(org, body, feet);
    w[0] = org[0]; w[1] = org[1]; w[2] = org[2] + 64.0f;
    WorldToTracking(w, body, top);
    return 1;
}

static int SwingMelee(float dt, float *aimYaw, float *aimPitch)
{
    BYTE *gm;
    float body, pts[SWING_POINTS][3];
    DWORD now = GetTickCount();
    int i, k, n, hand;
    if (g_meleeRelease) { Cbuf("-melee\n"); g_meleeRelease = 0; }
    if (!cfg.swingMelee || !g_gunValid || g_pointerOn || !MapLoaded() || dt <= 0) { g_swingPrevValid = 0; return 0; }
    gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    if (!gm) return 0;
    body = BodyYaw(g_lastViewAxis[0]);
    hand = cfg.realistic ? g_gunHand : (cfg.rightHanded ? 1 : 0);

    // the blow on its way: keep aiming at the one who was hit until it lands
    if (g_meleeTarget > 0 && now < g_meleeUntil) {
        float feet[3], top[3], t[3];
        __try {
            if (MeleeCandidate(gm, g_meleeTarget, body, feet, top)) {
                for (k = 0; k < 3; k++) t[k] = feet[k] + (top[k] - feet[k]) * 0.75f - g_head.pos[k];
                *aimYaw = YawOf(t);
                *aimPitch = PitchOf(t);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return 1;
    }
    g_meleeTarget = -1;

    // the gun from the stock to the muzzle, now and in the last frame
    for (k = 0; k < SWING_POINTS; k++) {
        float s = -0.3f + 0.8f * k / (SWING_POINTS - 1);
        for (i = 0; i < 3; i++) pts[k][i] = g_gun.pos[i] + g_gun.axis[0][i] * s;
    }
    if (!g_swingPrevValid || now < g_meleeNext) {
        memcpy(g_swingPrev, pts, sizeof(pts));
        g_swingPrevValid = 1;
        return 0;
    }
    __try {
        n = *(int *)(gm + G_OFS_numEntities);
        if (n > 1024) n = 1024;
        for (i = 1; i < n; i++) {
            float feet[3], top[3], dx, dy;
            if (!MeleeCandidate(gm, i, body, feet, top)) continue;
            dx = feet[0] - g_head.pos[0]; dy = feet[1] - g_head.pos[1];
            if (dx * dx + dy * dy > 3.0f * 3.0f) continue;
            for (k = 0; k < SWING_POINTS; k++) {
                // inside the body (a 35 cm upright capsule), moving into it fast enough
                float *p = pts[k], v[3], h[2], d, into;
                if (p[2] < feet[2] + 0.1f || p[2] > top[2] + 0.15f) continue;
                h[0] = feet[0] + (top[0] - feet[0]) * 0.5f - p[0];
                h[1] = feet[1] + (top[1] - feet[1]) * 0.5f - p[1];
                d = sqrtf(h[0] * h[0] + h[1] * h[1]);
                if (d > 0.35f || d < 1e-3f) continue;
                v[0] = (p[0] - g_swingPrev[k][0]) / dt;
                v[1] = (p[1] - g_swingPrev[k][1]) / dt;
                v[2] = (p[2] - g_swingPrev[k][2]) / dt;
                into = (v[0] * h[0] + v[1] * h[1]) / d;
                if (sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) < cfg.meleeSpeed || into < cfg.meleeSpeed * 0.5f) continue;
                Log("swing melee: entity %d (type %d), %.1f m/s, %.1f into it, point %d",
                    i, *(int *)(gm + G_OFS_entities + i * G_ENTITY_SIZE + GENT_OFS_eType),
                    sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), into, k);
                g_meleeTarget = i;
                g_meleeUntil = now + 450;
                g_meleeNext = now + 700;
                g_meleeRelease = 1;
                Cbuf("+melee\n");
                if (hand >= 0) Buzz(hand, 1.0f, 70, cfg.hapShots);
                memcpy(g_swingPrev, pts, sizeof(pts));
                return 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    memcpy(g_swingPrev, pts, sizeof(pts));
    return 0;
}

// Builds a screen pose at pos facing away from the viewer along f, its up as close to vertical as f allows.
static void ScreenPose(vr_pose_t *p, const float pos[3], const float f[3])
{
    float up[3] = { 0, 0, 1 }, l[3], fw[3];
    memcpy(fw, f, sizeof(fw));
    if (Normalize(fw) < 1e-3f) { fw[0] = 1; fw[1] = fw[2] = 0; }
    Cross(up, fw, l);                    // left = up x forward
    if (Normalize(l) < 1e-3f) { l[0] = 0; l[1] = 1; l[2] = 0; }
    Cross(fw, l, up);                    // up = forward x left
    memcpy(p->pos, pos, sizeof(p->pos));
    memcpy(p->axis[0], fw, sizeof(fw));
    memcpy(p->axis[1], l, sizeof(l));
    memcpy(p->axis[2], up, sizeof(up));
}

// Menus and loading screens: placed level in front of the head when they come up, then fixed
// in the room, so you can look around them (and point at their corners).
static void PlaceMenuQuad(void)
{
    const vr_hud_t *hl = &cfg.hudLayer;
    float f[3] = { g_head.axis[0][0], g_head.axis[0][1], 0 }, pos[3];
    int i;
    if (Normalize(f) < 1e-3f) { f[0] = 1; f[1] = 0; }
    for (i = 0; i < 3; i++) pos[i] = g_head.pos[i] + f[i] * hl->distance;
    pos[2] += hl->offsetY;
    ScreenPose(&g_menuQuad, pos, f);
    g_menuRefZ = g_head.pos[2];
    g_menuQuadValid = 1;
}

// The objectives are held on the hand that does not hold the gun: the other one than the dominant
// hand chosen in the launcher.
static int ObjHand(void) { return cfg.rightHanded ? 0 : 1; }

// The back of that hand: the controller's palm faces the hand's thumb side inwards, so the back
// is the controller's left on the left hand (axis[1]) and its right on the right hand.
static void HandBack(int h, float out[3])
{
    int i;
    for (i = 0; i < 3; i++) out[i] = h == 0 ? g_in.hand[h].axis[1][i] : -g_in.hand[h].axis[1][i];
}

// The objectives screen lies on the back of the off hand like a watch: a few centimetres off the
// controller, a little towards the wrist, flat on the hand, its text upright for the eyes.
static void ObjectivesPose(vr_pose_t *p)
{
    const vr_pose_t *h = &g_in.hand[ObjHand()];
    float back[3], pos[3], fw[3], up[3], l[3], d;
    int i;
    HandBack(ObjHand(), back);
    for (i = 0; i < 3; i++) {
        pos[i] = h->pos[i] + back[i] * 0.04f - h->axis[0][i] * 0.06f;
        fw[i] = -back[i];   // away from the viewer: into the hand
    }
    if (Normalize(fw) < 1e-3f) { for (i = 0; i < 3; i++) fw[i] = pos[i] - g_head.pos[i]; ScreenPose(p, pos, fw); return; }
    // up: the eyes' up, laid onto the back of the hand
    d = g_head.axis[2][0] * fw[0] + g_head.axis[2][1] * fw[1] + g_head.axis[2][2] * fw[2];
    for (i = 0; i < 3; i++) up[i] = g_head.axis[2][i] - fw[i] * d;
    if (Normalize(up) < 1e-3f) memcpy(up, h->axis[0], sizeof(up));
    Cross(up, fw, l);
    Normalize(l);
    Cross(fw, l, up);
    memcpy(p->pos, pos, sizeof(p->pos));
    memcpy(p->axis[0], fw, sizeof(fw));
    memcpy(p->axis[1], l, sizeof(l));
    memcpy(p->axis[2], up, sizeof(up));
}

// ---------------------------------------------------------------------------
// Comfort vignette: seeing motion the body does not feel (walking with the stick, turning
// smoothly) is what makes VR sickness; narrowing the view while it happens removes the motion at
// the edges of the view, and it opens again when it stops. Walking for real (roomscale) and snap
// turns do not close it. The speed is the game's own (pushing against a wall moves nothing).
// ---------------------------------------------------------------------------
static float g_vigAmount;   // 0 = open .. 1 = as closed as VignetteStrength allows

static void UpdateVignette(float dt, float stick, float turnRate)
{
    static float prev[2], speedF;
    static int havePrev;
    float speed = 0, target = 0, tau;
    if (havePrev && dt > 0) {
        float dx = g_lastViewOrg[0] - prev[0], dy = g_lastViewOrg[1] - prev[1];
        speed = sqrtf(dx * dx + dy * dy) / dt;
        if (speed > 2000.0f) speed = 0;   // a jump in place: level start, load, teleport
    }
    prev[0] = g_lastViewOrg[0]; prev[1] = g_lastViewOrg[1];
    havePrev = 1;
    if (stick < 0.15f) speed = 0;   // roomscale: the body is really moving
    if (g_inVehicle && g_noRideAids) {
        target = 0;   // the boat of "ship": no vignette at all
        stick = 0;
    } else if (g_inVehicle && cfg.vehicleVignette > 0) {
        // carried by a vehicle: its speed and turning, the stick has nothing to do with it
        float moving = (g_vehicleSpeed - cfg.vignetteSpeed) / (cfg.vignetteSpeed * 3.0f);
        float turning = (g_vehicleTurnRate - 8.0f) / 30.0f;
        target = moving > turning ? moving : turning;
        if (target < 0) target = 0;
        if (target > 1) target = 1;
        g_vigStrength = cfg.vehicleVignette;
        stick = 0;
    } else {
        g_vigStrength = cfg.vignetteStrength;
    }
    // The game moves the player in steps, so the speed seen frame by frame jitters: smooth it
    // (and let the stick, which is steady, say how much it closes) or the edge would pulse.
    if (dt > 0) speedF += (speed - speedF) * (1.0f - expf(-dt / 0.3f));
    if (cfg.vignette && !g_inVehicle) {
        // moving: the smoothed speed is past the threshold (fully at twice it), as far as the
        // stick is pushed; turning: nothing below the threshold, fully closed at four times it
        float moving = (speedF - cfg.vignetteSpeed) / cfg.vignetteSpeed;
        float push = (stick - 0.15f) / 0.6f;
        float bySpeed, byTurn = (turnRate - cfg.vignetteTurn) / (cfg.vignetteTurn * 3.0f);
        if (moving < 0) moving = 0;
        if (moving > 1) moving = 1;
        if (push < 0) push = 0;
        if (push > 1) push = 1;
        bySpeed = moving * push;
        target = bySpeed > byTurn ? bySpeed : byTurn;
        if (target < 0) target = 0;
        if (target > 1) target = 1;
    }
    // closes quickly (protection), opens slowly (no jarring release)
    tau = target > g_vigAmount ? 0.06f : 0.22f;
    if (dt > 0) g_vigAmount += (target - g_vigAmount) * (1.0f - expf(-dt / tau));
    if (target == 0 && g_vigAmount < 0.002f) g_vigAmount = 0;
    {
        static int logged;
        if (!logged && g_vigAmount > 0.5f) { logged = 1; Log("comfort vignette active (speed %.0f, turn %.0f)", speed, turnRate); }
    }
}

// The objectives held up on the hand: a translucent black panel with rounded corners behind
// them (drawn "under" what the game drew: where the HUD is transparent the panel shows, the text
// on top is untouched), so they read well against the world.
static GLuint g_objFbo, g_objTex;
static int    g_objW, g_objH;
static HGLRC  g_objCtx;

// The game draws the objectives almost to the screen's edge: to leave a margin of panel around
// them, the HUD image is put back a little smaller (centred) before the panel goes under it.
static void InsetHud(GLuint fbo, int w, int h)
{
    int mx = (int)(w * cfg.objMargin), my = (int)(h * cfg.objMargin);
    GLint prevTex = 0;
    if (mx <= 0 && my <= 0) return;
    if (g_objFbo && (g_objW != w || g_objH != h || g_objCtx != gl.GetCurrentContext())) {
        if (g_objCtx == gl.GetCurrentContext()) { gl.DeleteFramebuffers(1, &g_objFbo); gl.DeleteTextures(1, &g_objTex); }
        g_objFbo = g_objTex = 0;
    }
    if (!g_objFbo) {
        gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        gl.GenTextures(1, &g_objTex);
        InitColorTexture(g_objTex, w, h);
        gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        gl.GenFramebuffers(1, &g_objFbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, g_objFbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_objTex, 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.DeleteFramebuffers(1, &g_objFbo); gl.DeleteTextures(1, &g_objTex);
            g_objFbo = g_objTex = 0;
            return;
        }
        g_objW = w; g_objH = h; g_objCtx = gl.GetCurrentContext();
    }
    GL_Blit(fbo, w, h, g_objFbo, w, h);   // a copy of the HUD as the game drew it
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.PushAttrib(GL_COLOR_BUFFER_BIT | GL_SCISSOR_BIT);
    gl.Disable(GL_SCISSOR_TEST);
    gl.ColorMask(1, 1, 1, 1);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.PopAttrib();
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, g_objFbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    gl.BlitFramebuffer(0, 0, w, h, mx, my, w - mx, h - my, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void DrawObjectivesPanel(GLuint fbo, int w, int h)
{
    float a = cfg.objBackground, proj[16] = { 0 }, x0, y0, x1, y1, r;
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    int i, c;
    if (!fbo || !gl.BlendFunc || w <= 0 || h <= 0) return;
    InsetHud(fbo, w, h);
    if (a <= 0.01f) return;
    if (a > 1) a = 1;
    proj[0] = 2.0f / w; proj[5] = 2.0f / h; proj[10] = 1; proj[12] = -1; proj[13] = -1; proj[15] = 1;
    // nearly the whole screen: the objectives text starts close to its left edge
    x0 = w * 0.005f; x1 = w * 0.995f; y0 = h * 0.01f; y1 = h * 0.99f; r = h * 0.045f;
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.PushAttrib(0x000FFFFF);   // GL_ALL_ATTRIB_BITS
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(0x0B71); gl.Disable(0x0BC0); gl.Disable(0x0B44);   // depth, alpha test, cull
    gl.Disable(0x0B60); gl.Disable(0x0B50); gl.Disable(0x0B90);   // fog, lighting, stencil
    if (gl.ActiveTexture) {
        for (i = 3; i >= 0; i--) { gl.ActiveTexture(0x84C0 + i); gl.Disable(GL_TEXTURE_2D); }
    } else {
        gl.Disable(GL_TEXTURE_2D);
    }
    gl.ColorMask(1, 1, 1, 1);
    gl.Enable(0x0BE2);                 // GL_BLEND
    gl.BlendFunc(0x0305, 1);           // ONE_MINUS_DST_ALPHA, ONE: "under" what is already there
    gl.Viewport(0, 0, w, h);
    gl.MatrixMode(0x1701); gl.PushMatrix(); gl.LoadMatrixf(proj);    // GL_PROJECTION, pixels
    gl.MatrixMode(0x1700); gl.PushMatrix(); gl.LoadMatrixf(ident);   // GL_MODELVIEW
    gl.Color4f(0, 0, 0, a);            // premultiplied black
    gl.Begin(0x0006);                  // GL_TRIANGLE_FAN
    gl.Vertex3f((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0);
    for (c = 0; c < 4; c++) {
        // corners counter-clockwise: bottom right, top right, top left, bottom left
        float cx = (c == 0 || c == 1) ? x1 - r : x0 + r, cy = (c == 0 || c == 3) ? y0 + r : y1 - r;
        for (i = 0; i <= 8; i++) {
            float t = (c * 90.0f - 90.0f + i * (90.0f / 8)) * 0.0174533f;
            gl.Vertex3f(cx + cosf(t) * r, cy + sinf(t) * r, 0);
        }
    }
    gl.Vertex3f(x1 - r, y0, 0);        // close the fan where it started
    gl.End();
    gl.MatrixMode(0x1700); gl.PopMatrix();
    gl.MatrixMode(0x1701); gl.PopMatrix();
    gl.PopAttrib();
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Draws the tunnel over both eye images: clear around the eye's own straight-ahead direction
// (the eyes' fields of view are asymmetric), a soft edge, black outside. Every GL state it
// touches is saved and restored.
static void DrawVignette(void)
{
    float a = g_vigAmount * g_vigStrength, fade = g_tpFade > 1.0f ? 1.0f : g_tpFade;
    int eye, i, k;
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    if ((a <= 0.002f && fade <= 0.002f) || !gl.BlendFunc || !fb_ok) return;
    for (eye = 0; eye < 2; eye++) {
        const vr_pose_t *e = &g_eyes[eye];
        float l = e->tanLeft, r = e->tanRight, u = e->tanUp, d = e->tanDown;
        float sx, sy, cx, cy, rad[3];
        if (r - l < 1e-3f || u - d < 1e-3f) continue;
        sx = 2.0f / (r - l); sy = 2.0f / (u - d);          // NDC per unit of tangent
        cx = -(r + l) / (r - l); cy = -(u + d) / (u - d);  // where straight ahead is in NDC
        // The headset shows about tan 1.0-1.2 to each side: the clear aperture goes from the edge of
        // the view (a = 0) to tan 0.3 (~17 deg, a = 1); Medium (0.6) clears ~34 deg.
        rad[0] = 1.25f + (0.30f - 1.25f) * a;              // clear aperture, in tangent units
        rad[1] = rad[0] * 1.35f + 0.08f;                   // end of the soft edge
        rad[2] = 10.0f;                                    // past every corner
        gl.BindFramebuffer(GL_FRAMEBUFFER, fb_fbo[eye]);
        gl.PushAttrib(0x000FFFFF);   // GL_ALL_ATTRIB_BITS
        gl.Disable(GL_SCISSOR_TEST);
        gl.Disable(0x0B71); gl.Disable(0x0BC0); gl.Disable(0x0B44);   // depth, alpha test, cull
        gl.Disable(0x0B60); gl.Disable(0x0B50); gl.Disable(0x0B90);   // fog, lighting, stencil
        if (gl.ActiveTexture) {
            for (i = 3; i >= 0; i--) { gl.ActiveTexture(0x84C0 + i); gl.Disable(GL_TEXTURE_2D); }
        } else {
            gl.Disable(GL_TEXTURE_2D);
        }
        gl.ColorMask(1, 1, 1, 1);
        gl.Enable(0x0BE2);                     // GL_BLEND
        gl.BlendFunc(0x0302, 0x0303);          // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
        gl.Viewport(0, 0, eye_w, eye_h);
        gl.MatrixMode(0x1701); gl.PushMatrix(); gl.LoadMatrixf(ident);   // GL_PROJECTION
        gl.MatrixMode(0x1700); gl.PushMatrix(); gl.LoadMatrixf(ident);   // GL_MODELVIEW
        for (k = 0; k < 2 && a > 0.002f; k++) {
            gl.Begin(0x0008);   // GL_QUAD_STRIP
            for (i = 0; i <= 48; i++) {
                float t = i * (6.2831853f / 48), c = cosf(t), s = sinf(t);
                gl.Color4f(0, 0, 0, k == 0 ? 0.0f : 1.0f);
                gl.Vertex3f(cx + c * rad[k] * sx, cy + s * rad[k] * sy, 0);
                gl.Color4f(0, 0, 0, 1.0f);
                gl.Vertex3f(cx + c * rad[k + 1] * sx, cy + s * rad[k + 1] * sy, 0);
            }
            gl.End();
        }
        if (fade > 0.002f) {
            // Blink: the whole eye goes black and comes back (the move's fade)
            gl.Begin(0x0007);   // GL_QUADS
            gl.Color4f(0, 0, 0, fade);
            gl.Vertex3f(-1, -1, 0); gl.Vertex3f(1, -1, 0); gl.Vertex3f(1, 1, 0); gl.Vertex3f(-1, 1, 0);
            gl.End();
        }
        gl.MatrixMode(0x1700); gl.PopMatrix();
        gl.MatrixMode(0x1701); gl.PopMatrix();
        gl.PopAttrib();
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// While a scoped rifle zooms in (and out) the game shows the zoomed view on the whole screen and
// draws its round scope overlay only once the zoom is complete: in the headset the scope screen
// was a black-edged rectangle until then. A round mask is drawn over the HUD buffer instead,
// growing with the zoom (the fov from where the scope view starts, ScopeMaxFov, to the weapon's
// adsZoomFov) up to the size of the overlay's circle (adsOverlayHeight, 400 of 480: the overlay
// then takes over without a jump).
static void DrawScopeMask(GLuint fbo, int w, int h)
{
    const BYTE *def = CurrentWeaponDef();
    float zoom = 0, ovH = 400.0f, p, rad[3], sx, sy;
    int i, k;
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    if (!fbo || !gl.BlendFunc || w <= 0 || h <= 0) return;
    __try {
        if (def) {
            zoom = *(const float *)(def + WEAPDEF_OFS_adsZoomFov);
            ovH = *(const float *)(def + WEAPDEF_OFS_adsOverlayHeight);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        zoom = 0;
    }
    if (ovH < 100.0f || ovH > 480.0f) ovH = 400.0f;
    p = zoom > 0 && zoom < cfg.scopeMaxFov ? (cfg.scopeMaxFov - g_scopeFov) / (cfg.scopeMaxFov - zoom) : 1.0f;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    // radius in pixels of the HUD buffer: a third of the overlay's circle at the start, all of it at the end
    rad[0] = (0.35f + 0.65f * p) * ovH * 0.5f / 480.0f * h;
    rad[1] = rad[0] * 1.03f + 1.0f;             // a soft edge of a few pixels
    rad[2] = (float)(w + h);                     // past every corner
    sx = 2.0f / w; sy = 2.0f / h;                // NDC per pixel
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.PushAttrib(0x000FFFFF);   // GL_ALL_ATTRIB_BITS
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(0x0B71); gl.Disable(0x0BC0); gl.Disable(0x0B44);   // depth, alpha test, cull
    gl.Disable(0x0B60); gl.Disable(0x0B50); gl.Disable(0x0B90);   // fog, lighting, stencil
    if (gl.ActiveTexture) {
        for (i = 3; i >= 0; i--) { gl.ActiveTexture(0x84C0 + i); gl.Disable(GL_TEXTURE_2D); }
    } else {
        gl.Disable(GL_TEXTURE_2D);
    }
    gl.ColorMask(1, 1, 1, 1);
    gl.Enable(0x0BE2);                     // GL_BLEND
    gl.BlendFunc(0x0302, 0x0303);          // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
    gl.Viewport(0, 0, w, h);
    gl.MatrixMode(0x1701); gl.PushMatrix(); gl.LoadMatrixf(ident);   // GL_PROJECTION
    gl.MatrixMode(0x1700); gl.PushMatrix(); gl.LoadMatrixf(ident);   // GL_MODELVIEW
    for (k = 0; k < 2; k++) {
        gl.Begin(0x0008);   // GL_QUAD_STRIP
        for (i = 0; i <= 64; i++) {
            float t = i * (6.2831853f / 64), c = cosf(t), s = sinf(t);
            gl.Color4f(0, 0, 0, k == 0 ? 0.0f : 1.0f);
            gl.Vertex3f(c * rad[k] * sx, s * rad[k] * sy, 0);
            gl.Color4f(0, 0, 0, 1.0f);
            gl.Vertex3f(c * rad[k + 1] * sx, s * rad[k + 1] * sy, 0);
        }
        gl.End();
    }
    gl.MatrixMode(0x1700); gl.PopMatrix();
    gl.MatrixMode(0x1701); gl.PopMatrix();
    gl.PopAttrib();
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Recoil while the gun is held with both hands: the kick ranges of every weapon (the gun model's
// and the view's) are scaled down, from their original values kept here, and put back when the
// support hand lets go. In the game (which kicks the aim) and the cgame (which draws it).
#define KICK_FIELDS 16
typedef struct { const BYTE *def; char name[32]; float orig[KICK_FIELDS]; } kick_save_t;
static kick_save_t g_kickSave[2][257];

static void ScaleKick(HMODULE m, int ofsDefs, int ofsCount, kick_save_t *save, float factor)
{
    int groups[4] = { WEAPDEF_OFS_adsGunKick, WEAPDEF_OFS_adsViewKick, WEAPDEF_OFS_hipGunKick, WEAPDEF_OFS_hipViewKick };
    __try {
        int count = *(int *)((BYTE *)m + ofsCount), i, g, k;
        BYTE **defs = *(BYTE ***)((BYTE *)m + ofsDefs);
        if (!defs || count <= 0 || count > 256) return;
        for (i = 1; i <= count; i++) {
            BYTE *d = defs[i];
            if (!d) continue;
            const char *nm = WeaponName(d);
            if (save[i].def != d || strncmp(save[i].name, nm ? nm : "", sizeof(save[i].name) - 1)) {
                // a new level loads new weapon defs: keep their own values
                save[i].def = d;
                strncpy(save[i].name, nm ? nm : "", sizeof(save[i].name) - 1);
                save[i].name[sizeof(save[i].name) - 1] = 0;
                for (g = 0; g < 4; g++) for (k = 0; k < 4; k++) save[i].orig[g * 4 + k] = ((float *)(d + groups[g]))[k];
            }
            for (g = 0; g < 4; g++) for (k = 0; k < 4; k++) ((float *)(d + groups[g]))[k] = save[i].orig[g * 4 + k] * factor;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void UpdateTwoHandKick(void)
{
    static float applied = 1.0f;
    static DWORD last;
    float factor = g_twoHand ? 1.0f - cfg.twoHandKick : 1.0f;
    HMODULE g, cg;
    // on a change, and every second (weapons load as the level uses them)
    if (factor == applied && GetTickCount() - last < 1000) return;
    if (factor == 1.0f && applied == 1.0f && cfg.twoHandKick <= 0) return;
    last = GetTickCount();
    applied = factor;
    g = GetModuleHandleA(GAME_DLL);
    cg = GetModuleHandleA(CGAME_DLL);
    if (g && PlayerClient()) ScaleKick(g, G_OFS_weaponDefs, G_OFS_numWeaponDefs, g_kickSave[0], factor);
    if (cg && CurrentWeaponDef()) ScaleKick(cg, CG_OFS_weaponDefs, CG_OFS_numWeaponDefs, g_kickSave[1], factor);
}

// A short press of "use" (getting on or off a turret).
static void UsePulse(void)
{
    Cbuf("+activate\n");
    Cbuf("-activate\n");
}

static int BothGrips(void) { return g_in.grip[0] > 0.6f && g_in.grip[1] > 0.6f; }
static int NoGrips(void) { return g_in.grip[0] < 0.3f && g_in.grip[1] < 0.3f; }

// The turret's pivot in tracking space (where the gun turns).
static int TurretPivot(int idx, float out[3])
{
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    if (!gm || idx <= 0) return 0;
    __try {
        const float *o = (const float *)(gm + G_OFS_entities + idx * G_ENTITY_SIZE + GENT_OFS_currentOrigin);
        WorldToTracking(o, BodyYaw(g_lastViewAxis[0]), out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 1;
}

// Once per frame: on a turret, next to a free one, or neither.
static int CfgInt(const char *sec, const char *key, int def, const char *ini);
static float IniFloat(const char *sec, const char *key, float def, const char *ini);

// [Turret] OffsetX / OffsetY / OffsetZ (cm) and NoCollision are read again every second, so they can be
// tuned while playing (settings.ini, else cod_vr.ini).
static void ReloadTurretTuning(void)
{
    static DWORD last;
    char ini[MAX_PATH];
    if (last && GetTickCount() - last < 1000) return;
    last = GetTickCount();
    _snprintf(ini, sizeof(ini), "%s\\cod_vr.ini", g_dir);
    cfg.turretOffset[0] = IniFloat("Turret", "OffsetX", 0.0f, ini);
    cfg.turretOffset[1] = IniFloat("Turret", "OffsetY", 0.0f, ini);
    cfg.turretOffset[2] = IniFloat("Turret", "OffsetZ", 0.0f, ini);
    cfg.turretNoCollision = CfgInt("Turret", "NoCollision", 1, ini);
    cfg.turretSeatLift = IniFloat("Turret", "CrouchedSeatLift", 0.0f, ini);
    cfg.turretSeat[0][0] = IniFloat("Turret", "DorsalX", 0.0f, ini);
    cfg.turretSeat[0][1] = IniFloat("Turret", "DorsalY", 0.0f, ini);
    cfg.turretSeat[0][2] = IniFloat("Turret", "DorsalZ", 0.0f, ini);
    cfg.turretSeat[1][0] = IniFloat("Turret", "TailX", 0.0f, ini);
    cfg.turretSeat[1][1] = IniFloat("Turret", "TailY", 0.0f, ini);
    cfg.turretSeat[1][2] = IniFloat("Turret", "TailZ", 0.0f, ini);
}

// The mounted gun's own collision pushed the player (in the game it is a box of its own, 0 to 56 units
// above its origin, which the player's box overlaps when standing right behind it). While mounted the
// box is made empty (r.contents 0) and put back when the player gets off. Only the gun is touched, not
// the player or the world, and only when the field holds the value the game's spawn code gives it.
#define TURRET_CONTENTS 0x200004
static void TurretCollision(int mounted, int ent)
{
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    if (!gm) return;
    __try {
        if (mounted && cfg.turretNoCollision) {
            int *c = (int *)(gm + G_OFS_entities + ent * G_ENTITY_SIZE + GENT_OFS_contents);
            if (!g_turretContentsZeroed) {
                if (*c != TURRET_CONTENTS) {
                    static int told;
                    if (!told) { told = 1; Log("turret: collision left as it is (contents %08x, expected %08x)", *c, TURRET_CONTENTS); }
                    return;
                }
                g_turretEnt = ent;
                g_turretContentsOrig = *c;
                g_turretContentsZeroed = 1;
                Log("turret: the gun's collision is off while mounted (entity %d)", ent);
            }
            *c = 0;   // again every frame: the game may set it back
        } else if (g_turretContentsZeroed) {
            BYTE *e = gm + G_OFS_entities + g_turretEnt * G_ENTITY_SIZE;
            int *c = (int *)(e + GENT_OFS_contents);
            if (e[GENT_OFS_inuse] && *(void **)(e + GENT_OFS_turretInfo) && *c == 0) *c = g_turretContentsOrig;
            g_turretContentsZeroed = 0;
            Log("turret: the gun's collision is back");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_turretContentsZeroed = 0;
    }
}

static void UpdateTurret(void)
{
    ReloadTurretTuning();
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    int on = 0, nearby = 0, n, i, scriptedMount;
    char map[64];
    if (gm && MapLoaded()) {
        __try {
            const float *me = (const float *)(gm + G_OFS_entities + GENT_OFS_currentOrigin);
            n = *(int *)(gm + G_OFS_numEntities);
            if (n > 1024) n = 1024;
            for (i = 1; i < n && !on; i++) {
                BYTE *e = gm + G_OFS_entities + i * G_ENTITY_SIZE;
                const float *o;
                float dx, dy, dz;
                if (!e[GENT_OFS_inuse] || !*(void **)(e + GENT_OFS_turretInfo)) continue;
                if (e[GENT_OFS_turretInUse]) {
                    if (*(int *)(e + GENT_OFS_ownerNum) == 0) on = i;
                    continue;
                }
                o = (const float *)(e + GENT_OFS_currentOrigin);
                dx = o[0] - me[0]; dy = o[1] - me[1]; dz = o[2] - me[2] - 40.0f;
                if (dx * dx + dy * dy + dz * dz < 100.0f * 100.0f) nearby = i;   // the game's own hint says when it can be used
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            on = nearby = 0;
        }
    }
    // UO's bomber puts the player on its turrets from the mission script (`turret useby(player)`, again
    // every time he stands in the mount trigger): that is the mission's own mount, not a stray press
    // of "use", so it is kept (the log of the bomber test showed it thrown off 244 times, with the
    // view then neither fixed to the gun nor firing it)
    scriptedMount = on && !g_onTurret && !g_turretViaGrips && g_uo && CurrentMap(map, sizeof(map)) && !_stricmp(map, "bomber");
    if (on && !g_onTurret && !g_turretViaGrips && !scriptedMount) {
        // the game's own "use" (one grip) put the player on it: turrets take both hands, off again
        // (once: the game needs a frame or two to let go)
        static DWORD kickedAt;
        if (GetTickCount() - kickedAt > 600) {
            UsePulse();
            kickedAt = GetTickCount();
            Log("turret: got on without both grips, off again");
        }
        on = 0;
    }
    g_turretViaGrips = 0;
    if (on && !g_onTurret) {
        g_turretBody = YawOf(g_lastViewAxis[0]) - g_headYawCmd;   // the body as it was, from now on fixed
        g_turretPivotOk = 0;   // once the eyes are placed (ApplyEyePose)
        g_turretRestPitch = 0;
        g_turretRestSet = 0;
        g_turretOrgSet = 0;
        g_turretOnAt = GetTickCount();
        __try {
            const BYTE *t = gm + G_OFS_entities + on * G_ENTITY_SIZE;
            memcpy(g_turretPivotWorld, t + GENT_OFS_currentOrigin, sizeof(g_turretPivotWorld));
            g_turretYaw = g_turretYawNow = ((const float *)(t + GENT_OFS_currentAngles))[1];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        g_turretScripted = scriptedMount;
        if (g_turretScripted) {
            g_turretGrab = 1;     // no exit by letting go of the grips until both were held once (UpdateTurret's bottom)
            GamePrintBold("Point the controller of your gun hand to aim.");
        }
        Log("turret: on (entity %d)%s", on, g_turretScripted ? ", put on by the mission script" : "");
        g_gunHand = -1;           // the gun in the hand goes away: drawn again from its holster later
        g_useHand = -1;
        g_turretReleasedAt = 0;
    } else if (!on && g_onTurret) {
        Log("turret: off");
        g_turretScripted = 0;
        g_realSync = 1;           // the grips as they are now, without actions
    }
    g_onTurret = on != 0;
    if (on) g_turretIdx = on;
    TurretCollision(on != 0, on);
    if (g_onTurret && g_turretScripted) {
        // The body is the game's view yaw minus our aim, as everywhere off a gun (BodyYaw). On the bomber the game turns
        // the view after the mount (the log: 25 -> -61 -> -67 a second or two later, then -67 -> -10 as the plane turns),
        // so a body taken once left the picture and the aim apart by that turn for the rest of the ride. A turn of the
        // view that is large is taken at once (the game putting the view on the gun); small ones slowly, so the gun
        // lagging behind the controller (turn speed) does not swing the picture.
        static DWORD last;
        DWORD now = GetTickCount();
        float target = YawOf(g_lastViewAxis[0]) - g_headYawCmd, diff = AngleNormalize180(target - g_turretBody);
        float dt = last ? (now - last) / 1000.0f : 0;
        last = now;
        if (dt > 0.25f) dt = 0.25f;
        g_turretBody = AngleNormalize180(g_turretBody + (fabsf(diff) > 20.0f ? diff : diff * (1.0f - expf(-dt / 0.7f))));
    }
    if (g_onTurret && g_turretScripted) {
        // The bomber's turrets ride on the plane, which flies: the pivot moves in the world, and the
        // eyes (placed once, see ApplyEyePose) move with it by the same step, so the player stays at
        // the same place behind the gun instead of being left behind (or, as the game does, swung
        // around the pivot as the gun turns). Only the translation: the world frame stays.
        __try {
            const float *o = (const float *)(gm + G_OFS_entities + on * G_ENTITY_SIZE + GENT_OFS_currentOrigin);
            g_turretYawNow = ((const float *)(gm + G_OFS_entities + on * G_ENTITY_SIZE + GENT_OFS_currentAngles))[1];
            float step[3] = { o[0] - g_turretPivotWorld[0], o[1] - g_turretPivotWorld[1], o[2] - g_turretPivotWorld[2] };
            if (step[0] * step[0] + step[1] * step[1] + step[2] * step[2] < 500.0f * 500.0f) {   // not a teleport
                if (g_turretOrgSet) {
                    static DWORD nextMove;
                    int k;
                    for (k = 0; k < 3; k++) g_turretOrgBase[k] += step[k];
                    if (GetTickCount() >= nextMove && (step[0] != 0 || step[1] != 0 || step[2] != 0)) {
                        nextMove = GetTickCount() + 2000;
                        Log("turret: the pivot moves with the plane (%.1f %.1f %.1f this frame)", step[0], step[1], step[2]);
                    }
                }
                memcpy(g_turretPivotWorld, o, sizeof(g_turretPivotWorld));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (g_onTurret) {
        // for finding what moves the player on a gun: the game's player origin, the camera and the gun's pitch
        static DWORD nextDiag;
        if (GetTickCount() >= nextDiag) {
            BYTE *pc = PlayerClient();
            nextDiag = GetTickCount() + 1000;
            __try {
                const BYTE *t = gm + G_OFS_entities + on * G_ENTITY_SIZE;
                Log("turret diag: player origin %.1f %.1f %.1f, camera z %.1f, gun pitch %.1f yaw %.1f, contents %08x",
                    pc ? *(float *)(pc + GCL_OFS_origin) : 0, pc ? *(float *)(pc + GCL_OFS_origin + 4) : 0,
                    pc ? *(float *)(pc + GCL_OFS_origin + 8) : 0, g_lastViewOrg[2],
                    ((const float *)(t + GENT_OFS_currentAngles))[0], ((const float *)(t + GENT_OFS_currentAngles))[1],
                    *(const int *)(t + GENT_OFS_contents));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    g_nearTurret = !on ? nearby : 0;
    g_turretInView = 0;
    if (g_nearTurret) {
        // the head looks at it (within 25 degrees): its use hint is the turret's, not a pickup's
        __try {
            const float *o = (const float *)(gm + G_OFS_entities + g_nearTurret * G_ENTITY_SIZE + GENT_OFS_currentOrigin);
            float w[3] = { o[0], o[1], o[2] + 8.0f }, t[3], d[3], len;
            WorldToTracking(w, BodyYaw(g_lastViewAxis[0]), t);
            for (i = 0; i < 3; i++) d[i] = t[i] - g_head.pos[i];
            len = Normalize(d);
            {
                // the game's own view (its "use" follows it): within 10 degrees of the gun
                float v[3];
                RotateZ(g_lastViewAxis[0], -BodyYaw(g_lastViewAxis[0]), v);   // into tracking space
                g_turretInView = len > 0.05f && d[0] * v[0] + d[1] * v[1] + d[2] * v[2] > 0.985f;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (!g_in.active) return;
    if (g_turretUseIn > 0 && --g_turretUseIn == 3) UsePulse();   // the view is on the turret (and stays a few frames)
    if (g_nearTurret) {
        // Getting on: both grips together. "Use" works on what the view looks at, and with the
        // hands on the handles they point along the barrel: the view turns to the turret for a
        // few frames (TurretAimOverride) and then uses it, from anywhere around it.
        if (BothGrips() && !g_turretGrab && g_turretInView) {
            // Its use hint showing, both grips: the player goes behind it and on it, through the
            // game's own start ("use" would take whatever the view hits first, like a pistol on the
            // window sill, and only from right behind the gun).
            BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL), *cl = PlayerClient();
            __try {
                const BYTE *t = gm + G_OFS_entities + g_nearTurret * G_ENTITY_SIZE;
                const float *o = (const float *)(t + GENT_OFS_currentOrigin);
                float yaw = RAD(((const float *)(t + GENT_OFS_currentAngles))[1]);
                float *me = (float *)(cl + GCL_OFS_origin), *vel = (float *)(cl + GCL_OFS_origin + 12);
                me[0] = o[0] - cosf(yaw) * 24.0f;
                me[1] = o[1] - sinf(yaw) * 24.0f;
                vel[0] = vel[1] = vel[2] = 0;
                g_lastGameOrgValid = 0;   // not a roomscale step
                {
                    BYTE *vm = GameVM(), *prev = MEM(BYTE *, ADDR_currentVM);
                    void (__cdecl *startUse)(BYTE *, BYTE *) = (void (__cdecl *)(BYTE *, BYTE *))(gm + G_OFS_TurretStartUse);
                    MEM(BYTE *, ADDR_currentVM) = vm;   // it may call the engine
                    g_turretViaGrips = 1;
                    startUse((BYTE *)t, gm + G_OFS_entities);
                    MEM(BYTE *, ADDR_currentVM) = prev;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("turret: could not get on");
            }
            g_turretTarget = 0;
            g_turretUseIn = 0;
            g_turretGrab = 1;
            Log("turret: grabbed with both hands");
        }
    } else if (g_onTurret) {
        // getting off: a hand lets go of its handle (for a moment, not a slip)
        if (!BothGrips()) {
            if (!g_turretReleasedAt) g_turretReleasedAt = GetTickCount();
            else if (GetTickCount() - g_turretReleasedAt > 300 && !g_turretGrab) { UsePulse(); g_turretGrab = 1; }
        } else {
            g_turretReleasedAt = 0;
        }
    }
    {
        // A turret the mission put him on (the bomber's) is left with a fresh press of a grip: he never grabbed it
        // with both hands, so the release above (which needs both held first) could never fire, and the only way off
        // was the game's own "use" button. Half a second after getting on, so the grips held at the mount do not count.
        static int wasDown[2];
        int h, edge = 0;
        for (h = 0; h < 2; h++) {
            int down = g_in.grip[h] > 0.6f;
            if (down && !wasDown[h]) edge = 1;
            wasDown[h] = down;
        }
        if (edge && g_onTurret && g_turretScripted && GetTickCount() - g_turretOnAt > 500) {
            UsePulse();
            g_turretReleasedAt = 0;
            Log("turret: grip pressed, getting off");
        }
    }
    if (NoGrips() && !g_onTurret) g_turretGrab = 0;
    if (BothGrips() && g_onTurret) g_turretGrab = 0;
}

// MODS (things the game itself does not offer): god mode is the game's own flag, set while it is
// chosen; infinite ammo fills the reserve (and the clip) of the weapons carried.
//
// Real life: every weapon's damage (bullets and melee, the player's and the soldiers') times 20,
// so one hit kills. The originals are kept per weapon definition (they are loaded again per map).
#define REAL_LIFE_DAMAGE 20
static void RealLifeDamage(BYTE *gm, int on)
{
    static const BYTE *seen[256];
    static int orig[256][2], applied[256];
    BYTE **defs = *(BYTE ***)(gm + G_OFS_weaponDefs);
    int n = *(int *)(gm + G_OFS_numWeaponDefs), w, f = on ? REAL_LIFE_DAMAGE : 1;
    if (!defs) return;
    if (n > 255) n = 255;
    for (w = 1; w <= n; w++) {
        BYTE *def = defs[w];
        if (!def) continue;
        if (seen[w] != def) {
            seen[w] = def;
            orig[w][0] = *(int *)(def + WEAPDEF_OFS_damage);
            orig[w][1] = *(int *)(def + WEAPDEF_OFS_meleeDamage);
            applied[w] = 1;
        }
        if (applied[w] != f) {
            *(int *)(def + WEAPDEF_OFS_damage) = orig[w][0] * f;
            *(int *)(def + WEAPDEF_OFS_meleeDamage) = orig[w][1] * f;
            applied[w] = f;
        }
    }
}

// Perma death: dying ends the run. The checkpoints (save\autosave, save\internal) are erased, the
// saves made by hand are moved to save\permadeath_backup\<date> (nothing of the player is lost), and
// the game goes back to the main menu: a new run starts from New Game.
static void MoveDir(const char *from, const char *to, int keep)
{
    char pattern[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    _snprintf(pattern, sizeof(pattern), "%s\\*", from);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        _snprintf(a, sizeof(a), "%s\\%s", from, fd.cFileName);
        if (keep) {
            _snprintf(b, sizeof(b), "%s\\%s", to, fd.cFileName);
            MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        } else {
            DeleteFileA(a);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void PermaDeath(void)
{
    char save[MAX_PATH], dir[MAX_PATH], backup[MAX_PATH];
    SYSTEMTIME t;
    GetLocalTime(&t);
    _snprintf(save, sizeof(save), "%s\\%s\\save", g_dir, g_uo ? "uo" : "main");   // United Offensive saves in uo\save
    _snprintf(dir, sizeof(dir), "%s\\autosave", save);
    MoveDir(dir, NULL, 0);
    _snprintf(dir, sizeof(dir), "%s\\internal", save);
    MoveDir(dir, NULL, 0);
    _snprintf(backup, sizeof(backup), "%s\\permadeath_backup", save);
    CreateDirectoryA(backup, NULL);
    _snprintf(backup, sizeof(backup), "%s\\permadeath_backup\\%04d-%02d-%02d_%02d-%02d-%02d", save,
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    CreateDirectoryA(backup, NULL);
    MoveDir(save, backup, 1);
    Log("perma death: checkpoints erased, saves moved to %s", backup);
}

static void UpdateMods(void)
{
    static int godSet, wasAlive, deathHandled;
    static DWORD deathTime;
    static int bloodSet;
    BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL), *cl = PlayerClient(), *player;
    if (!bloodSet) {   // after the game's config (which may have it off)
        Cbuf(cfg.gore ? "set cg_blood 1\n" : "set cg_blood 0\n");
        Cbuf("set cg_cursorHints 3\n");   // the use hints (a test build of the turrets turned them off)
        bloodSet = 1;
    }
    if (deathHandled && GetTickCount() - deathTime > 2500) {   // after the death has been seen
        Cbuf("disconnect\n");
        deathHandled = 0;
    }
    if (!gm || !cl) { godSet = wasAlive = 0; return; }
    player = gm + G_OFS_entities;
    __try {
        int health = *(int *)(player + GENT_OFS_health);
        RealLifeDamage(gm, cfg.difficulty == 1 || cfg.difficulty == 3);
        if (cfg.difficulty >= 2) {
            if (wasAlive && health <= 0 && !deathHandled) {
                PermaDeath();
                deathHandled = 1;
                deathTime = GetTickCount();
            }
            wasAlive = health > 0;
        }
        int *flags = (int *)(player + GENT_OFS_flags);
        if (cfg.difficulty == 4 && health > 0) {
            *flags |= 1;
            godSet = 1;
        } else if (godSet) {
            *flags &= ~1;
            godSet = 0;
        }
        if (cfg.infiniteAmmo) {
            BYTE **defs = *(BYTE ***)(gm + G_OFS_weaponDefs);
            int numDefs = *(int *)(gm + G_OFS_numWeaponDefs), s;
            for (s = 0; s <= 5 && defs; s++) {
                int w = s ? SlotWeapon(cl, s) : GameWeapon(cl);
                const BYTE *def;
                int *reserve, *clip, grenade;
                if (w <= 0 || w > numDefs || !(def = defs[w])) continue;
                reserve = (int *)(cl + GCL_OFS_ammo + *(const int *)(def + WEAPDEF_OFS_ammoIndex) * 4);
                clip = (int *)(cl + GCL_OFS_ammoclip + *(const int *)(def + WEAPDEF_OFS_clipIndex) * 4);
                grenade = *(const int *)(def + WEAPDEF_OFS_weaponType) == WEAPTYPE_GRENADE;
                if (*reserve < *(const int *)(def + WEAPDEF_OFS_maxAmmo)) *reserve = *(const int *)(def + WEAPDEF_OFS_maxAmmo);
                if ((cfg.infiniteAmmo == 2 || grenade) && *clip < *(const int *)(def + WEAPDEF_OFS_clipSize))
                    *clip = *(const int *)(def + WEAPDEF_OFS_clipSize);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Objectives by looking at the back of the off hand, as at a wristwatch: the hand close in front
// of the face (under 55 cm) with its back turned to the eyes for a quarter of a second shows them;
// turning it away hides them (HandBack). Not while that hand holds a weapon.
static int g_watchScores, g_watchSent;   // the gesture shows the objectives / it pressed +scores itself

static int WatchGesture(void)
{
    static DWORD since;
    static int on;
    int oh = ObjHand();
    const vr_pose_t *h = &g_in.hand[oh];
    float d[3], back[3], len, facing, ahead;
    int i, want;
    if (!cfg.objGesture || !g_in.handValid[oh] || g_gunHand == oh || g_onTurret) { since = 0; on = 0; return 0; }
    for (i = 0; i < 3; i++) d[i] = g_head.pos[i] - h->pos[i];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 0.05f || len > 0.55f) { since = 0; on = 0; return 0; }
    for (i = 0; i < 3; i++) d[i] /= len;
    HandBack(oh, back);
    facing = back[0] * d[0] + back[1] * d[1] + back[2] * d[2];         // back of the hand to the eyes
    ahead = -(g_head.axis[0][0] * d[0] + g_head.axis[0][1] * d[1] + g_head.axis[0][2] * d[2]);   // the hand in front of the face
    want = on ? facing > 0.55f && ahead > 0.2f : facing > 0.8f && ahead > 0.4f;
    if (!want) { since = 0; if (on) Log("objectives: the back of the hand turned away"); on = 0; return 0; }
    if (!on) {
        if (!since) since = GetTickCount();
        else if (GetTickCount() - since > 250) { on = 1; Log("objectives: shown by looking at the back of the hand"); }
    }
    return on;
}

// Reads the controllers once per frame (left eye): buttons, turning, movement and aim.
static void ProcessControllers(void)
{
    float *va = (float *)ADDR_cl_viewangles;
    int dom = cfg.rightHanded ? 1 : 0, off = 1 - dom;
    float headYaw = YawOf(g_head.axis[0]), headPitch = PitchOf(g_head.axis[0]);
    float aimYaw = headYaw, aimPitch = headPitch, dt;
    int fromHand = 0;
    LARGE_INTEGER now, freq;

    // Frame time from the high-resolution clock: GetTickCount moves in ~15.6 ms steps, so at
    // 72-90 Hz some frames would get 0 and others twice the real time.
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    dt = g_lastInputTick.QuadPart ? (float)((double)(now.QuadPart - g_lastInputTick.QuadPart) / (double)freq.QuadPart) : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    g_lastInputTick = now;
    UpdateMods();   // also with a menu open (the death screen)

    if (cfg.controllers) XR_GetInput(&g_in);
    UpdateTurret();
    UpdateVehicle(dt);
    if (g_in.active) { UpdateSkipMission(dt); UpdateGunVehicleGrip(); }
    else memset(&g_in, 0, sizeof(g_in));
    UpdateHipAccuracy();

    if (g_ignoreHeld && !AnyInputHeld()) g_ignoreHeld = 0;
    if (g_in.active && (MEM(int, ADDR_cls_keyCatchers) & 2)) {   // a menu is open
        ReleaseAllButtons();
        g_realSync = 1;
        g_moveForward = g_moveRight = 0;
        g_vigAmount = 0;
        MenuControls(dom);
        return;
    }
    LeaveMenuMode();

    if (!g_in.active) {
        ReleaseAllButtons();
        g_gunValid = g_twoHand = 0;
        g_moveForward = g_moveRight = 0;
        g_realSync = 1;
    } else {
        int ts = cfg.swapSticks ? off : dom, ms = cfg.swapSticks ? dom : off;   // turning and moving sticks
        if (cfg.moveStick >= 0) { ms = cfg.moveStick ? 1 : 0; ts = 1 - ms; }
        float sx = g_in.stick[ts][0], sy = g_in.stick[ts][1];
        float mx = g_in.stick[ms][0], my = g_in.stick[ms][1];
        float turnRate = 0;

        if (!g_btnDown[BT_PTRIGGER] && Pressed(g_in.trigger[dom], BT_PTRIGGER) && cfg.logNear) {
            g_nearLogFrames = 6;   // log a few frames after the shot
            Log("---- trigger pressed: entities near the view ----");
        } else if (g_nearLogFrames > 0) {
            g_nearLogFrames--;
        }
        if (!g_ignoreHeld) {
            if (g_onTurret || (g_inVehicle && g_gunVehicle)) {
                // on a turret or a vehicle's gun: either trigger fires it, the grips only hold it
                SetButton(BT_PTRIGGER, Pressed(g_in.trigger[0], BT_PTRIGGER) || Pressed(g_in.trigger[1], BT_PTRIGGER));
                UpdateHaptics();
                SetButton(BT_PGRIP, 0);
                SetButton(BT_STRIGGER, 0);
                SetButton(BT_SGRIP, 0);
            } else if (cfg.realistic) {
                RealisticHands();
            } else {
                SetButton(BT_PTRIGGER, Pressed(g_in.trigger[dom], BT_PTRIGGER) ||
                                       (g_slideShow && Pressed(g_in.trigger[off], BT_PTRIGGER)));
                SetButton(BT_PGRIP, Pressed(g_in.grip[dom], BT_PGRIP));
                SetButton(BT_STRIGGER, Pressed(g_in.trigger[off], BT_STRIGGER));
                SetButton(BT_SGRIP, Pressed(g_in.grip[off], BT_SGRIP));
            }
            {
                int face[4];
                face[FACE_A] = (g_in.buttons & VRB_A) != 0;
                face[FACE_B] = (g_in.buttons & VRB_B) != 0;
                face[FACE_X] = (g_in.buttons & VRB_X) != 0;
                face[FACE_Y] = (g_in.buttons & VRB_Y) != 0;
                UpdateFaceButtons(face);
            }
            SetButton(BT_LCLICK, (g_in.buttons & VRB_LSTICK) != 0);
            SetButton(BT_RCLICK, (g_in.buttons & VRB_RSTICK) != 0);
            SetButton(BT_MENU, (g_in.buttons & VRB_MENU) != 0);
        } else {
            g_realSync = 1;
        }
        g_mirrorGun = cfg.mirrorLeft && (cfg.realistic ? g_gunHand == 0 : !cfg.rightHanded);
        if (!cfg.realistic) {
            UpdateGunPose(dom, off, g_btnDown[BT_SGRIP]);
            if (g_in.handValid[dom]) AdjustGunPose(dt);
        } else if (g_gunHand >= 0) {
            int o = 1 - g_gunHand;
            UpdateGunPose(g_gunHand, o, g_gripDown[o] && g_useHand != o);
            if (g_in.handValid[g_gunHand]) AdjustGunPose(dt);
            if (!g_heldWeapon) g_gunValid = 0;   // still switching: the previous weapon is being lowered
        } else {
            g_gunValid = g_twoHand = 0;   // empty hands: no gun drawn, aim with the head
        }

        if (g_gunValid && MapLoaded()) {
            // Lying down the game's eyes are a hand's width from the ground, but the real hands are
            // well below the head: the gun went into the floor. It stays a little above it.
            BYTE *pc = PlayerClient();
            __try {
                const float *feet = pc ? (const float *)(pc + GCL_OFS_origin) : NULL;
                if (feet && g_lastViewOrg[2] - feet[2] < 30.0f) {   // prone
                    float ground[3] = { feet[0], feet[1], feet[2] }, t[3];
                    WorldToTracking(ground, BodyYaw(g_lastViewAxis[0]), t);
                    if (g_gun.pos[2] < t[2] + 0.12f) g_gun.pos[2] = t[2] + 0.12f;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        g_tankStick[0] = g_tankStick[1] = 0;
        g_tankDt = dt;
        if (g_tankMode) {
            // the right stick turns the tank's gun (UpdateViewAngles), not the body or the stance
            g_tankStick[0] = fabsf(sx) > 0.15f ? sx : 0;
            g_tankStick[1] = fabsf(sy) > 0.15f ? sy : 0;
            sx = sy = 0;
        }
        // The stick that does not move you: each direction runs the action chosen for it
        // (CONTROLS). Turn left / right turn the body (smooth or snap) with the stick's push; any
        // other action runs once per flick.
        {
            float tin = 0;
            tin += TurnOf(cfg.bind[BT_STICKRIGHT]) * (sx > 0 ? sx : 0);
            tin += TurnOf(cfg.bind[BT_STICKLEFT]) * (sx < 0 ? -sx : 0);
            tin += TurnOf(cfg.bind[BT_STICKUP]) * (sy > 0 ? sy : 0);
            tin += TurnOf(cfg.bind[BT_STICKDOWN]) * (sy < 0 ? -sy : 0);
            if (cfg.snapTurn > 0) {
                if (!g_turnLatched && fabsf(tin) > 0.7f) { va[YAW] -= (tin > 0 ? 1 : -1) * cfg.snapTurn; g_turnLatched = 1; }
                else if (fabsf(tin) < 0.3f) g_turnLatched = 0;
            } else if (fabsf(tin) > 0.15f) {
                va[YAW] -= tin * cfg.turnSpeed * dt;
                turnRate = fabsf(tin) * cfg.turnSpeed;
            }
        }
        if (!g_sideLatched && fabsf(sx) > 0.7f && fabsf(sx) >= fabsf(sy)) {
            int bt = sx > 0 ? BT_STICKRIGHT : BT_STICKLEFT;
            if (!TurnOf(cfg.bind[bt])) { SetButton(bt, 1); SetButton(bt, 0); }
            g_sideLatched = 1;
        } else if (fabsf(sx) < 0.3f) g_sideLatched = 0;
        if (!g_stanceLatched && fabsf(sy) > 0.7f && fabsf(sy) > fabsf(sx)) {
            if (g_inVehicle && (!g_gunVehicle || g_rideGun) && !g_noRideAids) {
                // in a vehicle: the vignette's strength, kept for next time
                char v[16];
                cfg.vehicleVignette += sy > 0 ? 0.15f : -0.15f;
                if (cfg.vehicleVignette < 0.0f) cfg.vehicleVignette = 0.0f;
                if (cfg.vehicleVignette > 0.9f) cfg.vehicleVignette = 0.9f;
                _snprintf(v, sizeof(v), "%.2f", cfg.vehicleVignette);
                if (g_userIni[0]) WritePrivateProfileStringA("Comfort", "VehicleVignette", v, g_userIni);
                g_vignetteShownAt = GetTickCount();
                Log("vehicle vignette %.2f", cfg.vehicleVignette);
            } else {
                int bt = sy > 0 ? BT_STICKUP : BT_STICKDOWN;
                if (!TurnOf(cfg.bind[bt])) { SetButton(bt, 1); SetButton(bt, 0); }
            }
            g_stanceLatched = 1;
        } else if (fabsf(sy) < 0.3f) g_stanceLatched = 0;
        if (g_inVehicle && (!g_gunVehicle || g_rideGun) && g_vignetteShownAt && GetTickCount() - g_vignetteShownAt < 1500) {
            // the new strength as a bar, like the skip one
            float f[3] = { g_head.axis[0][0], g_head.axis[0][1], 0 }, c[3], r[3], u[3] = { 0, 0, 0.025f };
            float len = sqrtf(f[0] * f[0] + f[1] * f[1]);
            int i;
            if (len > 1e-3f) {
                f[0] /= len; f[1] /= len;
                for (i = 0; i < 3; i++) c[i] = g_head.pos[i] + f[i] * 1.2f;
                c[2] -= 0.12f;
                r[0] = f[1] * 0.25f; r[1] = -f[0] * 0.25f; r[2] = 0;
                XR_SetProgress(c, r, u, cfg.vehicleVignette / 0.9f);
            }
        }

        // Aim: from the eye through a point ConvergeDistance metres along the gun, so the
        // shots (which leave from the eye) cross the barrel line there.
        g_pointerOn = 0;
        {
            const vr_pose_t *h = g_gunValid ? &g_gun : NULL;
            float t[3], len;
            int handDir = 0;
            float uc[3];
            if (g_gunHand >= 0 && g_useHand >= 0 && g_useHand != g_gunHand && g_useEnt && !g_onTurret && !g_inVehicle &&
                UseTargetCenter(g_useEnt, uc)) {
                // the free hand's grip is using something it reached (a bomb): the aim goes to its centre, from the eyes,
                // for as long as the grip is held, so the game uses it whatever the gun points at
                float c[3], dir[3];
                int k;
                WorldToTracking(uc, BodyYaw(g_lastViewAxis[0]), c);
                for (k = 0; k < 3; k++) dir[k] = c[k] - g_head.pos[k];
                if (Normalize(dir) > 0.01f) {
                    memcpy(g_aimDir, dir, sizeof(g_aimDir));
                    aimYaw = YawOf(dir);
                    aimPitch = PitchOf(dir);
                    fromHand = 1;
                }
                h = NULL;
            } else if (g_gunHand >= 0 && g_useHand >= 0 && g_useHand != g_gunHand && g_in.handValid[g_useHand] && !g_onTurret && !g_inVehicle) {
                // The empty hand's grip is "use" (planting explosives, doors, switches), and the game uses what the aim
                // points at: with a gun in the other hand that is the gun's line, so the soldier had to point his weapon
                // at the spot. While the off hand holds its grip the aim is that hand's own pointing instead, from the eyes
                // through where it points like the gun's; the gun goes on being drawn in its hand.
                h = &g_in.hand[g_useHand];
            }
            if (g_uo && g_inVehicle && g_gunVehicle && !g_tankMode) {
                // the jeep's machine gun follows the way the dominant controller points (the other one only when the
                // dominant is not tracked), whatever the head looks at; no grip needed
                int k = g_in.handValid[dom] ? dom : g_in.handValid[off] ? off : -1;
                if (k >= 0) { h = &g_in.hand[k]; handDir = 1; }
            }
            if (g_onTurret && g_turretScripted) {
                // a turret the mission put him on (the bomber's dorsal turret): like the jeep's gun it follows the way
                // the dominant controller points (the other one only when the dominant is not tracked), whatever the
                // head looks at. The direction is taken as it is: the game's arcs limit it, and no resting pitch applies
                int k = g_in.handValid[dom] ? dom : g_in.handValid[off] ? off : -1;
                if (k >= 0) { h = &g_in.hand[k]; handDir = 1; }
            }
            if (g_onTurret && g_turretOrgSet) {
                // the pivot in tracking space, seen from where the eyes are (that moves with the Turret offsets)
                float w[3] = { g_turretPivotWorld[0], g_turretPivotWorld[1], g_turretPivotWorld[2] };
                WorldToTracking(w, g_turretBody, g_turretPivot);
                g_turretPivotOk = 1;
            }
            if (g_onTurret && !g_turretScripted && g_turretPivotOk && g_in.handValid[0] && g_in.handValid[1]) {
                // The handles are behind the pivot: the barrel points from the hands through it
                // (hands to the left turn it right, hands down raise it), as a real mounted gun.
                float mid[3], d[3], dl;
                int i;
                for (i = 0; i < 3; i++) { mid[i] = (g_in.hand[0].pos[i] + g_in.hand[1].pos[i]) * 0.5f; d[i] = g_turretPivot[i] - mid[i]; }
                dl = sqrtf(d[0] * d[0] + d[1] * d[1]);
                if (dl > 0.08f) {
                    float pitch = PitchOf(d);
                    if (!g_turretRestSet) { g_turretRestPitch = pitch; g_turretRestSet = 1; }   // level as taken
                    aimYaw = YawOf(d);
                    aimPitch = pitch - g_turretRestPitch;
                    fromHand = 1;
                    {
                        // the barrel's direction, for the crosshair
                        float cp = cosf(RAD(aimPitch)), sp = sinf(RAD(aimPitch));
                        g_aimDir[0] = cosf(RAD(aimYaw)) * cp;
                        g_aimDir[1] = sinf(RAD(aimYaw)) * cp;
                        g_aimDir[2] = -sp;
                    }
                }
                {
                    static DWORD nextLog;
                    if (GetTickCount() >= nextLog) {
                        nextLog = GetTickCount() + 1000;
                        Log("turret aim: yaw %.1f pitch %.1f (hands %.2f m behind the pivot), body %.1f, eyes at %.0f %.0f %.0f, game view %.1f",
                            aimYaw, aimPitch, dl, g_turretBody, g_turretOrg[0], g_turretOrg[1], g_turretOrg[2], YawOf(g_lastViewAxis[0]));
                    }
                }
            }
            if (h) {
            int i;
            for (i = 0; i < 3; i++) t[i] = handDir ? h->axis[0][i] : h->pos[i] + h->axis[0][i] * cfg.converge - g_head.pos[i];
            len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            if (len > 0.01f) {
                for (i = 0; i < 3; i++) t[i] /= len;
                memcpy(g_aimDir, t, sizeof(g_aimDir));
                aimYaw = YawOf(t);
                aimPitch = PitchOf(t);
                fromHand = 1;
                if (g_onTurret && g_turretScripted) {
                    static DWORD nextLog;
                    if (GetTickCount() >= nextLog) {
                        nextLog = GetTickCount() + 1000;
                        Log("turret aim (controller): yaw %.1f pitch %.1f, head yaw %.1f, body %.1f, game view %.1f pitch %.1f",
                            aimYaw, aimPitch, YawOf(g_head.axis[0]), g_turretBody, YawOf(g_lastViewAxis[0]), PitchOf(g_lastViewAxis[0]));
                    }
                }
            }
            }
        }

        if (!g_onTurret && !g_inVehicle) {
            BYTE *pc = PlayerClient();
            int onLadder = 0, ahead = 0;
            static int wasLadder;
            static DWORD ladderUntil;
            __try {
                onLadder = pc && (*(int *)(pc + GCL_OFS_pmFlags) & PMF_LADDER);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            if (onLadder) ladderUntil = GetTickCount() + 800;
            if (!onLadder && my > 0.3f) ahead = LadderAhead(BodyYaw(g_lastViewAxis[0]) + headYaw);
            // At the top the ladder lets go for a moment before the player is off it, and the gun
            // comes back to the hand: its aim (often down, the hand hangs) and "forward" put the
            // player back on the ladder going down, again and again (UO's Foy, the church loft).
            // Still pushing forward: keep the ladder's aim a little longer, to step off at the top.
            if (!onLadder && my > 0.3f && GetTickCount() < ladderUntil) ahead = 1;
            g_ladderMove = onLadder || ahead;
            if (onLadder || ahead) {
                aimYaw = headYaw;
                aimPitch = -50.0f;   // up: forward climbs, back goes down
                fromHand = 1;
            }
            if (onLadder != wasLadder) { Log("ladder: %s", onLadder ? "on" : "off"); wasLadder = onLadder; }
        }
        // Move stick, relative to where the head looks. The game moves relative to its view
        // (the aim), so rotate the stick by the angle between head and aim. Its speed comes from
        // the larger of the two axes and it is slower sideways (0.8) and backwards (0.7) of the
        // view: aiming off to one side would slow a walk straight ahead. So its own scales are 1
        // and the stick's tilt sets the speed, slower sideways and backwards of the head instead.
        {
            // Blink / Shift (MoveMode): only on foot, free to walk, not on a ladder, in a scene, a vehicle or on a gun
            int tpOn = cfg.moveMode != MOVE_CONTINUOUS && !g_tankMode && !g_inVehicle && !g_onTurret && !g_linkedScene &&
                       !g_viewLocked && !g_slideShow && !g_ladderMove && g_lastHudWorld;
            // the arc is aimed by the hand whose stick moves (the dominant one stays free to shoot), the other as a fallback
            UpdateTeleport(tpOn, dt, mx, my, ms, 1 - ms);
            if (g_tpState != TP_IDLE) tpOn = 1;   // a jump under way finishes whatever the stick does
        if (g_tankMode) {
            // a tank drives as it is told: forward and back, turn left and right
            // A tank is heavy: it speeds up, slows down and swings round slowly, and not as fast
            // as the stick asks at once.
            // A tank is heavy and driven gently: a low share of the stick's tilt, taken up slowly,
            // and let go of at once (it would keep turning on its own); turning has a wide dead zone.
            static float drive, turn;
            float wantDrive = fabsf(my) > 0.2f ? my * 0.12f : 0;
            float wantTurn = fabsf(mx) > 0.4f ? (mx > 0 ? 1.0f : -1.0f) * (fabsf(mx) - 0.4f) / 0.6f * 0.06f : 0;
            if (dt > 0) {
                drive = wantDrive == 0 ? 0 : drive + (wantDrive - drive) * (1.0f - expf(-dt / 0.4f));
                turn = wantTurn == 0 ? 0 : turn + (wantTurn - turn) * (1.0f - expf(-dt / 0.3f));
            }
            g_moveForward = fabsf(drive) > 0.02f ? ClampMove(drive) : 0;
            g_moveRight = fabsf(turn) > 0.02f ? ClampMove(turn) : 0;
            {
                static DWORD nextLog;
                if ((g_moveForward || g_moveRight) && GetTickCount() >= nextLog) {
                    nextLog = GetTickCount() + 1000;
                    Log("tank drive: move %d %d, speed %.0f", g_moveForward, g_moveRight, g_vehicleSpeed);
                }
            }
        } else if (tpOn) {
            g_moveForward = g_moveRight = 0;   // Blink and Shift: the stick aims a jump (UpdateTeleport), it does not walk
        } else if (mx * mx + my * my > 0.15f * 0.15f) {
            float d = RAD(AngleNormalize180(headYaw - aimYaw));
            float f = my, l = -mx, len = sqrtf(f * f + l * l);
            float tilt = ((len > 1 ? 1 : len) - 0.15f) / 0.85f;
            float fa = fabsf(f) / len, la = fabsf(l) / len;
            float slow = (fa * (f < 0 ? 0.7f : 1.0f) > la * 0.8f ? fa * (f < 0 ? 0.7f : 1.0f) : la * 0.8f) / (fa > la ? fa : la);
            float f2 = f * cosf(d) - l * sinf(d), l2 = f * sinf(d) + l * cosf(d);
            float big = fabsf(f2) > fabsf(l2) ? fabsf(f2) : fabsf(l2);
            g_moveForward = ClampMove(f2 / big * tilt * slow);
            g_moveRight = ClampMove(-l2 / big * tilt * slow);
        } else {
            g_moveForward = g_moveRight = 0;
        }
        }
        {
            BYTE *pc = PlayerClient();
            __try {
                if (pc) *(float *)(pc + GCL_OFS_strafeSpeedScale) = *(float *)(pc + GCL_OFS_backSpeedScale) = 1.0f;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        if (fromHand && !g_crosshairOff) { Cbuf("cg_drawCrosshair 0\n"); g_crosshairOff = 1; }
        UpdateAutoAds();
        UpdateVignette(dt, sqrtf(mx * mx + my * my), turnRate);
        UpdatePhysicalCrouch(dt);
        UpdateTwoHandKick();
        if (fromHand && !g_pointerOn) UpdateGrenadeArc();
        if (g_inVehicle && g_gunVehicle && g_tankMode)   // a tank: its gun, the game's view
            RotateZ(g_lastViewAxis[0], -BodyYaw(g_lastViewAxis[0]), g_aimDir);
        if (g_inVehicle && g_gunVehicle && g_tankMode)   // a tank: its gun, the game's view
            RotateZ(g_lastViewAxis[0], -BodyYaw(g_lastViewAxis[0]), g_aimDir);
        else if (g_inVehicle && g_gunVehicle && !fromHand) memcpy(g_aimDir, g_head.axis[0], sizeof(g_aimDir));   // aimed with the head
        if (g_pointerOn) DrawPointer(dom);
        else if (fromHand || (g_inVehicle && g_gunVehicle)) UpdateCrosshair();
        if (fromHand) SwingMelee(dt, &aimYaw, &aimPitch);
        if (g_turretUseIn > 0 && g_turretTarget > 0) {
            BYTE *gm = (BYTE *)GetModuleHandleA(GAME_DLL);
            __try {
                const float *o = (const float *)(gm + G_OFS_entities + g_turretTarget * G_ENTITY_SIZE + GENT_OFS_currentOrigin);
                float w[3] = { o[0], o[1], o[2] + 8.0f }, t[3], d[3];
                int i;
                WorldToTracking(w, BodyYaw(g_lastViewAxis[0]), t);
                for (i = 0; i < 3; i++) d[i] = t[i] - g_head.pos[i];
                aimYaw = YawOf(d);
                aimPitch = PitchOf(d);
                fromHand = 1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        {
            // objectives held up: the HUD screen goes above the left hand
            int b, held = 0, look = WatchGesture();
            for (b = 0; b < BT_COUNT; b++) if (g_btnDown[b] && !strcmp(cfg.bind[b], "+scores")) held = 1;
            if (ShortcutHolds("+scores")) held = 1;
            if (look && !g_watchScores) {   // the game shows its objectives screen while +scores is held
                if (!held) { Cbuf("+scores\n"); g_watchSent = 1; }
                g_watchScores = 1;
            } else if (!look && g_watchScores) {
                if (g_watchSent) Cbuf("-scores\n");   // only what the gesture pressed (not the button's)
                g_watchSent = g_watchScores = 0;
            }
            held |= look;
            g_objOnHand = cfg.objOnHand && held && g_in.handValid[ObjHand()];
            UpdateHudMode(held);
        }
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

// Some weapons (the Thompson) have their muzzle flash as world effect sprites (reType 4 / 13,
// no RF_DEPTHHACK) that the game puts on the aim line 25-60 units ahead of the eye, far in
// front of the gun in the hand. While firing, such sprites close to that line are taken to the
// hand like the view weapon's own flashes.
static int IsWorldMuzzleFlash(const BYTE *ent)
{
    static DWORD lastFiring;
    const float *o = (const float *)(ent + REFENT_OFS_origin);
    float d[3], l[3];
    int i, k, type = *(const int *)ent;
    if (g_btnDown[BT_PTRIGGER]) lastFiring = GetTickCount();
    else if (GetTickCount() - lastFiring > 200) return 0;
    if ((type != 4 && type != 13) || *(const int *)(ent + REFENT_OFS_renderfx) != 0) return 0;
    for (i = 0; i < 3; i++) d[i] = o[i] - g_rawViewOrg[i];
    for (k = 0; k < 3; k++) l[k] = d[0] * g_lastViewAxis[k][0] + d[1] * g_lastViewAxis[k][1] + d[2] * g_lastViewAxis[k][2];
    return l[0] > 8.0f && l[0] < 75.0f && l[1] * l[1] + l[2] * l[2] < 7.0f * 7.0f;
}


static void __cdecl hk_AddRefEntity(void *ent, int param)
{
    int worldFlash = 0;
    int hold = g_vrFrame && g_eye == 0 && cfg.weaponInHand && g_gunValid && g_heldCount < MAX_HELD &&
               ((*(int *)((BYTE *)ent + REFENT_OFS_renderfx) & RF_DEPTHHACK) ||
                (worldFlash = IsWorldMuzzleFlash(ent)) != 0);
    if (cfg.logNear && g_nearLogFrames > 0 && g_nearLogLines < 400 && g_eye == 0) LogNearEntity(ent, param, hold);
    if (g_vrFrame && g_eye == 0 && cfg.weaponInHand && !g_gunValid &&
        (*(int *)((BYTE *)ent + REFENT_OFS_renderfx) & RF_DEPTHHACK))
        return;   // gun hand not tracked: hide the view weapon rather than draw it at the face
    if (cfg.logNear) g_statTypes[*(int *)ent & 15]++;
    if (hold) {
        if (*(int *)ent != REFENT_TYPE_MODEL) g_gunFlashed = 1;   // flash sprites of the shot
        memcpy(g_held[g_heldCount], ent, REFENT_SIZE);
        g_heldWorldFlash[g_heldCount] = worldFlash;
        g_heldParam[g_heldCount++] = param;
        return;
    }
    orig_AddRefEntity(ent, param);
}

// Per-weapon muzzle flash correction: [MuzzleFlash] lines "name=forward right up" (game units),
// where name is part of the weapon's internal name (the log says which name is in the hand).
// The section is read again every second, so it can be tuned while playing.
static float g_flashAdj[3];

// A printable name of 2..63 characters at p, or 0.
static int IsName(const char *p)
{
    int i;
    for (i = 0; i < 63 && p[i] >= 32 && p[i] < 127; i++) ;
    return i >= 2 && p[i] == 0;
}

// The weapon's name: a string pointer in the first fields (internal name first), or the
// string stored in the def itself.
static const char *WeaponName(const BYTE *def)
{
    int ofs[] = { WEAPDEF_OFS_internalName, 4, WEAPDEF_OFS_displayName, 12 };
    const char *s = NULL;
    int k;
    for (k = 0; k < 4 && !s; k++) {
        __try {
            const char *q = *(const char * const *)(def + ofs[k]);
            if ((DWORD)q > 0x10000 && IsName(q)) s = q;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s = NULL;
        }
    }
    if (!s) {
        __try {
            if (IsName((const char *)def)) s = (const char *)def;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s = NULL;
        }
    }
    return s;
}

static void UpdateFlashAdjust(void)
{
    static const BYTE *lastDef;
    static DWORD lastRead;
    static char sect[4096];
    const BYTE *def = CurrentWeaponDef();
    const char *wn, *e;
    char name[64], ini[MAX_PATH];
    float a[3] = { 0, 0, 0 }, def3[3] = { 0, 0, 0 };
    int found = 0;

    if (def == lastDef && GetTickCount() - lastRead < 1000) return;
    lastRead = GetTickCount();
    wn = def ? WeaponName(def) : NULL;
    if (wn) {
        strncpy(name, wn, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        _strlwr(name);
        _snprintf(ini, sizeof(ini), "%s\\cod_vr.ini", g_dir);
        if (!GetPrivateProfileSectionA("MuzzleFlash", sect, sizeof(sect), ini)) sect[0] = sect[1] = 0;
        for (e = sect; *e; e += strlen(e) + 1) {
            char key[64];
            const char *eq = strchr(e, '=');
            int len;
            if (!eq || *e == ';') continue;
            len = (int)(eq - e);
            while (len > 0 && e[len - 1] == ' ') len--;
            if (len <= 0 || len >= (int)sizeof(key)) continue;
            memcpy(key, e, len); key[len] = 0;
            _strlwr(key);
            if (!strcmp(key, "default")) { sscanf(eq + 1, "%f %f %f", &def3[0], &def3[1], &def3[2]); continue; }
            if (strstr(name, key)) { sscanf(eq + 1, "%f %f %f", &a[0], &a[1], &a[2]); found = 1; break; }
        }
        if (!found) memcpy(a, def3, sizeof(a));   // Default= for every weapon without its own line
    }
    if (def != lastDef) {
        if (wn) {
            Log("weapon %s: muzzle flash correction %.1f %.1f %.1f", wn, a[0], a[1], a[2]);
        } else if (def) {
            const DWORD *w = (const DWORD *)def;
            __try {
                Log("weapon %d at %p has no readable name: %08x %08x %08x %08x %08x %08x",
                    MEM(BYTE, ADDR_cl_cgameUserCmdValue), def, w[0], w[1], w[2], w[3], w[4], w[5]);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("weapon %d at %p unreadable", MEM(BYTE, ADDR_cl_cgameUserCmdValue), def);
            }
        } else {
            Log("weapon %d: no weapon def", MEM(BYTE, ADDR_cl_cgameUserCmdValue));
        }
    }
    memcpy(g_flashAdj, a, sizeof(a));
    lastDef = def;
}

// Re-expresses a point given in the game's view frame (gameOrg, gameAxis) in the hand frame.
// kind: 0 = direction, 1 = point of the view weapon model, 2 = point of a muzzle effect,
// 3 = point of a world muzzle flash sprite (spread 27..60 units along the aim line: compressed).
#define WORLD_FLASH_START 26.0f   // where the Thompson's world flash sprites begin, ahead of the eye

static void ViewToHand(float *p, const cod_refdef_t *fd, const float handOrg[3], float handAxis[3][3], int kind)
{
    float local[3], d[3];
    int i, k;
    int isPoint = kind != 0;
    // from the game's own view origin: the entities were placed from it, and fd->vieworg has the camera shake
    // filter on it (FilterShake), whose lag while walking moved the gun in the hand like an elastic
    for (i = 0; i < 3; i++) d[i] = p[i] - (isPoint ? g_rawViewOrg[i] : 0.0f);
    for (k = 0; k < 3; k++) local[k] = d[0] * fd->viewaxis[k][0] + d[1] * fd->viewaxis[k][1] + d[2] * fd->viewaxis[k][2];
    if (isPoint) {
        // the gun sits at weaponOffset from the eye in the game; put that spot in the hand
        local[0] -= cfg.weaponOffset[0];
        local[1] += cfg.weaponOffset[1];   // right -> -left
        local[2] -= cfg.weaponOffset[2];
    }
    if (kind == 3) {
        // local[0] already has WeaponOffsetForward taken off
        local[0] = WORLD_FLASH_START - cfg.weaponOffset[0] +
                   (local[0] + cfg.weaponOffset[0] - WORLD_FLASH_START) * cfg.worldFlashLength;
        kind = 2;
    }
    if (kind == 2) {
        // The flat game draws muzzle flashes near the screen centre line, not at the hip gun's
        // muzzle: keep their distance along the barrel and put them on the barrel axis, plus
        // the current weapon's correction from [MuzzleFlash].
        local[0] += g_flashAdj[0];
        local[1] = -(cfg.muzzleOffset[0] + g_flashAdj[1]);
        local[2] = cfg.muzzleOffset[1] + g_flashAdj[2];
    }
    if (g_mirrorGun) local[1] = -local[1];   // left hand: the right-handed model mirrored (see hook_glDepthRange)
    for (i = 0; i < 3; i++)
        p[i] = (isPoint ? handOrg[i] : 0.0f) + local[0] * handAxis[0][i] + local[1] * handAxis[1][i] + local[2] * handAxis[2][i];
}

// Moves the held view-weapon entities from the game's view to the dominant hand and gives
// them to the renderer. fd is the game's own view for this scene (before the eye pose).
static void FlushHeldEntities(const cod_refdef_t *fd)
{
    int n, i, k;
    const vr_pose_t *h = &g_gun;
    float body = BodyYaw(fd->viewaxis[0]);
    float rel[3], handOrg[3], handAxis[3][3];

    if (!g_heldCount) return;
    UpdateFlashAdjust();
    BodyOffset(h->pos, rel);
    for (i = 0; i < 3; i++) rel[i] *= cfg.worldScale;
    RotateZ(rel, body, handOrg);
    for (i = 0; i < 3; i++) handOrg[i] += fd->vieworg[i];
    for (k = 0; k < 3; k++) RotateZ(h->axis[k], body, handAxis[k]);

    for (n = 0; n < g_heldCount; n++) {
        BYTE *e = g_held[n];
        float *axis = (float *)(e + REFENT_OFS_axis);
        int kind = *(int *)e == REFENT_TYPE_MODEL ? 1 : g_heldWorldFlash[n] ? 3 : 2;   // models vs sprites/beams (flash)
        ViewToHand((float *)(e + REFENT_OFS_origin), fd, handOrg, handAxis, kind);
        ViewToHand((float *)(e + REFENT_OFS_oldorigin), fd, handOrg, handAxis, kind);
        if (*(int *)(e + REFENT_OFS_renderfx) & RF_LIGHTING_ORIGIN)
            ViewToHand((float *)(e + REFENT_OFS_lightingOrigin), fd, handOrg, handAxis, 1);
        for (k = 0; k < 3; k++) ViewToHand(axis + 3 * k, fd, handOrg, handAxis, 0);
        orig_AddRefEntity(e, g_heldParam[n]);
    }
    g_heldCount = 0;
}

#ifdef COD_VR_TEST
// Development only (built with /DCOD_VR_TEST): when cod_vr_test.txt is in the game folder the mod plays a
// short script in the first level by itself (drop the weapon, look at it, press use) and writes what the
// game does to cod_vr.log, so a change can be tried without a headset.
static void TestDriver(void)
{
    static int enabled = -1, stage;
    static DWORD t0, tNext;
    static float va0[2];
    char path[MAX_PATH];
    BYTE *cl, *gm;
    DWORD now = GetTickCount();
    if (enabled < 0) {
        _snprintf(path, sizeof(path), "%s\\cod_vr_test.txt", g_dir);
        enabled = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!enabled || !MapLoaded()) return;
    cl = PlayerClient();
    gm = (BYTE *)GetModuleHandleA(GAME_DLL);
    if (!cl || !gm) return;
    if (stage == 0) {   // wait for the level to be playable
        if (!t0) t0 = now;
        if (now - t0 < 14000) return;
        stage = 1;
    }
    {
        float *org = (float *)(cl + GCL_OFS_origin), *va = (float *)ADDR_cl_viewangles;
        float eye[3] = { org[0], org[1], org[2] + 56.0f };
        static DWORD lastStat;
        if (now - lastStat > 700 && stage >= 2) {
            lastStat = now;
            Log("TEST use entity %d, looked-at entity %d (%d = none), view %.1f %.1f", *(int *)(cl + (g_uo ? 0x8324 : 0x74dc)),
                *(unsigned short *)(cl + (g_uo ? 0x5ec : 0x3ac)), 0x7ff, *(float *)(cl + GCL_OFS_viewangles + 4), *(float *)(cl + GCL_OFS_viewangles));
            Log("TEST stage %d: weapon in use %d, slots %d %d %d %d %d, pos %.0f %.0f %.0f, item %p", stage, GameWeapon(cl),
                SlotWeapon(cl, 1), SlotWeapon(cl, 2), SlotWeapon(cl, 3), SlotWeapon(cl, 4), SlotWeapon(cl, 5), org[0], org[1], org[2], g_testItem);
            if (g_testItem) {
                float *io = (float *)(g_testItem + GENT_OFS_currentOrigin), *tb = (float *)(g_testItem + GENT_OFS_posBase);
                Log("TEST item: inuse %d eType %d origin %.0f %.0f %.0f trBase %.0f %.0f %.0f flags %08x", g_testItem[GENT_OFS_inuse],
                    *(int *)(g_testItem + GENT_OFS_eType), io[0], io[1], io[2], tb[0], tb[1], tb[2], *(int *)(g_testItem + GENT_OFS_flags));
            }
        }
        if (now < tNext) goto keep;
        switch (stage) {
        case 1:
            Log("TEST start: weapon %d, slots %d %d %d %d, player %.0f %.0f %.0f", GameWeapon(cl), SlotWeapon(cl, 1), SlotWeapon(cl, 2),
                SlotWeapon(cl, 3), SlotWeapon(cl, 4), org[0], org[1], org[2]);
            Cbuf("weaponslot primary\n");
            stage = 2; tNext = now + 2500;
            break;
        case 2:
            // the drop, from where the eyes are (no controllers here)
            memcpy(g_lastViewOrg, eye, sizeof(eye));
            g_lastViewAxis[0][0] = 1; g_lastViewAxis[0][1] = 0; g_lastViewAxis[0][2] = 0;
            g_lastViewAxis[1][0] = 0; g_lastViewAxis[1][1] = 1; g_lastViewAxis[1][2] = 0;
            g_lastViewAxis[2][0] = 0; g_lastViewAxis[2][1] = 0; g_lastViewAxis[2][2] = 1;
            Log("TEST: dropping");
            DropHeldWeapon(1);
            if (g_testItem) {
                // in front of the player, 60 units away, to fall there
                float yawr = RAD(va[YAW]), pos[3] = { eye[0] + cosf(yawr) * 60.0f, eye[1] + sinf(yawr) * 60.0f, eye[2] - 10.0f };
                memcpy(g_testItem + GENT_OFS_posBase, pos, sizeof(pos));
                memcpy(g_testItem + GENT_OFS_currentOrigin, pos, sizeof(pos));
            }
            stage = 3; tNext = now + 4000;
            break;
        case 3:
            Log("TEST: looking at the item");
            stage = 4; tNext = now + 1500;
            break;
        case 4:
            Log("TEST: hand pickup");
            Log("TEST: HandPickup returned %d", HandPickup(1));
            stage = 5; tNext = now + 3000;
            break;
        case 5:
            Cbuf("-activate\n");
            Log("TEST: use released; walking forward");
            g_moveForward = 100;
            stage = 6; tNext = now + 2500;
            break;
        case 6:
            g_moveForward = 0;
            Log("TEST: end");
            Cbuf("quit\n");
            stage = 7; tNext = now + 99999;
            break;
        }
    keep:
        if (stage >= 4 && stage <= 5 && g_testItem) {
            // keep looking at the item
            const float *io = (const float *)(g_testItem + GENT_OFS_currentOrigin);
            float d[3] = { io[0] - eye[0], io[1] - eye[1], io[2] - eye[2] }, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 1.0f) { va[YAW] = DEG(atan2f(d[1], d[0])); va[PITCH] = -DEG(asinf(d[2] / len)); }
        }
        (void)va0;
    }
}
#endif

static void __cdecl hk_BeginFrame(int stereoFrame)
{
    if (!cfg.enabled) { orig_BeginFrame(stereoFrame); return; }
#ifdef COD_VR_TEST
    if (stereoFrame != 2) TestDriver();
#endif

    if (stereoFrame != 2) {   // start of a frame
        MEM(int, ADDR_xsurfCount) = 0;   // the per-scene reset happens here, once per frame
        g_heldCount = 0;
        g_pitchSampled = 0;
        if (fb_ok) StartVR();
        HookKeyBindingText();
        HookGameKeyBinding();
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
// The camera is the game's eye plus where the head is from it, and with roomscale the head can lean or walk
// where the game's player cannot go: walls stop the player, not the head, and the view went into door frames
// and corners (Pavlov's house). The head's offset is traced against the world and stops HEAD_WALL_GAP units
// before what it hits; both eyes get the same correction, so the stereo stays as it is.
#define HEAD_WALL_GAP 4.0f
static float g_headPush[3];

static void UpdateHeadPush(const cod_refdef_t *fd)
{
    float rel[3], p[3], end[3], len, frac, keep;
    int i;
    g_headPush[0] = g_headPush[1] = g_headPush[2] = 0;
    if (!cfg.headCollision || !cfg.positional || g_onTurret || g_inVehicle || !MapLoaded()) return;
    BodyOffset(g_head.pos, rel);
    RotateZ(rel, BodyYaw(fd->viewaxis[0]), p);
    for (i = 0; i < 3; i++) { p[i] *= cfg.worldScale; end[i] = fd->vieworg[i] + p[i]; }
    len = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (len < 0.5f) return;
    frac = WorldTrace(fd->vieworg, end);
    if (frac >= 1.0f) return;
    keep = frac * len - HEAD_WALL_GAP;
    if (keep < 0) keep = 0;
    for (i = 0; i < 3; i++) g_headPush[i] = p[i] * (keep / len) - p[i];
}

// The weapon name of turret entity idx (its entity state's weapon byte indexes the game's weapon definitions), or NULL.
static const char *TurretWeaponName(int idx)
{
    HMODULE gm = GetModuleHandleA(GAME_DLL);
    const char *name = NULL;
    if (!gm || idx <= 0) return NULL;
    __try {
        int w = *(const BYTE *)((BYTE *)gm + G_OFS_entities + idx * G_ENTITY_SIZE + GENT_OFS_weapon);
        BYTE **defs = *(BYTE ***)((BYTE *)gm + G_OFS_weaponDefs);
        if (defs && w > 0 && w <= *(int *)((BYTE *)gm + G_OFS_numWeaponDefs) && defs[w]) name = WeaponName(defs[w]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name = NULL;
    }
    return name;
}

static void ApplyEyePose(cod_refdef_t *fd, const vr_pose_t *eye, float out_axis[3][3], float out_org[3])
{
    float body = BodyYaw(fd->viewaxis[0]);
    float rel[3], p[3];
    int i;

    for (i = 0; i < 3; i++) RotateZ(eye->axis[i], body, out_axis[i]);

    // Eye position relative to the head (IPD), plus the head itself when 6DoF is on.
    BodyOffset(eye->pos, rel);
    RotateZ(rel, body, p);
    if (g_onTurret) {
        // On a turret the game swings the player around its pivot as it turns, and its view was
        // made for a monitor (standing, the gun sits low in a window): the eyes go just behind
        // and a little above the gun when getting on, then stay put (the head moves freely).
        if (!g_turretOrgSet) {
            // The game is still putting the player in place: follow it until its eye has held still relative
            // to the gun for 0.1 s (at least 0.2 s; at most 1.5 s). A mount restored from a save settles later
            // than one made by hand, and the place taken too early stayed wrong for the whole ride.
            static float lastRel[3];
            static DWORD stillSince, stillFor;
            DWORD now = GetTickCount();
            float d = 0;
            for (i = 0; i < 3; i++) { float r = fd->vieworg[i] - g_turretPivotWorld[i]; d += (r - lastRel[i]) * (r - lastRel[i]); lastRel[i] = r; }
            if (stillFor != g_turretOnAt || d > 1.0f) { stillFor = g_turretOnAt; stillSince = now; }
            if (now - g_turretOnAt < 200 || (now - stillSince < 100 && now - g_turretOnAt < 1500)) {
                // A turret the script mounted turns the player's view to the gun while it settles; the body taken in
                // the first frame (UpdateTurret) is then not the game's: the picture and the gun's aim would differ by
                // that turn for the whole ride. The body is the game's view minus our aim, as everywhere else.
                if (g_turretScripted) g_turretBody = YawOf(fd->viewaxis[0]) - g_headYawCmd;
                for (i = 0; i < 3; i++) out_org[i] = fd->vieworg[i] + p[i] * cfg.worldScale;
                return;
            }
        }
        if (!g_turretOrgSet) {
            // where the game puts the gunner's eye for this gun (right behind it, a little above),
            // with this eye there now: the player's own height and place in the room aside
            for (i = 0; i < 3; i++) g_turretOrgBase[i] = fd->vieworg[i] - p[i] * cfg.worldScale;
            // the game's own gunner eye relative to the pivot, without the head: the seat Recenter returns to.
            // Further from the pivot than any real seat (38 units behind for the MG42, 15 on the bomber) it
            // is not one (the player was not put on the gun yet): behind the gun as the grip mount does it.
            for (i = 0; i < 3; i++) g_turretCanon[i] = fd->vieworg[i] - g_turretPivotWorld[i];
            if (g_turretScripted) {
                // The game's eye turns around the pivot with the gun (the player rides on it): the eye taken as it is
                // depends on where the gun points when it is taken - aimed up it is lower, aimed down higher (the
                // tail gun's eye is 19 units behind the pivot, so 20 degrees of pitch are 6 units of height, seen
                // as the camera z in the log). The same eye is taken in the gun's own frame (along the barrel, to its
                // left, up) and put back with the gun at rest: its base yaw, level. The aim at that moment no longer counts.
                // The angles are the player state's (ps.viewangles: the gun's target, kept inside its arcs), NOT the
                // camera's: the camera follows the gun at its turn speed, but the eye is placed from the target at once
                // (log: aimed 70 degrees up the eye was 12.6 lower than aimed level while the camera still showed
                // 4 degrees; the tail and dorsal eye fit a rigid seat in the clamped target's frame to within 1 unit).
                float e[3] = { g_turretCanon[0], g_turretCanon[1], g_turretCanon[2] }, b = RAD(g_turretYawNow);
                float ax[3][3], pitch = PitchOf(fd->viewaxis[0]), yaw = YawOf(fd->viewaxis[0]), cp, sp, cy, sy;
                BYTE *pc = PlayerClient();
                __try {
                    if (pc) { pitch = *(float *)(pc + GCL_OFS_viewangles); yaw = *(float *)(pc + GCL_OFS_viewangles + 4); }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }
                {
                    // The eye is placed from the pitch kept inside the gun's arcs, but the player state can still hold the
                    // raw target when it is read (tail gun, controller raised: -65.8 against a 28 degree arc: the seat
                    // came out 10 units too high, "well above the sight"). The arcs are the mission's own
                    // (bomber.gsc: tail settoparc(28) setbottomarc(22), dorsal 41 and 2); other guns are left alone.
                    const char *gun = TurretWeaponName(g_turretIdx);
                    float up = 0, down = 0;
                    if (gun && strstr(gun, "rear")) { up = 28.0f; down = 22.0f; }
                    else if (gun && strstr(gun, "dorsal")) { up = 41.0f; down = 2.0f; }
                    if (up > 0 && pitch < -up) pitch = -up;
                    if (down > 0 && pitch > down) pitch = down;
                }
                cp = cosf(RAD(pitch)); sp = sinf(RAD(pitch)); cy = cosf(RAD(yaw)); sy = sinf(RAD(yaw));   // pitch is positive looking down
                ax[0][0] = cp * cy; ax[0][1] = cp * sy; ax[0][2] = -sp;      // along the barrel
                ax[1][0] = -sy;     ax[1][1] = cy;      ax[1][2] = 0;        // to its left
                ax[2][0] = sp * cy; ax[2][1] = sp * sy; ax[2][2] = cp;       // up
                {
                    float ef = e[0] * ax[0][0] + e[1] * ax[0][1] + e[2] * ax[0][2];
                    float el = e[0] * ax[1][0] + e[1] * ax[1][1] + e[2] * ax[1][2];
                    float eu = e[0] * ax[2][0] + e[1] * ax[2][1] + e[2] * ax[2][2];
                    g_turretCanon[0] = ef * cosf(b) - el * sinf(b);
                    g_turretCanon[1] = ef * sinf(b) + el * cosf(b);
                    g_turretCanon[2] = eu;
                    Log("turret: the seat in the gun's frame: %.1f along, %.1f left, %.1f up (the game's eye was %.1f %.1f %.1f from the pivot, target pitch %.1f yaw %.1f, camera pitch %.1f)",
                        ef, el, eu, e[0], e[1], e[2], pitch, yaw, PitchOf(fd->viewaxis[0]));
                }
            }
            if (g_turretCanon[0] * g_turretCanon[0] + g_turretCanon[1] * g_turretCanon[1] > 90.0f * 90.0f || fabsf(g_turretCanon[2]) > 90.0f) {
                g_turretCanon[0] = -cosf(RAD(g_turretYaw)) * 24.0f;
                g_turretCanon[1] = -sinf(RAD(g_turretYaw)) * 24.0f;
                g_turretCanon[2] = 24.0f;
                for (i = 0; i < 3; i++) g_turretOrgBase[i] = g_turretPivotWorld[i] + g_turretCanon[i] - p[i] * cfg.worldScale;
                Log("turret: the game's eye is not at the gun (%.0f units away), the seat is 24 units behind it", sqrtf(
                    (fd->vieworg[0] - g_turretPivotWorld[0]) * (fd->vieworg[0] - g_turretPivotWorld[0]) +
                    (fd->vieworg[1] - g_turretPivotWorld[1]) * (fd->vieworg[1] - g_turretPivotWorld[1])));
            }
            g_turretCanonYaw = g_turretYawNow;
            // A gun that makes the soldier crouch (the bomber's tail gun: weapon stance "duck") gives the game's eye 40
            // units above the player's origin instead of 60: the seat is 20 lower than a standing one, the dorsal
            // turret's, and the physical sight is too high for it. Scripted guns only; the amount is
            // [Turret] CrouchedSeatLift (game units, 60 - 40 = 20). Evidence: tail gun, eye 40.1 above the origin.
            g_turretLift = 0;
            if (g_turretScripted) {
                // Which of the bomber's guns: by the turret's weapon (mg_b17_rear is the tail gun, mg_b17_dorsal the
                // dorsal), else by the seat (the tail gun's is 2..6 units above the pivot, the dorsal's 11..12). The
                // game's eye height above the player's origin used before is no guide: it moves with the gun's pitch
                // (36.7 .. 56.4 on the tail gun in one session).
                const char *wn = TurretWeaponName(g_turretIdx);
                int known = wn && (strstr(wn, "rear") || strstr(wn, "dorsal"));
                g_turretLift = known ? strstr(wn, "rear") != NULL : g_turretCanon[2] < 8.0f;
                Log("turret: gun %s (%s), seat %.1f up%s", wn ? wn : "?", known ? "by its weapon" : "by its seat", g_turretCanon[2],
                    g_turretLift ? ": the tail gun" : ": the dorsal gun");
            }
            if (g_turretScripted) TurretReseat();   // the seat, not the eye as the aim happened to place it
            g_turretOrgSet = 1;
            Log("turret: eyes at %.0f %.0f %.0f (gun pivot %.0f %.0f %.0f), body %.1f, game view %.1f, aim %.1f",
                fd->vieworg[0], fd->vieworg[1], fd->vieworg[2], g_turretPivotWorld[0], g_turretPivotWorld[1],
                g_turretPivotWorld[2], g_turretBody, YawOf(fd->viewaxis[0]), g_headYawCmd);
        }
        {
            // [Turret] Offset X / Y / Z: where the player stands relative to the gun, in the gun's own
            // directions as it was when mounted (X along the barrel, Y to its left, Z up), in cm
            float local[3], off[3];
            // the bomber's guns have an offset of their own: [Turret] Dorsal X/Y/Z, Tail X/Y/Z (the crouched one)
            for (i = 0; i < 3; i++)
                local[i] = (cfg.turretOffset[i] + (g_turretScripted ? cfg.turretSeat[g_turretLift ? 1 : 0][i] : 0.0f)) * cfg.worldScale / 100.0f;
            RotateZ(local, g_turretYaw, off);
            if (g_turretLift) off[2] += cfg.turretSeatLift;
            for (i = 0; i < 3; i++) g_turretOrg[i] = g_turretOrgBase[i] + off[i];
        }
        for (i = 0; i < 3; i++) out_org[i] = g_turretOrg[i] + p[i] * cfg.worldScale;
        return;
    }
    g_turretOrgSet = 0;
    for (i = 0; i < 3; i++) out_org[i] = fd->vieworg[i] + p[i] * cfg.worldScale + g_headPush[i];
}

static void SamplePitchOffset(const cod_refdef_t *fd)
{
    float raw = AngleNormalize180(PitchOf(fd->viewaxis[0]) - g_clPitchCmd);
    // On a gun the mission mounted the game keeps the view inside the gun's arcs, so the view pitch stops following
    // what we send; measuring the difference then made the offset grow by itself (the tail gun's log: cl_viewangles
    // pitch -95..-118, beyond +-90, with the game's pitch stuck near the limit). Kept as it was settled instead.
    if (g_onTurret && g_turretScripted && g_pitchOffsetInit && GetTickCount() - g_turretOnAt > 600) return;
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
    int saved[4], x = fd->x, w = fd->width;

    g_scopeActive = 0;
    if (!cfg.scope || !g_hudActive || fd->fov_x >= cfg.scopeMaxFov || fd->x != 0) return;
    fd->x = 1;
    fd->width = w - 1;

    saved[0] = MEM(int, ADDR_r_firstSceneEntity);
    saved[1] = MEM(int, ADDR_r_firstSceneDlight);
    saved[2] = MEM(int, ADDR_r_firstScenePoly);
    saved[3] = MEM(int, ADDR_r_firstSceneList6);
    g_projOverride = -1;
    orig_RenderScene(fd);
    MEM(int, ADDR_r_firstSceneEntity) = saved[0];
    MEM(int, ADDR_r_firstSceneDlight) = saved[1];
    MEM(int, ADDR_r_firstScenePoly) = saved[2];
    MEM(int, ADDR_r_firstSceneList6) = saved[3];

    fd->x = x;
    fd->width = w;
    g_scopeActive = 1;
    g_scopeFov = fd->fov_x;
}

// Earthquakes and explosions shake the game's camera. In a headset the world wobbling around the head
// is hard to bear, so the shake of the view's origin is cut down: the smooth path of the origin is
// followed (Holt smoothing, which keeps a steady walk free of lag) and what departs from it is kept
// only in part (ShakeKeep). Small movements (a step's bob) are left alone, and a real jump (a teleport,
// roomscale catching up) is followed at once. The yaw is not touched: it holds the head's own turns
// (the aim is written into the game's view), and smoothing it made the image lag and wobble when the
// head turned (v1.6.4).
static void FilterShake(cod_refdef_t *fd)
{
    static float s[3], b[3];   // smoothed origin x y z, and how fast each is moving
    static int init;
    static LARGE_INTEGER last;
    LARGE_INTEGER now, freq;
    float out[3], dt, a;
    int i;
    if (cfg.shakeKeep >= 0.999f) { init = 0; return; }
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart) : 0.0f;
    last = now;
    if (!init || dt <= 0 || dt > 0.25f) {
        for (i = 0; i < 3; i++) { s[i] = fd->vieworg[i]; b[i] = 0; }
        init = 1;
        return;
    }
    a = 1.0f - expf(-dt / 0.12f);
    for (i = 0; i < 3; i++) {
        float pred = s[i] + b[i] * dt, xi = fd->vieworg[i], d, ns;
        d = xi - pred;
        if (fabsf(d) > 24.0f) { s[i] = xi; b[i] = 0; out[i] = xi; continue; }
        ns = pred + a * d;
        b[i] += a * ((ns - s[i]) / dt - b[i]);
        s[i] = ns;
        d = xi - ns;
        out[i] = ns + (fabsf(d) > 1.0f ? (d > 0 ? 1.0f : -1.0f) * (1.0f + (fabsf(d) - 1.0f) * cfg.shakeKeep) : d);
    }
    for (i = 0; i < 3; i++) fd->vieworg[i] = out[i];
}

// Headset frame: the scope view (when zoomed), the view weapon in the hand, then the scene from
// each eye. The right eye reuses the scene's entities, lights and polys (first-item counters
// restored) and is marked for the backend by x + 2.
static void RenderBothEyes(cod_refdef_t *fd)
{
    float savedOrg[3], savedAxis[3][3], savedFov[2], cullFov[2];
    int saved[4], x = fd->x, y = fd->y, w = fd->width, h = fd->height, eye, k;

    // A sky portal scene (rdflags bit 3) has its own camera: the aim, the scope and the view
    // weapon belong to the main scene.
    if (!(fd->rdflags & 8)) {
        if (!g_pitchSampled) { SamplePitchOffset(fd); g_pitchSampled = 1; }
        memcpy(g_rawViewOrg, fd->vieworg, sizeof(g_rawViewOrg));
        FilterShake(fd);
        memcpy(g_lastViewOrg, fd->vieworg, sizeof(g_lastViewOrg));
        memcpy(g_lastViewAxis, fd->viewaxis, sizeof(g_lastViewAxis));
        {
            static DWORD nextViewLog;
            if (g_uo && GetTickCount() >= nextViewLog) {
                float *cl = (float *)ADDR_cl_viewangles;
                nextViewLog = GetTickCount() + 1000;
                Log("view: game yaw %.1f pitch %.1f | cl_viewangles yaw %.1f pitch %.1f | head yaw %.1f pitch %.1f | headCmd %.1f prev %.1f",
                    YawOf(fd->viewaxis[0]), PitchOf(fd->viewaxis[0]), cl[1], cl[0], YawOf(g_head.axis[0]), PitchOf(g_head.axis[0]),
                    g_headYawCmd, g_headYawPrev);
            }
        }
        {
            // Does the game's view follow the aim we write into it? When it does not (a scripted
            // scene holds it) the head is free: it turns on top of the held view.
            static float lastView, lastHead, accView, accHead;
            static int init, heldFrames;
            float v = YawOf(fd->viewaxis[0]);
            if (init) {
                accView = accView * 0.92f + fabsf(AngleNormalize180(v - lastView));
                accHead = accHead * 0.92f + fabsf(AngleNormalize180(g_headYawPrev - lastHead));
                // for a good while, not a few frames (drawing a weapon holds the view briefly)
                heldFrames = (accHead > 4.0f && accView < 0.15f * accHead) ? heldFrames + 1 : 0;
                if (g_linkedScene && !g_viewLocked) { g_viewLocked = 1; Log("view held by a linked scene: the head turns freely"); }
                if (!g_viewLocked && heldFrames >= 45) { g_viewLocked = 1; Log("view held by the game: the head turns freely"); }
                else if (g_viewLocked && !g_linkedScene && accHead > 1.0f && accView > 0.5f * accHead) { g_viewLocked = 0; Log("view follows the aim again"); }
            }
            lastView = v;
            lastHead = g_headYawPrev;
            init = 1;
        }
        SetCrouchCorrection(fd->vieworg);
        UpdateRoomScale(fd);
        RenderScopeView(fd);
        FlushHeldEntities(fd);
        UpdateHeadPush(fd);
    }

    memcpy(savedOrg, fd->vieworg, sizeof(savedOrg));
    memcpy(savedAxis, fd->viewaxis, sizeof(savedAxis));
    savedFov[0] = fd->fov_x;
    savedFov[1] = fd->fov_y;
    saved[0] = MEM(int, ADDR_r_firstSceneEntity);
    saved[1] = MEM(int, ADDR_r_firstSceneDlight);
    saved[2] = MEM(int, ADDR_r_firstScenePoly);
    saved[3] = MEM(int, ADDR_r_firstSceneList6);

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
        // The headset shows each frame a little later, re-aimed at where the head is by then
        // (reprojection): on a quick head turn its edges come from just outside the view the
        // frame was drawn for. What the game culled there flashed in and out, so the game draws
        // a margin around the view.
        cullFov[0] = 2.0f * DEG(atanf(tx)) + 2.0f * cfg.cullMargin;
        cullFov[1] = 2.0f * DEG(atanf(ty)) + 2.0f * cfg.cullMargin;
        if (cullFov[0] > 170.0f) cullFov[0] = 170.0f;
        if (cullFov[1] > 170.0f) cullFov[1] = 170.0f;
        // The model LOD scale grows with the fov: keep the game's own detail level.
        g_lodRatio = tanf(RAD(savedFov[0] * 0.5f)) / tanf(RAD(cullFov[0] * 0.5f));
    }

    for (k = 0; k < 2; k++) {
        const vr_pose_t *e;
        eye = cfg.rightEyeFirst ? 1 - k : k;
        e = &g_eyes[eye];
        // The eyes always fill their buffer: the game's letterbox (cinematic bars) would
        // otherwise be stretched over the headset image.
        fd->x = eye ? 2 : 0;
        fd->y = 0;
        fd->width = fb_w - fd->x;
        fd->height = fb_h;
        if (k == 1) {
            // the second eye draws the same scene: every list the renderer starts per scene
            MEM(int, ADDR_r_firstSceneEntity) = saved[0];
            MEM(int, ADDR_r_firstSceneDlight) = saved[1];
            MEM(int, ADDR_r_firstScenePoly) = saved[2];
            MEM(int, ADDR_r_firstSceneList6) = saved[3];
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

// HUD modes. The compass and the subtitles have their own dvars; health, ammo, the weapon name
// and the stance come from ui/hud.menu, so their pictures (and text) are left out by where that
// file puts them on the 640x480 screen. Holding Objectives shows everything.
// cg_drawStatus stays on in every mode (it also draws the use hints); what it would add (the grenade
// icons by the ammo) is left out with the rest of the bottom right.
enum { HUD_FULL, HUD_NONE, HUD_SUBTITLES, HUD_COMPASS, HUD_STATUS };
#define SUBTITLE_BOOST     1.25f   // every SubtitleSize is drawn this much bigger (Small = 1.25 x the game's size), FlushGlyphs
enum { HIDE_NOTHING, HIDE_STATUS, HIDE_STATUS_CORNER, HIDE_STANCE };
static int g_hudShown = -1;    // the mode the dvars were last set for
static int g_hudHide;          // HIDE_*: which of the hud.menu pictures are left out

static void UpdateHudMode(int objectivesHeld)
{
    // cg_drawStatus stays on in every mode: it draws all of ui/hud.menu, and with it off the use hints went
    // too ("Cursorhints": the weapon's name and picture when you are near one). What a mode hides is cut out
    // of the pictures instead (HiddenPicture), as the compass-only mode always did.
    static const int hide[] = { HIDE_NOTHING, HIDE_STATUS_CORNER, HIDE_STATUS_CORNER, HIDE_STATUS_CORNER, HIDE_STANCE };
    int mode = objectivesHeld ? HUD_FULL : cfg.hudMode;
    char line[200];
    // bigger subtitles (SubtitleSize) wrap earlier: the game's own line is 80 characters wide
    int subWidth = (int)(80.0f / (cfg.subtitleSize * SUBTITLE_BOOST) + 0.5f);
    if (mode < HUD_FULL || mode > HUD_STATUS) mode = HUD_FULL;
    g_hudHide = hide[mode];
    if (mode == g_hudShown) return;
    if (subWidth < 16) subWidth = 16;
    if (subWidth > 160) subWidth = 160;
    _snprintf(line, sizeof(line), "set cg_hudCompassSize %s; set cg_subtitles %d; set cg_drawStatus 1; set cg_subtitleWidth %d\n",
              mode == HUD_FULL || mode == HUD_COMPASS ? "1" : "0",
              mode == HUD_FULL || mode == HUD_SUBTITLES,
              subWidth);
    Cbuf(line);
    g_hudShown = mode;
    Log("HUD mode %d", mode);
}

static int HiddenPicture(float x, float y, float w, float h)
{
    float cx = (x + w * 0.5f) * 640.0f / fb_w, cy = (y + h * 0.5f) * 480.0f / fb_h;
    // the stance icon: 40x40 at the bottom (hud.menu says x 100; on a wide screen it ends up elsewhere)
    int stance = w * 640.0f / fb_w >= 36 && w * 640.0f / fb_w <= 44 && h * 480.0f / fb_h >= 36 &&
                 h * 480.0f / fb_h <= 44 && cy >= 425;
    if (g_hudHide == HIDE_STANCE) return stance;
    return stance ||
           (cx >= 242 && cx <= 563 && cy >= 430 && cy <= 477) ||   // weapon name
           (cx >= 537 && cy >= 420 && cy <= 486) ||                // fire mode, ammo
           (cx >= 495 && cx <= 636 && cy >= 455 && cy <= 476) ||   // health bar
           (g_hudHide == HIDE_STATUS_CORNER && cx >= 440 && cy >= 380);   // grenade icons
}

// [Debug] LogHud: the HUD's pictures (640x480), a frame every few seconds, to find what is where.
static void LogHudPicture(float x, float y, float w, float h, int shader)
{
    static DWORD next;
    static int frameLines;
    DWORD now = GetTickCount();
    if (now >= next) { next = now + 3000; frameLines = 60; Log("---- HUD pictures (mode %d) ----", g_hudShown); }
    if (frameLines <= 0) return;
    frameLines--;
    Log("  pic shader %d at %.0f,%.0f size %.0fx%.0f", shader, x * 640.0f / fb_w, y * 480.0f / fb_h,
        w * 640.0f / fb_w, h * 480.0f / fb_h);
}

// A picture over the whole screen during play (damage tint, fade, flash) would be a small
// rectangle on the HUD screen: keep it for DrawFullscreenEffects.
// Subtitles: the game draws its text one glyph at a time, at a size of its own, too small and too
// tight to read in the headset. The glyphs of the lower part of the screen are held back and drawn
// at the end of the frame, each block of lines scaled about the middle of its bottom edge (so it
// grows upwards and stays centred), with more room between its lines and a translucent black box
// behind it, like the objectives screen. A glyph is a small picture that shows only a part of its
// image: icons (the compass, the weapon pictures) show the whole of theirs, and the weapon, ammo and
// health readouts sit lower or further right than the subtitles.
#define MAX_GLYPHS 768
#define SUBTITLE_LINE_GAP  0.35f   // extra room between the lines of a block, in line heights
#define SUBTITLE_PAD       6.0f    // the black box's margin round the text, in 640x480 pixels (its opacity: ObjectivesBackground)
typedef struct { float r[8]; int shader; float color[4]; int hasColor; } glyph_t;
static glyph_t g_glyphs[MAX_GLYPHS];
static int     g_glyphCount;

// A letter: a small picture that shows only a part of its image (icons show the whole of theirs).
static int IsGlyph(float w, float h, float s1, float t1, float s2, float t2)
{
    if (w > fb_w * 40.0f / 640.0f || h > fb_h * 40.0f / 480.0f) return 0;
    return s2 - s1 <= 0.35f && t2 - t1 <= 0.35f;
}

static int IsSubtitleGlyph(float x, float y, float w, float h, float s1, float t1, float s2, float t2)
{
    float cx = (x + w * 0.5f) / fb_w, cy = (y + h * 0.5f) / fb_h;
    if (!IsGlyph(w, h, s1, t1, s2, t2)) return 0;
    if (cy < 250.0f / 480.0f || cy > 470.0f / 480.0f) return 0;
    if (cx < 0.05f || cx > 0.95f) return 0;
    if (cx > 0.67f && cy > 0.78f) return 0;   // the ammo and grenade corner
    return 1;
}

// Diagnostics: where the HUD's text is (640x480) and how much of it is taken as subtitles, a line
// every 2 seconds while there is text (finding the subtitles' real place).
static float g_textBox[2][4];   // [0] all letters, [1] the ones taken: min x, min y, max x, max y
static int   g_textCount[2];

static void NoteText(float x, float y, float w, float h, int taken)
{
    float cx = (x + w * 0.5f) * 640.0f / fb_w, cy = (y + h * 0.5f) * 480.0f / fb_h;
    int k;
    for (k = 0; k <= taken; k++) {
        float *b = g_textBox[k];
        if (!g_textCount[k]++) { b[0] = b[2] = cx; b[1] = b[3] = cy; }
        if (cx < b[0]) b[0] = cx;
        if (cy < b[1]) b[1] = cy;
        if (cx > b[2]) b[2] = cx;
        if (cy > b[3]) b[3] = cy;
    }
}

static void LogText(void)
{
    static DWORD next;
    if (g_textCount[0] && GetTickCount() >= next) {
        next = GetTickCount() + 2000;
        Log("hud text: %d letters at x %.0f-%.0f y %.0f-%.0f; %d taken as subtitles (x %.0f-%.0f y %.0f-%.0f)",
            g_textCount[0], g_textBox[0][0], g_textBox[0][2], g_textBox[0][1], g_textBox[0][3],
            g_textCount[1], g_textBox[1][0], g_textBox[1][2], g_textBox[1][1], g_textBox[1][3]);
    }
    g_textCount[0] = g_textCount[1] = 0;
}

// The renderer's plain white picture (what the client draws its console with), for the box.
typedef int (__cdecl *PFN_RegisterShader)(const char *name, int type);
static int WhiteShader(void)
{
    int h = 0;
    __try {
        PFN_RegisterShader reg = MEM(PFN_RegisterShader, ADDR_re_client + RE_OFS_RegisterShader);
        if (reg) h = reg("white", 2);   // already loaded: only a lookup
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        h = 0;
    }
    return h;
}

static void FlushGlyphs(void)
{
    int idx[MAX_GLYPHS], line[MAX_GLYPHS], n = g_glyphCount, i, j, start, white;
    float k = cfg.subtitleSize * SUBTITLE_BOOST, gap = fb_h * 30.0f / 480.0f;
    float pad = fb_h * SUBTITLE_PAD / 480.0f;
    float back[4] = { 0, 0, 0, cfg.objBackground };   // as dark as the objectives screen's panel
    g_glyphCount = 0;
    if (n <= 0) return;
    white = WhiteShader();
    for (i = 0; i < n; i++) {   // by height (insertion sort)
        float cy = g_glyphs[i].r[1] + g_glyphs[i].r[3] * 0.5f;
        for (j = i; j > 0 && g_glyphs[idx[j - 1]].r[1] + g_glyphs[idx[j - 1]].r[3] * 0.5f > cy; j--) idx[j] = idx[j - 1];
        idx[j] = i;
    }
    for (start = 0; start < n; ) {
        int end = start + 1, lines;
        float minX = 1e9f, maxX = -1e9f, bottom = -1e9f, maxH = 0, top = 1e9f, cx, lineCy, extra;
        while (end < n) {
            const float *a = g_glyphs[idx[end - 1]].r, *b = g_glyphs[idx[end]].r;
            if (b[1] + b[3] * 0.5f - (a[1] + a[3] * 0.5f) > gap) break;
            end++;
        }
        for (i = start; i < end; i++) {
            const float *r = g_glyphs[idx[i]].r;
            if (r[0] < minX) minX = r[0];
            if (r[0] + r[2] > maxX) maxX = r[0] + r[2];
            if (r[1] + r[3] > bottom) bottom = r[1] + r[3];
            if (r[3] > maxH) maxH = r[3];
        }
        // the lines of the block: a glyph further down than 0.6 of a glyph's height starts a new one
        lines = 0;
        lineCy = g_glyphs[idx[start]].r[1] + g_glyphs[idx[start]].r[3] * 0.5f;
        for (i = start; i < end; i++) {
            float cy = g_glyphs[idx[i]].r[1] + g_glyphs[idx[i]].r[3] * 0.5f;
            if (cy - lineCy > maxH * 0.6f) { lines++; lineCy = cy; }
            line[i] = lines;
        }
        extra = maxH * k * SUBTITLE_LINE_GAP;   // each line above the last one goes up by this much more
        cx = (minX + maxX) * 0.5f;
        for (i = start; i < end; i++) {
            float y = bottom - (bottom - g_glyphs[idx[i]].r[1]) * k - (lines - line[i]) * extra;
            if (y < top) top = y;
        }
        if (white) {
            float x0 = cx + (minX - cx) * k - pad, x1 = cx + (maxX - cx) * k + pad;
            orig_SetColor(back);
            orig_StretchPic(x0, top - pad, x1 - x0, bottom - top + 2 * pad, 0, 0, 1, 1, white);
        }
        for (i = start; i < end; i++) {
            const glyph_t *g = &g_glyphs[idx[i]];
            orig_SetColor(g->hasColor ? g->color : NULL);
            orig_StretchPic(cx + (g->r[0] - cx) * k, bottom - (bottom - g->r[1]) * k - (lines - line[i]) * extra,
                            g->r[2] * k, g->r[3] * k, g->r[4], g->r[5], g->r[6], g->r[7], g->shader);
        }
        start = end;
    }
    orig_SetColor(g_curColorSet ? g_curColor : NULL);
}

static void __cdecl hk_StretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader)
{
    // the scope overlay: a big picture over the zoomed view (520x400 of 640x480 for the rifles)
    if (g_scopeActive && w * 640.0f / fb_w >= 200.0f && h * 480.0f / fb_h >= 200.0f) g_scopeOverlay = 1;
    if (g_hudActive && !(MEM(int, ADDR_cls_keyCatchers) & 2)) {
        if (cfg.logHud) LogHudPicture(x, y, w, h, shader);

        if (g_hudHide && HiddenPicture(x, y, w, h)) return;
        if (IsGlyph(w, h, s1, t1, s2, t2)) NoteText(x, y, w, h, !g_scopeActive && IsSubtitleGlyph(x, y, w, h, s1, t1, s2, t2));
    }
    if (g_hudActive && !g_scopeActive && !(MEM(int, ADDR_cls_keyCatchers) & 2) &&
        g_glyphCount < MAX_GLYPHS && IsSubtitleGlyph(x, y, w, h, s1, t1, s2, t2)) {
        glyph_t *g = &g_glyphs[g_glyphCount++];
        float r[8] = { x, y, w, h, s1, t1, s2, t2 };
        memcpy(g->r, r, sizeof(r));
        g->shader = shader;
        g->hasColor = g_curColorSet;
        memcpy(g->color, g_curColor, sizeof(g_curColor));
        return;   // drawn at the end of the frame, resized (FlushGlyphs)
    }
    if (x <= 1 && y <= 1 && x + w >= fb_w - 1 && y + h >= fb_h - 1) {   // diagnostics
        g_fullShader = shader;
        g_fullAlpha = g_curColorSet ? g_curColor[3] : 1.0f;
    }
    if (g_singlePass && g_hudActive && !g_scopeActive && !(MEM(int, ADDR_cls_keyCatchers) & 2) &&
        x <= 1 && y <= 1 && x + w >= fb_w - 1 && y + h >= fb_h - 1 && (!g_curColorSet || g_curColor[3] >= 0.98f)) {
        g_slideFrame = 1;   // the black picture that starts a briefing; what follows it is the slides
    }
    if (g_singlePass && g_hudActive && !g_scopeActive && g_fsCount < MAX_FULLSCREEN && !g_slideFrame &&
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
    FlushGlyphs();
    LogText();
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
        g_worldProj[0] = m[0][10];   // depth to distance for the ambient occlusion
        g_worldProj[1] = m[0][14];
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

// RB_SurfaceStaticModelCached (SP 0x4fabd0, UO 0x4ffda0: the game's static models: props, trees, rubble)
// appends the surface's indices to a cache of 0x20000 shorts whose counter sits right after it, and
// never checks the room. In a prop-heavy view (the mission rocket, with the wide VR view) the cache
// overflowed, the write hit the counter, and the next store crashed the game; on UO's forests
// (bastogne1) the overwritten counter showed as trees flashing and turning black in the left eye. A
// surface that does not fit is left out of that frame instead.
#define STATIC_CACHE_COUNT   ADDR_staticCacheCount
#define STATIC_CACHE_SHORTS  0x20000
typedef void (__cdecl *PFN_StaticModel)(void *arg);
static PFN_StaticModel tramp_StaticModel;

static void __cdecl hk_StaticModel(void *arg)
{
    __try {
        const BYTE *surf = *(const BYTE **)((const BYTE *)arg + 4);
        int n = *(const unsigned short *)(surf + 0x10);
        int count = *(const int *)STATIC_CACHE_COUNT;
        if (count < 0 || count + n + 8 > STATIC_CACHE_SHORTS) {
            static int skipped;
            if (skipped++ < 5 || skipped % 500 == 0)
                Log("static models: the index cache is full (%d of %d, surface of %d), surface left out (%d so far)",
                    count, STATIC_CACHE_SHORTS, n, skipped);
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;   // an argument that cannot be read: not worth drawing
    }
    tramp_StaticModel(arg);
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
        if (cfg.ssao > 0 && g_vrFrame && !g_depthCaptured[g_fxTarget - 1]) {
            int e = g_fxTarget - 1;   // this scene clears the depth: keep it for the SSAO first
            PostFX_CaptureDepth(e, g_msaaOn ? fb_ms[e] : fb_fbo[e], eye_w, eye_h);
            g_depthCaptured[e] = 1;
        }
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
    // mov dword ptr [backEnd_projection2D], 1
    BYTE gl2d[] = { 0xC7, 0x05, 0, 0, 0, 0, 0x01, 0x00, 0x00, 0x00 };
    // push ecx; mov eax, dword ptr [ADDR_det_bdv]
    BYTE bdv[] = { 0x51, 0xA1, 0, 0, 0, 0 };
    // mov eax, dword ptr [ADDR_det_joy]
    BYTE joy[] = { 0xA1, 0, 0, 0, 0 };
    memcpy(gl2d + 2, &ADDR_backEnd_projection2D, 4);
    memcpy(bdv + 2, &ADDR_det_bdv, 4);
    memcpy(joy + 1, &ADDR_det_joy, 4);
    if (!tramp_StaticModel) {
        // mov eax, dword ptr [ADDR_staticModelDet]; mov ecx, dword ptr [eax + 0x20]
        BYTE sm[] = { 0xA1, 0, 0, 0, 0, 0x8B, 0x48, 0x20 };
        memcpy(sm + 1, &ADDR_staticModelDet, 4);
        tramp_StaticModel = (PFN_StaticModel)Detour(ADDR_RB_SurfaceStaticModelCached, sm, sizeof(sm), (void *)hk_StaticModel);
        if (tramp_StaticModel) Log("hooked RB_SurfaceStaticModelCached (index cache guard)");
    }
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
    {
        // The eyes are two scenes of one frame: the second one must not reuse (overwrite) the
        // animated model surfaces of the first before the backend draws them. The count is reset
        // once per frame instead (hk_BeginFrame); the game keeps it within its 8192 items.
        BYTE reset[10] = { 0xC7, 0x05, 0, 0, 0, 0, 0, 0, 0, 0 };
        memcpy(reset + 2, &ADDR_xsurfCount, 4);
        BYTE *p = (BYTE *)ADDR_RenderScene_xsurfReset;
        if (!memcmp(p, reset, sizeof(reset))) {
            DWORD old;
            VirtualProtect(p, sizeof(reset), PAGE_EXECUTE_READWRITE, &old);
            memset(p, 0x90, sizeof(reset));
            VirtualProtect(p, sizeof(reset), old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, sizeof(reset));
            Log("model surfaces kept across the eyes' scenes");
        }
    }
    callTarget = ADDR_CALL_SetupProjection + 5 + MEM(DWORD, ADDR_CALL_SetupProjection + 1);
    if (MEM(BYTE, ADDR_CALL_SetupProjection) == 0xE8 && callTarget == ADDR_R_SetupProjection) {
        PatchCall(ADDR_CALL_SetupProjection, (void *)hk_SetupProjection);
        Log("hooked R_SetupProjection call");
    } else if (callTarget != (DWORD)hk_SetupProjection) {
        static int warned;
        if (!warned) {
            warned = 1;
            Log("R_SetupProjection call NOT hooked (byte %02x, target %08x, expected %08x): the eyes keep the game's"
                " own projection, so the headset view is stretched", MEM(BYTE, ADDR_CALL_SetupProjection), callTarget,
                ADDR_R_SetupProjection);
        }
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
        if (g_msaaOn) {
            gl.DeleteFramebuffers(2, fb_ms);
            gl.DeleteRenderbuffers(2, fb_msColor);
            gl.DeleteRenderbuffers(2, fb_msDepth);
        }
        if (hud_fbo[0]) {
            gl.DeleteFramebuffers(2, hud_fbo);
            gl.DeleteRenderbuffers(1, &hud_depth);
            gl.DeleteTextures(2, hud_tex);
        }
    }
    fb_ok = 0;
    g_msaaOn = 0;
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
        gl.RenderbufferStorageMultisample = (void *)GetGLProc("glRenderbufferStorageMultisample", "glRenderbufferStorageMultisampleEXT");
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
        // Tell the launcher what the headset asks for, so it can show the real resolution of
        // each "Headset resolution" choice.
        const char *appdata = getenv("APPDATA");
        int rw, rh, mw, mh;
        XR_RecommendedSize(&rw, &rh, &mw, &mh);
        if (appdata && rw > 0) {
            char path[MAX_PATH], v[16];
            _snprintf(path, sizeof(path), "%s\\CoD-VR", appdata);
            path[sizeof(path) - 1] = 0;
            CreateDirectoryA(path, NULL);
            strncat(path, "\\settings.ini", sizeof(path) - strlen(path) - 1);
            sprintf(v, "%d", rw); WritePrivateProfileStringA("Headset", "RecommendedWidth", v, path);
            sprintf(v, "%d", rh); WritePrivateProfileStringA("Headset", "RecommendedHeight", v, path);
            sprintf(v, "%d", mw); WritePrivateProfileStringA("Headset", "MaxWidth", v, path);
            sprintf(v, "%d", mh); WritePrivateProfileStringA("Headset", "MaxHeight", v, path);
        }
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

// The Patreon supporters beside the game's main menu: the launcher draws the list (supporters.ini) into
// %APPDATA%\CoD-VR\supporters_panel.bin ("SUPP", width, height, premultiplied BGRA, bottom row first) each time it
// starts; the mod loads it into a texture the first time the main menu is up and shows it as a flat panel to the left
// of the menu screen. No file, no panel (it is tried again every 5 s while the menu is up).
static GLuint g_supFbo;
static int    g_supW, g_supH;
static DWORD  g_supTried;

static void LoadSupporterPanel(void)
{
    char path[MAX_PATH], *slash;
    HANDLE f;
    DWORD head[3], got = 0;
    BYTE *px;
    GLuint tex = 0, fbo = 0;
    GLint prevTex = 0;
    if (g_supFbo || !g_userIni[0] || !gl.GenTextures || !gl.TexImage2D) return;
    if (g_supTried && GetTickCount() - g_supTried < 5000) return;
    g_supTried = GetTickCount() | 1;
    strncpy(path, g_userIni, MAX_PATH - 40);
    path[MAX_PATH - 40] = 0;
    slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "supporters_panel.bin");
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        static int told;
        if (!told) { told = 1; Log("supporters: no panel file at %s (the launcher writes it when it starts)", path); }
        return;
    }
    if (!ReadFile(f, head, sizeof(head), &got, NULL) || got != sizeof(head) || head[0] != 0x50505553 ||
        head[1] < 16 || head[1] > 2048 || head[2] < 16 || head[2] > 4096) {
        CloseHandle(f);
        Log("supporters: %s is not a panel file", path);
        return;
    }
    px = (BYTE *)malloc((size_t)head[1] * head[2] * 4);
    if (!px || !ReadFile(f, px, head[1] * head[2] * 4, &got, NULL) || got != head[1] * head[2] * 4) {
        CloseHandle(f);
        free(px);
        Log("supporters: %s is cut short", path);
        return;
    }
    CloseHandle(f);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    gl.GenTextures(1, &tex);
    InitColorTexture(tex, (int)head[1], (int)head[2]);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (int)head[1], (int)head[2], 0, 0x80E1 /* GL_BGRA */, GL_UNSIGNED_BYTE, px);
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    free(px);
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    g_supFbo = fbo;
    g_supW = (int)head[1];
    g_supH = (int)head[2];
    Log("supporters: panel %dx%d loaded", g_supW, g_supH);
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
    int i, ew, eh;
    GLint prevTex = 0;

    if (fb_failed || !ctx || w <= 0 || h <= 0) return 0;
    if (!XR_EyeSize(&ew, &eh)) { ew = w; eh = h; }   // no headset yet: the window's size
    if (fb_ok && ctx == fb_ctx && w == fb_w && h == fb_h && ew == eye_w && eh == eye_h) return 1;
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
        InitColorTexture(fb_tex[i], ew, eh);

        gl.BindRenderbuffer(GL_RENDERBUFFER, fb_depth[i]);
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, ew, eh);

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
    if (cfg.msaa > 1 && gl.RenderbufferStorageMultisample) {
        GLint maxSamples = 0;
        int samples = cfg.msaa, ok = 1;
        gl.GetIntegerv(0x8D57, &maxSamples);   // GL_MAX_SAMPLES
        if (maxSamples > 0 && samples > maxSamples) samples = maxSamples;
        gl.GenFramebuffers(2, fb_ms);
        gl.GenRenderbuffers(2, fb_msColor);
        gl.GenRenderbuffers(2, fb_msDepth);
        for (i = 0; i < 2 && ok; i++) {
            gl.BindRenderbuffer(GL_RENDERBUFFER, fb_msColor[i]);
            gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, ew, eh);
            gl.BindRenderbuffer(GL_RENDERBUFFER, fb_msDepth[i]);
            gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, ew, eh);
            gl.BindFramebuffer(GL_FRAMEBUFFER, fb_ms[i]);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb_msColor[i]);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb_msDepth[i]);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb_msDepth[i]);
            if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
        }
        if (ok) {
            g_msaaOn = samples;
            Log("eye antialiasing: %dx MSAA", samples);
        } else {
            gl.DeleteFramebuffers(2, fb_ms);
            gl.DeleteRenderbuffers(2, fb_msColor);
            gl.DeleteRenderbuffers(2, fb_msDepth);
            fb_ms[0] = fb_ms[1] = 0;
            Log("eye antialiasing: %dx MSAA unavailable, off", samples);
        }
    }
    if (!CreateHudBuffers(w, h)) Log("HUD buffer incomplete: HUD stays in the eyes");
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    gl.BindRenderbuffer(GL_RENDERBUFFER, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.GetError();

    fb_ok = 1; fb_ctx = ctx; fb_w = w; fb_h = h; eye_w = ew; eye_h = eh;
    g_eyeBound = 0;
    Log("eye framebuffers %dx%d ready (game window %dx%d)", ew, eh, w, h);

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

// Puts the game's last viewport and scissor rectangle back, scaled for an eye buffer.
static void ScaleRect(const GLint r[4], GLint out[4])
{
    if (g_eyeBound && fb_w > 0 && fb_h > 0 && (eye_w != fb_w || eye_h != fb_h)) {
        float sx = (float)eye_w / fb_w, sy = (float)eye_h / fb_h;
        GLint x0 = (GLint)floorf(r[0] * sx + 0.5f), y0 = (GLint)floorf(r[1] * sy + 0.5f);
        out[0] = x0;
        out[1] = y0;
        out[2] = (GLint)floorf((r[0] + r[2]) * sx + 0.5f) - x0;
        out[3] = (GLint)floorf((r[1] + r[3]) * sy + 0.5f) - y0;
    } else {
        memcpy(out, r, 4 * sizeof(GLint));
    }
}

static void ReapplyRects(void)
{
    GLint r[4];
    if (g_vpSet) { ScaleRect(g_vp, r); gl.Viewport(r[0], r[1], r[2], r[3]); }
    if (g_scSet && gl.Scissor) { ScaleRect(g_sc, r); gl.Scissor(r[0], r[1], r[2], r[3]); }
}

static void SetEyeBound(int on)
{
    if (on == g_eyeBound) return;
    g_eyeBound = on;
    ReapplyRects();
}

static void BindTarget(GLuint fbo)
{
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    g_boundFbo = 1;
    SetEyeBound(fbo && (fbo == fb_fbo[0] || fbo == fb_fbo[1] || fbo == fb_ms[0] || fbo == fb_ms[1]));
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
    gl.BindFramebuffer(GL_FRAMEBUFFER, g_msaaOn ? fb_ms[eye] : fb_fbo[eye]);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    g_boundFbo = 1;
    SetEyeBound(1);
}

static void BindWindow(GLenum mode)
{
    if (g_boundFbo) { gl.BindFramebuffer(GL_FRAMEBUFFER, 0); g_boundFbo = 0; }
    gl.DrawBuffer(mode);
    SetEyeBound(0);
}

// MSAA: the eyes' multisampled images into their textures, before anything reads them.
static void ResolveEyes(void)
{
    GLboolean scissor;
    int i;
    if (!g_msaaOn) return;
    scissor = gl.IsEnabled(GL_SCISSOR_TEST);
    if (scissor) gl.Disable(GL_SCISSOR_TEST);
    for (i = 0; i < 2; i++) {
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb_ms[i]);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_fbo[i]);
        gl.BlitFramebuffer(0, 0, eye_w, eye_h, 0, 0, eye_w, eye_h, GL_COLOR_BUFFER_BIT, 0x2600);   // GL_NEAREST
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (scissor) gl.Enable(GL_SCISSOR_TEST);
}

// Anisotropic filtering: when the game gives a texture a mipmapped filter, the texture also gets
// the configured anisotropy (clamped to the driver's), and a lower level the game asks for itself
// is raised to it. Surfaces seen at an angle (floors, walls) stay sharp.
static float MaxAnisotropy(void)
{
    static float level = -1;
    if (level < 0) {
        float maxA = 0;
        if (gl.GetFloatv) gl.GetFloatv(0x84FF, &maxA);   // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
        if (gl.GetError) gl.GetError();
        level = cfg.anisotropy > maxA ? maxA : cfg.anisotropy;
        if (level > 1) Log("anisotropic filtering %.0fx (driver max %.0f)", level, maxA);
    }
    return level;
}

static void ForceAnisotropy(GLenum target, GLenum pname, float value)
{
    if (!cfg.enabled || cfg.anisotropy <= 1 || target != GL_TEXTURE_2D || !gl.TexParameterf) return;
    if (pname == GL_TEXTURE_MIN_FILTER && value >= 0x2700 && value <= 0x2703) {   // *_MIPMAP_*
        float a = MaxAnisotropy();
        if (a > 1) gl.TexParameterf(GL_TEXTURE_2D, 0x84FE, a);                    // GL_TEXTURE_MAX_ANISOTROPY_EXT
    }
}

void WINAPI hook_glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    if (pname == 0x84FE && cfg.enabled && cfg.anisotropy > 1) {
        float a = MaxAnisotropy();
        if (a > param) param = a;
    }
    gl.TexParameterf(target, pname, param);
    ForceAnisotropy(target, pname, param);
}

void WINAPI hook_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    gl.TexParameteri(target, pname, param);
    ForceAnisotropy(target, pname, (float)param);
}

void WINAPI hook_glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GLint r[4];
    g_vp[0] = x; g_vp[1] = y; g_vp[2] = w; g_vp[3] = h;
    g_vpSet = 1;
    ScaleRect(g_vp, r);
    gl.Viewport(r[0], r[1], r[2], r[3]);
}

void WINAPI hook_glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    GLint r[4];
    g_sc[0] = x; g_sc[1] = y; g_sc[2] = w; g_sc[3] = h;
    g_scSet = 1;
    ScaleRect(g_sc, r);
    gl.Scissor(r[0], r[1], r[2], r[3]);
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

// The renderer draws the view weapon (RF_DEPTHHACK) with glDepthRange(0, 0.1) and everything
// else with (0, 1). While a mirrored weapon is drawn, its front faces wind clockwise.
void WINAPI hook_glDepthRange(double zNear, double zFar)
{
    static GLenum front = GL_CCW;
    GLenum want = g_mirrorGun && zFar < 0.99 ? GL_CW : GL_CCW;
    gl.DepthRange(zNear, zFar);
    if (want != front && gl.FrontFace) { gl.FrontFace(want); front = want; }
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
        hud.world = (g_worldDrawn && !g_slideFrame) || !hud.fbo;
        {
            // diagnostics: how this frame is classified, a line whenever it changes (briefings, loading screens)
            static int last[8] = { -1 };
            int now[8] = { MEM(int, ADDR_cls_keyCatchers), MapLoaded(), g_slideFrame, g_fsCount, g_worldDrawn, g_hudActive, hud.world, g_fullShader };
            if (memcmp(last, now, sizeof(now))) {
                memcpy(last, now, sizeof(now));
                Log("screen: keyCatchers %d, map %d, slide %d (shown %d), fullscreen pictures %d, world %d, hud %d -> eyes %d, last full-screen shader %d alpha %.2f",
                    now[0], now[1], now[2], g_slideShow, now[3], now[4], now[5], now[6], g_fullShader, g_fullAlpha);
            }
        }
        // Scope: only the game's scope screen, close to the face, nothing around it
        if (g_scopeActive && hud.fbo) {
            hud.opaque = 1;
            hud.world = 0;
            hud.width = hud.menuWidth = cfg.scopeWidth;
            hud.distance = cfg.scopeDistance;
            hud.offsetY = 0;
        }
        if (!g_scopeActive) g_lastHudWorld = hud.world;
        if (!g_scopeActive && hud.fbo) {
            if (!hud.world && !cfg.menuFollowsHead) {
                // The screen stays where it appeared. When the head has moved well above or below it
                // (the player sat down or stood up) it is put in front of the head again, once: it
                // does not follow the head (following it for its first seconds made players dizzy).
                if (!g_menuQuadValid || fabsf(g_head.pos[2] - g_menuRefZ) > 0.3f) PlaceMenuQuad();
                hud.posed = 1;
                hud.pose = g_menuQuad;
            } else if (hud.world) {
                g_menuQuadValid = 0;   // the next menu or loading screen is placed again
                if (g_objOnHand) {
                    hud.posed = 1;
                    ObjectivesPose(&hud.pose);
                    hud.width = cfg.objWatchWidth;   // on the back of the hand, as a watch
                }
            }
        }
        if (drawn && g_vrFrame && hud.world && cfg.ssao > 0) {
            int e;
            for (e = 0; e < 2; e++)
                if (!g_depthCaptured[e]) PostFX_CaptureDepth(e, g_msaaOn ? fb_ms[e] : fb_fbo[e], eye_w, eye_h);
        }
        g_depthCaptured[0] = g_depthCaptured[1] = 0;
        if (g_boundFbo) BindWindow(GL_BACK);
        if (drawn) ResolveEyes();
        if (drawn && g_vrFrame && hud.world && cfg.ssao > 0) {
            float tans[2][4];
            int e;
            for (e = 0; e < 2; e++) {
                tans[e][0] = g_eyes[e].tanLeft; tans[e][1] = g_eyes[e].tanRight;
                tans[e][2] = g_eyes[e].tanDown; tans[e][3] = g_eyes[e].tanUp;
            }
            PostFX_SSAO(fb_fbo, eye_w, eye_h, (const float (*)[4])tans, g_worldProj, cfg.ssao, cfg.ssaoRadius);
        }
        if (drawn && g_vrFrame && hud.world) PostFX_Apply(fb_fbo, fb_tex, eye_w, eye_h, &cfg.postfx);
        if (drawn && g_vrFrame && hud.world && !g_scopeActive) DrawVignette();
        if (drawn && g_vrFrame && hud.posed && hud.world && g_objOnHand) DrawObjectivesPanel(hud.fbo, fb_w, fb_h);
        if (cfg.dumpEyes && drawn && g_vrFrame && hud.world) {
            static DWORD since;
            static int done;
            if (!since) since = GetTickCount();
            else if (!done && GetTickCount() - since > 15000) {
                XR_RequestDump(g_dir);   // next to the log
                done = 1;
            }
        }
        if (drawn && g_vrFrame && g_scopeActive && hud.fbo && !g_scopeOverlay) DrawScopeMask(hud.fbo, fb_w, fb_h);
        {
            int menuUp = hud.fbo && hud.posed && !hud.world && !g_scopeActive && !g_slideFrame && (MEM(int, ADDR_cls_keyCatchers) & 2) && !MapLoaded();
        if (menuUp) {
            // the main menu: the supporters' panel outside the menu screen, on its left (the same height centre), so the
            // Load / Save lists and the other menus never cover it or get covered
            LoadSupporterPanel();
            if (g_supFbo) {
                const float panelWidth = 0.365f, gap = 0.07f;
                float pos[3], ax[3][3];
                int k;
                memcpy(ax, hud.pose.axis, sizeof(ax));
                for (k = 0; k < 3; k++)   // axis[1] is the left, axis[0] points away from the viewer
                    pos[k] = hud.pose.pos[k] + hud.pose.axis[1][k] * (hud.menuWidth * 0.5f + gap + panelWidth * 0.5f);
                static int told;
                if (!told) { told = 1; Log("supporters: the panel is shown beside the menu (menu width %.2f, centre %.2f %.2f %.2f)", hud.menuWidth, pos[0], pos[1], pos[2]); }
                XR_SetPanel(g_supFbo, g_supW, g_supH, pos, (const float (*)[3])ax, panelWidth);
            }
        }
        }
        XR_EndFrame(drawn && g_vrFrame ? fb_fbo : NULL, eye_w, eye_h, fb_w, fb_h, &hud, NULL);
        // Monitor: the left eye while playing, the HUD buffer (menus) otherwise.
        if (drawn) {
            if (hud.world) GL_Blit(fb_fbo[0], eye_w, eye_h, 0, fb_w, fb_h);
            else GL_Blit(hud.fbo, fb_w, fb_h, 0, fb_w, fb_h);
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (g_stereoFrameDrawn && fb_ok) {
        int i;
        if (g_boundFbo) BindWindow(GL_BACK);
        ResolveEyes();
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        for (i = 0; i < 2; i++) {
            // half-width side by side
            GLboolean scissor = gl.IsEnabled(GL_SCISSOR_TEST);
            int x0 = ((i ^ cfg.swapEyes) ? fb_w / 2 : 0);
            if (scissor) gl.Disable(GL_SCISSOR_TEST);
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fb_fbo[i]);
            gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            gl.BlitFramebuffer(0, 0, eye_w, eye_h, x0, 0, x0 + fb_w / 2, fb_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            if (scissor) gl.Enable(GL_SCISSOR_TEST);
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (g_boundFbo) {
        BindWindow(GL_BACK);
    }
    g_stereoFrameDrawn = 0;
    g_worldDrawn = 0;
    if (g_slideShow != g_slideFrame) Log("slides: %s", g_slideFrame ? "on" : "off");
    g_slideShow = g_slideFrame;
    g_slideFrame = 0;
    g_hudCleared = 0;
    g_scopeWasActive = g_scopeActive;
    g_scopeActive = 0;
    g_scopeOverlay = 0;
    while (gl.GetError()) {}
    return orig_SwapBuffers(dc);
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
// The launcher keeps the player's choices in %APPDATA%/CoD-VR/settings.ini (writable without
// administrator rights, unlike the game folder): a key there overrides cod_vr.ini.
static char g_userIni[MAX_PATH];

static int CfgInt(const char *sec, const char *key, int def, const char *ini)
{
    int v = (int)GetPrivateProfileIntA(sec, key, def, ini);
    return g_userIni[0] ? (int)GetPrivateProfileIntA(sec, key, v, g_userIni) : v;
}

static void CfgStr(const char *sec, const char *key, const char *def, char *out, DWORD size, const char *ini)
{
    char v[256];
    GetPrivateProfileStringA(sec, key, def, v, sizeof(v), ini);
    if (g_userIni[0]) GetPrivateProfileStringA(sec, key, v, out, size, g_userIni);
    else lstrcpynA(out, v, (int)size);
}

static float IniFloat(const char *sec, const char *key, float def, const char *ini)
{
    char buf[64], d[64];
    sprintf(d, "%g", def);
    CfgStr(sec, key, d, buf, sizeof(buf), ini);
    return (float)atof(buf);
}

static void LoadConfig(void)
{
    char ini[MAX_PATH];
    _snprintf(ini, sizeof(ini), "%s\\cod_vr.ini", g_dir);
    {
        const char *appdata = getenv("APPDATA");
        g_userIni[0] = 0;
        if (appdata && appdata[0]) {
            _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD-VR\\settings.ini", appdata);
            g_userIni[sizeof(g_userIni) - 1] = 0;
            if (GetFileAttributesA(g_userIni) == INVALID_FILE_ATTRIBUTES) g_userIni[0] = 0;
        }
    }
    cfg.enabled    = CfgInt("Stereo", "Enabled", cfg.enabled, ini);
    cfg.swapEyes   = CfgInt("Stereo", "SwapEyes", cfg.swapEyes, ini);
    cfg.separation = IniFloat("Stereo", "Separation", cfg.separation, ini);
    cfg.vr         = CfgInt("VR", "Enabled", cfg.vr, ini);
    cfg.worldScale = IniFloat("VR", "WorldScale", cfg.worldScale, ini);
    cfg.positional = CfgInt("VR", "PositionalTracking", cfg.positional, ini);
    cfg.headAim    = CfgInt("VR", "HeadAim", cfg.headAim, ini);
    cfg.hud                 = CfgInt("HUD", "Enabled", cfg.hud, ini);
    cfg.hudLayer.headLocked = CfgInt("HUD", "HeadLocked", cfg.hudLayer.headLocked, ini);
    cfg.hudLayer.distance   = IniFloat("HUD", "Distance", cfg.hudLayer.distance, ini);
    cfg.hudLayer.offsetY    = IniFloat("HUD", "OffsetY", cfg.hudLayer.offsetY, ini);
    cfg.hudLayer.width      = IniFloat("HUD", "Width", cfg.hudLayer.width, ini);
    cfg.hudLayer.menuWidth  = IniFloat("HUD", "MenuWidth", cfg.hudLayer.menuWidth, ini);
    cfg.controllers     = CfgInt("Controllers", "Enabled", cfg.controllers, ini);
    cfg.rightHanded     = CfgInt("Controllers", "RightHanded", cfg.rightHanded, ini);
    cfg.swapSticks      = CfgInt("Controllers", "SwapSticks", 0, ini);
    cfg.moveStick       = CfgInt("Controllers", "MoveStick", -1, ini);
    cfg.moveMode        = CfgInt("Controllers", "MoveMode", 2, ini);
    if (cfg.moveMode < 0 || cfg.moveMode > 2) cfg.moveMode = 2;
    cfg.snapTurn        = IniFloat("Controllers", "SnapTurn", cfg.snapTurn, ini);
    cfg.turnSpeed       = IniFloat("Controllers", "SmoothTurnSpeed", cfg.turnSpeed, ini);
    cfg.converge        = IniFloat("Controllers", "ConvergeDistance", cfg.converge, ini);
    cfg.weaponInHand    = CfgInt("Controllers", "WeaponInHand", cfg.weaponInHand, ini);
    cfg.weaponOffset[0] = IniFloat("Controllers", "WeaponOffsetForward", cfg.weaponOffset[0], ini);
    cfg.weaponOffset[1] = IniFloat("Controllers", "WeaponOffsetRight", cfg.weaponOffset[1], ini);
    cfg.weaponOffset[2] = IniFloat("Controllers", "WeaponOffsetUp", cfg.weaponOffset[2], ini);
    cfg.twoHanded       = CfgInt("Controllers", "TwoHanded", cfg.twoHanded, ini);
    cfg.twoHandReach    = IniFloat("Controllers", "TwoHandReach", cfg.twoHandReach, ini);
    cfg.scope           = CfgInt("Scope", "Enabled", cfg.scope, ini);
    cfg.scopeMaxFov     = IniFloat("Scope", "MaxFov", cfg.scopeMaxFov, ini);
    cfg.scopeWidth      = IniFloat("Scope", "Width", cfg.scopeWidth, ini);
    cfg.scopeDistance   = IniFloat("Scope", "Distance", cfg.scopeDistance, ini);
    cfg.twoHandGripHeight = IniFloat("Controllers", "TwoHandGripHeight", cfg.twoHandGripHeight, ini);
    cfg.logNear         = CfgInt("Debug", "LogNearEntities", cfg.logNear, ini);
    cfg.muzzleOffset[0] = IniFloat("Controllers", "MuzzleOffsetRight", cfg.muzzleOffset[0], ini);
    cfg.muzzleOffset[1] = IniFloat("Controllers", "MuzzleOffsetUp", cfg.muzzleOffset[1], ini);
    cfg.worldFlashLength = IniFloat("MuzzleFlash", "WorldFlashLength", 0.25f, ini);
    // weapons are taken from the body and held while the grip is pressed (the code for the gun
    // always in the dominant hand stays behind cfg.realistic = 0, not offered for now)
    cfg.realistic = 1;
    cfg.hipAccuracy   = CfgInt("Controllers", "HipFireAccuracy", 1, ini);
    cfg.mirrorLeft    = CfgInt("Controllers", "MirrorLeftHand", 1, ini);
    cfg.emptyPointer  = CfgInt("Controllers", "EmptyHandPointer", 0, ini);   // empty hands: the head looks (signs count)
    cfg.crosshair     = CfgInt("Controllers", "Crosshair", 0, ini);
    cfg.crosshairSize = IniFloat("Controllers", "CrosshairSize", 1.5f, ini);
    cfg.swingMelee    = CfgInt("Controllers", "SwingMelee", 1, ini);
    cfg.meleeSpeed    = IniFloat("Controllers", "MeleeSpeed", 2.2f, ini);
    cfg.renderScale   = IniFloat("VR", "RenderScale", 1.0f, ini);
    cfg.cullMargin    = IniFloat("VR", "CullMargin", 10.0f, ini);
    cfg.rightEyeFirst = CfgInt("Debug", "RightEyeFirst", 0, ini);
    cfg.dumpEyes      = CfgInt("Debug", "DumpEyes", 0, ini);
    if (cfg.rightEyeFirst) Log("debug: the right eye is drawn first");
    if (cfg.cullMargin < 0) cfg.cullMargin = 0;
    if (cfg.cullMargin > 30) cfg.cullMargin = 30;
    cfg.msaa          = CfgInt("VR", "MSAA", 4, ini);
    cfg.anisotropy    = IniFloat("VR", "Anisotropy", 16.0f, ini);
    {
        // colour styles: contrast, saturation, tint (Original leaves the game's colours alone)
        static const struct { const char *name; float contrast, sat, tint[3]; } styles[] = {
            { "original", 1.00f, 1.00f, { 1.00f, 1.00f, 1.00f } },
            { "natural",  1.05f, 1.08f, { 1.00f, 1.00f, 1.00f } },
            { "cinema",   1.10f, 0.82f, { 1.04f, 1.00f, 0.93f } },   // a muted, warm war film
            { "vivid",    1.08f, 1.25f, { 1.00f, 1.00f, 1.00f } },
        };
        char style[32];
        int k;
        cfg.ssao = IniFloat("PostFX", "AmbientOcclusion", 0.0f, ini);
        cfg.ssaoRadius = IniFloat("PostFX", "AmbientOcclusionRadius", 20.0f, ini);
        if (cfg.ssaoRadius < 2) cfg.ssaoRadius = 2;
        cfg.postfx.sharpen = IniFloat("PostFX", "Sharpen", 0.0f, ini);
        cfg.postfx.bloom = IniFloat("PostFX", "Bloom", 0.0f, ini);
        cfg.postfx.bloomThreshold = IniFloat("PostFX", "BloomThreshold", 0.7f, ini);
        CfgStr("PostFX", "Color", "original", style, sizeof(style), ini);
        cfg.postfx.contrast = 1; cfg.postfx.saturation = 1;
        cfg.postfx.tint[0] = cfg.postfx.tint[1] = cfg.postfx.tint[2] = 1;
        for (k = 0; k < (int)(sizeof(styles) / sizeof(styles[0])); k++)
            if (!_stricmp(style, styles[k].name)) {
                cfg.postfx.contrast = styles[k].contrast;
                cfg.postfx.saturation = styles[k].sat;
                memcpy(cfg.postfx.tint, styles[k].tint, sizeof(cfg.postfx.tint));
            }
        if (cfg.postfx.sharpen < 0) cfg.postfx.sharpen = 0;
        if (cfg.postfx.sharpen > 1) cfg.postfx.sharpen = 1;
        if (cfg.postfx.bloomThreshold > 0.95f) cfg.postfx.bloomThreshold = 0.95f;
    }
    if (cfg.msaa < 1) cfg.msaa = 1;
    cfg.haptics          = IniFloat("Controllers", "Haptics", 0.6f, ini);
    cfg.hapShots         = IniFloat("Controllers", "HapticsShots", cfg.haptics, ini);
    cfg.hapDamage        = IniFloat("Controllers", "HapticsDamage", cfg.haptics, ini);
    cfg.hapHolster       = IniFloat("Controllers", "HapticsHolsters", cfg.haptics, ini);
    cfg.holsterHum       = CfgInt("Controllers", "HolsterHum", 1, ini);
    cfg.objGesture       = CfgInt("HUD", "ObjectivesGesture", 1, ini);
    cfg.objWatchWidth    = IniFloat("HUD", "ObjectivesWatchWidth", 0.34f, ini);
    cfg.physCrouch       = CfgInt("Controllers", "PhysicalCrouch", 1, ini);
    cfg.crouchDepth      = IniFloat("Controllers", "CrouchDepth", 0.32f, ini);
    cfg.heightOffset     = IniFloat("VR", "HeightOffset", 0.0f, ini);
    cfg.twoHandStab      = IniFloat("Controllers", "TwoHandStabilization", 0.75f, ini);
    cfg.twoHandKick      = IniFloat("Controllers", "TwoHandRecoilReduction", 0.8f, ini);
    if (cfg.twoHandStab < 0) cfg.twoHandStab = 0;
    if (cfg.twoHandStab > 1) cfg.twoHandStab = 1;
    if (cfg.twoHandKick < 0) cfg.twoHandKick = 0;
    if (cfg.twoHandKick > 1) cfg.twoHandKick = 1;
    if (cfg.crouchDepth < 0.12f) cfg.crouchDepth = 0.12f;
    cfg.vignette         = CfgInt("Comfort", "Vignette", 1, ini);
    cfg.vehicleVignette  = IniFloat("Comfort", "VehicleVignette", 0.6f, ini);
    cfg.vignetteStrength = IniFloat("Comfort", "VignetteStrength", 0.6f, ini);
    cfg.vignetteSpeed    = IniFloat("Comfort", "VignetteSpeed", 40.0f, ini);
    cfg.vignetteTurn     = IniFloat("Comfort", "VignetteTurn", 45.0f, ini);
    if (cfg.vignetteStrength < 0) cfg.vignetteStrength = 0;
    if (cfg.vignetteStrength > 1) cfg.vignetteStrength = 1;
    if (cfg.vignetteSpeed < 1) cfg.vignetteSpeed = 1;
    if (cfg.vignetteTurn < 1) cfg.vignetteTurn = 1;
    XR_SetRenderScale(cfg.renderScale);
    cfg.menuFollowsHead = CfgInt("HUD", "MenuFollowsHead", 0, ini);
    cfg.objOnHand     = CfgInt("HUD", "ObjectivesOnHand", 1, ini);
    cfg.hudMode       = CfgInt("HUD", "HudMode", 0, ini);
    cfg.infiniteAmmo  = CfgInt("Mods", "InfiniteAmmo", 0, ini);
    cfg.difficulty    = CfgInt("Mods", "Difficulty", 0, ini);
    cfg.gore          = CfgInt("Mods", "Gore", 1, ini);
    cfg.modernFont    = CfgInt("HUD", "ModernFont", 1, ini);
    cfg.logHud        = CfgInt("Debug", "LogHud", 0, ini);
    cfg.objWidth      = IniFloat("HUD", "ObjectivesWidth", 0.52f, ini);
    cfg.objHeight     = IniFloat("HUD", "ObjectivesHeight", 0.15f, ini);
    cfg.objBackground = IniFloat("HUD", "ObjectivesBackground", 0.88f, ini);
    cfg.objMargin     = IniFloat("HUD", "ObjectivesMargin", 0.02f, ini);
    if (cfg.objMargin < 0) cfg.objMargin = 0;
    if (cfg.objMargin > 0.2f) cfg.objMargin = 0.2f;
    // [Holsters] (v1.2.0 called the section [Realistic]: still read as the default)
    cfg.dropOnRelease = CfgInt("Holsters", "DropOnRelease", CfgInt("Realistic", "DropOnRelease", 1, ini), ini);
    cfg.drawTime      = CfgInt("Holsters", "DrawTime", CfgInt("Realistic", "DrawTime", 50, ini), ini);
    {
        // [Holsters] zone = none | primary | primaryb | switch | pistol | grenade (the shoulders
        // were [Realistic] LeftShoulder / RightShoulder before)
        static const struct { int zone; const char *key, *def; } zones[] = {
            { ZONE_SHOULDER_L, "LeftShoulder", "primaryb" }, { ZONE_SHOULDER_R, "RightShoulder", "primary" },
            { ZONE_CHEST, "Chest", "none" }, { ZONE_WAIST_L, "WaistLeft", "pistol" }, { ZONE_WAIST_R, "WaistRight", "none" },
            { ZONE_WAIST_F, "WaistFront", "none" }, { ZONE_WAIST_B, "WaistBack", "grenade" },
        };
        static const struct { const char *name; int act; } acts[] = {
            { "none", ACT_NONE }, { "primary", ACT_W1 }, { "primaryb", ACT_W2 }, { "switch", ACT_SWITCH },
            { "pistol", ACT_PISTOL }, { "grenade", ACT_GRENADE }, { "smokegrenade", ACT_GRENADE },
        };
        char v[32], old[32];
        int z, k;
        for (z = 0; z < (int)(sizeof(zones) / sizeof(zones[0])); z++) {
            CfgStr("Realistic", zones[z].key, zones[z].def, old, sizeof(old), ini);
            CfgStr("Holsters", zones[z].key, old, v, sizeof(v), ini);
            cfg.zoneAct[zones[z].zone] = ACT_NONE;
            for (k = 0; k < (int)(sizeof(acts) / sizeof(acts[0])); k++)
                if (!_stricmp(v, acts[k].name)) cfg.zoneAct[zones[z].zone] = acts[k].act;
        }
    }
    cfg.subtitleSize = IniFloat("HUD", "SubtitleSize", 1.5f, ini);   // 1 = the game's own size
    if (cfg.subtitleSize < 0.3f) cfg.subtitleSize = 0.3f;
    if (cfg.subtitleSize > 6.0f) cfg.subtitleSize = 6.0f;
    cfg.shakeKeep = IniFloat("Comfort", "ShakeKeep", 0.4f, ini);
    if (cfg.shakeKeep < 0) cfg.shakeKeep = 0;
    if (cfg.shakeKeep > 1) cfg.shakeKeep = 1;
    cfg.vehicleSmooth = IniFloat("Comfort", "VehicleTurnSmooth", 0.5f, ini);
    ReloadTurretTuning();
    if (cfg.vehicleSmooth < 0.02f) cfg.vehicleSmooth = 0.02f;
    cfg.waistReach    = IniFloat("Holsters", "WaistReach", 0.32f, ini);
    cfg.backHeight    = IniFloat("Holsters", "BackHeight", IniFloat("Realistic", "BackHeight", -0.30f, ini), ini);
    cfg.hipHeight     = IniFloat("Holsters", "HipHeight", IniFloat("Realistic", "HipHeight", -0.75f, ini), ini);
    cfg.lowerBackHeight = IniFloat("Holsters", "LowerBackHeight", IniFloat("Realistic", "LowerBackHeight", -0.70f, ini), ini);
    cfg.roomScale       = CfgInt("VR", "RoomScale", cfg.roomScale, ini);
    cfg.headCollision   = CfgInt("VR", "HeadCollision", 1, ini);
    cfg.weaponAngle[0]  = IniFloat("Controllers", "WeaponPitch", cfg.weaponAngle[0], ini);
    cfg.weaponAngle[1]  = IniFloat("Controllers", "WeaponYaw", cfg.weaponAngle[1], ini);
    cfg.aimSmoothing    = IniFloat("Controllers", "AimSmoothing", cfg.aimSmoothing, ini);
    cfg.autoAds         = CfgInt("Controllers", "AutoADS", cfg.autoAds, ini);
    cfg.grenadeArc      = CfgInt("Grenade", "Trajectory", cfg.grenadeArc, ini);
    cfg.gravity         = IniFloat("Grenade", "Gravity", cfg.gravity, ini);
    cfg.grenadeOpacity  = IniFloat("Grenade", "Opacity", cfg.grenadeOpacity, ini);
    {
        char mask[32];
        CfgStr("Grenade", "TraceMask", "1", mask, sizeof(mask), ini);
        cfg.traceMask = (int)strtoul(mask, NULL, 0);
    }
    cfg.scopeSmoothing  = IniFloat("Scope", "Smoothing", cfg.scopeSmoothing, ini);
    cfg.menuPointer     = (float)CfgInt("Controllers", "MenuPointer", (int)cfg.menuPointer, ini);
    {
        int b;
        for (b = 0; b < BT_COUNT; b++)
            CfgStr("Buttons", g_buttons[b].key, g_buttons[b].def, cfg.bind[b], sizeof(cfg.bind[b]), ini);
    }
    {
        // shortcuts: a wildcard (X or Y right-handed, A or B left-handed) held, then another face button; keys named
        // Shortcut<held><pressed>. The default: weapon switch on Y + A (right-handed), B + X (left-handed)
        static const char *const keys[4][4] = {
            { NULL, "ShortcutAB", "ShortcutAX", "ShortcutAY" },
            { "ShortcutBA", NULL, "ShortcutBX", "ShortcutBY" },
            { "ShortcutXA", "ShortcutXB", NULL, "ShortcutXY" },
            { "ShortcutYA", "ShortcutYB", "ShortcutYX", NULL } };
        int m, f;
        for (m = 0; m < 4; m++)
            for (f = 0; f < 4; f++) {
                cfg.shortcut[m][f][0] = 0;
                if (keys[m][f])
                    CfgStr("Buttons", keys[m][f], (cfg.rightHanded ? m == FACE_Y && f == FACE_A : m == FACE_B && f == FACE_X) ? "vr:switchweapon" : "",
                           cfg.shortcut[m][f], sizeof(cfg.shortcut[m][f]), ini);
            }
    }
    if (cfg.enabled && !IsKnownExe()) {
        Log("not CoDSP.exe 1.3 or CoDUOSP.exe 1.51 (timestamp %08x expected): passthrough only", CODSP_TIMESTAMP);
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
    RESOLVE(DepthRange, "glDepthRange");         RESOLVE(FrontFace, "glFrontFace");
    RESOLVE(Scissor, "glScissor");              RESOLVE(BlendFunc, "glBlendFunc");
    RESOLVE(TexParameterf, "glTexParameterf");  RESOLVE(GetFloatv, "glGetFloatv");
    RESOLVE(ReadPixels, "glReadPixels");
    return TRUE;
}

// Packs the mod builds that are optional (Main\pakz_vr_gore.pk3, pakz_vr_font.pk3): the game loads every pk3 in
// Main, so while an option is off the game's file listing does not see its pack (its kernel32
// FindFirstFileA / FindNextFileA imports skip the name).
typedef HANDLE (WINAPI *PFN_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *PFN_FindNextFileA)(HANDLE, LPWIN32_FIND_DATAA);
static PFN_FindFirstFileA orig_FindFirstFileA;
static PFN_FindNextFileA orig_FindNextFileA;

static int HiddenPack(const char *name)
{
    return (cfg.gore != 2 && !_stricmp(name, "pakz_vr_gore.pk3")) ||
           (!cfg.modernFont && !_stricmp(name, "pakz_vr_font.pk3"));
}

static BOOL WINAPI hk_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd)
{
    BOOL ok;
    while ((ok = orig_FindNextFileA(h, fd)) && HiddenPack(fd->cFileName)) {}
    return ok;
}

static HANDLE WINAPI hk_FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA fd)
{
    HANDLE h = orig_FindFirstFileA(pattern, fd);
    if (h != INVALID_HANDLE_VALUE && HiddenPack(fd->cFileName) && !hk_FindNextFileA(h, fd)) {
        FindClose(h);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

// Points CoDSP.exe's import of module!func at hook; returns what was there (NULL = not found).
static void *HookImport(const char *module, const char *func, void *hook)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
    void *real = (void *)GetProcAddress(GetModuleHandleA(module), func);
    if (!dir->VirtualAddress || !real) return NULL;
    for (; imp->Name; imp++) {
        void **slot = (void **)(base + imp->FirstThunk);
        if (_stricmp((const char *)(base + imp->Name), module)) continue;
        for (; *slot; slot++)
            if (*slot == real) {
                PatchPointer((DWORD)slot, hook);
                return real;
            }
    }
    return NULL;
}

static void HideOptionalPacks(void)
{
    orig_FindFirstFileA = (PFN_FindFirstFileA)HookImport("kernel32.dll", "FindFirstFileA", (void *)hk_FindFirstFileA);
    orig_FindNextFileA = (PFN_FindNextFileA)HookImport("kernel32.dll", "FindNextFileA", (void *)hk_FindNextFileA);
    if (!orig_FindFirstFileA || !orig_FindNextFileA) Log("optional packs: file listing imports not found");
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
    {
        // The launcher's diagnostic loads the headset runtime, which loads opengl32: this DLL, next to the
        // launcher. Only the game writes cod_vr.log: the launcher must not wipe the game's last log.
        char name[MAX_PATH];
        const char *base;
        GetModuleFileNameA(NULL, name, sizeof(name));
        base = strrchr(name, '\\');
        base = base ? base + 1 : name;
        if (_stricmp(base, "CoD-VR-Launcher.exe")) {
            _snprintf(logPath, sizeof(logPath), "%s\\cod_vr.log", g_dir);
            g_log = fopen(logPath, "w");
            if (!g_log) {
                GetTempPathA(sizeof(logPath), logPath);
                strcat(logPath, "cod_vr.log");
                g_log = fopen(logPath, "w");
            }
        }
    }

    SelectGame();
    LoadConfig();
    {
        // which CoDSP.exe this is (the mod is made for 1.3: other builds put things elsewhere)
        BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
        char exePath[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA fa;
        GetModuleFileNameA(NULL, exePath, sizeof(exePath));
        GetFileAttributesExA(exePath, GetFileExInfoStandard, &fa);
        Log("game: %s, timestamp %08lx (1.3 is %08x), %lu bytes%s", exePath, nt->FileHeader.TimeDateStamp,
            CODSP_TIMESTAMP, fa.nFileSizeLow, nt->FileHeader.TimeDateStamp == CODSP_TIMESTAMP ? "" : " - NOT 1.3");
    }
    Log("cod_vr phase 4 loaded: enabled=%d vr=%d separation=%.2f worldScale=%.2f positional=%d headAim=%d "
        "hud=%d (distance %.2f, width %.2f, menu %.2f, headLocked %d)",
        cfg.enabled, cfg.vr, cfg.separation, cfg.worldScale, cfg.positional, cfg.headAim,
        cfg.hud, cfg.hudLayer.distance, cfg.hudLayer.width, cfg.hudLayer.menuWidth, cfg.hudLayer.headLocked);
    if (g_userIni[0]) Log("player settings from %s", g_userIni);
    Log("holsters: drop on release %d, back %.2f, hip %.2f, lower back %.2f, draw %d ms",
        cfg.dropOnRelease, cfg.backHeight, cfg.hipHeight, cfg.lowerBackHeight, cfg.drawTime);
    Log("controllers=%d rightHanded=%d snapTurn=%.0f converge=%.1f weaponInHand=%d offset=(%.1f %.1f %.1f)",
        cfg.controllers, cfg.rightHanded, cfg.snapTurn, cfg.converge, cfg.weaponInHand,
        cfg.weaponOffset[0], cfg.weaponOffset[1], cfg.weaponOffset[2]);
    if (!LoadRealOpenGL()) return FALSE;
    if (cfg.gore != 2 || !cfg.modernFont) HideOptionalPacks();
    Log("blood: %s", cfg.gore == 2 ? "gore" : cfg.gore ? "on" : "off");
    EnsureHooks();
    return TRUE;
}
