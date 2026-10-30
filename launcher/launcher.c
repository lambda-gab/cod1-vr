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
#include <winhttp.h>
#include <olectl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "launcher_res.h"
#include "../third_party/miniz/miniz.h"

#define VERSION_TEXT L"v1.8.0"
#define URL_GITHUB   "https://github.com/lambda-gab/cod1-vr"
#define URL_DISCORD  "https://discord.gg/SRsK2x5Tn"
#define URL_RELEASES "https://github.com/lambda-gab/cod1-vr/releases/latest"

// ---------------------------------------------------------------------------
// Layout, in pixels at 96 dpi (scaled by P())
// ---------------------------------------------------------------------------
#define BASE_W   1000
#define BASE_H   650
#define BANNER_H 364
#define BAR_Y    570

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
    int swatch;   // a colour: shown as a swatch with its hex value, the click opens the colour picker (PickCrosshairColor)
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
    { L"Movement", L"Continuous: walk with the move stick. Blink (beta): push the stick forward to aim an arc with that stick's hand (your gun hand stays free), let go to teleport (the screen blinks). Shift (beta): the same, gliding there.",
      "Controllers", 3, { L"Blink (beta)", L"Shift (beta)", L"Continuous" }, { "MoveMode=0", "MoveMode=1", "MoveMode=2" }, 2 },
    { L"Comfort vignette", L"Narrows the view while you move with the stick or turn smoothly. Helps if you feel sick.",
      "Comfort", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "Vignette=0", "Vignette=1|VignetteStrength=0.35", "Vignette=1|VignetteStrength=0.6", "Vignette=1|VignetteStrength=0.9" }, 2 },
    { L"Vehicle vignette", L"Narrows the view while a vehicle carries you (truck, car, plane) and turns or speeds up.",
      "Comfort", 4, { L"Off", L"Low", L"Medium", L"High" },
      { "VehicleVignette=0", "VehicleVignette=0.35", "VehicleVignette=0.6", "VehicleVignette=0.9" }, 2 },
    { L"Vehicle turns", L"How fast the view follows a vehicle round a bend (truck, car). Smoother turns are gentler if bends make you dizzy.",
      "Comfort", 3, { L"Original", L"Smooth", L"Smoother" },
      { "VehicleTurnSmooth=0.15", "VehicleTurnSmooth=0.5", "VehicleTurnSmooth=1" }, 1 },
    { L"Camera shake", L"How much the view shakes in earthquakes and explosions (bombing runs, shelling). Less is gentler in VR.",
      "Comfort", 4, { L"Original", L"Reduced", L"Low", L"Off" },
      { "ShakeKeep=1", "ShakeKeep=0.4", "ShakeKeep=0.2", "ShakeKeep=0" }, 1 },
    { L"Turning", L"Right stick: turn in steps (gentler) or smoothly.",
      "Controllers", 4, { L"Snap 30\x00b0", L"Snap 45\x00b0", L"Snap 90\x00b0", L"Smooth" },
      { "SnapTurn=30", "SnapTurn=45", "SnapTurn=90", "SnapTurn=0" }, 1 },
    { L"Smooth turn speed", L"Only used with smooth turning.",
      "Controllers", 3, { L"Slow", L"Normal", L"Fast" },
      { "SmoothTurnSpeed=90", "SmoothTurnSpeed=150", "SmoothTurnSpeed=220" }, 1 },
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
    { L"Subtitle size", L"How big the subtitles are (on a dark box). Small is a little bigger than the game's text; Extra Large is for reading from far away. Exact size: SubtitleSize in cod_vr.ini ([HUD]).",
      "HUD", 4, { L"Small", L"Medium", L"Large", L"Extra Large" },
      { "SubtitleSize=1", "SubtitleSize=1.5", "SubtitleSize=2.2", "SubtitleSize=3" }, 1 },
    // right column
    // not a setting of cod_vr.ini: the "no hands" pack in the game folder (HandsPack functions); the click is handled by Click
    { L"Player hands", L"(Beta) Shown: turns the soldier's arms on in the game. With some weapons the arms can look distorted. If Weapon handling is Free hands, the arms disappear when you let go of the weapon and come back when you take it.\n\nHidden: self-explanatory XD  (Windows may ask for administrator rights.)",
      "Launcher", 2, { L"Shown (beta)", L"Hidden" }, { "", "" }, 0 },
    // not a plain setting either: [Controllers] AlwaysArmed of the active control profile (ChangeArmed); the click is handled by Click
    { L"Weapon handling", L"Always in hand: the weapon stays in your dominant hand (never dropped) and a grip on your body (either hand) switches weapons. Free hands: only the gun is in your hand, taken from and put back on your body, dropped when you let go. Works with the player hands shown or hidden.",
      "Launcher", 2, { L"Always in hand", L"Free hands" }, { "", "" }, 0 },
    { L"Crosshair", L"A crosshair where the gun is aimed, for playing without aiming down the sights.",
      "Controllers", 6, { L"None", L"Cross", L"Dot", L"Circle", L"Circle + dot", L"Cross + dot" },
      { "Crosshair=0", "Crosshair=1", "Crosshair=2", "Crosshair=3", "Crosshair=4", "Crosshair=5" }, 0, 0, 0, 1 },
    { L"Crosshair color", L"Click to pick the crosshair's colour from the colour box: drag in the square and the bar, or type the numbers (it only shows when a crosshair style above is chosen).",
      "Controllers", 8, { L"White", L"Red", L"Green", L"Yellow", L"Cyan", L"Blue", L"Orange", L"Magenta" },
      { "CrosshairColor=white", "CrosshairColor=red", "CrosshairColor=green", "CrosshairColor=yellow", "CrosshairColor=cyan",
        "CrosshairColor=blue", "CrosshairColor=orange", "CrosshairColor=magenta" }, 0, 0, 0, 0, NULL, 1 },
    { L"Crosshair opacity", L"How solid the crosshair is: lower is gentler on the eyes, higher is easier to see.",
      "Controllers", 4, { L"30%", L"50%", L"80%", L"100%" },
      { "CrosshairOpacity=0.3", "CrosshairOpacity=0.5", "CrosshairOpacity=0.8", "CrosshairOpacity=1" }, 2 },
    { L"Grenade line", L"A line that shows where a grenade will fly and land.",
      "Grenade", 3, { L"Off", L"While aiming", L"Always" }, { "Trajectory=0", "Trajectory=1", "Trajectory=2" }, 1 },
    { L"Scope", L"Original: the game's own scope (the view zooms in with a round picture when you aim down the sights). Realistic (beta): the zoomed picture shows inside the rifle's own scope and you look through it by bringing the gun to your eye. Tune it in the mod menu (hold Y, press B: Adjust scope).",
      "Scope", 2, { L"Original", L"Realistic (beta)" }, { "Lens=0", "Lens=1" }, 1 },
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
    // not a setting either: the Portuguese dub pack in the game folder (VoicePack functions); the click is handled by Click
    { L"Voice", L"Language of the soldiers' voices. Portuguese (Brazil) needs the dub pack made by Dublagem-para-PK3 (pakz_vr_dub_ptbr.pk3 in the main folder); English is the original game. Windows may ask for administrator rights.",
      "Launcher", 2, { L"English", L"Portuguese (Brazil)" }, { "", "" }, 0 },
};
#define VR_COL2 13
#define VR_HANDS    VR_COL2          // the right column starts with Player hands, then Weapon handling
#define VR_HANDLING (VR_COL2 + 1)

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
    { L"Text font", L"Modern: the game's text in a cleaner font that is easier to read in the headset.",
      "HUD", 2, { L"Original", L"Modern" }, { "ModernFont=0", "ModernFont=1" }, 1 },
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
#define GFX_POST_FIRST 6

#define NUM_VR  (int)(sizeof(g_vrOpts) / sizeof(g_vrOpts[0]))
// The Voice row (Portuguese dub, the LAST entry of g_vrOpts) is for testing only and is never shown in a release until the
// maintainer asks for it (see CLAUDE.md, "Never release"). The code stays; only the row is not drawn.
#define VOICE_ENABLED 0
#define NUM_VR_SHOWN (NUM_VR - (VOICE_ENABLED ? 0 : 1))
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
static int  g_haveGame, g_haveMod, g_haveSP, g_haveUO, g_playUO;   // g_playUO: United Offensive is the game PLAY starts

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

// Graphics profiles, one per game (United Offensive has more on screen and may need lower settings). The mod reads one set of
// graphics keys, so the profile of the game that starts is written into them (GfxSelect); each profile is kept in its own section
// of settings.ini as the index of the choice of every row of the GRAPHICS page (o<row>=<choice>).
static int g_gfxProf;   // the profile the GRAPHICS page edits: 0 = Call of Duty, 1 = United Offensive

static void GfxSection(int p, char *s) { strcpy(s, p ? "GfxProfile.UO" : "GfxProfile.SP"); }

static int GfxProfileSaved(int p)
{
    char sec[32];
    GfxSection(p, sec);
    return GetPrivateProfileIntA(sec, "Saved", 0, g_userIni) == 1;
}

static void GfxSaveProfile(int p)
{
    char sec[32], key[16], val[16], dir[MAX_PATH], *slash;
    int i;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
    GfxSection(p, sec);
    for (i = 0; i < NUM_GFX; i++) {
        if (g_gfxOpts[i].cur < 0) continue;
        _snprintf(key, sizeof(key), "o%d", i);
        _snprintf(val, sizeof(val), "%d", g_gfxOpts[i].cur);
        WritePrivateProfileStringA(sec, key, val, g_userIni);
    }
    WritePrivateProfileStringA(sec, "Saved", "1", g_userIni);
}

// Makes profile p the one in use: its choices go into the keys the mod reads. A profile never saved starts as what is set now.
static void GfxApplyProfile(int p)
{
    char sec[32], key[16];
    int i;
    if (!GfxProfileSaved(p)) { GfxSaveProfile(p); return; }
    GfxSection(p, sec);
    for (i = 0; i < NUM_GFX; i++) {
        int v;
        _snprintf(key, sizeof(key), "o%d", i);
        v = GetPrivateProfileIntA(sec, key, -1, g_userIni);
        if (v < 0 || v >= g_gfxOpts[i].count) continue;
        g_gfxOpts[i].cur = v;
        SaveOption(&g_gfxOpts[i]);
    }
}

static void GfxSelect(int p)
{
    if (p == g_gfxProf) { GfxApplyProfile(p); return; }
    GfxSaveProfile(g_gfxProf);
    g_gfxProf = p;
    GfxApplyProfile(p);
}

// CONTROLS: what each body zone holds and what each controller button does, as drop-down lists.
// The mod reads [Holsters] (zone = weapon) and [Buttons] (button = game command, "" = nothing) of
// settings.ini; one never changed keeps its default, the one the mod uses when the key is missing.
typedef struct { const wchar_t *name; const char *val; } choice_t;

static const choice_t g_holsterActs[] = {
    { L"Nothing", "none" },
    { L"Weapon 1", "primary" },
    { L"Weapon 2", "primaryb" },
    { L"Switch Weapon 1 & 2", "switch" },
    { L"Pistol", "pistol" },
    { L"Grenade", "grenade" },
    { L"Binoculars (United Offensive)", "binocular" },
};

static const choice_t g_actions[] = {
    { L"Nothing", "" },
    { L"Fire", "+attack" },
    { L"Jump", "+gostand" },
    { L"Reload", "+reload" },
    { L"Melee", "+melee" },
    { L"Use / pick up", "+activate" },
    { L"Objectives", "+scores" },
    { L"Aim down sights", "+speed" },
    { L"Crouch (hold)", "+movedown" },
    { L"Crouch (toggle)", "togglecrouch" },
    { L"Go prone", "toggleprone" },
    { L"Lean left", "+leanleft" },
    { L"Lean right", "+leanright" },
    { L"Next weapon", "weapnext" },
    { L"Previous weapon", "weapprev" },
    { L"Raise stance", "raisestance" },
    { L"Lower stance", "lowerstance" },
    { L"Recenter", "vr:recenter" },
    { L"Sprint (United Offensive)", "+sprint" },
    { L"Switch weapon 1 & 2", "vr:switchweapon" },     // without reaching for the shoulders
    { L"Comfort vignette (next)", "vr:vignette" },      // Off, Low, Medium, High, Off...
    { L"Menu", "key:ESCAPE" },          // the game's menu, for controllers without a menu button (opens it, and closes it from inside)
    { L"Mod menu (settings)", "vr:modmenu" },   // the mod's own menu in the middle of the game (height, ...)
    { L"Turn left", "vr:turnleft" },    // only for the free stick (the lists of the buttons stop before these)
    { L"Turn right", "vr:turnright" },
};
#define NUM_BTN_ACTS 23   // the entries of g_actions a button can have
#define NUM_STK_ACTS 25   // and the free stick's directions

