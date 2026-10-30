// CoD VR setup: installs the mod into a Call of Duty folder (one window, drawn by hand), builds the optional data
// packs from the player's own pak files, and uninstalls.
//
// The mod files (opengl32.dll, openxr_loader.dll, cod_vr.ini) and the launcher are embedded as resources. No game
// file is shipped: the hands pack is made here from the installed game's pak*.pk3, the same way
// as mods/build_pk3.py.
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../third_party/miniz/miniz.h"
#define STB_DXT_IMPLEMENTATION
#include "../third_party/stb/stb_dxt.h"
#include "setup_res.h"

#define CODSP_TIMESTAMP   0x40443fda              // CoDSP.exe 1.3
#define CODUO_TIMESTAMP   0x412653f0              // CoDUOSP.exe 1.51 (United Offensive)
#define BACKUP_SUFFIX     L".cod_vr_backup"
#define HANDS_PACK        L"main\\pakz_vr_nohands.pk3"
#define TEXT_PACK         L"main\\pakz_vr_text.pk3"
#define GORE_PACK         L"main\\pakz_vr_gore.pk3"   // loaded only with MODS > Gore: Gore (the mod hides it otherwise)
#define FONT_PACK         L"main\\pakz_vr_font.pk3"   // loaded only with GRAPHICS > Text font: Modern
#define SHOCK_PACK        L"main\\pakz_vr_noshake.pk3" // shell shock without its view shake (always on)
#define LAUNCHER_EXE      L"CoD-VR-Launcher.exe"
#define SHORTCUT_NAME     L"Call of Duty VR.lnk"
#define INVISIBLE_SKIN    "cloth_masked@vr_invisible.tga"   // alpha-tested skin type, alpha 0

static const wchar_t *TITLE = L"Call of Duty VR";
static int pt;   // Portuguese UI

#define T(en, ptbr) (pt ? (ptbr) : (en))

static int Ask(UINT type, const wchar_t *fmt, ...)
{
    wchar_t msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 2047, fmt, ap);
    va_end(ap);
    msg[2047] = 0;
    return MessageBoxW(NULL, msg, TITLE, type | MB_SETFOREGROUND);
}

static int FileExists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void Join(wchar_t *out, const wchar_t *dir, const wchar_t *name)
{
    _snwprintf(out, MAX_PATH, L"%s\\%s", dir, name);
    out[MAX_PATH - 1] = 0;
}

static int HasExe(const wchar_t *dir, const wchar_t *exe)
{
    wchar_t p[MAX_PATH];
    Join(p, dir, exe);
    return FileExists(p);
}

// A Call of Duty folder: the original game (CoDSP.exe), United Offensive (CoDUOSP.exe) or both in one folder.
static int HasGame(const wchar_t *dir)
{
    return HasExe(dir, L"CoDSP.exe") || HasExe(dir, L"CoDUOSP.exe");
}

// PE header TimeDateStamp of a game exe, 0 if unreadable
static DWORD ExeTimestamp(const wchar_t *dir, const wchar_t *exe)
{
    wchar_t p[MAX_PATH];
    BYTE buf[1024];
    DWORD n = 0, pe;
    HANDLE f;
    Join(p, dir, exe);
    f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ReadFile(f, buf, sizeof(buf), &n, NULL);
    CloseHandle(f);
    if (n < 0x40) return 0;
    pe = *(DWORD *)(buf + 0x3c);
    if (pe + 12 > n || memcmp(buf + pe, "PE\0\0", 4)) return 0;
    return *(DWORD *)(buf + pe + 8);
}

static int RegPath(HKEY root, const wchar_t *key, const wchar_t *value, wchar_t *out)
{
    DWORD size = MAX_PATH * sizeof(wchar_t), type;
    HKEY k;
    int ok = 0;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | KEY_WOW64_32KEY, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExW(k, value, NULL, &type, (BYTE *)out, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        size_t len = wcslen(out);
        while (len && (out[len - 1] == L'\\' || out[len - 1] == L'/')) out[--len] = 0;
        ok = len > 0;
    }
    RegCloseKey(k);
    return ok;
}

// Candidates: the folder this setup runs from, the Activision install keys (the original game, United Offensive), Steam.
static int FindGame(wchar_t *out)
{
    static const wchar_t *const keys[] = { L"SOFTWARE\\Activision\\Call of Duty", L"SOFTWARE\\Activision\\Call of Duty United Offensive" };
    static const wchar_t *const steam[] = { L"\\steamapps\\common\\Call of Duty", L"\\steamapps\\common\\Call of Duty United Offensive" };
    wchar_t p[MAX_PATH], *slash;
    int i;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    if ((slash = wcsrchr(p, L'\\')) != NULL) *slash = 0;
    if (HasGame(p)) { wcscpy(out, p); return 1; }
    for (i = 0; i < 2; i++)
        if (RegPath(HKEY_LOCAL_MACHINE, keys[i], L"InstallPath", p) && HasGame(p)) { wcscpy(out, p); return 1; }
    for (i = 0; i < 2; i++)
        if (RegPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", p)) {
            wcscat(p, steam[i]);
            if (HasGame(p)) { wcscpy(out, p); return 1; }
        }
    return 0;
}

static int PickFolder(wchar_t *out)
{
    BROWSEINFOW bi = { 0 };
    PIDLIST_ABSOLUTE id;
    bi.lpszTitle = T(L"Select the Call of Duty folder (the one with CoDSP.exe or CoDUOSP.exe)",
                     L"Selecione a pasta do Call of Duty (a que tem o CoDSP.exe ou o CoDUOSP.exe)");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    id = SHBrowseForFolderW(&bi);
    if (!id) return 0;
    SHGetPathFromIDListW(id, out);
    CoTaskMemFree(id);
    return 1;
}

// ---------------------------------------------------------------------------
// Embedded files
// ---------------------------------------------------------------------------
static const void *Resource(int id, DWORD *size)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g;
    if (!r) return NULL;
    g = LoadResource(NULL, r);
    *size = SizeofResource(NULL, r);
    return g ? LockResource(g) : NULL;
}

static int WriteResource(int id, const wchar_t *path)
{
    DWORD size, written = 0;
    const void *data = Resource(id, &size);
    HANDLE f;
    if (!data) return 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    WriteFile(f, data, size, &written, NULL);
    CloseHandle(f);
    return written == size;
}

