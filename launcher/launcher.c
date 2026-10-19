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
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "launcher_res.h"

#define VERSION_TEXT L"v1.3.0"
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
} option_t;

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
    { L"Roomscale", L"Walk, lean and crouch for real: the soldier follows your body.",
      "VR", 2, { L"On", L"Off" }, { "RoomScale=1", "RoomScale=0" }, 0 },
    { L"Grenade trajectory", L"A line that shows where a grenade will fly and land.",
      "Grenade", 3, { L"Off", L"While aiming", L"Always" }, { "Trajectory=0", "Trajectory=1", "Trajectory=2" }, 1 },
    { L"Drop weapon on release", L"Letting go of the grip away from its holster drops the weapon on the floor.",
      "Holsters", 2, { L"On", L"Off" }, { "DropOnRelease=1", "DropOnRelease=0" }, 0 },
    { L"Objectives on the hand", L"The objectives (X button) show above your left hand instead of in front of you.",
      "HUD", 2, { L"On", L"Off" }, { "ObjectivesOnHand=1", "ObjectivesOnHand=0" }, 0 },
    { L"Menus and loading screens", L"Fixed in front of you, or following your head.",
      "HUD", 2, { L"Fixed", L"Follow head" }, { "MenuFollowsHead=0", "MenuFollowsHead=1" }, 0 },
};

static option_t g_gfxOpts[] = {
    { L"Headset resolution", L"Sharpness in the headset, per eye. Lower it if the game stutters; above Ultra needs a strong GPU.",
      "VR", 9, { L"Potato (50%)", L"Low (70%)", L"Medium (85%)", L"Native (100%)", L"High (120%)", L"Ultra (140%)",
                 L"Eagle Eye (160%)", L"Sniper Vision (180%)", L"GPU Furnace (200%)" },
      { "RenderScale=0.5", "RenderScale=0.7", "RenderScale=0.85", "RenderScale=1", "RenderScale=1.2", "RenderScale=1.4",
        "RenderScale=1.6", "RenderScale=1.8", "RenderScale=2" }, 3, 0, 1 },
    { L"Texture quality", L"Detail of the game's textures. Maximum suits any recent PC.",
      "Launcher", 3, { L"Maximum", L"High", L"Medium" }, { "Picmip=0", "Picmip=1", "Picmip=2" }, 0 },
    { L"Brightness", L"Makes the game brighter in the headset (applied when the game starts).",
      "Launcher", 4, { L"Normal", L"Brighter", L"Bright", L"Brightest" },
      { "Gamma=1", "Gamma=1.2", "Gamma=1.4", "Gamma=1.6" }, 0 },
};

#define NUM_VR  (int)(sizeof(g_vrOpts) / sizeof(g_vrOpts[0]))
#define NUM_GFX (int)(sizeof(g_gfxOpts) / sizeof(g_gfxOpts[0]))

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
    for (i = 0; i < NUM_VR; i++) LoadOption(&g_vrOpts[i]);
    for (i = 0; i < NUM_GFX; i++) LoadOption(&g_gfxOpts[i]);
}

// ---------------------------------------------------------------------------
// Starting the game
// ---------------------------------------------------------------------------
static int StartGame(HWND wnd)
{
    char exe[MAX_PATH], cmd[512], picmip[16], gamma[16];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    _snprintf(exe, sizeof(exe), "%s\\CoDSP.exe", g_dir);
    GetPrivateProfileStringA("Launcher", "Picmip", "0", picmip, sizeof(picmip), g_userIni);
    GetPrivateProfileStringA("Launcher", "Gamma", "1", gamma, sizeof(gamma), g_userIni);
    // r_ignorehwgamma: the game's own brightness is a ramp on the monitor, which the headset never
    // sees; with it ignored, r_gamma brightens the textures themselves
    _snprintf(cmd, sizeof(cmd), "\"%s\" +set r_picmip %d +set r_ignorehwgamma 1 +set r_gamma %.2f",
              exe, atoi(picmip), atof(gamma));
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
        MessageBoxW(wnd, L"Could not start CoDSP.exe.", L"Call of Duty VR", MB_ICONERROR);
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
enum { PAGE_HOME, PAGE_CONTROLS, PAGE_GRAPHICS, PAGE_VR, PAGE_ABOUT };
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

static void DrawOptions(HDC dc, option_t *opts, int n, int rowH, int top)
{
    int i, hint = -1;
    for (i = 0; i < n; i++) {
        option_t *o = &opts[i];
        int y0 = P(top + i * rowH), y1 = P(top + (i + 1) * rowH - 4);
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
    if (hint >= 0)
        Text(dc, g_fSmall, COL_DIM, P(50), P(444), P(950), P(476), opts[hint].hint, DT_SINGLELINE | DT_END_ELLIPSIS);
    else
        Text(dc, g_fSmall, COL_DIM, P(50), P(444), P(950), P(476),
             L"Click a setting to change it. Changes are saved at once and used the next time the game starts.", DT_SINGLELINE);
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
        { L"Left stick click", L"Objectives" },
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
    static const wchar_t *names[] = { L"CONTROLS", L"GRAPHICS", L"VR", L"ABOUT" };
    int i, x = P(250);
    Fill(dc, 0, P(BAR_Y), P(BASE_W), P(BASE_H), COL_BAR);
    Fill(dc, 0, P(BAR_Y), P(BASE_W), P(BAR_Y) + P(2), COL_GOLD_DARK);
    // PLAY
    {
        int x0 = P(30), y0 = P(BAR_Y + 14), x1 = P(210), y1 = P(BASE_H - 14);
        int hot = Hovered(HIT_PLAY, 0);
        Fill(dc, x0, y0, x1, y1, hot ? RGB(236, 192, 80) : COL_GOLD);
        Text(dc, g_fPlay, RGB(20, 16, 6), x0, y0, x1, y1, L"PLAY", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(x0, y0, x1, y1, HIT_PLAY, 0);
    }
    for (i = 0; i < 4; i++) {
        int page = PAGE_CONTROLS + i, w = P(i == 2 ? 80 : 160);
        int on = g_page == page, hot = Hovered(HIT_NAV, page);
        int y0 = P(BAR_Y + 14), y1 = P(BASE_H - 14);
        Text(dc, g_fNav, on || hot ? COL_GOLD : COL_TEXT, x, y0, x + w, y1, names[i], DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (on) Fill(dc, x + P(16), y1 - P(4), x + w - P(16), y1 - P(1), COL_GOLD);
        AddHit(x, y0, x + w, y1, HIT_NAV, page);
        x += w + P(20);
    }
    Text(dc, g_fSmall, COL_DIM, P(860), P(BAR_Y + 14), P(980), P(BASE_H - 14), VERSION_TEXT, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
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
            DrawPageTitle(dc, L"GRAPHICS", L"The game's own options (Options in the game menu) still apply on top of these.");
            DrawOptions(dc, g_gfxOpts, NUM_GFX, 52, 104);
        } else if (g_page == PAGE_VR) {
            DrawPageTitle(dc, L"VR", NULL);
            DrawOptions(dc, g_vrOpts, NUM_VR, 39, 70);
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
        if (StartGame(wnd)) PostMessageW(wnd, WM_CLOSE, 0, 0);
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
            if (StartGame(wnd)) PostMessageW(wnd, WM_CLOSE, 0, 0);
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
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