static const choice_t g_handActs[] = {
    { L"Right", "1" },
    { L"Left", "0" },
};

// What the hands do with the weapons: taken from the body and put away (free hands), or one weapon always in the dominant hand,
// never dropped, the free hand's grip on a shoulder switching it (always armed). Not shown in CONTROLS any more: the VR tab's
// The VR tab's "Weapon handling" sets it (always in hand / free hands), whether the player hands are shown or not; see ChangeArmed.
static const choice_t g_armedActs[] = {
    { L"Free hands", "0" },
    { L"Always armed (gunstock)", "1" },
};

// The stick that moves you; the other one is free: each of its four directions gets an action.
static const choice_t g_stickActs[] = {
    { L"Left stick", "0" },
    { L"Right stick", "1" },
};

#define NUM_HOLSTERS 7
#define NUM_HANDS    2   // the entries after the holsters: dominant hand, then the weapon handling (free hands / always armed)
#define NUM_BUTTONS  6   // then A, B, X, Y and the stick clicks (the page shows the two of the dominant hand)
#define NUM_SHORTCUTS 12 // then the shortcuts: X + A, X + B, X + Y, Y + A, Y + B, Y + X (right-handed), A + X, A + Y, A + B, B + X, B + Y, B + A (left-handed)
#define CTL_SHORTCUT (NUM_HOLSTERS + NUM_HANDS + NUM_BUTTONS)
#define CTL_MOVE     (CTL_SHORTCUT + NUM_SHORTCUTS)   // then the move stick and the free stick's up, down, left, right
static struct { const wchar_t *label; const char *section, *key, *def; const choice_t *ch; int n; int cur; } g_ctl[] = {
    { L"Left shoulder", "Holsters", "LeftShoulder", "primaryb", g_holsterActs, 7, 0 },
    { L"Right shoulder", "Holsters", "RightShoulder", "primary", g_holsterActs, 7, 0 },
    { L"Chest", "Holsters", "Chest", "binocular", g_holsterActs, 7, 0 },
    { L"Waist, right", "Holsters", "WaistRight", "none", g_holsterActs, 7, 0 },
    { L"Waist, left", "Holsters", "WaistLeft", "pistol", g_holsterActs, 7, 0 },
    { L"Waist, front", "Holsters", "WaistFront", "none", g_holsterActs, 7, 0 },
    { L"Waist, back", "Holsters", "WaistBack", "grenade", g_holsterActs, 7, 0 },
    { L"Dominant hand", "Controllers", "RightHanded", "1", g_handActs, 2, 0 },
    { L"Weapon handling", "Controllers", "AlwaysArmed", "0", g_armedActs, 2, 0 },
    { L"A", "Buttons", "A", "+gostand", g_actions, NUM_BTN_ACTS, 0 },
    { L"B", "Buttons", "B", "+reload", g_actions, NUM_BTN_ACTS, 0 },
    { L"X", "Buttons", "X", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y", "Buttons", "Y", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Left stick click", "Buttons", "LeftStickClick", "vr:recenter", g_actions, NUM_BTN_ACTS, 0 },
    { L"Right stick click", "Buttons", "RightStickClick", "toggleprone", g_actions, NUM_BTN_ACTS, 0 },
    { L"X + A", "Buttons", "ShortcutXA", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"X + B", "Buttons", "ShortcutXB", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"X + Y", "Buttons", "ShortcutXY", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y + A", "Buttons", "ShortcutYA", "vr:switchweapon", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y + B", "Buttons", "ShortcutYB", "vr:modmenu", g_actions, NUM_BTN_ACTS, 0 },
    { L"Y + X", "Buttons", "ShortcutYX", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"A + X", "Buttons", "ShortcutAX", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"A + Y", "Buttons", "ShortcutAY", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"A + B", "Buttons", "ShortcutAB", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"B + X", "Buttons", "ShortcutBX", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"B + Y", "Buttons", "ShortcutBY", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"B + A", "Buttons", "ShortcutBA", "", g_actions, NUM_BTN_ACTS, 0 },
    { L"Move with", "Controllers", "MoveStick", "0", g_stickActs, 2, 0 },
    { L"Right stick up", "Buttons", "TurnStickUp", "raisestance", g_actions, NUM_STK_ACTS, 0 },      // the labels follow the move stick (DrawControls)
    { L"Right stick down", "Buttons", "TurnStickDown", "lowerstance", g_actions, NUM_STK_ACTS, 0 },
    { L"Right stick left", "Buttons", "TurnStickLeft", "vr:turnleft", g_actions, NUM_STK_ACTS, 0 },
    { L"Right stick right", "Buttons", "TurnStickRight", "vr:turnright", g_actions, NUM_STK_ACTS, 0 },
};
#define NUM_CTL (int)(sizeof(g_ctl) / sizeof(g_ctl[0]))
#define CTL_ARG 100   // hit argument of the first drop-down (the option pages use small ones)

// The defaults follow the dominant hand: the table above holds the right-handed ones; with the left hand chosen the weapons
// are on the opposite shoulders and hip, the left stick turns, the face buttons and stick clicks swap controllers. The profile
// called Default means these, whichever hand is chosen.
static int g_leftHand;   // the dominant hand of the profile in use: 1 = left
static int g_armed;      // the profile in use is always armed (the weapon never leaves the dominant hand)

static const char *CtlDefFor(int i, int left)
{
    // always armed: both shoulders switch between weapon 1 and 2 (the free hand reaches back, the weapon stays in the other)
    if (g_armed && (!strcmp(g_ctl[i].key, "LeftShoulder") || !strcmp(g_ctl[i].key, "RightShoulder"))) return "switch";
    // always armed: the free hand's X (right-handed) or A (left-handed) tapped switches weapon, so nothing needs the holsters
    if (g_armed && !strcmp(g_ctl[i].key, left ? "A" : "X")) return "vr:switchweapon";
    static const char *const mirrored[][2] = {
        { "LeftShoulder", "primary" }, { "RightShoulder", "primaryb" }, { "WaistRight", "pistol" }, { "WaistLeft", "none" },
        { "MoveStick", "1" }, { "A", "" }, { "B", "" }, { "X", "+gostand" }, { "Y", "+reload" },
        { "LeftStickClick", "toggleprone" }, { "RightStickClick", "vr:recenter" },
        { "ShortcutYA", "" }, { "ShortcutBX", "vr:switchweapon" }, { "ShortcutYB", "" }, { "ShortcutBY", "vr:modmenu" },
    };
    int k;
    if (left)
        for (k = 0; k < (int)(sizeof(mirrored) / sizeof(mirrored[0])); k++)
            if (!strcmp(mirrored[k][0], g_ctl[i].key)) return mirrored[k][1];
    return g_ctl[i].def;
}
static const char *CtlDef(int i) { return CtlDefFor(i, g_leftHand); }

static void LoadControls(void)
{
    int i, a;
    {
        char hand[8];
        GetPrivateProfileStringA("Controllers", "RightHanded", "1", hand, sizeof(hand), g_gameIni);
        GetPrivateProfileStringA("Controllers", "RightHanded", hand, hand, sizeof(hand), g_userIni);
        g_leftHand = !strcmp(hand, "0");
        GetPrivateProfileStringA("Controllers", "AlwaysArmed", "0", hand, sizeof(hand), g_gameIni);
        GetPrivateProfileStringA("Controllers", "AlwaysArmed", hand, hand, sizeof(hand), g_userIni);
        g_armed = !strcmp(hand, "1");
    }
    for (i = 0; i < NUM_CTL; i++) {
        char val[64], v[64];
        GetPrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, CtlDef(i), v, sizeof(v), g_gameIni);
        GetPrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, v, val, sizeof(val), g_userIni);
        g_ctl[i].cur = -1;   // something typed into the ini that is not in the list: Custom
        for (a = 0; a < g_ctl[i].n; a++)
            if (!_stricmp(val, g_ctl[i].ch[a].val)) { g_ctl[i].cur = a; break; }
    }
}

static void EnsureUserDir(void)
{
    char dir[MAX_PATH], *slash;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, 0x5c);
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
}

// Control profiles. The mod reads the active choices from [Holsters] and [Buttons]; each profile
// keeps its own copy in a [Profile.<name>] section of the same file (the launcher's settings.ini:
// the game folder is not writable without administrator rights). The Default profile is built
// in: it is not stored, cannot be edited or deleted, and always means the defaults.
#define MAX_PROFILES 32
static char g_profNames[MAX_PROFILES][32] = { "Default" };
static int  g_numProfiles = 1, g_profile;   // g_profile 0 = Default

static void ProfileSection(int i, char *out, int size)
{
    _snprintf(out, size, "Profile.%s", g_profNames[i]);
    out[size - 1] = 0;
}

// The Leftie preset is gone: Default follows the dominant hand (CtlDefFor). Once, an existing [Profile.Leftie] is removed; if it
// was the profile in use, left-handed play moves to Default.
static void RemoveLeftieProfile(void)
{
    char done[8], cur[64];
    GetPrivateProfileStringA("Launcher", "LeftieRemoved", "", done, sizeof(done), g_userIni);
    if (done[0]) return;
    GetPrivateProfileStringA("Launcher", "Profile", "Default", cur, sizeof(cur), g_userIni);
    if (!_stricmp(cur, "Leftie")) {
        WritePrivateProfileStringA("Profile.Default", "RightHanded", "0", g_userIni);
        WritePrivateProfileStringA("Launcher", "Profile", "Default", g_userIni);
    }
    WritePrivateProfileStringA("Profile.Leftie", NULL, NULL, g_userIni);   // the whole section
    WritePrivateProfileStringA("Launcher", "LeftieRemoved", "1", g_userIni);
}

// Once, when the wildcard buttons became the two of the hand that does not hold the gun (X and Y right-handed, A and B
// left-handed) and Reload moved to the dominant hand: every profile (and the live [Buttons]) is converted. Right-handed:
// Reload (was X) goes to B, the jump stays on A, X and Y are free, the weapon switch (was B + X) is Y + A. Left-handed:
// Reload (was A) to Y, the jump stays on X, A and B free, the switch (was Y + A) is B + X.
static void MigrateFaceButtons(void)
{
    char all[4096] = "", done[8], *q;
    GetPrivateProfileStringA("Launcher", "FaceButtonsV2", "", done, sizeof(done), g_userIni);
    if (done[0]) return;
    GetPrivateProfileSectionNamesA(all, sizeof(all), g_userIni);
    for (q = all; *q; q += strlen(q) + 1) {
        char a[64], x[64], sw[64], hand[8];
        int live = !_stricmp(q, "Buttons"), rh;
        if (_strnicmp(q, "Profile.", 8) && !live) continue;
        GetPrivateProfileStringA(live ? "Controllers" : q, "RightHanded", "1", hand, sizeof(hand), g_userIni);
        rh = strcmp(hand, "0") != 0;
        GetPrivateProfileStringA(q, "A", "?", a, sizeof(a), g_userIni);
        GetPrivateProfileStringA(q, "X", "?", x, sizeof(x), g_userIni);
        if (rh) {
            GetPrivateProfileStringA(q, "ShortcutBX", "?", sw, sizeof(sw), g_userIni);
            if (strcmp(x, "?")) WritePrivateProfileStringA(q, "B", x, g_userIni);          // reload (the jump stays on A)
            WritePrivateProfileStringA(q, "X", "", g_userIni);
            WritePrivateProfileStringA(q, "Y", "", g_userIni);
            if (strcmp(sw, "?")) WritePrivateProfileStringA(q, "ShortcutYA", sw, g_userIni);
            {   // B + Y (hold B) is now Y + B (hold Y): B is a plain button
                char by[64], yb[64];
                GetPrivateProfileStringA(q, "ShortcutBY", "", by, sizeof(by), g_userIni);
                GetPrivateProfileStringA(q, "ShortcutYB", "", yb, sizeof(yb), g_userIni);
                if (by[0] && !yb[0]) WritePrivateProfileStringA(q, "ShortcutYB", by, g_userIni);
            }
        } else {
            GetPrivateProfileStringA(q, "ShortcutYA", "?", sw, sizeof(sw), g_userIni);
            if (strcmp(a, "?")) WritePrivateProfileStringA(q, "Y", a, g_userIni);          // reload (the jump stays on X)
            WritePrivateProfileStringA(q, "A", "", g_userIni);
            WritePrivateProfileStringA(q, "B", "", g_userIni);
            if (strcmp(sw, "?")) WritePrivateProfileStringA(q, "ShortcutBX", sw, g_userIni);
            {   // Y + B (hold Y) is now B + Y (hold B): Y is a plain button
                char by[64], yb[64];
                GetPrivateProfileStringA(q, "ShortcutBY", "", by, sizeof(by), g_userIni);
                GetPrivateProfileStringA(q, "ShortcutYB", "", yb, sizeof(yb), g_userIni);
                if (yb[0] && !by[0]) WritePrivateProfileStringA(q, "ShortcutBY", yb, g_userIni);
            }
        }
    }
    WritePrivateProfileStringA("Launcher", "FaceButtonsV2", "1", g_userIni);
}