// Our opengl32.dll carries this marker in its log text.
static int IsOurDll(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, n = 0;
    char *buf;
    int found = 0;
    if (f == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(f, NULL);
    if (size > 0 && size < 16 * 1024 * 1024 && (buf = malloc(size)) != NULL) {
        ReadFile(f, buf, size, &n, NULL);
        for (DWORD i = 0; i + 12 <= n && !found; i++) found = !memcmp(buf + i, "cod_vr phase", 12);
        free(buf);
    }
    CloseHandle(f);
    return found;
}

// ---------------------------------------------------------------------------
// "No hands" pack: the game's first-person hand models with a transparent skin
// ---------------------------------------------------------------------------
typedef struct { char name[128]; void *data; size_t size; } entry_t;
static entry_t g_hands[64];
static int g_handCount;

static int CmpW(const void *a, const void *b) { return _wcsicmp((const wchar_t *)a, (const wchar_t *)b); }

static void KeepHandModel(const char *name, void *data, size_t size)
{
    int i;
    for (i = 0; i < g_handCount; i++)
        if (!_stricmp(g_hands[i].name, name)) { free(g_hands[i].data); break; }   // later pak wins
    if (i == g_handCount) {
        if (g_handCount == 64) { free(data); return; }
        g_handCount++;
    }
    strncpy(g_hands[i].name, name, sizeof(g_hands[i].name) - 1);
    g_hands[i].data = data;
    g_hands[i].size = size;
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

static int BuildHandsPack(const wchar_t *game, int *models)
{
    wchar_t pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int count = 0, cap = 0, i, ok = 1;
    mz_zip_archive zw;
    static BYTE tga[18 + 4 * 4 * 4];
    char utf8[MAX_PATH * 3];

    _snwprintf(pattern, MAX_PATH, L"%s\\main\\pak*.pk3", game);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!_wcsnicmp(fd.cFileName, L"pakz_vr", 7)) continue;   // our own packs
        if (count == cap) { cap = cap ? cap * 2 : 32; paks = realloc(paks, cap * sizeof(*paks)); }
        _snwprintf(paks[count++], MAX_PATH, L"%s", fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(paks, count, sizeof(*paks), CmpW);   // the engine's order: later paks override

    g_handCount = 0;
    for (i = 0; i < count; i++) {
        mz_zip_archive z;
        mz_uint e;
        wchar_t path[MAX_PATH];
        memset(&z, 0, sizeof(z));
        _snwprintf(path, MAX_PATH, L"%s\\main\\%s", game, paks[i]);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
        if (!mz_zip_reader_init_file(&z, utf8, 0)) continue;
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
    if (!g_handCount) return 0;

    // 4x4 32-bit TGA, every pixel transparent
    memset(tga, 0, sizeof(tga));
    tga[2] = 2; tga[12] = 4; tga[14] = 4; tga[16] = 32; tga[17] = 8;

    Join(out, game, HANDS_PACK);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, utf8, 0)) return 0;
    ok = mz_zip_writer_add_mem(&zw, "skins/" INVISIBLE_SKIN, tga, sizeof(tga), MZ_DEFAULT_COMPRESSION);
    *models = 0;
    for (i = 0; i < g_handCount && ok; i++) {
        size_t size;
        void *data = ReplaceSkins(g_hands[i].data, g_hands[i].size, &size);
        if (data) {
            ok = mz_zip_writer_add_mem(&zw, g_hands[i].name, data, size, MZ_DEFAULT_COMPRESSION);
            free(data);
            (*models)++;
        }
        free(g_hands[i].data);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    if (!ok || !*models) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// "VR texts" pack: the training texts that talk about the mouse, rewritten. Only these
// sentences come from the mod; the rest of each file comes from the player's localized pak.
// (The "Press [%s]" hints are handled by the mod itself, which names the controller buttons.)
// ---------------------------------------------------------------------------
static const struct { const char *file, *ref, *text; } g_vrTexts[] = {
    { "localizedstrings/english/training.str", "FIVESIGNS", "Point your gun hand at the five large signs." },
    { "localizedstrings/english/training.str", "CLIMBLADDER",
      "To climb a ladder, push the move stick into the ladder and point your gun hand up." },
    // the tank hint about lining the hull up with the turret (a mouse move: the stick drives the tank here)
    { "localizedstrings/english/tankdrivecountry.str", "PRESSTHEJUMPBUTTON", " " },
};

static int IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Replaces the text of   REFERENCE <ref>  LANG_ENGLISH "<text>"   in a .str file. Returns a new
// buffer (and its size) or NULL if the entry is not there.
static char *ReplaceString(const char *d, size_t n, const char *ref, const char *text, size_t *outSize)
{
    size_t i, refLen = strlen(ref);
    for (i = 0; i + 9 < n; i++) {
        size_t k = i + 9, open, close;
        char *out;
        if (memcmp(d + i, "REFERENCE", 9)) continue;
        while (k < n && IsSpace(d[k])) k++;
        if (k + refLen > n || memcmp(d + k, ref, refLen) || (k + refLen < n && !IsSpace(d[k + refLen]))) continue;
        k += refLen;
        while (k < n && IsSpace(d[k])) k++;
        if (k + 12 > n || memcmp(d + k, "LANG_ENGLISH", 12)) continue;
        k += 12;
        while (k < n && IsSpace(d[k])) k++;
        if (k >= n || d[k] != '"') continue;
        open = k + 1;
        for (close = open; close < n && d[close] != '"' && d[close] != '\n'; close++) {}
        if (close >= n || d[close] != '"') continue;
        *outSize = open + strlen(text) + (n - close);
        out = malloc(*outSize);
        if (!out) return NULL;
        memcpy(out, d, open);
        memcpy(out + open, text, strlen(text));
        memcpy(out + open + strlen(text), d + close, n - close);
        return out;
    }
    return NULL;
}

static int BuildTextPack(const wchar_t *game)
{
    wchar_t pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int count = 0, cap = 0, i, t, f, written = 0, ok = 1, zipOpen = 0;
    char utf8[MAX_PATH * 3];
    mz_zip_archive zw;
    const int nTexts = (int)(sizeof(g_vrTexts) / sizeof(g_vrTexts[0]));

    _snwprintf(pattern, MAX_PATH, L"%s\\main\\localized_english_pak*.pk3", game);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;   // not an English game: nothing to change
    do {
        if (count == cap) { cap = cap ? cap * 2 : 16; paks = realloc(paks, cap * sizeof(*paks)); }
        _snwprintf(paks[count++], MAX_PATH, L"%s", fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(paks, count, sizeof(*paks), CmpW);

    Join(out, game, TEXT_PACK);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (mz_zip_writer_init_file(&zw, utf8, 0)) zipOpen = 1; else ok = 0;

    for (f = 0; ok && f < nTexts; f++) {   // one file at a time (its texts follow each other)
        const char *file = g_vrTexts[f].file;
        void *data = NULL;
        size_t size = 0;
        int changedAny = 0;
        if (f > 0 && !strcmp(file, g_vrTexts[f - 1].file)) continue;
        for (i = 0; i < count; i++) {   // the last pak that has the file wins
            mz_zip_archive z;
            int e;
            wchar_t path[MAX_PATH];
            memset(&z, 0, sizeof(z));
            _snwprintf(path, MAX_PATH, L"%s\\main\\%s", game, paks[i]);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
            if (!mz_zip_reader_init_file(&z, utf8, 0)) continue;
            e = mz_zip_reader_locate_file(&z, file, NULL, 0);
            if (e >= 0) {
                size_t sz = 0;
                void *d = mz_zip_reader_extract_to_heap(&z, e, &sz, 0);
                if (d) { free(data); data = d; size = sz; }
            }
            mz_zip_reader_end(&z);
        }
        if (!data) continue;
        for (t = 0; t < nTexts; t++) {
            size_t newSize;
            char *changed;
            if (strcmp(g_vrTexts[t].file, file)) continue;
            changed = ReplaceString(data, size, g_vrTexts[t].ref, g_vrTexts[t].text, &newSize);
            if (changed) { free(data); data = changed; size = newSize; changedAny = 1; }
        }
        if (changedAny) {
            ok = mz_zip_writer_add_mem(&zw, file, data, size, MZ_DEFAULT_COMPRESSION);
            written++;
        }
        free(data);
    }
    free(paks);
    if (zipOpen) {
        if (ok && written) ok = mz_zip_writer_finalize_archive(&zw);
        mz_zip_writer_end(&zw);
    }
    if (!ok || !written) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// Gore pack: the game's blood hit effect (fx/impacts/flesh_hit.efx) made stronger, plus a spray
// of drops of our own. The blood particles (those drawn with the flesh_hit pictures) get more
// particles, live longer, fly faster, fall harder and grow bigger.
// ---------------------------------------------------------------------------
static const char g_goreDrops[] =
    "\r\nParticle\r\n{\r\n"
    "\tname\t\t\t\tvr_drops\r\n\r\n"
    "\tflags\t\t\t\tuseAlpha\r\n\r\n"
    "\tspawnFlags\t\t\torgOnCylinder axisFromSphere\r\n\r\n"
    "\tcount\t\t\t\t16 12\r\n\r\n"
    "\tlife\t\t\t\t700 400\r\n\r\n"
    "\tradius\t\t\t\t4 2\r\n\r\n"
    "\tvelocity\t\t\t160 0 0 200 0 0\r\n\r\n"
    "\tgravity\t\t\t\t-900\r\n\r\n"
    "\trgb\r\n\t{\r\n\t\tstart\t\t\t0.38 0.02 0.02\r\n\t\tend\t\t\t\t0.22 0.01 0.01\r\n\t\tflags\t\t\trandom linear\r\n\t}\r\n\r\n"
    "\talpha\r\n\t{\r\n\t\tstart\t\t\t0.95\r\n\t\tend\t\t\t\t0\r\n\t\tflags\t\t\tlinear\r\n\t}\r\n\r\n"
    "\tsize\r\n\t{\r\n\t\tstart\t\t\t2 2\r\n\t\tend\t\t\t\t5 6\r\n\t\tflags\t\t\tlinear\r\n\t}\r\n\r\n"
    "\tshaders\r\n\t[\r\n\t\tgfx/impact/flesh_hit1\r\n\t]\r\n}\r\n";

// One line of a blood particle, its numbers scaled: key and factor by where the line is.
static void GoreLine(const char *line, size_t len, int depth, const char *sub, char **out, size_t *outLen)
{
    static const struct { const char *key, *sub; double f; } k[] = {
        { "count", "", 3.0 }, { "life", "", 2.5 }, { "velocity", "", 1.8 }, { "gravity", "", 3.0 },
        { "start", "size", 1.6 }, { "end", "size", 1.6 },
    };
    char buf[512], key[64] = "";
    const char *p = line;
    double f = 1.0;
    int i, n;
    while (p < line + len && (*p == ' ' || *p == '\t')) p++;
    for (i = 0; p + i < line + len && i < 63 && p[i] > ' '; i++) key[i] = p[i];
    key[i] = 0;
    for (i = 0; i < (int)(sizeof(k) / sizeof(k[0])); i++)
        if (!strcmp(key, k[i].key) && !strcmp(sub, k[i].sub) && (k[i].sub[0] ? depth == 2 : depth == 1)) f = k[i].f;
    if (f == 1.0 || len >= sizeof(buf) - 64) {
        memcpy(*out + *outLen, line, len);
        *outLen += len;
        return;
    }
    // the indent and key as they are, then each number times f
    n = (int)(p - line) + (int)strlen(key);
    memcpy(buf, line, n);
    p = line + n;
    while (p < line + len) {
        char *end;
        double v;
        if (*p == ' ' || *p == '\t') { buf[n++] = *p++; continue; }
        v = strtod(p, &end);
        if (end == p || end > line + len) { while (p < line + len) buf[n++] = *p++; break; }
        n += sprintf(buf + n, "%g", v * f);
        p = end;
    }
    memcpy(*out + *outLen, buf, n);
    *outLen += n;
}

static char *GoreEffect(const char *d, size_t n, size_t *outLen)
{
    char *out = malloc(n * 2 + sizeof(g_goreDrops) + 64), sub[32] = "", prev[32] = "";
    size_t i = 0, len = 0;
    int depth = 0, blood = 0;
    if (!out) return NULL;
    while (i < n) {
        size_t j = i, e;
        const char *t;
        while (j < n && d[j] != '\n') j++;
        e = j < n ? j + 1 : j;   // with its newline
        t = d + i;
        while (t < d + j && (*t == ' ' || *t == '\t')) t++;
        if (depth == 0 && !strncmp(t, "Particle", 8)) {
            // a new particle: is it blood? (drawn with the flesh_hit pictures)
            const char *next = strstr(d + j, "Particle");
            const char *hit = strstr(d + j, "flesh_hit");
            blood = hit && (!next || hit < next);
        }
        if (*t == '{') {
            depth++;
            if (depth == 2) strcpy(sub, prev);
        } else if (*t == '}') {
            depth--;
            if (depth < 2) sub[0] = 0;
        }
        if (blood && depth >= 1) GoreLine(d + i, e - i, depth, sub, &out, &len);
        else { memcpy(out + len, d + i, e - i); len += e - i; }
        if (t < d + j && *t != '{' && *t != '}') {
            int k;
            for (k = 0; k < 31 && t + k < d + j && t[k] > ' '; k++) prev[k] = t[k];
            prev[k] = 0;
        }
        i = e;
    }
    memcpy(out + len, g_goreDrops, sizeof(g_goreDrops) - 1);
    len += sizeof(g_goreDrops) - 1;
    *outLen = len;
    return out;
}

static int BuildGorePack(const wchar_t *game)
{
    static const char *file = "fx/impacts/flesh_hit.efx";
    wchar_t pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int count = 0, cap = 0, i, ok;
    char utf8[MAX_PATH * 3], *gore;
    void *data = NULL;
    size_t size = 0, goreSize;
    mz_zip_archive zw;

    _snwprintf(pattern, MAX_PATH, L"%s\\main\\pak*.pk3", game);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!_wcsnicmp(fd.cFileName, L"pakz_vr", 7)) continue;   // our own packs
        if (count == cap) { cap = cap ? cap * 2 : 32; paks = realloc(paks, cap * sizeof(*paks)); }
        _snwprintf(paks[count++], MAX_PATH, L"%s", fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(paks, count, sizeof(*paks), CmpW);
    for (i = 0; i < count; i++) {   // the last pak that has the file wins
        mz_zip_archive z;
        int e;
        wchar_t path[MAX_PATH];
        memset(&z, 0, sizeof(z));
        _snwprintf(path, MAX_PATH, L"%s\\main\\%s", game, paks[i]);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
        if (!mz_zip_reader_init_file(&z, utf8, 0)) continue;
        e = mz_zip_reader_locate_file(&z, file, NULL, 0);
        if (e >= 0) {
            void *d = mz_zip_reader_extract_to_heap(&z, e, &size, 0);
            if (d) { free(data); data = d; }
        }
        mz_zip_reader_end(&z);
    }
    free(paks);
    if (!data || !(data = realloc(data, size + 1))) return 0;
    ((char *)data)[size] = 0;   // searched as a string
    gore = GoreEffect(data, size, &goreSize);
    free(data);
    if (!gore) return 0;

    Join(out, game, GORE_PACK);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, utf8, 0)) { free(gore); return 0; }
    ok = mz_zip_writer_add_mem(&zw, file, gore, goreSize, MZ_DEFAULT_COMPRESSION);
    ok = ok && mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    free(gore);
    if (!ok) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// Font pack: the game's text fonts (fonts/fontImage_<size>.dat, the glyph metrics, and its
// 256x256 pages fontImage_<page>_<size>.dds) drawn again from Segoe UI Semibold, which comes with
// Windows, on this PC. Same names, sizes and pages as the game's, so every text uses it.
// ---------------------------------------------------------------------------
#define FONT_TEX 256
static const int g_fontSizes[] = { 12, 16, 18, 24, 30, 32 };
static const int g_fontPages[] = { 1, 1, 1, 2, 3, 3 };

typedef struct {                // the game's glyph (80 bytes)
    int height, width;          // the picture's size in pixels (width again below)
    float top, left, xSkip;     // above the baseline, from the pen, the advance
    int imageWidth, imageHeight;
    float s, t, s2, t2;
    int glyph;
    char shaderName[32];
} fontGlyph_t;

// One DXT5 page (white, coverage in alpha), no mipmaps, as the game's.
static void *FontPageDDS(const unsigned char *alpha, size_t *len)
{
    unsigned char *out = calloc(1, 128 + FONT_TEX * FONT_TEX), block[64];
    int bx, by, x, y;
    memcpy(out, "DDS ", 4);
    ((int *)out)[1] = 124;
    ((int *)out)[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000;
    ((int *)out)[3] = FONT_TEX;
    ((int *)out)[4] = FONT_TEX;
    ((int *)out)[5] = FONT_TEX * FONT_TEX;
    ((int *)out)[19] = 32;
    ((int *)out)[20] = 0x4;
    memcpy(out + 84, "DXT5", 4);
    ((int *)out)[27] = 0x1000;
    for (by = 0; by < FONT_TEX / 4; by++)
        for (bx = 0; bx < FONT_TEX / 4; bx++) {
            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++) {
                    unsigned char *p = block + (y * 4 + x) * 4;
                    p[0] = p[1] = p[2] = 255;
                    p[3] = alpha[(by * 4 + y) * FONT_TEX + bx * 4 + x];
                }
            stb_compress_dxt_block(out + 128 + (by * (FONT_TEX / 4) + bx) * 16, block, 1, STB_DXT_HIGHQUAL);
        }
    *len = 128 + FONT_TEX * FONT_TEX;
    return out;
}

static int AddFile(mz_zip_archive *zw, const char *name, const void *d, size_t n)
{
    return mz_zip_writer_add_mem(zw, name, d, n, MZ_DEFAULT_COMPRESSION);
}

// One size: its .dat and pages, drawn with GDI.
static int BuildFontSize(mz_zip_archive *zw, int size, int pages)
{
    HDC dc = CreateCompatibleDC(NULL);
    HFONT font = CreateFontW(-size, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    static const MAT2 ident = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
    unsigned char *alpha = calloc(pages, FONT_TEX * FONT_TEX), *buf = malloc(256 * 256);
    unsigned char dat[256 * 80 + 72];
    fontGlyph_t *g = (fontGlyph_t *)dat;
    int c, page = 0, x = 1, y = 1, rowH = 0, maxH = 0, ok = 1;
    float scale = 48.0f / size;
    char name[64];
    memset(dat, 0, sizeof(dat));
    SelectObject(dc, font);
    for (c = 32; c < 256 && ok; c++) {
        GLYPHMETRICS gm;
        DWORD n;
        int w, h, pitchBytes, r, k;
        if (c >= 127 && c < 160) continue;   // controls: none in the game's fonts either
        n = GetGlyphOutlineW(dc, (WCHAR)c, GGO_GRAY8_BITMAP, &gm, 256 * 256, buf, &ident);
        if (n == GDI_ERROR) {   // nothing to draw (the space): only its advance
            if (GetGlyphOutlineW(dc, (WCHAR)c, GGO_METRICS, &gm, 0, NULL, &ident) == GDI_ERROR) continue;
            n = 0;
        }
        w = n ? (int)gm.gmBlackBoxX : 0;
        h = n ? (int)gm.gmBlackBoxY : 0;
        if (x + w + 1 > FONT_TEX) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + h + 1 > FONT_TEX) {
            if (++page >= pages) { if (c > 127) break; ok = 0; break; }   // no room: the accents are left out
            x = 1; y = 1; rowH = 0;
        }
        pitchBytes = (w + 3) & ~3;
        for (r = 0; r < h; r++)
            for (k = 0; k < w; k++) {
                int v = buf[r * pitchBytes + k] * 255 / 64;
                alpha[page * FONT_TEX * FONT_TEX + (y + r) * FONT_TEX + x + k] = (unsigned char)(v > 255 ? 255 : v);
            }
        g[c].height = h;
        g[c].width = w;
        g[c].top = n ? (float)gm.gmptGlyphOrigin.y : 1.0f;
        g[c].left = (float)gm.gmptGlyphOrigin.x;
        g[c].xSkip = (float)gm.gmCellIncX;
        g[c].imageWidth = w;
        g[c].imageHeight = h;
        g[c].s = (float)x / FONT_TEX;
        g[c].t = (float)y / FONT_TEX;
        g[c].s2 = (float)(x + w) / FONT_TEX;
        g[c].t2 = (float)(y + h) / FONT_TEX;
        sprintf(g[c].shaderName, "fonts/fontImage_%d_%d.tga", page, size);
        if (h > maxH) maxH = h;
        x += w + 1;
        if (h > rowH) rowH = h;
    }
    // after the glyphs: glyphScale (48 / size) and the tallest glyph, scaled
    ((float *)(dat + 256 * 80))[0] = scale;
    ((float *)(dat + 256 * 80))[1] = maxH * scale;
    DeleteObject(font);
    DeleteDC(dc);
    free(buf);
    if (ok) {
        sprintf(name, "fonts/fontImage_%d.dat", size);
        ok = AddFile(zw, name, dat, sizeof(dat) - 0);
    }
    for (c = 0; ok && c < pages; c++) {
        size_t len;
        void *dds = FontPageDDS(alpha + c * FONT_TEX * FONT_TEX, &len);
        sprintf(name, "fonts/fontImage_%d_%d.dds", c, size);
        ok = AddFile(zw, name, dds, len);
        free(dds);
    }
    free(alpha);
    return ok;
}

static int BuildFontPack(const wchar_t *game)
{
    wchar_t out[MAX_PATH], tmp[MAX_PATH];
    char utf8[MAX_PATH * 3];
    mz_zip_archive zw;
    int i, ok = 1;
    Join(out, game, FONT_PACK);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, utf8, 0)) return 0;
    for (i = 0; ok && i < (int)(sizeof(g_fontSizes) / sizeof(g_fontSizes[0])); i++)
        ok = BuildFontSize(&zw, g_fontSizes[i], g_fontPages[i]);
    ok = ok && mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    if (!ok) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// No-shake pack: the game's shell shock settings (scripts/*.shock, after explosions and hits) with
// the view shake (cg_shock_viewKickRadius) at 0: in VR the world wobbling around the head makes
// people sick. The blur, the muffled sound and the rest stay.
// ---------------------------------------------------------------------------
static char *NoShake(const char *d, size_t n, size_t *outLen)
{
    static const char key[] = "cg_shock_viewKickRadius";
    char *out = malloc(n + 64);
    size_t i = 0, len = 0;
    if (!out) return NULL;
    while (i < n) {
        size_t j = i;
        while (j < n && d[j] != '\n') j++;
        if (j < n) j++;
        // a line that starts with the key (a commented one starts with //): its value becomes 0
        if (j - i > sizeof(key) && !memcmp(d + i, key, sizeof(key) - 1)) {
            const char *q = memchr(d + i, '"', j - i);
            const char *q2 = q ? memchr(q + 1, '"', j - i - (q + 1 - (d + i))) : NULL;
            if (q && q2) {
                memcpy(out + len, d + i, q - (d + i) + 1); len += q - (d + i) + 1;
                out[len++] = '0';
                memcpy(out + len, q2, j - (q2 - d)); len += j - (q2 - d);
                i = j;
                continue;
            }
        }
        memcpy(out + len, d + i, j - i); len += j - i;
        i = j;
    }
    *outLen = len;
    return out;
}

static int BuildShockPack(const wchar_t *game)
{
    wchar_t pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    char utf8[MAX_PATH * 3], names[16][64];
    void *files[16] = { 0 };
    size_t sizes[16];
    int count = 0, cap = 0, i, e, numFiles = 0, ok = 1;
    mz_zip_archive zw;

    _snwprintf(pattern, MAX_PATH, L"%s\\main\\pak*.pk3", game);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!_wcsnicmp(fd.cFileName, L"pakz_vr", 7)) continue;
        if (count == cap) { cap = cap ? cap * 2 : 32; paks = realloc(paks, cap * sizeof(*paks)); }
        _snwprintf(paks[count++], MAX_PATH, L"%s", fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(paks, count, sizeof(*paks), CmpW);
    for (i = 0; i < count; i++) {   // the last pak with each file wins
        mz_zip_archive z;
        memset(&z, 0, sizeof(z));
        _snwprintf(path, MAX_PATH, L"%s\\main\\%s", game, paks[i]);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
        if (!mz_zip_reader_init_file(&z, utf8, 0)) continue;
        for (e = 0; e < (int)mz_zip_reader_get_num_files(&z); e++) {
            char fn[260];
            int k;
            size_t len = 0, nl;
            mz_zip_reader_get_filename(&z, e, fn, sizeof(fn));
            nl = strlen(fn);
            if (_strnicmp(fn, "scripts/", 8) || nl < 6 || _stricmp(fn + nl - 6, ".shock") || nl >= 64) continue;
            for (k = 0; k < numFiles && _stricmp(names[k], fn); k++) ;
            if (k == numFiles) { if (numFiles == 16) continue; strcpy(names[numFiles++], fn); }
            free(files[k]);
            files[k] = mz_zip_reader_extract_to_heap(&z, e, &len, 0);
            sizes[k] = files[k] ? len : 0;
        }
        mz_zip_reader_end(&z);
    }
    free(paks);
    if (!numFiles) return 0;

    Join(out, game, SHOCK_PACK);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, utf8, 0)) ok = 0;
    for (i = 0; ok && i < numFiles; i++) {
        size_t len;
        char *changed;
        if (!files[i]) continue;
        changed = NoShake(files[i], sizes[i], &len);
        ok = changed && mz_zip_writer_add_mem(&zw, names[i], changed, len, MZ_DEFAULT_COMPRESSION);
        free(changed);
    }
    for (i = 0; i < numFiles; i++) free(files[i]);
    if (ok) ok = mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    if (!ok) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// LMG pack (United Offensive): the DP-28, MG34 and .30 cal are made to be set up on the floor, and until then they are held in the carry
// pose. In VR they are fired as held: their idle / fire / last-shot animations become the deployed (firing) ones, and the walk is slower
// (the slower walk is the mod's, LmgWalkScale). Same change as tools-dev/lmgpack.py. Written to uo\pakz_vr_lmg.pk3 from the player's own paks.
// ---------------------------------------------------------------------------
static int LmgField(const char *d, size_t n, const char *key, size_t *vs, size_t *vl)
{
    size_t kl = strlen(key), i;
    for (i = 0; i + kl + 2 < n; i++) {
        if (d[i] == '\\' && !memcmp(d + i + 1, key, kl) && d[i + 1 + kl] == '\\') {
            size_t s = i + kl + 2, e = s;
            while (e < n && d[e] != '\\') e++;
            *vs = s;
            *vl = e - s;
            return 1;
        }
    }
    return 0;
}

// soundaliases/wep_gmi_sound.csv: the DP-28's sounds (weap_dp28_*) are only loaded in kharkov1, kharkov2, kursk, kursk2, ponyri and trenches
// (the last column of their lines): without that column they load in every mission, so the gun is not silent in the others.
static char *LmgSound(const char *d, size_t n, size_t *outLen)
{
    char *out = malloc(n + 1);
    size_t i = 0, len = 0;
    if (!out) return NULL;
    while (i < n) {
        size_t j = i, e, c;
        while (j < n && d[j] != '\n') j++;
        e = j;
        if (e > i && d[e - 1] == '\r') e--;
        if (e - i > 10 && !memcmp(d + i, "weap_dp28_", 10)) {
            c = e;
            while (c > i && d[c - 1] != ',') c--;   // just after the last comma
            memcpy(out + len, d + i, c - i); len += c - i;
            memcpy(out + len, d + e, j - e); len += j - e;
        } else {
            memcpy(out + len, d + i, j - i); len += j - i;
        }
        if (j < n) out[len++] = '\n';
        i = j + 1;
    }
    *outLen = len;
    return out;
}

// the weapon file with the animations of the firing pose, class rifle (not lmg) and the lmg set-up fields emptied;
// NULL when it is not a deployable LMG. {field, copied from this other field (NULL: the constant)}, in this order.
static char *LmgCarry(const char *d, size_t n, size_t *outLen)
{
    static const struct { const char *key, *from, *value; } ops[7] = {
        { "idleAnim", "lmgDeployedAnim", NULL }, { "fireAnim", "adsFireAnim", NULL }, { "lastShotAnim", "adsLastShotAnim", NULL },
        { "weaponClass", NULL, "rifle" },
        { "lmgDeployedAnim", NULL, "" }, { "lmgDeployAnim", NULL, "" }, { "lmgBreakdownAnim", NULL, "" } };
    size_t vs, vl, cs, cl;
    char *cur, *tmp;
    int k;
    if (!LmgField(d, n, "weaponClass", &cs, &cl) || cl != 3 || memcmp(d + cs, "lmg", 3)) return NULL;
    if (!LmgField(d, n, "lmgDeployedAnim", &vs, &vl) || !vl) return NULL;
    cur = malloc(n + 1);
    if (!cur) return NULL;
    memcpy(cur, d, n);
    *outLen = n;
    for (k = 0; k < 7; k++) {
        const char *key = ops[k].key;
        char val[200];
        size_t ts, tl, fs, fl;
        if (ops[k].from) {
            if (!LmgField(cur, *outLen, ops[k].from, &fs, &fl) || !fl || fl >= sizeof(val)) continue;
            memcpy(val, cur + fs, fl);
            val[fl] = 0;
        } else {
            strcpy(val, ops[k].value);
        }
        if (!LmgField(cur, *outLen, key, &ts, &tl)) continue;
        tmp = malloc(*outLen - tl + strlen(val) + 1);
        if (!tmp) { free(cur); return NULL; }
        memcpy(tmp, cur, ts);
        memcpy(tmp + ts, val, strlen(val));
        memcpy(tmp + ts + strlen(val), cur + ts + tl, *outLen - ts - tl);
        *outLen = *outLen - tl + strlen(val);
        free(cur);
        cur = tmp;
    }
    return cur;
}

static int BuildLmgPack(const wchar_t *game)
{
    static const wchar_t *const dirs[2] = { L"main", L"uo" };
    wchar_t pattern[MAX_PATH], (*paks)[MAX_PATH] = NULL, out[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    char utf8[MAX_PATH * 3], names[192][64];
    void *files[192] = { 0 };
    size_t sizes[192];
    int d, count, cap, i, e, numFiles = 0, ok = 1, written = 0;
    mz_zip_archive zw;

    Join(path, game, L"uo");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return 0;   // no United Offensive here
    for (d = 0; d < 2; d++) {   // main first, then uo: the last pak with each file wins
        HANDLE h;
        count = cap = 0;
        paks = NULL;
        _snwprintf(pattern, MAX_PATH, L"%s\\%s\\pak*.pk3", game, dirs[d]);
        h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!_wcsnicmp(fd.cFileName, L"pakz_vr", 7)) continue;
            if (count == cap) { cap = cap ? cap * 2 : 32; paks = realloc(paks, cap * sizeof(*paks)); }
            _snwprintf(paks[count++], MAX_PATH, L"%s", fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        qsort(paks, count, sizeof(*paks), CmpW);
        for (i = 0; i < count; i++) {
            mz_zip_archive z;
            memset(&z, 0, sizeof(z));
            _snwprintf(path, MAX_PATH, L"%s\\%s\\%s", game, dirs[d], paks[i]);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
            if (!mz_zip_reader_init_file(&z, utf8, 0)) continue;
            for (e = 0; e < (int)mz_zip_reader_get_num_files(&z); e++) {
                char fn[260];
                int k;
                size_t len = 0, nl;
                mz_zip_reader_get_filename(&z, e, fn, sizeof(fn));
                nl = strlen(fn);
                if ((_strnicmp(fn, "weapons/sp/", 11) && _stricmp(fn, "soundaliases/wep_gmi_sound.csv")) || nl >= 64 || fn[nl - 1] == '/') continue;
                for (k = 0; k < numFiles && _stricmp(names[k], fn); k++) ;
                if (k == numFiles) { if (numFiles == 192) continue; strcpy(names[numFiles++], fn); }
                free(files[k]);
                files[k] = mz_zip_reader_extract_to_heap(&z, e, &len, 0);
                sizes[k] = files[k] ? len : 0;
            }
            mz_zip_reader_end(&z);
        }
        free(paks);
    }
    Join(out, game, L"uo\\pakz_vr_lmg.pk3");
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, utf8, 0)) ok = 0;
    for (i = 0; ok && i < numFiles; i++) {
        size_t len = 0;
        char *changed;
        if (!files[i]) continue;
        changed = !_stricmp(names[i], "soundaliases/wep_gmi_sound.csv") ? LmgSound(files[i], sizes[i], &len) : LmgCarry(files[i], sizes[i], &len);
        if (!changed) continue;
        ok = mz_zip_writer_add_mem(&zw, names[i], changed, len, MZ_DEFAULT_COMPRESSION);
        free(changed);
        written++;
    }
    for (i = 0; i < numFiles; i++) free(files[i]);
    if (ok) ok = mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    if (!ok || !written) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------
// The desktop shortcut to the launcher.
static int ShortcutPath(wchar_t *out)
{
    wchar_t desk[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk))) return 0;
    Join(out, desk, SHORTCUT_NAME);
    return 1;
}

static int CreateShortcut(const wchar_t *game)
{
    wchar_t lnk[MAX_PATH], exe[MAX_PATH];
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    int ok = 0;
    if (!ShortcutPath(lnk)) return 0;
    Join(exe, game, LAUNCHER_EXE);
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return 0;
    sl->lpVtbl->SetPath(sl, exe);
    sl->lpVtbl->SetWorkingDirectory(sl, game);
    sl->lpVtbl->SetIconLocation(sl, exe, 0);
    sl->lpVtbl->SetDescription(sl, L"Call of Duty VR");
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
        ok = SUCCEEDED(pf->lpVtbl->Save(pf, lnk, TRUE));
        pf->lpVtbl->Release(pf);
    }
    sl->lpVtbl->Release(sl);
    return ok;
}

