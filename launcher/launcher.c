// Call of Duty VR launcher: starts CoDSP.exe and lets the player change the common settings.
//
// It lives in the game folder. The player's choices go to %APPDATA%\CoD-VR\settings.ini, which
// the mod reads on top of cod_vr.ini (the game folder is usually under Program Files, where
// writing needs administrator rights). Graphics settings of the game itself are passed on the
// command line when it is started (+set ...), so they hold whatever the game saved before.
//
// Plain Win32 + GDI, drawn by hand: a banner, a bar of buttons and pages of options.
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "launcher_res.h"

#define VERSION_TEXT L"v1.5.0"
#define URL_GITHUB   "https://github.com/lambda-gab/cod1-vr"
#define URL_DISCORD  "https://discord.gg/SRsK2x5Tn"

// ---------------------------------------------------------------------------
// Layout, in pixels at 96 dpi (scaled by P())
// ---------------------------------------------------------------------------
#define BASE_W   1000
#define BASE_H   560
#define BANNER_H 364
#define BAR_Y    480

static float g_scale = 1.0f;
#define P(v) ((int)((v) * g_scale + 0.5f))

#define COL_GOLD      RGB(214, 170, 62)
#define COL_GOLD_DARK RGB(150, 112, 30)
#define COL_STEEL     RGB(170, 205, 220)
#define COL_TEXT      RGB(225, 225, 225)
#define COL_DIM       RGB(140, 140, 140)
#define COL_BAR       RGB(14, 14, 14)

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
// One option: choices are "key=value|key=value" lines written to [section] of settings.ini.
typedef struct {
    const wchar_t *label, *hint;
    const char *section;
    int count;
    const wchar_t *names[10];
    const char *values[10];
    int def;
    int cur;      // -1 = what is set matches no choice ("Custom")
    int showRes;  // RenderScale choices: show the eye resolution each one gives
    int reticle;  // crosshair styles: a small picture of each one beside its name
    const wchar_t *const *choiceHints;   // a hint for each choice (instead of hint), or NULL
} option_t;

static const wchar_t *const g_hudHints[] = {
    L"Everything on screen, as in the original game.",
    L"Nothing on screen. Hold the Objectives button to see health, ammo, compass and objectives.",
    L"Only the dialogue subtitles. Hold the Objectives button to see health, ammo and compass.",
    L"Only the compass. Hold the Objectives button to see health, ammo and subtitles.",
    L"Only health and ammo. Hold the Objectives button to see the compass, stance and subtitles.",
};

// VR page, two columns: comfort and movement on the left (VR_COL2 first on the right)
static option_t g_vrOpts[] = {
    { L"Comfort vignette", L"Narrows the view while you move with the stick or turn smoothly. Helps if you feel sick.",
      "Comfort", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "Vignette=0", "Vignette=1|VignetteStrength=0.35", "Vignette=1|VignetteStrength=0.6", "Vignette=1|VignetteStrength=0.9" }, 2 },
    { L"Turning", L"Right stick: turn in steps (gentler) or smoothly.",
      "Controllers", 4, { L"Snap 30\x00b0", L"Snap 45\x00b0", L"Snap 90\x00b0", L"Smooth" },
      { "SnapTurn=30", "SnapTurn=45", "SnapTurn=90", "SnapTurn=0" }, 1 },
    { L"Smooth turn speed", L"Only used with smooth turning.",
      "Controllers", 3, { L"Slow", L"Normal", L"Fast" },
      { "SmoothTurnSpeed=90", "SmoothTurnSpeed=150", "SmoothTurnSpeed=220" }, 1 },
    { L"Dominant hand", L"Which stick turns and which hand points in the menus. Weapons go to either hand.",
      "Controllers", 2, { L"Right", L"Left" }, { "RightHanded=1", "RightHanded=0" }, 0 },
    { L"Roomscale", L"Walk and lean for real: the soldier follows your body.",
      "VR", 2, { L"On", L"Off" }, { "RoomScale=1", "RoomScale=0" }, 0 },
    { L"Physical crouch", L"Crouching for real crouches the soldier. Recenter (left stick click) standing up straight.",
      "Controllers", 2, { L"On", L"Off" }, { "PhysicalCrouch=1", "PhysicalCrouch=0" }, 0 },
    { L"Crouch depth", L"How low your head goes to crouch the soldier.",
      "Controllers", 3, { L"Shallow 25 cm", L"Normal 32 cm", L"Deep 45 cm" },
      { "CrouchDepth=0.25", "CrouchDepth=0.32", "CrouchDepth=0.45" }, 1 },
    { L"Menus", L"Menus and loading screens: fixed in front of you, or following your head.",
      "HUD", 2, { L"Fixed", L"Follow head" }, { "MenuFollowsHead=0", "MenuFollowsHead=1" }, 0 },
    { L"HUD", NULL, "HUD", 5, { L"Full", L"Immersive (none)", L"Subtitles only", L"Compass only", L"Health and ammo" },
      { "HudMode=0", "HudMode=1", "HudMode=2", "HudMode=3", "HudMode=4" }, 0, 0, 0, 0, g_hudHints },
    // right column
    { L"Grenade line", L"A line that shows where a grenade will fly and land.",
      "Grenade", 3, { L"Off", L"While aiming", L"Always" }, { "Trajectory=0", "Trajectory=1", "Trajectory=2" }, 1 },
    { L"Crosshair", L"A crosshair where the gun is aimed, for playing without aiming down the sights.",
      "Controllers", 6, { L"None", L"Cross", L"Dot", L"Circle", L"Circle + dot", L"Cross + dot" },
      { "Crosshair=0", "Crosshair=1", "Crosshair=2", "Crosshair=3", "Crosshair=4", "Crosshair=5" }, 0, 0, 0, 1 },
    { L"Swing melee", L"Hit someone by swinging the gun at them, any part of it. Hard needs a faster swing.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Hard" },
      { "SwingMelee=0", "SwingMelee=1|MeleeSpeed=1.6", "SwingMelee=1|MeleeSpeed=2.2", "SwingMelee=1|MeleeSpeed=3" }, 2 },
    { L"Drop on release", L"Letting go of the grip away from its holster drops the weapon on the floor.",
      "Holsters", 2, { L"On", L"Off" }, { "DropOnRelease=1", "DropOnRelease=0" }, 0 },
    { L"Two-hand steadiness", L"With both hands on the gun, hand tremor is damped; deliberate swings still follow quickly.",
      "Controllers", 4, { L"Off", L"Low", L"Normal", L"High" },
      { "TwoHandStabilization=0", "TwoHandStabilization=0.4", "TwoHandStabilization=0.75", "TwoHandStabilization=1" }, 2 },
    { L"Two-hand recoil", L"How much the gun kicks while you hold it with both hands.",
      "Controllers", 3, { L"Full", L"Reduced", L"Minimal" },
      { "TwoHandRecoilReduction=0", "TwoHandRecoilReduction=0.5", "TwoHandRecoilReduction=0.8" }, 2 },
    { L"Objectives on hand", L"The objectives (X button) show above your left hand instead of in front of you.",
      "HUD", 2, { L"On", L"Off" }, { "ObjectivesOnHand=1", "ObjectivesOnHand=0" }, 0 },
    { L"Vibration: shots", L"Controller vibration when you fire.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Strong" },
      { "HapticsShots=0", "HapticsShots=0.35", "HapticsShots=0.6", "HapticsShots=1" }, 2 },
    { L"Vibration: hits", L"Both controllers shake when you are hit, harder for more damage.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Strong" },
      { "HapticsDamage=0", "HapticsDamage=0.35", "HapticsDamage=0.6", "HapticsDamage=1" }, 2 },
    { L"Vibration: holsters", L"Reaching a holster, taking, putting away and dropping weapons, recentering.",
      "Controllers", 4, { L"Off", L"Light", L"Normal", L"Strong" },
      { "HapticsHolsters=0", "HapticsHolsters=0.35", "HapticsHolsters=0.6", "HapticsHolsters=1" }, 2 },
};
#define VR_COL2 9