// Once, when B and Y became the shortcut buttons: a profile that had Reload on one of them gets it
// on the face button that had Objectives (the objectives now show on the back of the off hand),
// and a left-handed one gets its weapon switch on Y + A (the right-handed default is B + X).
static void SeedShortcuts(void)
{
    char all[4096] = "", done[8], *q;
    GetPrivateProfileStringA("Launcher", "ShortcutsSeeded", "", done, sizeof(done), g_userIni);
    if (done[0]) return;
    GetPrivateProfileSectionNamesA(all, sizeof(all), g_userIni);
    for (q = all; *q; q += strlen(q) + 1) {
        char a[64], b[64], x[64], y[64], hand[8], bx[64];
        if (_strnicmp(q, "Profile.", 8)) continue;
        GetPrivateProfileStringA(q, "A", "", a, sizeof(a), g_userIni);
        GetPrivateProfileStringA(q, "B", "", b, sizeof(b), g_userIni);
        GetPrivateProfileStringA(q, "X", "", x, sizeof(x), g_userIni);
        GetPrivateProfileStringA(q, "Y", "", y, sizeof(y), g_userIni);
        if (_stricmp(a, "+reload") && _stricmp(x, "+reload") && (!_stricmp(b, "+reload") || !_stricmp(y, "+reload"))) {
            if (!_stricmp(x, "+scores")) WritePrivateProfileStringA(q, "X", "+reload", g_userIni);
            else if (!_stricmp(a, "+scores")) WritePrivateProfileStringA(q, "A", "+reload", g_userIni);
        }
        GetPrivateProfileStringA(q, "RightHanded", "1", hand, sizeof(hand), g_userIni);
        GetPrivateProfileStringA(q, "ShortcutBX", "?", bx, sizeof(bx), g_userIni);
        if (!strcmp(hand, "0") && !strcmp(bx, "?")) {
            WritePrivateProfileStringA(q, "ShortcutBX", "", g_userIni);
            WritePrivateProfileStringA(q, "ShortcutYA", "vr:switchweapon", g_userIni);
        }
    }
    WritePrivateProfileStringA("Launcher", "ShortcutsSeeded", "1", g_userIni);
}

static void LoadProfileList(void)
{
    char all[4096] = "", cur[64], *q;
    int i;
    SeedShortcuts();
    MigrateFaceButtons();
    RemoveLeftieProfile();
    g_numProfiles = 1;
    GetPrivateProfileSectionNamesA(all, sizeof(all), g_userIni);
    for (q = all; *q && g_numProfiles < MAX_PROFILES; q += strlen(q) + 1)
        if (!_strnicmp(q, "Profile.", 8) && q[8] && strlen(q + 8) < sizeof(g_profNames[0]) && _stricmp(q + 8, "Default"))
            strcpy(g_profNames[g_numProfiles++], q + 8);   // (Default is the first one, kept in [Profile.Default] like the others)
    GetPrivateProfileStringA("Launcher", "Profile", "Default", cur, sizeof(cur), g_userIni);
    g_profile = 0;
    for (i = 1; i < g_numProfiles; i++) if (!_stricmp(cur, g_profNames[i])) g_profile = i;
}

static void SaveControl(int i)
{
    if (g_ctl[i].cur < 0) return;
    EnsureUserDir();
    WritePrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, g_ctl[i].ch[g_ctl[i].cur].val, g_userIni);
    {
        char sec[64];
        ProfileSection(g_profile, sec, sizeof(sec));
        WritePrivateProfileStringA(sec, g_ctl[i].key, g_ctl[i].ch[g_ctl[i].cur].val, g_userIni);
    }
}

static int HandsPackExists(void);
static void ChangeArmed(int armed);

// The active profile's choices become the ones the mod reads.
static void ApplyProfile(void)
{
    char sec[64], val[64];
    int i;
    EnsureUserDir();
    ProfileSection(g_profile, sec, sizeof(sec));
    GetPrivateProfileStringA(sec, "RightHanded", "1", val, sizeof(val), g_userIni);
    g_leftHand = !strcmp(val, "0");
    GetPrivateProfileStringA(sec, "AlwaysArmed", "0", val, sizeof(val), g_userIni);
    g_armed = !strcmp(val, "1");
    for (i = 0; i < NUM_CTL; i++) {
        GetPrivateProfileStringA(sec, g_ctl[i].key, CtlDef(i), val, sizeof(val), g_userIni);   // what was never changed is the default (of the hand)
        WritePrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, val, g_userIni);
    }
    WritePrivateProfileStringA("Launcher", "Profile", g_profNames[g_profile], g_userIni);
    LoadControls();
    g_vrOpts[VR_HANDLING].cur = g_armed ? 0 : 1;   // the VR tab's "Weapon handling" shows the profile's AlwaysArmed
}

// Does choice a of control i hold the default?
static int IsDefault(int i, int a)
{
    return a >= 0 && !_stricmp(g_ctl[i].ch[a].val, CtlDef(i));
}

// ---------------------------------------------------------------------------
// Player hands. The game's first-person hands (xmodel/viewmodel_hands*) come with sleeves and arms. The pack
// main\\pakz_vr_nohands.pk3 gives each of those models a transparent skin, so only the gun shows. The installer
// makes it; a manual install has no installer, so the launcher can make (and remove) the same pack here, from the
// player's own paks. Writing into the game folder usually needs administrator rights, so the launcher runs itself
// again elevated (/handspack hide|show) when it cannot write.
// ---------------------------------------------------------------------------
#define HANDS_PACK_FILE   "\\main\\pakz_vr_nohands.pk3"
#define INVISIBLE_SKIN    "cloth_masked@vr_invisible.tga"   // alpha-tested skin type, alpha 0

typedef struct { char name[128]; void *data; size_t size; } hand_entry_t;
static hand_entry_t g_handModels[64];
static int g_numHandModels;

static int CmpNames(const void *a, const void *b) { return _stricmp((const char *)a, (const char *)b); }

static void KeepHandModel(const char *name, void *data, size_t size)
{
    int i;
    for (i = 0; i < g_numHandModels; i++)
        if (!_stricmp(g_handModels[i].name, name)) { free(g_handModels[i].data); break; }   // a later pak wins
    if (i == g_numHandModels) {
        if (g_numHandModels == 64) { free(data); return; }
        g_numHandModels++;
    }
    strncpy(g_handModels[i].name, name, sizeof(g_handModels[i].name) - 1);
    g_handModels[i].data = data;
    g_handModels[i].size = size;
}

// xmodel v14: ... ff ff ff ff 00 00 00 00, u16 skin count, count NUL-terminated names to EOF.
static void *ReplaceSkins(const BYTE *d, size_t n, size_t *outSize)
{
    static const BYTE marker[8] = { 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 };
    size_t at = (size_t)-1, i, pos, k;
    unsigned count;
    BYTE *out, *w;
    for (i = 0; i + 8 <= n; i++) if (!memcmp(d + i, marker, 8)) at = i;
    if (at == (size_t)-1 || at + 10 > n) return NULL;
    pos = at + 8;
    count = d[pos] | (d[pos + 1] << 8);
    k = pos + 2;
    for (i = 0; i < count; i++) {          // each name must be non-empty and terminated
        size_t start = k;
        while (k < n && d[k]) k++;
        if (k >= n || k == start) return NULL;
        k++;
    }
    for (; k < n; k++) if (d[k]) return NULL;   // nothing else after the names
    *outSize = pos + 2 + count * (strlen(INVISIBLE_SKIN) + 1);
    out = w = malloc(*outSize);
    if (!out) return NULL;
    memcpy(w, d, pos + 2);
    w += pos + 2;
    for (i = 0; i < count; i++) { memcpy(w, INVISIBLE_SKIN, strlen(INVISIBLE_SKIN) + 1); w += strlen(INVISIBLE_SKIN) + 1; }
    return out;
}

static int HandsPackExists(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" HANDS_PACK_FILE, g_dir);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Makes the pack from the game's own paks (the same as the installer's). 1 when it is there.
static int BuildHandsPack(void)
{
    char pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int count = 0, cap = 0, i, ok, models = 0;
    mz_zip_archive zw;
    static BYTE tga[18 + 4 * 4 * 4];

    _snprintf(pattern, sizeof(pattern), "%s\\main\\pak*.pk3", g_dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!_strnicmp(fd.cFileName, "pakz_vr", 7)) continue;   // our own packs
        if (count == cap) { cap = cap ? cap * 2 : 32; paks = realloc(paks, cap * sizeof(*paks)); }
        _snprintf(paks[count], MAX_PATH, "%s", fd.cFileName);
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (count) qsort(paks, count, sizeof(*paks), CmpNames);   // the engine's order: later paks override

    g_numHandModels = 0;
    for (i = 0; i < count; i++) {
        mz_zip_archive z;
        mz_uint e;
        memset(&z, 0, sizeof(z));
        _snprintf(path, sizeof(path), "%s\\main\\%s", g_dir, paks[i]);
        if (!mz_zip_reader_init_file(&z, path, 0)) continue;
        for (e = 0; e < mz_zip_reader_get_num_files(&z); e++) {
            char name[256];
            mz_zip_reader_get_filename(&z, e, name, sizeof(name));
            if (!_strnicmp(name, "xmodel/viewmodel_hands", 22)) {
                size_t size;
                void *data = mz_zip_reader_extract_to_heap(&z, e, &size, 0);
                if (data) KeepHandModel(name, data, size);
            }
        }
        mz_zip_reader_end(&z);
    }
    free(paks);
    if (!g_numHandModels) return 0;

    memset(tga, 0, sizeof(tga));   // 4x4, 32 bits, every pixel transparent
    tga[2] = 2; tga[12] = 4; tga[14] = 4; tga[16] = 32; tga[17] = 8;

    _snprintf(out, sizeof(out), "%s" HANDS_PACK_FILE, g_dir);
    _snprintf(tmp, sizeof(tmp), "%s.tmp", out);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, tmp, 0)) return 0;
    ok = mz_zip_writer_add_mem(&zw, "skins/" INVISIBLE_SKIN, tga, sizeof(tga), MZ_DEFAULT_COMPRESSION);
    for (i = 0; i < g_numHandModels; i++) {
        size_t size;
        void *data = ok ? ReplaceSkins(g_handModels[i].data, g_handModels[i].size, &size) : NULL;
        if (data) {
            ok = mz_zip_writer_add_mem(&zw, g_handModels[i].name, data, size, MZ_DEFAULT_COMPRESSION);
            free(data);
            models++;
        }
        free(g_handModels[i].data);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    if (!ok || !models) { DeleteFileA(tmp); return 0; }
    return MoveFileExA(tmp, out, MOVEFILE_REPLACE_EXISTING) != 0;
}

static int RemoveHandsPack(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" HANDS_PACK_FILE, g_dir);
    return DeleteFileA(path) != 0 || !HandsPackExists();
}

// ---------------------------------------------------------------------------
// Voice. The Brazilian Portuguese dub is one pack in the game folder, made by the dub tool
// (dublagem\Dublagem-para-PK3): main\pakz_vr_dub_ptbr.pk3 holds only the voice files that were dubbed, and it sorts after
// the game's own paks, so it wins. The launcher switches it on and off by renaming it (.pk3 <-> .pk3.off, which the game
// does not load): English is the original game. Elevated again (/voicepack pt|en) when the folder is not writable.
// ---------------------------------------------------------------------------
#define VOICE_PACK_FILE   "\\main\\pakz_vr_dub_ptbr.pk3"