static int Install(const wchar_t *game, int *keptIni)
{
    wchar_t dll[MAX_PATH], bak[MAX_PATH], p[MAX_PATH];
    Join(dll, game, L"opengl32.dll");
    if (FileExists(dll) && !IsOurDll(dll)) {
        // another OpenGL wrapper is installed: keep it as a backup
        _snwprintf(bak, MAX_PATH, L"%s%s", dll, BACKUP_SUFFIX);
        MoveFileExW(dll, bak, MOVEFILE_REPLACE_EXISTING);
    }
    if (!WriteResource(IDR_OPENGL32, dll)) return 0;
    Join(p, game, L"openxr_loader.dll");
    if (!WriteResource(IDR_OPENXR_LOADER, p)) return 0;
    Join(p, game, LAUNCHER_EXE);
    if (!WriteResource(IDR_LAUNCHER, p)) return 0;
    Join(p, game, L"supporters.ini");   // the Patreon supporters shown beside the main menu: always the release's list
    WriteResource(IDR_SUPPORTERS, p);
    Join(p, game, L"cod_vr_weapons.ini");   // where each weapon sits in the hand and its scope's picture: always the release's tuning
    WriteResource(IDR_WEAPONS, p);
    Join(p, game, L"cod_vr.ini");
    *keptIni = FileExists(p);
    if (*keptIni) {
        // keep the player's settings; the new defaults go next to it
        Join(p, game, L"cod_vr.ini.default");
    }
    return WriteResource(IDR_COD_VR_INI, p);
}