static option_t g_gfxOpts[] = {
    { L"Headset resolution", L"Sharpness in the headset, per eye. Lower it if the game stutters; above Ultra needs a strong GPU.",
      "VR", 9, { L"Potato (50%)", L"Low (70%)", L"Medium (85%)", L"Native (100%)", L"High (120%)", L"Ultra (140%)",
                 L"Eagle Eye (160%)", L"Sniper Vision (180%)", L"GPU Furnace (200%)" },
      { "RenderScale=0.5", "RenderScale=0.7", "RenderScale=0.85", "RenderScale=1", "RenderScale=1.2", "RenderScale=1.4",
        "RenderScale=1.6", "RenderScale=1.8", "RenderScale=2" }, 3, 0, 1 },
    { L"Antialiasing", L"Smooths jagged edges and the shimmer of fences and wires. 4x suits most PCs; 8x is heavier.",
      "VR", 4, { L"Off", L"2x MSAA", L"4x MSAA", L"8x MSAA" }, { "MSAA=1", "MSAA=2", "MSAA=4", "MSAA=8" }, 2 },
    { L"Texture filtering", L"Keeps floors and walls sharp when seen at an angle. Almost free on any recent GPU.",
      "VR", 4, { L"Off", L"4x anisotropic", L"8x anisotropic", L"16x anisotropic" },
      { "Anisotropy=1", "Anisotropy=4", "Anisotropy=8", "Anisotropy=16" }, 3 },
    { L"Texture quality", L"Detail of the game's textures. Maximum suits any recent PC.",
      "Launcher", 3, { L"Maximum", L"High", L"Medium" }, { "Picmip=0", "Picmip=1", "Picmip=2" }, 0 },
    { L"Brightness", L"Makes the game brighter in the headset (applied when the game starts).",
      "Launcher", 4, { L"Normal", L"Brighter", L"Bright", L"Brightest" },
      { "Gamma=1", "Gamma=1.2", "Gamma=1.4", "Gamma=1.6" }, 0 },
    // post-processing, in its own section from here (GFX_POST_FIRST)
    { L"Ambient occlusion", L"Soft contact shadows where surfaces meet: corners, under objects, around crates.",
      "PostFX", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "AmbientOcclusion=0", "AmbientOcclusion=0.4", "AmbientOcclusion=0.65", "AmbientOcclusion=0.9" }, 0 },
    { L"Sharpening", L"Gives the old textures their definition back, without halos.",
      "PostFX", 4, { L"Off", L"Low", L"Medium", L"High" }, { "Sharpen=0", "Sharpen=0.3", "Sharpen=0.6", "Sharpen=1" }, 0 },
    { L"Color style", L"Original: the game's own colours. Cinema: muted and warm, like a war film.",
      "PostFX", 4, { L"Original", L"Natural", L"Cinema", L"Vivid" },
      { "Color=original", "Color=natural", "Color=cinema", "Color=vivid" }, 0 },
    { L"Bloom", L"A soft glow around bright light: fire, explosions, the sky.",
      "PostFX", 3, { L"Off", L"Low", L"Medium" }, { "Bloom=0", "Bloom=0.15", "Bloom=0.3" }, 0 },
};
#define GFX_POST_FIRST 5

#define NUM_VR  (int)(sizeof(g_vrOpts) / sizeof(g_vrOpts[0]))
#define NUM_GFX (int)(sizeof(g_gfxOpts) / sizeof(g_gfxOpts[0]))