static int VoicePackOn(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" VOICE_PACK_FILE, g_dir);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static int VoicePackOff(void)
{
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s" VOICE_PACK_FILE ".off", g_dir);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// 1 when the folder is in the wanted state afterwards (pt: the pack is on; otherwise it is not)
static int SetVoicePack(int pt)
{
    char on[MAX_PATH], off[MAX_PATH];
    _snprintf(on, sizeof(on), "%s" VOICE_PACK_FILE, g_dir);
    _snprintf(off, sizeof(off), "%s" VOICE_PACK_FILE ".off", g_dir);
    if (pt) {
        if (!VoicePackOn() && VoicePackOff()) MoveFileExA(off, on, MOVEFILE_REPLACE_EXISTING);
        return VoicePackOn();
    }
    if (VoicePackOn()) MoveFileExA(on, off, MOVEFILE_REPLACE_EXISTING);
    return !VoicePackOn();
}

// Runs the launcher again as administrator with these arguments; 1 when it worked (exit code 0).
static int RunElevated(HWND wnd, const char *args)
{
    char exe[MAX_PATH];
    SHELLEXECUTEINFOA sei;
    DWORD code = 1;
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = wnd;
    sei.lpVerb = "runas";
    sei.lpFile = exe;
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExA(&sei) || !sei.hProcess) return 0;
    WaitForSingleObject(sei.hProcess, 60000);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

// Runs the launcher again as administrator to make or remove the pack; 1 when it worked.
static int RunHandsHelper(HWND wnd, int hide)
{
    char exe[MAX_PATH];
    SHELLEXECUTEINFOA sei;
    DWORD code = 1;
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = wnd;
    sei.lpVerb = "runas";
    sei.lpFile = exe;
    sei.lpParameters = hide ? "/handspack hide" : "/handspack show";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExA(&sei) || !sei.hProcess) return 0;
    WaitForSingleObject(sei.hProcess, 60000);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
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
    g_vrOpts[VR_HANDS].cur = HandsPackExists();   // "Player hands": Hidden when the pack is there
    g_vrOpts[NUM_VR - 1].cur = VoicePackOn();       // "Voice": Portuguese when the dub pack is on
    for (i = 0; i < NUM_GFX; i++) LoadOption(&g_gfxOpts[i]);
    g_gfxProf = g_playUO;
    if (!GfxProfileSaved(g_gfxProf)) GfxSaveProfile(g_gfxProf);
    {
        // the first MODS had "Infinite health": now the God mode difficulty
        if (GetPrivateProfileIntA("Mods", "InfiniteHealth", 0, g_userIni) == 1 &&
            GetPrivateProfileIntA("Mods", "Difficulty", 0, g_userIni) == 0)
            WritePrivateProfileStringA("Mods", "Difficulty", "4", g_userIni);
        WritePrivateProfileStringA("Mods", "InfiniteHealth", NULL, g_userIni);
    }
    for (i = 0; i < NUM_MOD; i++) LoadOption(&g_modOpts[i]);
    LoadProfileList();
    ApplyProfile();
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
        if (_wcsicmp(pe.szExeFile, L"CoDSP.exe") && _wcsicmp(pe.szExeFile, L"CoDUOSP.exe")) continue;
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
#define STEAM_APP_ID    "2620"   // Call of Duty
#define STEAM_APP_ID_UO "2640"   // Call of Duty: United Offensive
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
static int StartThroughSteam(HWND wnd, const char *args, const char *app)
{
    char url[1024];
    int n = _snprintf(url, sizeof(url), "steam://run/%s//", app);
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
    GfxSelect(g_playUO);   // this game's graphics profile into the keys the mod and the lines below read
    _snprintf(exe, sizeof(exe), "%s\\%s", g_dir, g_playUO ? "CoDUOSP.exe" : "CoDSP.exe");
    GetPrivateProfileStringA("Launcher", "Picmip", "0", picmip, sizeof(picmip), g_userIni);
    GetPrivateProfileStringA("Launcher", "Gamma", "1", gamma, sizeof(gamma), g_userIni);
    // r_ignorehwgamma: the game's own brightness is a ramp on the monitor, which the headset never
    // sees; with it ignored, r_gamma brightens the textures themselves
    // full colour depth and trilinear filtering for the textures as well
    _snprintf(args, sizeof(args), "+set r_picmip %d +set r_texturebits 32 +set r_textureMode GL_LINEAR_MIPMAP_LINEAR"
              " +set r_ignorehwgamma 1 +set r_gamma %.2f +set com_introplayed 1", atoi(picmip), atof(gamma));
    if (SteamCopy() && SteamInstalled()) {
        if (StartThroughSteam(wnd, args, g_playUO ? STEAM_APP_ID_UO : STEAM_APP_ID)) return 1;
        MessageBoxW(wnd, L"Could not start the game through Steam. Is Steam installed and logged in?",
                    L"Call of Duty VR", MB_ICONERROR);
        return 0;
    }
    _snprintf(cmd, sizeof(cmd), "\"%s\" %s", exe, args);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
        MessageBoxW(wnd, g_playUO ? L"Could not start CoDUOSP.exe." : L"Could not start CoDSP.exe.", L"Call of Duty VR", MB_ICONERROR);
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
enum { HIT_NONE, HIT_PLAY, HIT_NAV, HIT_PREV, HIT_NEXT, HIT_ROW, HIT_LINK, HIT_GAME, HIT_RESET, HIT_PROFILE, HIT_NEWPROF, HIT_DELPROF, HIT_GFXPROF };

typedef struct { RECT r; int type, arg; } hit_t;
static hit_t   g_hits[256];   // a page of options has up to 3 areas each (row, left arrow, right arrow)
static int     g_numHits, g_hover = -1, g_page = PAGE_HOME;
static hit_t   g_hitsDone[256];   // the areas of the last COMPLETE paint: the hover is looked up here (g_hits is half built while a paint draws)
static int     g_numHitsDone;
static HBITMAP g_banner, g_bannerUO;   // the banner of each game
static int     g_bannerW, g_bannerH, g_bannerUOW, g_bannerUOH;
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
    return g_hover >= 0 && g_hover < g_numHitsDone && g_hitsDone[g_hover].type == type && g_hitsDone[g_hover].arg == arg;
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
static void VeilRGB(HDC dc, int x0, int y0, int x1, int y1, COLORREF color, BYTE alpha)
{
    HDC src = CreateCompatibleDC(dc);
    HBITMAP one = CreateCompatibleBitmap(dc, 1, 1);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    HGDIOBJ old = SelectObject(src, one);
    SetPixel(src, 0, 0, color);
    AlphaBlend(dc, x0, y0, x1 - x0, y1 - y0, src, 0, 0, 1, 1, bf);
    SelectObject(src, old);
    DeleteObject(one);
    DeleteDC(src);
}

static void Veil(HDC dc, int x0, int y0, int x1, int y1, BYTE alpha) { VeilRGB(dc, x0, y0, x1, y1, RGB(6, 8, 10), alpha); }

static void DrawBanner(HDC dc)
{
    Fill(dc, 0, 0, P(BASE_W), P(BAR_Y), RGB(0, 0, 0));
    {
        // the banner of the game PLAY starts
        HBITMAP bmp = g_playUO && g_bannerUO ? g_bannerUO : g_banner;
        int bw = g_playUO && g_bannerUO ? g_bannerUOW : g_bannerW, bh = g_playUO && g_bannerUO ? g_bannerUOH : g_bannerH;
        if (bmp) {
        HDC src = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(src, bmp);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, 0, 0, P(BASE_W), P(BANNER_H), src, 0, 0, bw, bh, SRCCOPY);
        SelectObject(src, old);
        DeleteDC(src);
        }
    }
}

// ---------------------------------------------------------------------------
// Update check: once at start, in the background, the latest release on GitHub. A newer one
// is shown on the home page (a link to its download page); nothing is downloaded.
// ---------------------------------------------------------------------------
#define WM_UPDATE_FOUND (WM_APP + 1)
static wchar_t g_newVersion[32];

// "v1.10.2" -> 1 10 2; a > b
static int NewerVersion(const char *a, const wchar_t *b)
{
    int x[3] = { 0 }, y[3] = { 0 }, i;
    if (*a == 'v' || *a == 'V') a++;
    if (*b == L'v' || *b == L'V') b++;
    sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    swscanf(b, L"%d.%d.%d", &y[0], &y[1], &y[2]);
    for (i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
    return 0;
}

static DWORD WINAPI UpdateCheckThread(LPVOID param)
{
    HWND wnd = (HWND)param;
    HINTERNET s = NULL, c = NULL, r = NULL;
    char body[65536], *tag;
    DWORD got, total = 0;
    s = WinHttpOpen(L"CoD-VR-Launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
    if (s) c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (c) r = WinHttpOpenRequest(c, L"GET", L"/repos/lambda-gab/cod1-vr/releases/latest", NULL,
                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        while (total < sizeof(body) - 1 && WinHttpReadData(r, body + total, sizeof(body) - 1 - total, &got) && got) total += got;
    }
    body[total] = 0;
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if ((tag = strstr(body, "\"tag_name\"")) && (tag = strchr(tag + 10, '"'))) {
        char v[32];
        int i;
        tag++;
        for (i = 0; i < 31 && tag[i] && tag[i] != '"'; i++) v[i] = tag[i];
        v[i] = 0;
        if (v[0] && NewerVersion(v, VERSION_TEXT)) {
            MultiByteToWideChar(CP_UTF8, 0, v, -1, g_newVersion, 32);
            PostMessageW(wnd, WM_UPDATE_FOUND, 0, 0);
        }
    }
    return 0;
}

int WriteDiagnostics(const char *gameDir, const wchar_t *version, char *outPath, int outLen);

// ---------------------------------------------------------------------------
// Patreon supporters: a rounded, translucent black column over the left of the home banner. The list is
// supporters.ini in the game folder (UTF-8), shown in the file's own order:
//   [Panel]       Title=Patreon Supporters
//   [Supporters]  one per line, "Tier|Name" (a "1=" before the tier is ignored); Tier = Recruit, Veteran or Master
// Masters get the most: a gold plate, the largest badge, the name in gold and "[MASTER]" under it. Veterans have a
// light plate and a bold name, Recruits a plain line. More names than fit are shown a page at a time.
// ---------------------------------------------------------------------------
enum { SUP_RECRUIT, SUP_VETERAN, SUP_MASTER, SUP_TIERS };
#define MAX_SUP 64
#define SUP_ICON 96
typedef struct { int tier; wchar_t name[48]; } supporter_t;
static supporter_t g_sup[MAX_SUP];
static int         g_numSup;
static wchar_t     g_supTitle[40] = L"Patreon Supporters";
static HBITMAP     g_supBmp[SUP_TIERS];       // each badge at its size on screen (made on first use)
static int         g_supSize[SUP_TIERS];
static int         g_supPageCount = 1;

static void LoadSupporters(void)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD got = 0;
    static char buf[65536];
    static wchar_t wide[65536];
    wchar_t *p, *line;
    int n, section = 0;
    g_numSup = 0;
    _snprintf(path, sizeof(path), "%s\\supporters.ini", g_dir);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(f, buf, sizeof(buf) - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    buf[got] = 0;
    {
        const char *src = buf;
        if (got >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF) src += 3;
        n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, wide, 65535);
        if (!n) n = MultiByteToWideChar(CP_ACP, 0, src, -1, wide, 65535);   // not UTF-8: the Windows code page
        if (!n) return;
    }
    for (p = wide; *p; ) {
        wchar_t *end, *bar;
        line = p;
        while (*p && *p != L'\n') p++;
        end = p;
        if (*p) p++;
        while (end > line && (end[-1] == L'\r' || end[-1] == L' ' || end[-1] == L'\t')) end--;
        *end = 0;
        while (*line == L' ' || *line == L'\t') line++;
        if (!*line || *line == L';' || *line == L'#') continue;
        if (*line == L'[') {
            section = !_wcsnicmp(line, L"[Panel]", 7) ? 1 : !_wcsnicmp(line, L"[Supporters]", 12) ? 2 : 0;
            continue;
        }
        if (section == 1) {
            if (!_wcsnicmp(line, L"Title=", 6) && line[6]) { wcsncpy(g_supTitle, line + 6, 39); g_supTitle[39] = 0; }
        } else if (section == 2 && g_numSup < MAX_SUP) {
            wchar_t *eq = wcschr(line, L'=');
            int tier;
            bar = wcschr(line, L'|');
            if (!bar || !bar[1]) continue;
            if (eq && eq < bar) line = eq + 1;      // "1=Master|Name"
            bar = wcschr(line, L'|');
            while (*line == L' ') line++;
            if (!_wcsnicmp(line, L"Master", 6)) tier = SUP_MASTER;
            else if (!_wcsnicmp(line, L"Veteran", 7)) tier = SUP_VETERAN;
            else if (!_wcsnicmp(line, L"Recruit", 7)) tier = SUP_RECRUIT;
            else continue;
            bar++;
            while (*bar == L' ') bar++;
            if (!*bar) continue;
            g_sup[g_numSup].tier = tier;
            wcsncpy(g_sup[g_numSup].name, bar, 47);
            g_sup[g_numSup].name[47] = 0;
            g_numSup++;
        }
    }
}

// A badge (96x96 premultiplied BGRA from the resource) shrunk to size x size with a box filter.
static HBITMAP MakeBadge(int tier, int size)
{
    static const int ids[SUP_TIERS] = { IDR_SUP_RECRUIT, IDR_SUP_VETERAN, IDR_SUP_MASTER };
    HMODULE inst = GetModuleHandleA(NULL);
    HRSRC res = FindResourceA(inst, MAKEINTRESOURCEA(ids[tier]), (LPCSTR)RT_RCDATA);
    const BYTE *src = res && SizeofResource(inst, res) >= SUP_ICON * SUP_ICON * 4 ? (const BYTE *)LockResource(LoadResource(inst, res)) : NULL;
    BITMAPINFO bi;
    BYTE *dst = NULL;
    HBITMAP bmp;
    int x, y;
    if (!src || size < 4) return NULL;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&dst, NULL, 0);
    if (!bmp || !dst) return NULL;
    for (y = 0; y < size; y++) {
        int sy0 = y * SUP_ICON / size, sy1 = (y + 1) * SUP_ICON / size;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (x = 0; x < size; x++) {
            int sx0 = x * SUP_ICON / size, sx1 = (x + 1) * SUP_ICON / size, sx, sy, k, cnt = 0, sum[4] = { 0, 0, 0, 0 };
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; sy++)
                for (sx = sx0; sx < sx1; sx++) {
                    const BYTE *s = src + (sy * SUP_ICON + sx) * 4;
                    for (k = 0; k < 4; k++) sum[k] += s[k];
                    cnt++;
                }
            for (k = 0; k < 4; k++) dst[(y * size + x) * 4 + k] = (BYTE)(sum[k] / cnt);
        }
    }
    return bmp;
}

static void DrawBadge(HDC dc, int tier, int x, int y, int size)
{
    HDC mem;
    HGDIOBJ old;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!g_supBmp[tier] || g_supSize[tier] != size) {
        if (g_supBmp[tier]) DeleteObject(g_supBmp[tier]);
        g_supBmp[tier] = MakeBadge(tier, size);
        g_supSize[tier] = size;
    }
    if (!g_supBmp[tier]) return;
    mem = CreateCompatibleDC(dc);
    old = SelectObject(mem, g_supBmp[tier]);
    AlphaBlend(dc, x, y, size, size, mem, 0, 0, size, size, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// A translucent plate with rounded corners (its fill blended over what is behind, then a thin frame).
static void RoundPlate(HDC dc, int x0, int y0, int x1, int y1, int radius, COLORREF fill, BYTE alpha, COLORREF edge)
{
    POINT corner[2] = { { x0, y0 }, { x1 + 1, y1 + 1 } };
    HRGN rgn;
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldPen, oldBrush;
    LPtoDP(dc, corner, 2);   // clip regions are in device units (the export draws with the viewport moved)
    rgn = CreateRoundRectRgn(corner[0].x, corner[0].y, corner[1].x, corner[1].y, radius, radius);
    SelectClipRgn(dc, rgn);
    VeilRGB(dc, x0, y0, x1, y1, fill, alpha);
    SelectClipRgn(dc, NULL);
    DeleteObject(rgn);
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, x0, y0, x1, y1, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static int SupRowHeight(int tier) { return tier == SUP_MASTER ? 36 : tier == SUP_VETERAN ? 26 : 22; }

// exportIt: every name on one tall panel (for the game's own menu, ExportSupporterPanel); *outH gets the panel's height
static void DrawSupporters(HDC dc, int exportIt, int *outH)
{
    const int x0 = 14, y0 = 14, x1 = 244, top = y0 + 34;
    int y1 = 354, bottom;
    int i, page = 0, pages = 1, y, used = 0, first = 0, last = 0;
    if (g_numSup <= 0) return;
    if (exportIt) {
        int total = 0;
        for (i = 0; i < g_numSup; i++) total += SupRowHeight(g_sup[i].tier) + 4;
        y1 = top + total + 8;
    }
    bottom = exportIt ? 100000 : y1 - 24;
    if (outH) *outH = y1 - y0;
    // the rows that fit on the page showing now (a new page every 6 s)
    for (i = 0, used = 0; i < g_numSup; i++) {
        int h = SupRowHeight(g_sup[i].tier) + 4;
        if (used + h > bottom - top && used > 0) { pages++; used = 0; }
        used += h;
    }
    g_supPageCount = pages;
    page = pages > 1 ? (int)((GetTickCount() / 6000) % pages) : 0;
    {
        int cur = 0;
        for (i = 0, used = 0; i < g_numSup; i++) {
            int h = SupRowHeight(g_sup[i].tier) + 4;
            if (used + h > bottom - top && used > 0) { cur++; used = 0; }
            if (cur == page) { if (used == 0 && i >= 0) first = i; last = i + 1; }
            used += h;
        }
    }
    RoundPlate(dc, P(x0), P(y0), P(x1), P(y1), P(16), RGB(0, 0, 0), 175, RGB(120, 98, 44));
    Text(dc, g_fBold, COL_GOLD, P(x0 + 12), P(y0 + 5), P(x1 - 8), P(y0 + 25), g_supTitle, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    Fill(dc, P(x0 + 12), P(y0 + 27), P(x1 - 12), P(y0 + 27) + 1, RGB(120, 98, 44));
    y = top;
    for (i = first; i < last; i++) {
        const supporter_t *s = &g_sup[i];
        int h = SupRowHeight(s->tier), rx0 = x0 + 8, rx1 = x1 - 8;
        if (s->tier == SUP_MASTER) {
            // the top rank: a gold plate, the big badge, the name in gold and its rank tag
            RoundPlate(dc, P(rx0), P(y), P(rx1), P(y + h), P(11), RGB(150, 110, 20), 85, RGB(214, 170, 62));
            DrawBadge(dc, SUP_MASTER, P(rx0 + 4), P(y + 4), P(28));
            Text(dc, g_fBold, RGB(255, 214, 110), P(rx0 + 38), P(y + 3), P(rx1 - 4), P(y + 20), s->name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            Text(dc, g_fSmall, COL_GOLD, P(rx0 + 38), P(y + 19), P(rx1 - 4), P(y + 33), L"\x2605 [MASTER] \x2605", DT_SINGLELINE | DT_VCENTER);
        } else if (s->tier == SUP_VETERAN) {
            RoundPlate(dc, P(rx0), P(y), P(rx1), P(y + h), P(9), RGB(190, 190, 205), 28, RGB(110, 110, 120));
            DrawBadge(dc, SUP_VETERAN, P(rx0 + 4), P(y + 3), P(20));
            Text(dc, g_fBold, COL_TEXT, P(rx0 + 30), P(y), P(rx1 - 4), P(y + h), s->name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        } else {
            DrawBadge(dc, SUP_RECRUIT, P(rx0 + 5), P(y + 3), P(16));
            Text(dc, g_fText, COL_DIM, P(rx0 + 30), P(y), P(rx1 - 4), P(y + h), s->name, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        y += h + 4;
    }
    if (pages > 1) {
        // which page: one dot each
        int d, cx = (x0 + x1) / 2 - (pages * 12) / 2 + 6;
        HBRUSH on = CreateSolidBrush(COL_GOLD), offb = CreateSolidBrush(RGB(90, 84, 70));
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN)), oldBr = SelectObject(dc, offb);
        for (d = 0; d < pages; d++) {
            SelectObject(dc, d == page ? on : offb);
            Ellipse(dc, P(cx + d * 12 - 3), P(y1 - 17), P(cx + d * 12 + 4), P(y1 - 10));
        }
        SelectObject(dc, oldBr);
        SelectObject(dc, oldPen);
        DeleteObject(on);
        DeleteObject(offb);
    }
}

// The same panel as an image for the game's main menu (the mod shows it beside the menu in the headset): the supporters
// drawn at twice the size on black, then given an alpha (a rounded translucent plate, solid where something was drawn),
// premultiplied, bottom row first: %APPDATA%\CoD-VR\supporters_panel.bin = "SUPP", width, height, BGRA pixels.
// No supporters: the file is removed.
static HFONT MakeFont(int px, int weight, const wchar_t *face);
static void ExportSupporterPanel(void)
{
    char path[MAX_PATH], *slash;
    float saved = g_scale;
    HFONT sb = g_fBold, ss = g_fSmall, st = g_fText;
    HDC dc;
    HBITMAP bmp, old;
    BITMAPINFO bi;
    BYTE *px = NULL;
    int w, h, bh = 0, x, y, r;
    HANDLE f;
    DWORD wrote;
    strncpy(path, g_userIni, MAX_PATH - 40);
    path[MAX_PATH - 40] = 0;
    slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "supporters_panel.bin");
    if (g_numSup <= 0) { DeleteFileA(path); return; }
    g_scale = 2.0f;
    g_fBold = MakeFont(14, FW_BOLD, L"Segoe UI");
    g_fText = MakeFont(13, FW_NORMAL, L"Segoe UI");
    g_fSmall = MakeFont(11, FW_NORMAL, L"Segoe UI");
    w = P(230);
    // the height is only known once the panel has said how tall it is: a first, measuring pass on a small bitmap
    dc = CreateCompatibleDC(NULL);
    {
        BITMAP bm;
        HBITMAP tiny = CreateCompatibleBitmap(GetDC(NULL), 8, 8);
        HGDIOBJ o = SelectObject(dc, tiny);
        (void)bm;
        DrawSupporters(dc, 1, &bh);
        SelectObject(dc, o);
        DeleteObject(tiny);
    }
    h = P(bh);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;   // positive: the bottom row comes first in memory
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (bmp && px) {
        old = SelectObject(dc, bmp);
        memset(px, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);   // the text has no box of its own colour
        SetViewportOrgEx(dc, -P(14), -P(14), NULL);
        DrawSupporters(dc, 1, NULL);
        SetViewportOrgEx(dc, 0, 0, NULL);
        GdiFlush();
        r = P(16);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                BYTE *p = px + ((size_t)y * w + x) * 4;
                int dx = x < r ? r - x : x > w - 1 - r ? x - (w - 1 - r) : 0;
                int dy = y < r ? r - y : y > h - 1 - r ? y - (h - 1 - r) : 0;
                int inside = dx * dx + dy * dy <= r * r;
                if (p[0] > 8 || p[1] > 8 || p[2] > 8) p[3] = 255;   // something drawn: solid
                else { p[0] = p[1] = p[2] = 0; p[3] = inside ? 225 : 0; }   // the plate: translucent black
            }
        }
        f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD head[3] = { 0x50505553, (DWORD)w, (DWORD)h };   // "SUPP"
            WriteFile(f, head, sizeof(head), &wrote, NULL);
            WriteFile(f, px, (DWORD)((size_t)w * h * 4), &wrote, NULL);
            CloseHandle(f);
        }
        SelectObject(dc, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    DeleteObject(g_fBold); DeleteObject(g_fText); DeleteObject(g_fSmall);
    g_fBold = sb; g_fSmall = ss; g_fText = st;
    g_scale = saved;
}

static void DrawHome(HDC dc)
{
    // (the supporters are on the game's own main menu now, not here: ExportSupporterPanel writes the image the mod shows)
    int both = g_haveSP && g_haveUO;   // which game PLAY starts is chosen on two big cards
    // Everything sits in the middle of the black area between the banner and the bar of tabs: T is where the block starts.
    int newer = g_newVersion[0] != 0;
    int blockH = both ? (newer ? 116 : 96) : (newer ? 74 : 52);
    int T = BANNER_H + ((BAR_Y - BANNER_H) - blockH) / 2;
    if (both) {
        static const wchar_t *names[2] = { L"CALL OF DUTY", L"UNITED OFFENSIVE" };
        static const wchar_t *subs[2] = { L"The original campaign", L"The expansion" };
        int i;
        for (i = 0; i < 2; i++) {
            int x0 = P(190 + i * 320), x1 = P(190 + i * 320 + 300), y0 = P(T), y1 = P(T + 54);
            int on = g_playUO == i, hot = Hovered(HIT_GAME, i);
            COLORREF edge = on ? COL_GOLD : hot ? COL_STEEL : RGB(80, 80, 80);
            Fill(dc, x0, y0, x1, y1, on ? RGB(52, 40, 12) : hot ? RGB(36, 36, 36) : RGB(24, 24, 24));
            Fill(dc, x0, y0, x1, y0 + P(2), edge);                 // the frame
            Fill(dc, x0, y1 - P(2), x1, y1, edge);
            Fill(dc, x0, y0, x0 + P(2), y1, edge);
            Fill(dc, x1 - P(2), y0, x1, y1, edge);
            Text(dc, g_fNav, on ? COL_GOLD : hot ? COL_TEXT : COL_DIM, x0, y0 + P(4), x1, y0 + P(32), names[i], DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            Text(dc, g_fSmall, on ? COL_TEXT : COL_DIM, x0, y0 + P(30), x1, y1 - P(4), subs[i], DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            AddHit(x0, y0, x1, y1, HIT_GAME, i);
        }
    }
    if (both)
        Text(dc, g_fSmall, COL_TEXT, P(40), P(T + 58), P(960), P(T + 76),
             g_playUO ? L"Call of Duty: United Offensive, in virtual reality." : L"The Call of Duty (2003) single player campaign, in virtual reality.",
             DT_CENTER | DT_SINGLELINE);
    else
        Text(dc, g_fText, COL_TEXT, P(40), P(T), P(960), P(T + 30),
             L"The Call of Duty (2003) single player campaign, in virtual reality.", DT_CENTER | DT_SINGLELINE);
    {
        int sy0 = both ? T + 78 : T + 36;
        if (!g_haveGame)
            Text(dc, g_fSmall, RGB(230, 110, 90), P(40), P(sy0), P(960), P(sy0 + 36),
                 L"CoDSP.exe was not found: put this launcher in the game folder (where CoDSP.exe or CoDUOSP.exe is).", DT_CENTER | DT_WORDBREAK);
        else if (!g_haveMod)
            Text(dc, g_fSmall, RGB(230, 110, 90), P(40), P(sy0), P(960), P(sy0 + 36),
                 L"The VR mod is not installed in this folder: run CoD-VR-Setup.exe first.", DT_CENTER | DT_WORDBREAK);
        else
            Text(dc, g_fSmall, COL_DIM, P(40), P(sy0), P(960), P(sy0 + 18),
                 L"Connect your headset (Virtual Desktop, Air Link or SteamVR with a 32-bit OpenXR runtime), then press PLAY.",
                 DT_CENTER | DT_WORDBREAK);
    }
    if (newer) {
        wchar_t line[128];
        int hot = Hovered(HIT_LINK, 2), y0 = both ? T + 98 : T + 56, y1 = y0 + 18;
        _snwprintf(line, 128, L"A new version (%s) is available: click here to download it.", g_newVersion);
        line[127] = 0;
        Text(dc, g_fBold, hot ? RGB(255, 220, 120) : COL_GOLD, P(40), P(y0), P(960), P(y1), line, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        AddHit(P(250), P(y0), P(750), P(y1), HIT_LINK, 2);
    }
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

static int CrosshairRGB(COLORREF *out, wchar_t *label, int labelLen);

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
        if (hot) {
            Fill(dc, P(x0), y0, P(x1), y1, RGB(66, 52, 22));
            Fill(dc, P(x0), y0, P(x0) + P(3), y1, COL_GOLD);
            hint = i;
        }
        Text(dc, g_fText, hot ? COL_GOLD : COL_TEXT, P(x0 + 12), y0, vx0, y1, o->label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (o->swatch) {   // a colour: the swatch and its hex value, no arrows (the click opens the picker)
            COLORREF c;
            wchar_t lab[16];
            int sz = y1 - y0 - P(6);
            CrosshairRGB(&c, lab, 16);
            Fill(dc, vx0 + P(34), y0 + P(3), vx0 + P(34) + sz, y0 + P(3) + sz, hot ? COL_GOLD : RGB(120, 120, 120));
            Fill(dc, vx0 + P(35), y0 + P(4), vx0 + P(34) + sz - P(1), y0 + P(3) + sz - P(1), c);
            Text(dc, g_fBold, COL_STEEL, vx0 + P(42) + sz, y0, vx1 - P(10), y1, lab, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            AddHit(P(x0), y0, P(x1), y1, HIT_ROW, i);
            continue;
        }
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

static void DrawHintAt(HDC dc, option_t *opts, int hint, int y)
{
    if (hint >= 0)
        Text(dc, g_fSmall, COL_DIM, P(50), P(y), P(950), P(y + 32),
             opts[hint].choiceHints && opts[hint].cur >= 0 ? opts[hint].choiceHints[opts[hint].cur] : opts[hint].hint,
             DT_SINGLELINE | DT_END_ELLIPSIS);
    else
        Text(dc, g_fSmall, COL_DIM, P(50), P(y), P(950), P(y + 32),
             L"Click a setting to change it. Changes are saved at once and used the next time the game starts.", DT_SINGLELINE);
}

static void DrawHint(HDC dc, option_t *opts, int hint) { DrawHintAt(dc, opts, hint, 444); }

// The description of the option under the mouse, in a rounded box at the bottom of the page (VR tab). Empty page: a line of advice.
static void DrawDescriptionBox(HDC dc, option_t *opts, int hint)
{
    int x0 = P(40), y0 = P(492), x1 = P(960), y1 = P(562);
    const wchar_t *s = hint >= 0 ? (opts[hint].choiceHints && opts[hint].cur >= 0 ? opts[hint].choiceHints[opts[hint].cur] : opts[hint].hint)
                                 : L"Move the mouse over an option to see what it does. Click it to change it: changes are saved at once and used the next time the game starts.";
    HPEN pen = CreatePen(PS_SOLID, P(1) > 1 ? P(1) : 1, hint >= 0 ? COL_GOLD_DARK : RGB(80, 80, 80));
    HBRUSH brush = CreateSolidBrush(RGB(18, 18, 18));
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
    RoundRect(dc, x0, y0, x1, y1, P(16), P(16));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    Text(dc, g_fSmall, hint >= 0 ? COL_TEXT : COL_DIM, x0 + P(14), y0 + P(7), x1 - P(14), y1 - P(6), s, DT_WORDBREAK);
}

static void DrawOptions(HDC dc, option_t *opts, int n, int rowH, int top)
{
    DrawHint(dc, opts, DrawRows(dc, opts, 0, n, rowH, top));
}

// GRAPHICS: the main settings, then the post-processing ones under their own title.
static void DrawGraphics(HDC dc)
{
    int h1, h2;
    int both = g_haveSP && g_haveUO;
    DrawPageTitle(dc, L"GRAPHICS", both ? L"Each game keeps its own graphics: pick the game first. The game's own options (Options in the game menu) still apply on top."
                                        : L"The game's own options (Options in the game menu) still apply on top of these.");
    if (both) {
        static const wchar_t *names[2] = { L"CALL OF DUTY", L"UNITED OFFENSIVE" };
        int i;
        for (i = 0; i < 2; i++) {
            int x0 = P(560 + i * 200), x1 = P(750 + i * 200), y0 = P(26), y1 = P(54);
            int on = g_gfxProf == i, hot = Hovered(HIT_GFXPROF, i);
            Fill(dc, x0, y0, x1, y1, on ? COL_GOLD : hot ? COL_STEEL : RGB(80, 80, 80));
            Fill(dc, x0 + P(1), y0 + P(1), x1 - P(1), y1 - P(1), on ? RGB(52, 40, 12) : RGB(24, 24, 24));
            Text(dc, g_fBold, on ? COL_GOLD : hot ? COL_TEXT : COL_DIM, x0, y0, x1, y1, names[i], DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            AddHit(x0, y0, x1, y1, HIT_GFXPROF, i);
        }
    }
    h1 =DrawRows(dc, g_gfxOpts, 0, GFX_POST_FIRST, 30, 94);
    Text(dc, g_fBold, COL_GOLD, P(50), P(278), P(950), P(300), L"POST-PROCESSING", DT_SINGLELINE | DT_VCENTER);
    Text(dc, g_fSmall, COL_DIM, P(50), P(300), P(950), P(318),
         L"Test options: if the image looks worse, leave them off.", DT_SINGLELINE | DT_VCENTER);
    h2 = DrawRows(dc, g_gfxOpts, GFX_POST_FIRST, NUM_GFX - GFX_POST_FIRST, 29, 322);
    DrawHint(dc, g_gfxOpts, h1 >= 0 ? h1 : h2);
}

// One drop-down box of the CONTROLS page: the label, the choice, an arrow; clicking opens the list.
static void DrawDropDown(HDC dc, int i, int y0, int y1, int lx0, int bx0, int rx0, int rx1)
{
    int arg = CTL_ARG + i, hot = Hovered(HIT_ROW, arg), cur = g_ctl[i].cur;
    int changed = cur < 0 || !IsDefault(i, cur);
    if (hot) Fill(dc, rx0, y0, rx1, y1, RGB(34, 30, 20));
    Text(dc, g_fText, COL_TEXT, lx0, y0, bx0, y1, g_ctl[i].label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    Fill(dc, bx0, y0, rx1, y1, hot ? COL_GOLD : RGB(80, 80, 80));
    Fill(dc, bx0 + P(1), y0 + P(1), rx1 - P(1), y1 - P(1), RGB(24, 24, 24));
    Text(dc, g_fBold, changed ? COL_GOLD : COL_STEEL, bx0 + P(8), y0, rx1 - P(26), y1,
         cur >= 0 ? g_ctl[i].ch[cur].name : L"Custom", DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    Text(dc, g_fSmall, hot ? COL_GOLD : COL_DIM, rx1 - P(26), y0, rx1, y1, L"\x25BC", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    AddHit(rx0, y0, rx1, y1, HIT_ROW, arg);
}

static void DrawControls(HDC dc)
{
    int i;
    DrawPageTitle(dc, L"CONTROLS", L"Touch controllers. Click a box to pick from a list. Changes are used the next time the game starts.");
    {
        // the profile in use: a drop-down, then New and Delete
        wchar_t pname[40];
        int hotP = Hovered(HIT_PROFILE, 0), hotN = Hovered(HIT_NEWPROF, 0), hotD = Hovered(HIT_DELPROF, 0), y0 = P(26), y1 = P(54);
        MultiByteToWideChar(CP_ACP, 0, g_profNames[g_profile], -1, pname, 40);
        Text(dc, g_fText, COL_TEXT, P(330), y0, P(410), y1, L"Profile", DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        Fill(dc, P(424), y0, P(690), y1, hotP ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, P(425), y0 + P(1), P(689), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, COL_STEEL, P(434), y0, P(662), y1, pname, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        Text(dc, g_fSmall, hotP ? COL_GOLD : COL_DIM, P(662), y0, P(690), y1, L"\x25BC", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(P(424), y0, P(690), y1, HIT_PROFILE, 0);
        Fill(dc, P(702), y0, P(810), y1, hotN ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, P(703), y0 + P(1), P(809), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, hotN ? COL_GOLD : COL_TEXT, P(702), y0, P(810), y1, L"New", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(P(702), y0, P(810), y1, HIT_NEWPROF, 0);
        Fill(dc, P(822), y0, P(930), y1, g_profile > 0 && hotD ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, P(823), y0 + P(1), P(929), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, g_profile == 0 ? COL_DIM : hotD ? COL_GOLD : COL_TEXT, P(822), y0, P(930), y1, L"Delete", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (g_profile > 0) AddHit(P(822), y0, P(930), y1, HIT_DELPROF, 0);
    }
    // the dominant hand is the most important choice: a row of its own across the page (what the hands do with the weapons
    // follows the VR tab's Weapon handling)
    DrawDropDown(dc, NUM_HOLSTERS, P(92), P(118), P(60), P(230), P(40), P(470));
    // left column: the weapons on the body, then the sticks; right column: the buttons, then their shortcuts
    Text(dc, g_fBold, COL_GOLD, P(50), P(130), P(480), P(154), g_armed ? L"Grip on your body (either hand): weapon to your hand" : L"Take weapons from your body (grip)", DT_SINGLELINE);
    for (i = 0; i < NUM_HOLSTERS; i++) {
        int y0 = P(158 + i * 30);
        DrawDropDown(dc, i, y0, y0 + P(26), P(60), P(230), P(40), P(480));
    }
    Text(dc, g_fBold, COL_GOLD, P(50), P(376), P(480), P(400), L"Sticks", DT_SINGLELINE);
    {
        // the stick that does not move you is free: its four directions take an action
        static const wchar_t *const left[4] = { L"Left stick up", L"Left stick down", L"Left stick left", L"Left stick right" };
        static const wchar_t *const right[4] = { L"Right stick up", L"Right stick down", L"Right stick left", L"Right stick right" };
        int moveRight = g_ctl[CTL_MOVE].cur == 1;
        for (i = CTL_MOVE; i < NUM_CTL; i++) {
            int y0 = P(404 + (i - CTL_MOVE) * 28);
            if (i > CTL_MOVE) g_ctl[i].label = moveRight ? left[i - CTL_MOVE - 1] : right[i - CTL_MOVE - 1];
            DrawDropDown(dc, i, y0, y0 + P(24), P(60), P(230), P(40), P(480));
        }
    }
    {
        // the dominant hand's two buttons (A and B, or X and Y), then the stick clicks
        int rh = g_ctl[NUM_HOLSTERS].cur != 1, first = NUM_HOLSTERS + NUM_HANDS, k;
        static const wchar_t *const plain[4] = { L"A", L"B", L"X", L"Y" };
        static const wchar_t *const tap[4] = { L"A (tap)", L"B (tap)", L"X (tap)", L"Y (tap)" };
        for (k = 0; k < 4; k++) g_ctl[first + k].label = plain[k];
        Text(dc, g_fBold, COL_GOLD, P(520), P(130), P(950), P(154), L"Buttons", DT_SINGLELINE);
        for (k = 0; k < 4; k++) {
            int idx = k < 2 ? first + (rh ? 0 : 2) + k : first + 4 + (k - 2);   // A B | X Y, then the two stick clicks
            int y0 = P(158 + k * 28);
            DrawDropDown(dc, idx, y0, y0 + P(24), P(540), P(710), P(520), P(960));
        }
        // the two buttons of the other hand are wildcards: tapped on their own they run their action (first two rows), held,
        // then another button, one shortcut each
        Text(dc, g_fBold, COL_GOLD, P(520), P(280), P(960), P(304), rh ? L"X and Y: tap, or hold then press" : L"A and B: tap, or hold then press", DT_SINGLELINE);
        for (k = 0; k < 2; k++) {
            int idx = first + (rh ? 2 : 0) + k, y0 = P(308 + k * 26);
            g_ctl[idx].label = tap[(rh ? 2 : 0) + k];
            DrawDropDown(dc, idx, y0, y0 + P(24), P(540), P(710), P(520), P(960));
        }
        for (k = 0; k < 6; k++) {
            int idx = CTL_SHORTCUT + (rh ? 0 : 6) + k, y0 = P(308 + (k + 2) * 26);
            DrawDropDown(dc, idx, y0, y0 + P(24), P(540), P(710), P(520), P(960));
        }
    }
    {
        int hot = Hovered(HIT_RESET, 0), x0 = P(760), y0 = P(524), x1 = P(960), y1 = P(550);
        Fill(dc, x0, y0, x1, y1, hot ? COL_GOLD : RGB(80, 80, 80));
        Fill(dc, x0 + P(1), y0 + P(1), x1 - P(1), y1 - P(1), RGB(24, 24, 24));
        Text(dc, g_fBold, hot ? COL_GOLD : COL_TEXT, x0, y0, x1, y1, L"Reset to defaults", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        AddHit(x0, y0, x1, y1, HIT_RESET, 0);
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
    Text(dc, g_fBold, Hovered(HIT_LINK, 3) ? COL_GOLD : COL_STEEL, P(50), P(330), P(900), P(360),
         L"Problem starting in VR? Save a diagnostic report to send on Discord", DT_SINGLELINE);
    AddHit(P(50), P(330), P(900), P(360), HIT_LINK, 3);
    Text(dc, g_fSmall, COL_DIM, P(50), P(380), P(950), P(470),
         L"Unofficial fan project, not affiliated with or endorsed by Activision. Call of Duty is a trademark of "
         L"Activision Publishing, Inc. A legitimate copy of the game is required; no game files are included.",
         DT_WORDBREAK);
}

static void DrawBar(HDC dc)
{
    // HOME is the page with the game cards: the way back from any option page
    static const wchar_t *names[] = { L"HOME", L"CONTROLS", L"GRAPHICS", L"VR", L"MODS", L"ABOUT" };
    static const int widths[] = { 80, 130, 130, 60, 90, 100 };
    int i, x = P(230);
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
    for (i = 0; i < 6; i++) {
        int page = PAGE_HOME + i, w = P(widths[i]);
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
            h1 = DrawColumn(dc, g_vrOpts, 0, VR_COL2, 26, 94, 40, 480);
            h2 = DrawColumn(dc, g_vrOpts, VR_COL2, NUM_VR_SHOWN - VR_COL2, 26, 94, 510, 960);   // 13 rows: 26 px each, like the left column
            DrawDescriptionBox(dc, g_vrOpts, h1 >= 0 ? h1 : h2);
        } else if (g_page == PAGE_MODS) {
            DrawPageTitle(dc, L"MODS", L"Things the original game does not have. Used the next time the game starts.");
            DrawOptions(dc, g_modOpts, NUM_MOD, 40, 100);
            if (g_modOpts[MOD_DIFFICULTY].cur >= 0)   // what the chosen difficulty does, always in view
                Text(dc, g_fSmall, COL_STEEL, P(60), P(100 + NUM_MOD * 40 + 12), P(950), P(100 + NUM_MOD * 40 + 60),
                     g_difficultyHints[g_modOpts[MOD_DIFFICULTY].cur], DT_WORDBREAK);
        } else if (g_page == PAGE_ABOUT) DrawAbout(dc);
    }
    DrawBar(dc);
    memcpy(g_hitsDone, g_hits, sizeof(g_hits));   // the list is complete now: this is what the hover is looked up in
    g_numHitsDone = g_numHits;

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
    if (g_page == PAGE_VR && i < NUM_VR_SHOWN) return &g_vrOpts[i];
    if (g_page == PAGE_GRAPHICS && i < NUM_GFX) return &g_gfxOpts[i];
    if (g_page == PAGE_MODS && i < NUM_MOD) return &g_modOpts[i];
    return NULL;
}

// ---- a small dialog asking for a profile's name (built in memory: no resource needed) ----
#define IDC_PROFNAME 100

static INT_PTR CALLBACK NameProc(HWND d, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INITDIALOG) {
        SetWindowLongPtrW(d, DWLP_USER, l);
        SendDlgItemMessageW(d, IDC_PROFNAME, EM_LIMITTEXT, 30, 0);
        SetFocus(GetDlgItem(d, IDC_PROFNAME));
        return FALSE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDOK) {
        GetDlgItemTextW(d, IDC_PROFNAME, (wchar_t *)GetWindowLongPtrW(d, DWLP_USER), 32);
        EndDialog(d, 1);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) {
        EndDialog(d, 0);
        return TRUE;
    }
    return FALSE;
}

static WORD *DlgAlign(WORD *p) { return (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3); }

static WORD *DlgItem(WORD *p, DWORD style, int x, int y, int cx, int cy, WORD id, WORD cls, const wchar_t *text)
{
    DLGITEMTEMPLATE *it;
    p = DlgAlign(p);
    it = (DLGITEMTEMPLATE *)p;
    it->style = style | WS_CHILD | WS_VISIBLE;
    it->dwExtendedStyle = 0;
    it->x = (short)x; it->y = (short)y; it->cx = (short)cx; it->cy = (short)cy;
    it->id = id;
    p = (WORD *)(it + 1);
    *p++ = 0xFFFF;
    *p++ = cls;
    while ((*p++ = *text++) != 0) ;
    *p++ = 0;   // no creation data
    return p;
}

// Asks for a name; returns 1 with it in name (32 characters), 0 when cancelled.
static int AskName(HWND wnd, wchar_t *name)
{
    DWORD buf[256] = { 0 };
    WORD *p = (WORD *)buf;
    DLGTEMPLATE *t = (DLGTEMPLATE *)buf;
    const wchar_t *title = L"New profile", *font = L"Segoe UI";
    t->style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    t->cdit = 4;
    t->cx = 200; t->cy = 62;
    p = (WORD *)(t + 1);
    *p++ = 0;   // no menu
    *p++ = 0;   // default dialog class
    while ((*p++ = *title++) != 0) ;
    *p++ = 9;
    while ((*p++ = *font++) != 0) ;
    p = DlgItem(p, SS_LEFT, 10, 8, 180, 10, (WORD)-1, 0x0082, L"Name of the new profile:");
    p = DlgItem(p, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 20, 180, 14, IDC_PROFNAME, 0x0081, L"");
    p = DlgItem(p, WS_TABSTOP | BS_DEFPUSHBUTTON, 90, 42, 48, 14, IDOK, 0x0080, L"OK");
    p = DlgItem(p, WS_TABSTOP | BS_PUSHBUTTON, 144, 42, 48, 14, IDCANCEL, 0x0080, L"Cancel");
    name[0] = 0;
    return DialogBoxIndirectParamW(GetModuleHandleW(NULL), (DLGTEMPLATE *)buf, wnd, NameProc, (LPARAM)name) == 1;
}

// New profile: asks for a name; it starts as a copy of the one in use and becomes the active one.
static void NewProfile(HWND wnd)
{
    wchar_t wname[32], back[32];
    char name[32], sec[64], val[64];
    int i, len;
    if (g_numProfiles >= MAX_PROFILES) {
        MessageBoxW(wnd, L"There are too many profiles: delete one first.", L"CoD VR", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!AskName(wnd, wname)) return;
    for (len = (int)wcslen(wname); len > 0 && wname[len - 1] == L' '; len--) wname[len - 1] = 0;
    for (i = 0; wname[i] == L' '; i++) ;
    if (i) memmove(wname, wname + i, (wcslen(wname + i) + 1) * sizeof(wchar_t));
    if (!wname[0]) return;
    WideCharToMultiByte(CP_ACP, 0, wname, -1, name, sizeof(name), NULL, NULL);
    name[sizeof(name) - 1] = 0;
    MultiByteToWideChar(CP_ACP, 0, name, -1, back, 32);
    if (wcscmp(wname, back) || strpbrk(name, "[]=")) {
        MessageBoxW(wnd, L"The name has characters that cannot be used.", L"CoD VR", MB_OK | MB_ICONWARNING);
        return;
    }
    for (i = 0; i < g_numProfiles; i++)
        if (!_stricmp(name, g_profNames[i])) {
            MessageBoxW(wnd, L"A profile with that name already exists.", L"CoD VR", MB_OK | MB_ICONWARNING);
            return;
        }
    strcpy(g_profNames[g_numProfiles], name);
    g_profile = g_numProfiles++;
    ProfileSection(g_profile, sec, sizeof(sec));
    for (i = 0; i < NUM_CTL; i++) {   // the copy of what is set now
        GetPrivateProfileStringA(g_ctl[i].section, g_ctl[i].key, CtlDef(i), val, sizeof(val), g_userIni);
        WritePrivateProfileStringA(sec, g_ctl[i].key, val, g_userIni);
    }
    ApplyProfile();
}

// The dominant hand changes: what the profile still holds at the OLD hand's default was never a choice, so it is dropped
// (it takes the new hand's default); what the player changed stays.
static void ChangeHand(int left)
{
    char sec[64], val[64];
    int i, old = g_leftHand;
    ProfileSection(g_profile, sec, sizeof(sec));
    for (i = 0; i < NUM_CTL; i++) {
        if (i == NUM_HOLSTERS || i == NUM_HOLSTERS + 1) continue;
        GetPrivateProfileStringA(sec, g_ctl[i].key, "\x01", val, sizeof(val), g_userIni);
        if (val[0] != 1 && !_stricmp(val, CtlDefFor(i, old))) WritePrivateProfileStringA(sec, g_ctl[i].key, NULL, g_userIni);
    }
    WritePrivateProfileStringA(sec, "RightHanded", left ? "0" : "1", g_userIni);
    ApplyProfile();
}

// The weapon handling changes (free hands <-> always armed): same rule, the buttons still at the old mode's default take the new one's.
static void ChangeArmed(int armed)
{
    char sec[64], val[64];
    int i;
    ProfileSection(g_profile, sec, sizeof(sec));
    for (i = 0; i < NUM_CTL; i++) {
        if (i == NUM_HOLSTERS || i == NUM_HOLSTERS + 1) continue;
        GetPrivateProfileStringA(sec, g_ctl[i].key, "\x01", val, sizeof(val), g_userIni);
        if (val[0] != 1 && !_stricmp(val, CtlDefFor(i, g_leftHand))) WritePrivateProfileStringA(sec, g_ctl[i].key, NULL, g_userIni);   // (g_armed still the old one)
    }
    WritePrivateProfileStringA(sec, "AlwaysArmed", armed ? "1" : "0", g_userIni);
    ApplyProfile();
}

// The crosshair's colour: [Controllers] CrosshairColor is a name (the presets) or RRGGBB (the colour picker). The mod reads both.
static COLORREF g_custColors[16];
static int CrosshairRGB(COLORREF *out, wchar_t *label, int labelLen)
{
    static const struct { const char *name; BYTE r, g, b; } cols[] = {
        { "white", 255, 255, 255 }, { "red", 255, 38, 31 }, { "green", 51, 255, 64 }, { "yellow", 255, 230, 26 },
        { "cyan", 26, 242, 255 }, { "blue", 64, 115, 255 }, { "orange", 255, 140, 26 }, { "magenta", 255, 51, 230 },
    };
    char v[64];
    int i;
    ReadSetting("Controllers", "CrosshairColor", v, sizeof(v));
    if (!v[0]) strcpy(v, "white");
    if (strlen(v) == 6 && strspn(v, "0123456789abcdefABCDEF") == 6) {
        unsigned long rgb = strtoul(v, NULL, 16);
        *out = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        _snwprintf(label, labelLen, L"#%06lX", rgb & 0xFFFFFF);
        label[labelLen - 1] = 0;
        return 1;
    }
    for (i = 0; i < (int)(sizeof(cols) / sizeof(cols[0])); i++)
        if (!_stricmp(v, cols[i].name)) {
            *out = RGB(cols[i].r, cols[i].g, cols[i].b);
            _snwprintf(label, labelLen, L"#%02X%02X%02X", cols[i].r, cols[i].g, cols[i].b);
            label[labelLen - 1] = 0;
            return 1;
        }
    *out = RGB(255, 255, 255);
    _snwprintf(label, labelLen, L"#FFFFFF");
    return 0;
}

static void PickCrosshairColor(HWND wnd)
{
    CHOOSECOLORW cc;
    COLORREF cur;
    wchar_t label[16];
    char hex[16], dir[MAX_PATH], *slash;
    CrosshairRGB(&cur, label, 16);
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = wnd;
    cc.rgbResult = cur;
    cc.lpCustColors = g_custColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    strcpy(dir, g_userIni);
    slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
    _snprintf(hex, sizeof(hex), "%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    WritePrivateProfileStringA("Controllers", "CrosshairColor", hex, g_userIni);
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
    case HIT_GAME:
        g_playUO = h->arg;
        if (g_userIni[0]) WritePrivateProfileStringA("Launcher", "Game", g_playUO ? "UO" : "SP", g_userIni);
        if (g_haveSP && g_haveUO) GfxSelect(g_playUO);   // the game's own graphics, also for a start through Steam
        break;
    case HIT_GFXPROF:
        GfxSelect(h->arg);
        break;
    case HIT_PREV:
    case HIT_NEXT:
    case HIT_ROW:
        if (g_page == PAGE_CONTROLS && h->arg >= CTL_ARG && h->arg < CTL_ARG + NUM_CTL) {
            // the drop-down list, under the box; the current choice is ticked
            int ct = h->arg - CTL_ARG, a, pick;
            HMENU menu = CreatePopupMenu();
            POINT pt = { h->r.right - P(250), h->r.bottom };
            for (a = 0; a < g_ctl[ct].n; a++) {
                wchar_t name[64];
                _snwprintf(name, 64, IsDefault(ct, a) ? L"%s   (default)" : L"%s", g_ctl[ct].ch[a].name);
                name[63] = 0;
                AppendMenuW(menu, MF_STRING | (a == g_ctl[ct].cur ? MF_CHECKED : 0), a + 1, name);
            }
            ClientToScreen(wnd, &pt);
            pick = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, wnd, NULL);
            DestroyMenu(menu);
            if (pick > 0 && ct == NUM_HOLSTERS && pick - 1 != g_ctl[ct].cur) ChangeHand(!strcmp(g_ctl[ct].ch[pick - 1].val, "0"));
            else if (pick > 0 && ct == NUM_HOLSTERS + 1 && pick - 1 != g_ctl[ct].cur) ChangeArmed(!strcmp(g_ctl[ct].ch[pick - 1].val, "1"));
            else if (pick > 0) { g_ctl[ct].cur = pick - 1; SaveControl(ct); }
            break;
        }
        o = PageOption(h->arg);
        if (!o) break;
        if (o->swatch) { PickCrosshairColor(wnd); break; }
        if (g_page == PAGE_VR && o == &g_vrOpts[NUM_VR - 1]) {
            // Voice: switch the dub pack on or off (elevated when the game folder is not writable)
            int pt = !VoicePackOn();
            if (pt && !VoicePackOff()) {
                MessageBoxW(wnd, L"There is no Portuguese dub in the game folder. Make it with Dublagem-para-PK3 (it creates "
                                 L"main\\pakz_vr_dub_ptbr.pk3), then choose it here.", L"CoD VR", MB_OK | MB_ICONINFORMATION);
                break;
            }
            if (!SetVoicePack(pt)) {
                char args[32];
                _snprintf(args, sizeof(args), "/voicepack %s", pt ? "pt" : "en");
                RunElevated(wnd, args);
            }
            o->cur = VoicePackOn();
            if (o->cur != pt)
                MessageBoxW(wnd, L"The voice could not be changed. Rename main\\pakz_vr_dub_ptbr.pk3 (on) or main\\pakz_vr_dub_ptbr.pk3.off (off) by hand.",
                            L"CoD VR", MB_OK | MB_ICONWARNING);
            break;
        }
        if (g_page == PAGE_VR && o == &g_vrOpts[VR_HANDLING]) {
            ChangeArmed(!g_armed);   // Weapon handling: always in hand <-> free hands (buttons still at the old default take the new one's)
            break;
        }
        if (g_page == PAGE_VR && o == &g_vrOpts[VR_HANDS]) {
            // Player hands: make or remove the pack (elevated when the game folder is not writable)
            int hide = !HandsPackExists();
            int done = hide ? BuildHandsPack() : RemoveHandsPack();
            if (!done) done = RunHandsHelper(wnd, hide);
            if (!done)
                MessageBoxW(wnd, hide ? L"The hands could not be hidden: the pack could not be made from your game files. "
                                        L"The mod works without it."
                                      : L"The pack could not be removed. Delete main\\pakz_vr_nohands.pk3 from the game folder.",
                            L"CoD VR", MB_OK | MB_ICONWARNING);
            o->cur = HandsPackExists();
            break;
        }
        if (o->cur < 0) o->cur = o->def;
        else if (h->type == HIT_PREV) o->cur = (o->cur + o->count - 1) % o->count;
        else o->cur = (o->cur + 1) % o->count;
        SaveOption(o);
        if (g_page == PAGE_GRAPHICS) GfxSaveProfile(g_gfxProf);
        break;
    case HIT_RESET:
        if (MessageBoxW(wnd, L"Put every weapon zone and button of this profile back to its default?", L"CoD VR", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            int c, a;
            for (c = 0; c < NUM_CTL; c++) {
                if (c == NUM_HOLSTERS || c == NUM_HOLSTERS + 1) continue;   // the dominant hand and the weapon handling stay: the defaults are theirs
                g_ctl[c].cur = -1;
                for (a = 0; a < g_ctl[c].n; a++)
                    if (IsDefault(c, a)) { g_ctl[c].cur = a; break; }
                SaveControl(c);   // written out: the defaults win over a cod_vr.ini edit as well
            }
        }
        break;
    case HIT_PROFILE: {
        // the drop-down of profiles: the one in use is ticked
        int a, pick;
        HMENU menu = CreatePopupMenu();
        POINT pt = { h->r.left, h->r.bottom };
        for (a = 0; a < g_numProfiles; a++) {
            wchar_t name[40];
            MultiByteToWideChar(CP_ACP, 0, g_profNames[a], -1, name, 40);
            AppendMenuW(menu, MF_STRING | (a == g_profile ? MF_CHECKED : 0), a + 1, name);
        }
        ClientToScreen(wnd, &pt);
        pick = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, wnd, NULL);
        DestroyMenu(menu);
        if (pick > 0 && pick - 1 != g_profile) { g_profile = pick - 1; ApplyProfile(); }
        break;
    }
    case HIT_NEWPROF:
        NewProfile(wnd);
        break;
    case HIT_DELPROF:
        if (g_profile > 0) {
            wchar_t msg[96], name[40];
            MultiByteToWideChar(CP_ACP, 0, g_profNames[g_profile], -1, name, 40);
            _snwprintf(msg, 96, L"Delete the profile \"%s\"?", name);
            msg[95] = 0;
            if (MessageBoxW(wnd, msg, L"CoD VR", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                char sec[64];
                ProfileSection(g_profile, sec, sizeof(sec));
                WritePrivateProfileStringA(sec, NULL, NULL, g_userIni);   // the whole section
                LoadProfileList();
                g_profile = 0;
                ApplyProfile();
            }
        }
        break;
    case HIT_LINK:
        if (h->arg == 3) {
            char out[MAX_PATH], arg[MAX_PATH + 16];
            wchar_t msg[MAX_PATH + 200], wout[MAX_PATH];
            if (!WriteDiagnostics(g_dir, VERSION_TEXT, out, MAX_PATH)) {
                MessageBoxW(wnd, L"The report could not be written.", L"CoD VR", MB_OK | MB_ICONWARNING);
                break;
            }
            MultiByteToWideChar(CP_ACP, 0, out, -1, wout, MAX_PATH);
            _snwprintf(msg, MAX_PATH + 200, L"The report was saved here:\n\n%s\n\nSend this file on Discord.", wout);
            MessageBoxW(wnd, msg, L"CoD VR", MB_OK | MB_ICONINFORMATION);
            _snprintf(arg, sizeof(arg), "/select,\"%s\"", out);
            ShellExecuteA(wnd, "open", "explorer.exe", arg, NULL, SW_SHOWNORMAL);
            break;
        }
        ShellExecuteA(wnd, "open", h->arg == 2 ? URL_RELEASES : h->arg ? URL_DISCORD : URL_GITHUB, NULL, NULL, SW_SHOWNORMAL);
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
        static int lastSupPage = -1;
        if (now != was) { was = now; InvalidateRect(wnd, NULL, FALSE); }
        if (g_page == PAGE_HOME && g_supPageCount > 1) {   // the supporters' next page
            int pg = (int)((GetTickCount() / 6000) % g_supPageCount);
            if (pg != lastSupPage) { lastSupPage = pg; InvalidateRect(wnd, NULL, FALSE); }
        }
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
    case WM_UPDATE_FOUND:
        InvalidateRect(wnd, NULL, FALSE);
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
static void LoadBannerRes(HINSTANCE inst, int id, HBITMAP *out, int *outW, int *outH)
{
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
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
            *out = (HBITMAP)CopyImage((HANDLE)(UINT_PTR)h, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
            if (*out && GetObject(*out, sizeof(bm), &bm)) { *outW = bm.bmWidth; *outH = bm.bmHeight; }
        }
        pic->lpVtbl->Release(pic);
    }
    stream->lpVtbl->Release(stream);
}

static void LoadBanner(HINSTANCE inst)
{
    LoadBannerRes(inst, IDR_BANNER, &g_banner, &g_bannerW, &g_bannerH);
    LoadBannerRes(inst, IDR_BANNER_UO, &g_bannerUO, &g_bannerUOW, &g_bannerUOH);
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
    (void)prev;

    GetModuleFileNameA(NULL, path, sizeof(path));
    slash = strrchr(path, '\\');
    if (slash) *slash = 0;
    strcpy(g_dir, path);
    _snprintf(g_gameIni, sizeof(g_gameIni), "%s\\cod_vr.ini", g_dir);
    _snprintf(g_userIni, sizeof(g_userIni), "%s\\CoD-VR\\settings.ini", appdata ? appdata : g_dir);
    _snprintf(path, sizeof(path), "%s\\CoDSP.exe", g_dir);
    g_haveSP = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    _snprintf(path, sizeof(path), "%s\\CoDUOSP.exe", g_dir);
    g_haveUO = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    g_haveGame = g_haveSP || g_haveUO;
    _snprintf(path, sizeof(path), "%s\\opengl32.dll", g_dir);
    g_haveMod = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    {
        char game[16];
        GetPrivateProfileStringA("Launcher", "Game", "SP", game, sizeof(game), g_userIni);
        g_playUO = g_haveUO && (!strcmp(game, "UO") || !g_haveSP);
    }
    if (cmdLine && wcsstr(cmdLine, L"/handspack"))   // the elevated helper of the Player hands option: no window
        return (wcsstr(cmdLine, L"hide") ? BuildHandsPack() : RemoveHandsPack()) ? 0 : 1;
    if (cmdLine && wcsstr(cmdLine, L"/voicepack"))   // the elevated helper of the Voice option: no window
        return SetVoicePack(wcsstr(cmdLine, L"pt") != NULL) ? 0 : 1;
    if (cmdLine && wcsstr(cmdLine, L"/exportpanel")) {   // the installer: only the supporters' panel image, no window
        LoadSupporters();
        ExportSupporterPanel();
        return 0;
    }
    LoadAll();
    LoadSupporters();

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
    ExportSupporterPanel();   // the same list for the game's main menu

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
    CloseHandle(CreateThread(NULL, 0, UpdateCheckThread, wnd, 0, NULL));
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