static void Uninstall(const wchar_t *game)
{
    wchar_t p[MAX_PATH], bak[MAX_PATH];
    Join(p, game, L"opengl32.dll");
    if (IsOurDll(p)) DeleteFileW(p);
    _snwprintf(bak, MAX_PATH, L"%s%s", p, BACKUP_SUFFIX);
    if (FileExists(bak) && !FileExists(p)) MoveFileW(bak, p);
    Join(p, game, L"openxr_loader.dll");   DeleteFileW(p);
    Join(p, game, L"cod_vr.ini");          DeleteFileW(p);
    Join(p, game, L"cod_vr.ini.default");  DeleteFileW(p);
    Join(p, game, L"supporters.ini");      DeleteFileW(p);
    Join(p, game, L"cod_vr_weapons.ini");  DeleteFileW(p);
    Join(p, game, HANDS_PACK);             DeleteFileW(p);
    Join(p, game, TEXT_PACK);              DeleteFileW(p);
    Join(p, game, GORE_PACK);              DeleteFileW(p);
    Join(p, game, FONT_PACK);              DeleteFileW(p);
    Join(p, game, SHOCK_PACK);             DeleteFileW(p);
    Join(p, game, L"uo\\pakz_vr_lmg.pk3");   DeleteFileW(p);
    Join(p, game, LAUNCHER_EXE);           DeleteFileW(p);
    if (ShortcutPath(p)) DeleteFileW(p);
}