// MODS: things the original game does not have
static const wchar_t *const g_difficultyHints[] = {
    L"The game's own difficulty (chosen when you start a new game).",
    L"Real life: one shot, one kill. For you and for them.",
    L"Perma death: if you die, the run is over. The checkpoints are erased and you start a new game (your own saves go to a backup folder).",
    L"Real life with perma death: one shot kills, and dying ends the run.",
    L"God mode: you never die.",
};
static option_t g_modOpts[] = {
    { L"Custom difficulty", NULL, "Mods", 5, { L"Normal", L"Real life", L"Perma death", L"Real life + perma death", L"God mode" },
      { "Difficulty=0", "Difficulty=1", "Difficulty=2", "Difficulty=3", "Difficulty=4" }, 0, 0, 0, 0, g_difficultyHints },
    { L"Blood", L"Off: no blood. On: the game's own blood. Gore: much more blood, spraying and falling from every hit.",
      "Mods", 3, { L"Off", L"On", L"Gore" }, { "Gore=0", "Gore=1", "Gore=2" }, 1 },
    { L"Infinite ammo", L"Full ammo: the reserve stays full (you still reload), or the clip too (no reloading).",
      "Mods", 3, { L"Off", L"Full reserve", L"No reloading" }, { "InfiniteAmmo=0", "InfiniteAmmo=1", "InfiniteAmmo=2" }, 0 },
};
#define NUM_MOD (int)(sizeof(g_modOpts) / sizeof(g_modOpts[0]))
#define MOD_DIFFICULTY 0

static char g_dir[MAX_PATH], g_gameIni[MAX_PATH], g_userIni[MAX_PATH];
static int  g_haveGame, g_haveMod;

// The value of a key as the mod will see it: settings.ini, else cod_vr.ini, else "".
static void ReadSetting(const char *sec, const char *key, char *out, int size)
{
    char v[128];
    GetPrivateProfileStringA(sec, key, "", v, sizeof(v), g_gameIni);
    GetPrivateProfileStringA(sec, key, v, out, size, g_userIni);
}

static int SameValue(const char *a, const char *b)
{
    char *ea, *eb;
    double x = strtod(a, &ea), y = strtod(b, &eb);
    if (*a && *b && !*ea && !*eb) return x - y < 1e-4 && y - x < 1e-4;
    return !_stricmp(a, b);
}