// Command line (no dialogs): /silent /dir="<game folder>" [/hands] [/shortcut] [/uninstall]
// Exit code 0 = done, 1 = failed, 2 = no game in that folder.
// The launcher draws the supporters' panel (the game shows it beside its main menu) from supporters.ini; it also does it each time it
// starts. Run once here so the panel is there even if the game is started without the launcher.
static void ExportSupportersPanel(const wchar_t *game)
{
    wchar_t exe[MAX_PATH], cmdline[MAX_PATH + 32];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    Join(exe, game, LAUNCHER_EXE);
    _snwprintf(cmdline, MAX_PATH + 32, L"\"%s\" /exportpanel", exe);
    cmdline[MAX_PATH + 31] = 0;
    if (CreateProcessW(exe, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, game, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static int Silent(const wchar_t *cmd)
{
    wchar_t game[MAX_PATH] = L"", pack[MAX_PATH];
    const wchar_t *d = wcsstr(cmd, L"/dir=");
    int keptIni, models = 0;
    if (d) {
        const wchar_t *s = d + 5, *e;
        if (*s == L'"') { s++; e = wcschr(s, L'"'); } else e = wcschr(s, L' ');
        if (!e) e = s + wcslen(s);
        if (e - s >= MAX_PATH) return 1;
        wcsncpy(game, s, e - s);
        game[e - s] = 0;
    } else if (!FindGame(game)) {
        return 2;
    }
    if (!HasGame(game)) return 2;
    if (wcsstr(cmd, L"/uninstall")) { Uninstall(game); return 0; }
    if (wcsstr(cmd, L"/gorepack")) return BuildGorePack(game) ? 0 : 1;   // only the gore pack
    if (wcsstr(cmd, L"/textpack")) return BuildTextPack(game) ? 0 : 1;   // only the text pack
    if (wcsstr(cmd, L"/fontpack")) return BuildFontPack(game) ? 0 : 1;   // only the font pack
    if (wcsstr(cmd, L"/shockpack")) return BuildShockPack(game) ? 0 : 1;   // only the no-shake pack
    if (!Install(game, &keptIni)) return 1;
    BuildTextPack(game);
    BuildGorePack(game);
    BuildFontPack(game);
    BuildShockPack(game);
    BuildLmgPack(game);
    ExportSupportersPanel(game);
    if (wcsstr(cmd, L"/shortcut")) CreateShortcut(game);
    Join(pack, game, HANDS_PACK);
    if (wcsstr(cmd, L"/hands")) return BuildHandsPack(game, &models) ? 0 : 1;
    DeleteFileW(pack);
    return 0;
}

// ---------------------------------------------------------------------------
// The setup window: one dark window in the launcher's colours (a welcome page, the progress, the end), drawn by hand.
// ---------------------------------------------------------------------------
#define SETUP_VERSION L"v1.8.0"
#define BASE_W 640
#define BASE_H 420

#define C_BG    RGB(14, 14, 14)
#define C_PANEL RGB(28, 28, 28)
#define C_GOLD  RGB(214, 170, 62)
#define C_GOLDD RGB(150, 112, 30)
#define C_TEXT  RGB(225, 225, 225)
#define C_DIM   RGB(140, 140, 140)
#define C_STEEL RGB(170, 205, 220)
#define C_OK    RGB(120, 200, 120)
#define C_WARN  RGB(232, 150, 64)
#define C_BAD   RGB(235, 90, 80)

enum { PG_WELCOME, PG_WORKING, PG_DONE, PG_REMOVED, PG_FAILED };
enum { B_PRIMARY = 1, B_SECOND, B_CHANGE, B_SHORTCUT, B_CLOSE };

typedef struct { RECT r; int id; } button_t;

static HWND    g_wnd;
static float   g_sc = 1.0f;
static HFONT   g_fTitle, g_fHead, g_fText, g_fSmall, g_fBold, g_fBtn;
static HICON   g_icon;
static button_t g_btn[8];
static int     g_nBtn, g_page, g_shortcut = 1, g_installed, g_unsupported, g_haveSP, g_haveUO, g_keptIni;
static wchar_t g_game[MAX_PATH], g_status[200], g_error[300];
static int     g_step, g_steps = 7;
static POINT   g_mouse = { -1, -1 };

#define S(v) ((int)((v) * g_sc + 0.5f))

static void Refresh(void)
{
    wchar_t dll[MAX_PATH];
    g_haveSP = g_haveUO = g_installed = g_unsupported = 0;
    if (!g_game[0]) return;
    g_haveSP = HasExe(g_game, L"CoDSP.exe");
    g_haveUO = HasExe(g_game, L"CoDUOSP.exe");
    Join(dll, g_game, L"opengl32.dll");
    g_installed = (g_haveSP || g_haveUO) && FileExists(dll) && IsOurDll(dll);
    // every game exe in the folder has to be the version the mod knows (CoDSP.exe 1.3, CoDUOSP.exe 1.51)
    g_unsupported = (g_haveSP && ExeTimestamp(g_game, L"CoDSP.exe") != CODSP_TIMESTAMP) ||
                    (g_haveUO && ExeTimestamp(g_game, L"CoDUOSP.exe") != CODUO_TIMESTAMP);
}

static void FillR(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    RECT r = { x0, y0, x1, y1 };
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void Txt(HDC dc, HFONT f, COLORREF c, int x0, int y0, int x1, int y1, const wchar_t *s, UINT flags)
{
    RECT r = { x0, y0, x1, y1 };
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

static void RoundBox(HDC dc, int x0, int y0, int x1, int y1, int rad, COLORREF fill, COLORREF edge)
{
    HPEN pen = CreatePen(PS_SOLID, S(1) > 1 ? S(1) : 1, edge);
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, br);
    RoundRect(dc, x0, y0, x1, y1, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static int Hot(const RECT *r) { return PtInRect(r, g_mouse); }

// primary: gold, filled; otherwise an outline. disabled buttons are drawn dim and get no area.
static void Button(HDC dc, int id, int x0, int y0, int x1, int y1, const wchar_t *label, int primary, int enabled)
{
    RECT r = { x0, y0, x1, y1 };
    int hot = enabled && Hot(&r);
    if (primary) {
        COLORREF fill = !enabled ? RGB(60, 52, 30) : hot ? RGB(236, 192, 80) : C_GOLD;
        RoundBox(dc, x0, y0, x1, y1, S(12), fill, fill);
        Txt(dc, g_fBtn, enabled ? RGB(20, 16, 6) : C_DIM, x0, y0, x1, y1, label, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        RoundBox(dc, x0, y0, x1, y1, S(12), hot ? RGB(40, 34, 20) : C_PANEL, hot ? C_GOLD : RGB(80, 80, 80));
        Txt(dc, g_fBtn, !enabled ? C_DIM : hot ? C_GOLD : C_TEXT, x0, y0, x1, y1, label, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    if (enabled && g_nBtn < 8) { g_btn[g_nBtn].r = r; g_btn[g_nBtn].id = id; g_nBtn++; }
}

static void Header(HDC dc)
{
    wchar_t sub[160];
    if (g_icon) DrawIconEx(dc, S(32), S(20), g_icon, S(72), S(72), 0, NULL, DI_NORMAL);
    Txt(dc, g_fTitle, C_GOLD, S(122), S(22), S(620), S(64), L"CALL OF DUTY VR", DT_SINGLELINE | DT_VCENTER);
    _snwprintf(sub, 160, T(L"Setup  %s  \x00b7  Call of Duty and United Offensive in virtual reality",
                           L"Instalador  %s  \x00b7  Call of Duty e United Offensive em realidade virtual"), SETUP_VERSION);
    sub[159] = 0;
    Txt(dc, g_fSmall, C_DIM, S(122), S(66), S(620), S(90), sub, DT_SINGLELINE | DT_VCENTER);
    FillR(dc, 0, S(110), S(BASE_W), S(112), C_GOLDD);
}

static void Footer(HDC dc) { FillR(dc, 0, S(340), S(BASE_W), S(342), RGB(40, 40, 40)); }

static void DrawWelcome(HDC dc)
{
    int ok = g_game[0] && (g_haveSP || g_haveUO);
    wchar_t line[300];
    Txt(dc, g_fBold, C_GOLD, S(32), S(126), S(400), S(148), T(L"GAME FOLDER", L"PASTA DO JOGO"), DT_SINGLELINE | DT_VCENTER);
    RoundBox(dc, S(32), S(152), S(470), S(190), S(10), C_PANEL, RGB(70, 70, 70));
    Txt(dc, g_fText, ok ? C_TEXT : C_WARN, S(46), S(152), S(458), S(190),
        g_game[0] ? g_game : T(L"Not found: click Change and pick the folder with CoDSP.exe or CoDUOSP.exe",
                               L"Não encontrada: clique em Alterar e escolha a pasta com o CoDSP.exe ou o CoDUOSP.exe"),
        DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
    Button(dc, B_CHANGE, S(482), S(152), S(608), S(190), T(L"Change...", L"Alterar..."), 0, 1);

    Txt(dc, g_fText, g_haveSP ? C_OK : C_DIM, S(32), S(202), S(320), S(224),
        g_haveSP ? T(L"\x2714  Call of Duty (CoDSP.exe)", L"\x2714  Call of Duty (CoDSP.exe)") : T(L"\x2013  Call of Duty not here", L"\x2013  Call of Duty não está aqui"),
        DT_SINGLELINE | DT_VCENTER);
    Txt(dc, g_fText, g_haveUO ? C_OK : C_DIM, S(330), S(202), S(620), S(224),
        g_haveUO ? T(L"\x2714  United Offensive (CoDUOSP.exe)", L"\x2714  United Offensive (CoDUOSP.exe)") : T(L"\x2013  United Offensive not here", L"\x2013  United Offensive não está aqui"),
        DT_SINGLELINE | DT_VCENTER);

    if (g_error[0]) {
        Txt(dc, g_fText, C_BAD, S(32), S(238), S(608), S(290), g_error, DT_WORDBREAK);
    } else if (ok && g_unsupported) {
        Txt(dc, g_fText, C_WARN, S(32), S(238), S(608), S(290),
            T(L"The game here is not the version the mod supports (CoDSP.exe 1.3, CoDUOSP.exe 1.51). The mod stays inactive in the other versions.",
              L"O jogo desta pasta não é a versão que o mod suporta (CoDSP.exe 1.3, CoDUOSP.exe 1.51). Nas outras versões o mod fica inativo."),
            DT_WORDBREAK);
    } else if (ok && g_installed) {
        Txt(dc, g_fText, C_STEEL, S(32), S(238), S(608), S(290),
            T(L"The VR mod is already installed in this folder. Installing again updates it; your settings are kept.",
              L"O mod de VR já está instalado nesta pasta. Instalar de novo atualiza; suas configurações são mantidas."),
            DT_WORDBREAK);
    } else if (ok) {
        Txt(dc, g_fText, C_DIM, S(32), S(238), S(608), S(290),
            T(L"Installs the VR mod, the launcher and the optional packs (made from your own game files). No game file is changed.",
              L"Instala o mod de VR, o launcher e os pacotes opcionais (gerados a partir dos seus arquivos do jogo). Nenhum arquivo do jogo é alterado."),
            DT_WORDBREAK);
    }

    {   // the shortcut checkbox
        RECT cb = { S(32), S(298), S(420), S(324) };
        int hot = Hot(&cb);
        RoundBox(dc, S(32), S(301), S(52), S(321), S(6), g_shortcut ? C_GOLD : C_PANEL, hot ? C_GOLD : RGB(110, 110, 110));
        if (g_shortcut) Txt(dc, g_fBold, RGB(20, 16, 6), S(32), S(301), S(52), S(321), L"\x2714", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        Txt(dc, g_fText, hot ? C_GOLD : C_TEXT, S(64), S(298), S(420), S(324), T(L"Create a desktop shortcut to the launcher", L"Criar um atalho do launcher na área de trabalho"),
            DT_SINGLELINE | DT_VCENTER);
        if (g_nBtn < 8) { g_btn[g_nBtn].r = cb; g_btn[g_nBtn].id = B_SHORTCUT; g_nBtn++; }
    }

    Footer(dc);
    Button(dc, B_CLOSE, S(32), S(356), S(150), S(398), T(L"Cancel", L"Cancelar"), 0, 1);
    if (g_installed && ok) Button(dc, B_SECOND, S(290), S(356), S(430), S(398), T(L"Uninstall", L"Desinstalar"), 0, 1);
    _snwprintf(line, 300, g_installed ? T(L"UPDATE", L"ATUALIZAR") : g_unsupported ? T(L"INSTALL ANYWAY", L"INSTALAR MESMO ASSIM") : T(L"INSTALL", L"INSTALAR"));
    line[299] = 0;
    Button(dc, B_PRIMARY, S(442), S(356), S(608), S(398), line, 1, ok);
}

static void DrawWorking(HDC dc)
{
    int w = S(608) - S(32), done = (int)((double)w * g_step / g_steps);
    Txt(dc, g_fHead, C_GOLD, S(32), S(140), S(608), S(176), T(L"Installing...", L"Instalando..."), DT_SINGLELINE | DT_VCENTER);
    Txt(dc, g_fText, C_TEXT, S(32), S(190), S(608), S(220), g_status, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RoundBox(dc, S(32), S(236), S(608), S(254), S(10), C_PANEL, RGB(70, 70, 70));
    if (done > S(10)) RoundBox(dc, S(32), S(236), S(32) + done, S(254), S(10), C_GOLD, C_GOLD);
    Txt(dc, g_fSmall, C_DIM, S(32), S(270), S(608), S(296), T(L"Please wait: some packs take a few seconds to build.", L"Aguarde: alguns pacotes levam alguns segundos para serem gerados."),
        DT_SINGLELINE | DT_VCENTER);
}

static void DrawDone(HDC dc)
{
    wchar_t msg[900];
    Txt(dc, g_fHead, C_GOLD, S(32), S(130), S(608), S(170), T(L"ALL SET!", L"TUDO PRONTO!"), DT_SINGLELINE | DT_VCENTER);
    _snwprintf(msg, 900,
               T(L"Connect your headset (Virtual Desktop or Quest Link: the game needs a 32-bit OpenXR runtime) and start the game with the Call of Duty VR launcher, "
                 L"which also has the settings.%s\n\nRun this setup again to update or uninstall.",
                 L"Conecte o headset (Virtual Desktop ou Quest Link: o jogo precisa de um runtime OpenXR de 32 bits) e abra o jogo pelo launcher do Call of Duty VR, "
                 L"que também tem as configurações.%s\n\nExecute este instalador de novo para atualizar ou desinstalar."),
               g_keptIni ? T(L"\n\nYour existing cod_vr.ini was kept (the new defaults are in cod_vr.ini.default).",
                             L"\n\nSeu cod_vr.ini foi mantido (os novos padrões estão em cod_vr.ini.default).") : L"");
    msg[899] = 0;
    Txt(dc, g_fText, C_TEXT, S(32), S(184), S(608), S(330), msg, DT_WORDBREAK);
    Footer(dc);
    Button(dc, B_CLOSE, S(32), S(356), S(150), S(398), T(L"Close", L"Fechar"), 0, 1);
    Button(dc, B_PRIMARY, S(390), S(356), S(608), S(398), T(L"OPEN THE LAUNCHER", L"ABRIR O LAUNCHER"), 1, 1);
}

static void DrawEnd(HDC dc, const wchar_t *title, const wchar_t *text, COLORREF col, const wchar_t *primary)
{
    Txt(dc, g_fHead, col, S(32), S(130), S(608), S(170), title, DT_SINGLELINE | DT_VCENTER);
    Txt(dc, g_fText, C_TEXT, S(32), S(184), S(608), S(330), text, DT_WORDBREAK);
    Footer(dc);
    Button(dc, B_CLOSE, S(32), S(356), S(150), S(398), T(L"Close", L"Fechar"), 0, 1);
    if (primary) Button(dc, B_PRIMARY, S(442), S(356), S(608), S(398), primary, 1, 1);
}

static void Paint(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(wnd, &ps), dc;
    RECT rc;
    HBITMAP bmp;
    HGDIOBJ old;
    GetClientRect(wnd, &rc);
    dc = CreateCompatibleDC(wdc);
    bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    old = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    FillR(dc, 0, 0, rc.right, rc.bottom, C_BG);
    g_nBtn = 0;
    Header(dc);
    switch (g_page) {
    case PG_WELCOME: DrawWelcome(dc); break;
    case PG_WORKING: DrawWorking(dc); break;
    case PG_DONE:    DrawDone(dc); break;
    case PG_REMOVED:
        DrawEnd(dc, T(L"REMOVED", L"REMOVIDO"), T(L"The VR mod was removed from the game folder. Your game files were not touched.",
                                                  L"O mod de VR foi removido da pasta do jogo. Os arquivos do jogo não foram alterados."), C_GOLD, NULL);
        break;
    case PG_FAILED:
        DrawEnd(dc, T(L"COULD NOT INSTALL", L"NÃO FOI POSSÍVEL INSTALAR"), g_error, C_BAD, T(L"BACK", L"VOLTAR"));
        break;
    }
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(wnd, &ps);
}

static void Pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}

// one line of progress; the window is drawn at once (the packs are built on this thread)
static void Step(const wchar_t *text)
{
    wcsncpy(g_status, text, 199);
    g_status[199] = 0;
    InvalidateRect(g_wnd, NULL, FALSE);
    UpdateWindow(g_wnd);
    Pump();
    g_step++;
}

static void DoInstall(void)
{
    int models = 0;
    (void)models;
    g_error[0] = 0;
    g_step = 0;
    g_page = PG_WORKING;
    Step(T(L"Copying the mod and the launcher...", L"Copiando o mod e o launcher..."));
    if (!Install(g_game, &g_keptIni)) {
        _snwprintf(g_error, 300, T(L"Could not write the files to:\n\n%s\n\nIs the game running?", L"Não foi possível gravar os arquivos em:\n\n%s\n\nO jogo está aberto?"), g_game);
        g_error[299] = 0;
        g_page = PG_FAILED;
        InvalidateRect(g_wnd, NULL, FALSE);
        return;
    }
    Step(T(L"Building the text pack...", L"Gerando o pacote de textos..."));
    BuildTextPack(g_game);   // English game only; the mod works without it
    Step(T(L"Building the blood pack...", L"Gerando o pacote de sangue..."));
    BuildGorePack(g_game);   // used only when MODS > Gore is set to Gore
    Step(T(L"Building the font pack...", L"Gerando o pacote de fontes..."));
    BuildFontPack(g_game);   // used only when GRAPHICS > Text font is Modern
    Step(T(L"Building the no-shake pack...", L"Gerando o pacote sem tremor..."));
    BuildShockPack(g_game);  // explosions without the view shake
    BuildLmgPack(g_game);    // United Offensive: the machine guns are fired as held (no set-up on the floor)
    Step(T(L"Preparing the supporters' panel...", L"Preparando o painel de apoiadores..."));
    ExportSupportersPanel(g_game);
    if (g_shortcut) {
        Step(T(L"Creating the desktop shortcut...", L"Criando o atalho..."));
        CreateShortcut(g_game);
    } else {
        g_step++;
    }
    g_step = g_steps;
    g_page = PG_DONE;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void DoUninstall(void)
{
    if (MessageBoxW(g_wnd, T(L"Remove the VR mod from this game folder?", L"Remover o mod de VR desta pasta do jogo?"), TITLE,
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    Uninstall(g_game);
    g_page = PG_REMOVED;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void Click(int id)
{
    wchar_t exe[MAX_PATH], picked[MAX_PATH];
    switch (id) {
    case B_CLOSE:
        DestroyWindow(g_wnd);
        break;
    case B_CHANGE:
        picked[0] = 0;
        if (PickFolder(picked)) {
            if (HasGame(picked)) {
                wcscpy(g_game, picked);
                g_error[0] = 0;
                Refresh();
            } else {
                _snwprintf(g_error, 300, T(L"CoDSP.exe or CoDUOSP.exe was not found in:\n%s", L"CoDSP.exe ou CoDUOSP.exe não foi encontrado em:\n%s"), picked);
                g_error[299] = 0;
            }
        }
        break;
    case B_SHORTCUT:
        g_shortcut = !g_shortcut;
        break;
    case B_SECOND:
        if (g_page == PG_WELCOME && g_installed) DoUninstall();
        break;
    case B_PRIMARY:
        if (g_page == PG_WELCOME) {
            if (g_game[0] && (g_haveSP || g_haveUO)) DoInstall();
        } else if (g_page == PG_FAILED) {
            g_page = PG_WELCOME;
            g_error[0] = 0;
        } else if (g_page == PG_DONE) {
            Join(exe, g_game, LAUNCHER_EXE);
            ShellExecuteW(NULL, L"open", exe, NULL, g_game, SW_SHOWNORMAL);
            DestroyWindow(g_wnd);
        }
        break;
    }
    InvalidateRect(g_wnd, NULL, FALSE);
}

static LRESULT CALLBACK SetupProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_PAINT:
        Paint(wnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
        TrackMouseEvent(&tme);
        g_mouse.x = (short)LOWORD(lp);
        g_mouse.y = (short)HIWORD(lp);
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_mouse.x = g_mouse.y = -1;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            int hand = 0;
            for (i = 0; i < g_nBtn; i++) if (PtInRect(&g_btn[i].r, g_mouse)) hand = 1;
            SetCursor(LoadCursor(NULL, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (g_page == PG_WORKING) return 0;
        for (i = 0; i < g_nBtn; i++)
            if (PtInRect(&g_btn[i].r, p)) { Click(g_btn[i].id); break; }
        return 0;
    }
    case WM_KEYDOWN:
        if (g_page == PG_WORKING) return 0;
        if (wp == VK_ESCAPE) DestroyWindow(wnd);
        else if (wp == VK_RETURN) Click(B_PRIMARY);
        return 0;
    case WM_CLOSE:
        if (g_page == PG_WORKING) return 0;
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HFONT MakeFont(int px, int weight)
{
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSW wc = { 0 };
    RECT rc;
    MSG m;
    HDC screen;
    int w, h, style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    pt = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE;
    CoInitialize(NULL);
    if (cmd && wcsstr(cmd, L"/silent")) return Silent(cmd);

    screen = GetDC(NULL);
    g_sc = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, screen);
    g_fTitle = MakeFont(30, FW_BOLD);
    g_fHead = MakeFont(26, FW_BOLD);
    g_fText = MakeFont(15, FW_NORMAL);
    g_fSmall = MakeFont(13, FW_NORMAL);
    g_fBold = MakeFont(13, FW_BOLD);
    g_fBtn = MakeFont(15, FW_SEMIBOLD);
    g_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SETUP), IMAGE_ICON, S(72), S(72), LR_DEFAULTCOLOR);

    FindGame(g_game);   // the folder this runs from, the Activision keys, Steam; empty when none
    Refresh();

    wc.lpfnWndProc = SetupProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SETUP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.lpszClassName = L"CoDVRSetup";
    RegisterClassW(&wc);
    SetRect(&rc, 0, 0, S(BASE_W), S(BASE_H));
    AdjustWindowRect(&rc, style, FALSE);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    g_wnd = CreateWindowExW(0, wc.lpszClassName, TITLE, style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                            NULL, NULL, inst, NULL);
    if (!g_wnd) return 1;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