// Does every key=value of a choice hold?
static int ChoiceMatches(const char *sec, const char *pairs, int isDefault)
{
    char buf[256], *tok, *ctx = NULL;
    strncpy(buf, pairs, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (tok = strtok_s(buf, "|", &ctx); tok; tok = strtok_s(NULL, "|", &ctx)) {
        char *eq = strchr(tok, '='), cur[128];
        if (!eq) continue;
        *eq = 0;
        ReadSetting(sec, tok, cur, sizeof(cur));
        if (!cur[0]) { if (!isDefault) return 0; continue; }   // unset: the default choice holds
        if (!SameValue(cur, eq + 1)) return 0;
    }
    return 1;
}

static void LoadOption(option_t *o)
{
    int i;
    o->cur = -1;
    for (i = 0; i < o->count; i++)
        if (ChoiceMatches(o->section, o->values[i], i == o->def)) { o->cur = i; break; }
}

static void SaveOption(option_t *o)
{
    char buf[256], *tok, *ctx = NULL, dir[MAX_PATH], *slash;
    if (o->cur < 0) return;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
    strncpy(buf, o->values[o->cur], sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (tok = strtok_s(buf, "|", &ctx); tok; tok = strtok_s(NULL, "|", &ctx)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        WritePrivateProfileStringA(o->section, tok, eq + 1, g_userIni);
    }
}

static void LoadAll(void)
{
    int i;
    {
        // v1.3 had one vibration setting: it becomes the default of the three kinds
        char old[16], cur[16];
        GetPrivateProfileStringA("Controllers", "Haptics", "", old, sizeof(old), g_userIni);
        GetPrivateProfileStringA("Controllers", "HapticsShots", "", cur, sizeof(cur), g_userIni);
        if (old[0] && !cur[0]) {
            WritePrivateProfileStringA("Controllers", "HapticsShots", old, g_userIni);
            WritePrivateProfileStringA("Controllers", "HapticsDamage", old, g_userIni);
            WritePrivateProfileStringA("Controllers", "HapticsHolsters", old, g_userIni);
        }
    }
    for (i = 0; i < NUM_VR; i++) LoadOption(&g_vrOpts[i]);
    for (i = 0; i < NUM_GFX; i++) LoadOption(&g_gfxOpts[i]);
    {
        // the first MODS had "Infinite health": now the God mode difficulty
        if (GetPrivateProfileIntA("Mods", "InfiniteHealth", 0, g_userIni) == 1 &&
            GetPrivateProfileIntA("Mods", "Difficulty", 0, g_userIni) == 0)
            WritePrivateProfileStringA("Mods", "Difficulty", "4", g_userIni);
        WritePrivateProfileStringA("Mods", "InfiniteHealth", NULL, g_userIni);
    }
    for (i = 0; i < NUM_MOD; i++) LoadOption(&g_modOpts[i]);
}

// ---------------------------------------------------------------------------
// Starting the game
// ---------------------------------------------------------------------------
// The game started from here, while it runs (the launcher stays open to try settings).
static HANDLE g_game;

// A CoDSP.exe from this folder is running (also when Steam started it). Looked up at most once a second.
static int GameProcessRunning(void)
{
    static DWORD last;
    static int running;
    PROCESSENTRY32W pe;
    HANDLE snap;
    BOOL ok;
    if (last && GetTickCount() - last < 1000) return running;
    last = GetTickCount();
    running = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    for (ok = Process32FirstW(snap, &pe); ok && !running; ok = Process32NextW(snap, &pe)) {
        HANDLE h;
        wchar_t path[MAX_PATH], dir[MAX_PATH];
        DWORD len = MAX_PATH;
        if (_wcsicmp(pe.szExeFile, L"CoDSP.exe")) continue;
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) { running = 1; break; }   // cannot tell where it is from: take it as ours
        if (QueryFullProcessImageNameW(h, 0, path, &len)) {
            wchar_t *slash = wcsrchr(path, L'\\');
            if (slash) *slash = 0;
            MultiByteToWideChar(CP_ACP, 0, g_dir, -1, dir, MAX_PATH);
            running = !_wcsicmp(path, dir);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return running;
}

static int GameRunning(void)
{
    if (g_game && WaitForSingleObject(g_game, 0) == WAIT_TIMEOUT) return 1;
    if (g_game) { CloseHandle(g_game); g_game = NULL; }
    return GameProcessRunning();
}

// The Steam version's CoDSP.exe only runs when Steam starts it (it reopens itself through Steam,
// or asks for Steam when it is not there): it is started with a steam:// link, which keeps the
// command line. Any other copy (CD, other stores) is started directly.
#define STEAM_APP_ID "2620"
static int SteamCopy(void)
{
    char p[MAX_PATH], lower[MAX_PATH];
    int i;
    for (i = 0; g_dir[i] && i < MAX_PATH - 1; i++) lower[i] = (char)tolower((unsigned char)g_dir[i]);
    lower[i] = 0;
    if (strstr(lower, "\\steamapps\\")) return 1;
    _snprintf(p, sizeof(p), "%s\\steam_api.dll", g_dir);
    if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) return 1;
    _snprintf(p, sizeof(p), "%s\\steam_appid.txt", g_dir);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static int SteamInstalled(void)
{
    HKEY k;
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "steam\\Shell\\Open\\Command", 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    RegCloseKey(k);
    return 1;
}

// steam://run/<app>//<arguments>/, spaces as %20
static int StartThroughSteam(HWND wnd, const char *args)
{
    char url[1024];
    int n = _snprintf(url, sizeof(url), "steam://run/" STEAM_APP_ID "//");
    for (; *args && n < (int)sizeof(url) - 5; args++) {
        if (*args == ' ') { memcpy(url + n, "%20", 3); n += 3; }
        else url[n++] = *args;
    }
    url[n++] = '/';
    url[n] = 0;
    return (INT_PTR)ShellExecuteA(wnd, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
}

static int StartGame(HWND wnd)
{
    char exe[MAX_PATH], cmd[512], args[400], picmip[16], gamma[16];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    if (GameRunning()) {
        MessageBoxW(wnd, L"The game is already running. Settings changed here apply the next time it starts.",
                    L"Call of Duty VR", MB_ICONINFORMATION);
        return 0;
    }
    _snprintf(exe, sizeof(exe), "%s\\CoDSP.exe", g_dir);
    GetPrivateProfileStringA("Launcher", "Picmip", "0", picmip, sizeof(picmip), g_userIni);
    GetPrivateProfileStringA("Launcher", "Gamma", "1", gamma, sizeof(gamma), g_userIni);
    // r_ignorehwgamma: the game's own brightness is a ramp on the monitor, which the headset never
    // sees; with it ignored, r_gamma brightens the textures themselves
    // full colour depth and trilinear filtering for the textures as well
    _snprintf(args, sizeof(args), "+set r_picmip %d +set r_texturebits 32 +set r_textureMode GL_LINEAR_MIPMAP_LINEAR"
              " +set r_ignorehwgamma 1 +set r_gamma %.2f", atoi(picmip), atof(gamma));
    if (SteamCopy() && SteamInstalled()) {
        if (StartThroughSteam(wnd, args)) return 1;
        MessageBoxW(wnd, L"Could not start the game through Steam. Is Steam installed and logged in?",
                    L"Call of Duty VR", MB_ICONERROR);
        return 0;
    }
    _snprintf(cmd, sizeof(cmd), "\"%s\" %s", exe, args);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
        MessageBoxW(wnd, L"Could not start CoDSP.exe.", L"Call of Duty VR", MB_ICONERROR);
        return 0;
    }
    CloseHandle(pi.hThread);
    g_game = pi.hProcess;
    return 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
enum { PAGE_HOME, PAGE_CONTROLS, PAGE_GRAPHICS, PAGE_VR, PAGE_MODS, PAGE_ABOUT };
enum { HIT_NONE, HIT_PLAY, HIT_NAV, HIT_PREV, HIT_NEXT, HIT_ROW, HIT_LINK };

typedef struct { RECT r; int type, arg; } hit_t;
static hit_t   g_hits[64];
static int     g_numHits, g_hover = -1, g_page = PAGE_HOME;
static HBITMAP g_banner;
static int     g_bannerW, g_bannerH;
static HFONT   g_fTitle, g_fNav, g_fPlay, g_fText, g_fSmall, g_fBold;

static void AddHit(int x0, int y0, int x1, int y1, int type, int arg)
{
    hit_t *h;
    if (g_numHits >= (int)(sizeof(g_hits) / sizeof(g_hits[0]))) return;
    h = &g_hits[g_numHits++];
    SetRect(&h->r, x0, y0, x1, y1);
    h->type = type;
    h->arg = arg;
}

static int Hovered(int type, int arg)
{
    return g_hover >= 0 && g_hover < g_numHits && g_hits[g_hover].type == type && g_hits[g_hover].arg == arg;
}

static void Fill(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(c);
    SetRect(&r, x0, y0, x1, y1);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void Text(HDC dc, HFONT f, COLORREF c, int x0, int y0, int x1, int y1, const wchar_t *s, UINT flags)
{
    RECT r;
    SetRect(&r, x0, y0, x1, y1);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

// A dark veil over the banner for the option pages.
static void Veil(HDC dc, int x0, int y0, int x1, int y1, BYTE alpha)
{
    HDC src = CreateCompatibleDC(dc);
    HBITMAP one = CreateCompatibleBitmap(dc, 1, 1);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    HGDIOBJ old = SelectObject(src, one);
    SetPixel(src, 0, 0, RGB(6, 8, 10));
    AlphaBlend(dc, x0, y0, x1 - x0, y1 - y0, src, 0, 0, 1, 1, bf);
    SelectObject(src, old);
    DeleteObject(one);
    DeleteDC(src);
}

static void DrawBanner(HDC dc)
{
    Fill(dc, 0, 0, P(BASE_W), P(BAR_Y), RGB(0, 0, 0));
    if (g_banner) {
        HDC src = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(src, g_banner);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, 0, 0, P(BASE_W), P(BANNER_H), src, 0, 0, g_bannerW, g_bannerH, SRCCOPY);
        SelectObject(src, old);
        DeleteDC(src);
    }
}

static void DrawHome(HDC dc)
{
    Text(dc, g_fText, COL_TEXT, P(40), P(384), P(960), P(414),
         L"The Call of Duty (2003) single player campaign, in virtual reality.", DT_CENTER | DT_SINGLELINE);
    if (!g_haveGame)
        Text(dc, g_fSmall, RGB(230, 110, 90), P(40), P(420), P(960), P(470),
             L"CoDSP.exe was not found: put this launcher in the game folder (where CoDSP.exe is).", DT_CENTER | DT_WORDBREAK);
    else if (!g_haveMod)
        Text(dc, g_fSmall, RGB(230, 110, 90), P(40), P(420), P(960), P(470),
             L"The VR mod is not installed in this folder: run CoD-VR-Setup.exe first.", DT_CENTER | DT_WORDBREAK);
    else
        Text(dc, g_fSmall, COL_DIM, P(40), P(420), P(960), P(470),
             L"Connect your headset (Virtual Desktop, Air Link or SteamVR with a 32-bit OpenXR runtime), then press PLAY.",
             DT_CENTER | DT_WORDBREAK);
}

static void DrawPageTitle(HDC dc, const wchar_t *title, const wchar_t *sub)
{
    Text(dc, g_fTitle, COL_GOLD, P(50), P(22), P(950), P(62), title, DT_SINGLELINE);
    if (sub) Text(dc, g_fSmall, COL_DIM, P(50), P(62), P(950), P(84), sub, DT_SINGLELINE);
}

// Rows opts[first .. first + n - 1] from y = top; returns the one under the mouse, or -1.
static int DrawRows(HDC dc, option_t *opts, int first, int n, int rowH, int top)
{
    int j, hint = -1;
    for (j = 0; j < n; j++) {
        int i = first + j;
        option_t *o = &opts[i];
        int y0 = P(top + j * rowH), y1 = P(top + (j + 1) * rowH - 4);
        int vx0 = P(560), vx1 = P(950);
        int hot = Hovered(HIT_ROW, i) || Hovered(HIT_PREV, i) || Hovered(HIT_NEXT, i);
        const wchar_t *val = o->cur >= 0 ? o->names[o->cur] : L"Custom";
        wchar_t withRes[96];
        if (o->showRes && o->cur >= 0) {
            // the eye size this gives, from what the headset asked for last time the game ran
            int rw = GetPrivateProfileIntA("Headset", "RecommendedWidth", 0, g_userIni);
            int rh = GetPrivateProfileIntA("Headset", "RecommendedHeight", 0, g_userIni);
            int mw = GetPrivateProfileIntA("Headset", "MaxWidth", 0, g_userIni);
            int mh = GetPrivateProfileIntA("Headset", "MaxHeight", 0, g_userIni);
            double sc = atof(strchr(o->values[o->cur], '=') + 1);
            if (rw > 0 && rh > 0) {
                int w = (int)(rw * sc + 0.5), h = (int)(rh * sc + 0.5);
                if (mw > 0 && w > mw) w = mw;
                if (mh > 0 && h > mh) h = mh;
                _snwprintf(withRes, 96, L"%s   %d\x00d7%d", val, w, h);
                withRes[95] = 0;
                val = withRes;
            }
        }
        if (hot) { Fill(dc, P(40), y0, P(960), y1, RGB(34, 30, 20)); hint = i; }
        Text(dc, g_fText, COL_TEXT, P(60), y0, P(540), y1, o->label, DT_SINGLELINE | DT_VCENTER);
        Text(dc, g_fBold, Hovered(HIT_PREV, i) ? COL_GOLD : COL_DIM, vx0, y0, vx0 + P(40), y1, L"\x25C0", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        Text(dc, g_fBold, COL_STEEL, vx0 + P(40), y0, vx1 - P(40), y1, val, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        Text(dc, g_fBold, Hovered(HIT_NEXT, i) ? COL_GOLD : COL_DIM, vx1 - P(40), y0, vx1, y1, L"\x25B6", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(vx0, y0, vx0 + P(40), y1, HIT_PREV, i);
        AddHit(vx1 - P(40), y0, vx1, y1, HIT_NEXT, i);
        AddHit(P(40), y0, P(960), y1, HIT_ROW, i);
    }
    return hint;
}

// A crosshair style (as the mod draws it: arms around a gap, a ring, a dot) in a dark square.
static void DrawReticle(HDC dc, int style, int x0, int y0, int sz)
{
    int cx = x0 + sz / 2, cy = y0 + sz / 2, r = sz * 7 / 16, k;
    int cross = style == 1 || style == 5, dot = style == 2 || style == 4 || style == 5, ring = style == 3 || style == 4;
    HPEN pen = CreatePen(PS_SOLID, P(1) > 1 ? P(1) : 1, RGB(235, 235, 235));
    HBRUSH white = CreateSolidBrush(RGB(235, 235, 235));
    HGDIOBJ oldPen, oldBrush;
    Fill(dc, x0, y0, x0 + sz, y0 + sz, RGB(52, 56, 50));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (style == 0) {   // none: a struck-through square
        MoveToEx(dc, x0 + sz / 4, y0 + sz * 3 / 4, NULL);
        LineTo(dc, x0 + sz * 3 / 4, y0 + sz / 4);
    }
    if (cross)
        for (k = 0; k < 4; k++) {
            int dx = k == 0 ? 1 : k == 1 ? -1 : 0, dy = k == 2 ? 1 : k == 3 ? -1 : 0;
            MoveToEx(dc, cx + dx * r * 45 / 100, cy + dy * r * 45 / 100, NULL);
            LineTo(dc, cx + dx * (r + 1), cy + dy * (r + 1));
        }
    if (ring) {
        int rr = r * 7 / 10;
        Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
    }
    if (dot) {
        int d = r / 4 > 1 ? r / 4 : 1;
        SelectObject(dc, white);
        Ellipse(dc, cx - d, cy - d, cx + d + 1, cy + d + 1);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(white);
    DeleteObject(pen);
}

// Rows in a column from x0 to x1 (label, then the value with its arrows); returns the hovered one.
static int DrawColumn(HDC dc, option_t *opts, int first, int n, int rowH, int top, int x0, int x1)
{
    int j, hint = -1, vx0 = P(x0 + 215), vx1 = P(x1);
    for (j = 0; j < n; j++) {
        int i = first + j;
        option_t *o = &opts[i];
        int y0 = P(top + j * rowH), y1 = P(top + (j + 1) * rowH - 4);
        int hot = Hovered(HIT_ROW, i) || Hovered(HIT_PREV, i) || Hovered(HIT_NEXT, i);
        const wchar_t *val = o->cur >= 0 ? o->names[o->cur] : L"Custom";
        if (hot) { Fill(dc, P(x0), y0, P(x1), y1, RGB(34, 30, 20)); hint = i; }
        Text(dc, g_fText, COL_TEXT, P(x0 + 12), y0, vx0, y1, o->label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        Text(dc, g_fBold, Hovered(HIT_PREV, i) ? COL_GOLD : COL_DIM, vx0, y0, vx0 + P(26), y1, L"\x25C0", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (o->reticle && o->cur >= 0) {   // the picture, then the name
            int sz = y1 - y0 - P(6);
            DrawReticle(dc, o->cur, vx0 + P(34), y0 + P(3), sz);
            Text(dc, g_fBold, COL_STEEL, vx0 + P(42) + sz, y0, vx1 - P(26), y1, val, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        } else
        Text(dc, g_fBold, COL_STEEL, vx0 + P(26), y0, vx1 - P(26), y1, val, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        Text(dc, g_fBold, Hovered(HIT_NEXT, i) ? COL_GOLD : COL_DIM, vx1 - P(26), y0, vx1, y1, L"\x25B6", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(vx0, y0, vx0 + P(26), y1, HIT_PREV, i);
        AddHit(vx1 - P(26), y0, vx1, y1, HIT_NEXT, i);
        AddHit(P(x0), y0, P(x1), y1, HIT_ROW, i);
    }
    return hint;
}

static void DrawHint(HDC dc, option_t *opts, int hint)
{
    if (hint >= 0)
        Text(dc, g_fSmall, COL_DIM, P(50), P(444), P(950), P(476),
             opts[hint].choiceHints && opts[hint].cur >= 0 ? opts[hint].choiceHints[opts[hint].cur] : opts[hint].hint,
             DT_SINGLELINE | DT_END_ELLIPSIS);
    else
        Text(dc, g_fSmall, COL_DIM, P(50), P(444), P(950), P(476),
             L"Click a setting to change it. Changes are saved at once and used the next time the game starts.", DT_SINGLELINE);
}

static void DrawOptions(HDC dc, option_t *opts, int n, int rowH, int top)
{
    DrawHint(dc, opts, DrawRows(dc, opts, 0, n, rowH, top));
}

// GRAPHICS: the main settings, then the post-processing ones under their own title.
static void DrawGraphics(HDC dc)
{
    int h1, h2;
    DrawPageTitle(dc, L"GRAPHICS", L"The game's own options (Options in the game menu) still apply on top of these.");
    h1 = DrawRows(dc, g_gfxOpts, 0, GFX_POST_FIRST, 36, 94);
    Text(dc, g_fBold, COL_GOLD, P(50), P(278), P(950), P(300), L"POST-PROCESSING", DT_SINGLELINE | DT_VCENTER);
    Text(dc, g_fSmall, COL_DIM, P(50), P(300), P(950), P(318),
         L"Test options: if the image looks worse, leave them off.", DT_SINGLELINE | DT_VCENTER);
    h2 = DrawRows(dc, g_gfxOpts, GFX_POST_FIRST, NUM_GFX - GFX_POST_FIRST, 29, 322);
    DrawHint(dc, g_gfxOpts, h1 >= 0 ? h1 : h2);
}

static void DrawControls(HDC dc)
{
    static const wchar_t *body[][2] = {
        { L"Over the right shoulder", L"Weapon 1 (primary)" },
        { L"Over the left shoulder", L"Weapon 2 (secondary)" },
        { L"At the hip", L"Pistol" },
        { L"Behind you, waist to lower back", L"Grenade" },
    };
    static const wchar_t *buttons[][2] = {
        { L"Either trigger", L"Fire" },
        { L"Empty hand grip", L"Use / pick up" },
        { L"Other grip near the gun", L"Hold it with both hands" },
        { L"Left stick", L"Move" },
        { L"Right stick", L"Turn; up / down: stance" },
        { L"A / B", L"Jump / reload" },
        { L"X", L"Objectives" },
        { L"Y", L"Melee" },
        { L"Left stick click", L"Recenter (stand up straight)" },
        { L"Right stick click", L"Go prone" },
        { L"Menu", L"Game menu" },
    };
    int i;
    DrawPageTitle(dc, L"CONTROLS", L"Touch controllers. Buttons can be changed in cod_vr.ini, [Buttons].");
    Text(dc, g_fBold, COL_GOLD, P(50), P(96), P(470), P(120), L"Take weapons from your body (grip)", DT_SINGLELINE);
    for (i = 0; i < 4; i++) {
        int y = P(126 + i * 30);
        Text(dc, g_fText, COL_TEXT, P(50), y, P(300), y + P(28), body[i][0], DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        Text(dc, g_fText, COL_STEEL, P(300), y, P(480), y + P(28), body[i][1], DT_SINGLELINE | DT_VCENTER);
    }
    Text(dc, g_fSmall, COL_DIM, P(50), P(252), P(470), P(440),
         L"Keep the grip pressed to hold the weapon. Let go where it belongs to put it away; "
         L"let go anywhere else and it falls to the floor. Grab it with an empty hand to pick it up.\n\n"
         L"Grenades: hold the trigger to see the trajectory, release to throw. "
         L"A scoped rifle zooms with the trigger of the hand that is not holding it.\n\n"
         L"In the menus, point with your hand: trigger clicks, B goes back, A confirms.",
         DT_WORDBREAK);
    Text(dc, g_fBold, COL_GOLD, P(520), P(96), P(950), P(120), L"Buttons", DT_SINGLELINE);
    for (i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); i++) {
        int y = P(126 + i * 29);
        Text(dc, g_fText, COL_TEXT, P(520), y, P(720), y + P(28), buttons[i][0], DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        Text(dc, g_fText, COL_STEEL, P(720), y, P(960), y + P(28), buttons[i][1], DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

static void DrawAbout(HDC dc)
{
    DrawPageTitle(dc, L"ABOUT", L"Call of Duty VR " VERSION_TEXT);
    Text(dc, g_fText, COL_TEXT, P(50), P(100), P(950), P(200),
         L"A free, fan-made mod that turns the original Call of Duty (2003) single player campaign into a PC VR game. "
         L"Made by a Call of Duty fan: CoD 1 was my first FPS.\n\n"
         L"The project is in development and bugs can happen. Please report them on Discord.",
         DT_WORDBREAK);
    Text(dc, g_fBold, Hovered(HIT_LINK, 0) ? COL_GOLD : COL_STEEL, P(50), P(250), P(600), P(280),
         L"GitHub: download, updates and documentation", DT_SINGLELINE);
    AddHit(P(50), P(250), P(600), P(280), HIT_LINK, 0);
    Text(dc, g_fBold, Hovered(HIT_LINK, 1) ? COL_GOLD : COL_STEEL, P(50), P(290), P(600), P(320),
         L"Discord: bugs, suggestions and news", DT_SINGLELINE);
    AddHit(P(50), P(290), P(600), P(320), HIT_LINK, 1);
    Text(dc, g_fSmall, COL_DIM, P(50), P(380), P(950), P(470),
         L"Unofficial fan project, not affiliated with or endorsed by Activision. Call of Duty is a trademark of "
         L"Activision Publishing, Inc. A legitimate copy of the game is required; no game files are included.",
         DT_WORDBREAK);
}

static void DrawBar(HDC dc)
{
    static const wchar_t *names[] = { L"CONTROLS", L"GRAPHICS", L"VR", L"MODS", L"ABOUT" };
    static const int widths[] = { 130, 130, 60, 90, 100 };
    int i, x = P(240);
    Fill(dc, 0, P(BAR_Y), P(BASE_W), P(BASE_H), COL_BAR);
    Fill(dc, 0, P(BAR_Y), P(BASE_W), P(BAR_Y) + P(2), COL_GOLD_DARK);
    // PLAY
    {
        int x0 = P(30), y0 = P(BAR_Y + 14), x1 = P(210), y1 = P(BASE_H - 14);
        int hot = Hovered(HIT_PLAY, 0), running = GameRunning();
        Fill(dc, x0, y0, x1, y1, running ? COL_GOLD_DARK : hot ? RGB(236, 192, 80) : COL_GOLD);
        Text(dc, running ? g_fNav : g_fPlay, RGB(20, 16, 6), x0, y0, x1, y1, running ? L"RUNNING" : L"PLAY",
             DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(x0, y0, x1, y1, HIT_PLAY, 0);
    }
    for (i = 0; i < 5; i++) {
        int page = PAGE_CONTROLS + i, w = P(widths[i]);
        int on = g_page == page, hot = Hovered(HIT_NAV, page);
        int y0 = P(BAR_Y + 14), y1 = P(BASE_H - 14);
        Text(dc, g_fNav, on || hot ? COL_GOLD : COL_TEXT, x, y0, x + w, y1, names[i], DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (on) Fill(dc, x + P(16), y1 - P(4), x + w - P(16), y1 - P(1), COL_GOLD);
        AddHit(x, y0, x + w, y1, HIT_NAV, page);
        x += w + P(12);
    }
    Text(dc, g_fSmall, COL_DIM, P(900), P(BAR_Y + 14), P(980), P(BASE_H - 14), VERSION_TEXT, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
}

static void Paint(HWND wnd, HDC wdc)
{
    RECT rc;
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    GetClientRect(wnd, &rc);
    dc = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    g_numHits = 0;

    DrawBanner(dc);
    if (g_page == PAGE_HOME) {
        DrawHome(dc);
    } else {
        Veil(dc, 0, 0, P(BASE_W), P(BAR_Y), 225);
        if (g_page == PAGE_CONTROLS) DrawControls(dc);
        else if (g_page == PAGE_GRAPHICS) {
            DrawGraphics(dc);
        } else if (g_page == PAGE_VR) {
            int h1, h2;
            DrawPageTitle(dc, L"VR", NULL);
            Text(dc, g_fBold, COL_GOLD, P(40), P(66), P(480), P(90), L"COMFORT AND MOVEMENT", DT_SINGLELINE | DT_VCENTER);
            Text(dc, g_fBold, COL_GOLD, P(510), P(66), P(960), P(90), L"WEAPONS AND VIBRATION", DT_SINGLELINE | DT_VCENTER);
            h1 = DrawColumn(dc, g_vrOpts, 0, VR_COL2, 38, 94, 40, 480);
            h2 = DrawColumn(dc, g_vrOpts, VR_COL2, NUM_VR - VR_COL2, 34, 94, 510, 960);
            DrawHint(dc, g_vrOpts, h1 >= 0 ? h1 : h2);
        } else if (g_page == PAGE_MODS) {
            DrawPageTitle(dc, L"MODS", L"Things the original game does not have. Used the next time the game starts.");
            DrawOptions(dc, g_modOpts, NUM_MOD, 40, 100);
            if (g_modOpts[MOD_DIFFICULTY].cur >= 0)   // what the chosen difficulty does, always in view
                Text(dc, g_fSmall, COL_STEEL, P(60), P(100 + NUM_MOD * 40 + 12), P(950), P(100 + NUM_MOD * 40 + 60),
                     g_difficultyHints[g_modOpts[MOD_DIFFICULTY].cur], DT_WORDBREAK);
        } else if (g_page == PAGE_ABOUT) DrawAbout(dc);
    }
    DrawBar(dc);

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static int HitAt(int x, int y)
{
    int i;
    POINT p = { x, y };
    for (i = 0; i < g_numHits; i++) if (PtInRect(&g_hits[i].r, p)) return i;
    return -1;
}

static option_t *PageOption(int i)
{
    if (g_page == PAGE_VR && i < NUM_VR) return &g_vrOpts[i];
    if (g_page == PAGE_GRAPHICS && i < NUM_GFX) return &g_gfxOpts[i];
    if (g_page == PAGE_MODS && i < NUM_MOD) return &g_modOpts[i];
    return NULL;
}

static void Click(HWND wnd, int idx)
{
    hit_t *h;
    option_t *o;
    if (idx < 0) return;
    h = &g_hits[idx];
    switch (h->type) {
    case HIT_PLAY:
        StartGame(wnd);
        break;
    case HIT_NAV:
        g_page = g_page == h->arg ? PAGE_HOME : h->arg;
        break;
    case HIT_PREV:
    case HIT_NEXT:
    case HIT_ROW:
        o = PageOption(h->arg);
        if (!o) break;
        if (o->cur < 0) o->cur = o->def;
        else if (h->type == HIT_PREV) o->cur = (o->cur + o->count - 1) % o->count;
        else o->cur = (o->cur + 1) % o->count;
        SaveOption(o);
        break;
    case HIT_LINK:
        ShellExecuteA(wnd, "open", h->arg ? URL_DISCORD : URL_GITHUB, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    InvalidateRect(wnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        Paint(wnd, dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER: {
        static int was;
        int now = GameRunning();
        if (now != was) { was = now; InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int h = HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
        TrackMouseEvent(&tme);
        if (h != g_hover) { g_hover = h; InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover != -1) { g_hover = -1; InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(NULL, g_hover >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        Click(wnd, HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_page != PAGE_HOME) { g_page = PAGE_HOME; InvalidateRect(wnd, NULL, FALSE); }
            else PostMessageW(wnd, WM_CLOSE, 0, 0);
        } else if (wp == VK_RETURN) {
            StartGame(wnd);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
// The banner is a JPEG resource, decoded by OLE (no image library needed).
static void LoadBanner(HINSTANCE inst)
{
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(IDR_BANNER), (LPCWSTR)RT_RCDATA);
    DWORD size = res ? SizeofResource(inst, res) : 0;
    const void *data = res ? LockResource(LoadResource(inst, res)) : NULL;
    HGLOBAL mem;
    IStream *stream = NULL;
    IPicture *pic = NULL;
    if (!data || !size) return;
    mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return;
    memcpy(GlobalLock(mem), data, size);
    GlobalUnlock(mem);
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) { GlobalFree(mem); return; }
    if (SUCCEEDED(OleLoadPicture(stream, (LONG)size, FALSE, &IID_IPicture, (void **)&pic)) && pic) {
        OLE_HANDLE h = 0;
        if (SUCCEEDED(pic->lpVtbl->get_Handle(pic, &h)) && h) {
            BITMAP bm;
            g_banner = (HBITMAP)CopyImage((HANDLE)(UINT_PTR)h, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (g_banner && GetObject(g_banner, sizeof(bm), &bm)) { g_bannerW = bm.bmWidth; g_bannerH = bm.bmHeight; }
        }
        pic->lpVtbl->Release(pic);
    }
    stream->lpVtbl->Release(stream);
}

static HFONT MakeFont(int px, int weight, const wchar_t *face)
{
    return CreateFontW(-P(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdLine, int show)
{
    WNDCLASSEXW wc;
    HWND wnd;
    MSG msg;
    RECT r;
    HDC screen;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    char path[MAX_PATH], *slash;
    const char *appdata = getenv("APPDATA");
    (void)prev; (void)cmdLine;

    GetModuleFileNameA(NULL, path, sizeof(path));
    slash = strrchr(path, '\\');
    if (slash) *slash = 0;
    strcpy(g_dir, path);
    _snprintf(g_gameIni, sizeof(g_gameIni), "%s\\cod_vr.ini", g_dir);
    _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD-VR\\settings.ini", appdata ? appdata : g_dir);
    _snprintf(path, sizeof(path), "%s\\CoDSP.exe", g_dir);
    g_haveGame = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    _snprintf(path, sizeof(path), "%s\\opengl32.dll", g_dir);
    g_haveMod = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    LoadAll();

    screen = GetDC(NULL);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, screen);
    if (g_scale < 1.0f) g_scale = 1.0f;

    CoInitialize(NULL);
    LoadBanner(inst);
    g_fTitle = MakeFont(30, FW_BOLD, L"Segoe UI");
    g_fNav   = MakeFont(19, FW_BOLD, L"Segoe UI");
    g_fPlay  = MakeFont(24, FW_HEAVY, L"Segoe UI");
    g_fText  = MakeFont(17, FW_NORMAL, L"Segoe UI");
    g_fBold  = MakeFont(17, FW_BOLD, L"Segoe UI");
    g_fSmall = MakeFont(14, FW_NORMAL, L"Segoe UI");

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_LAUNCHER));
    wc.lpszClassName = L"CoDVRLauncher";
    RegisterClassExW(&wc);

    SetRect(&r, 0, 0, P(BASE_W), P(BASE_H));
    AdjustWindowRect(&r, style, FALSE);
    wnd = CreateWindowExW(0, wc.lpszClassName, L"Call of Duty VR", style,
                          (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                          (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!wnd) return 1;
    ShowWindow(wnd, show);
    UpdateWindow(wnd);
    SetTimer(wnd, 1, 1000, NULL);   // PLAY comes back when the game is closed
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
