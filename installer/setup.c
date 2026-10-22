// CoD VR setup: installs the mod into a Call of Duty folder, optionally builds the "no hands"
// data pack from the player's own pak files, and uninstalls.
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

#define CODSP_TIMESTAMP   0x40443fda              // CoDSP.exe 1.5
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

static int HasGame(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    Join(p, dir, L"CoDSP.exe");
    return FileExists(p);
}

// PE header TimeDateStamp of CoDSP.exe, 0 if unreadable
static DWORD ExeTimestamp(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    BYTE buf[1024];
    DWORD n = 0, pe;
    HANDLE f;
    Join(p, dir, L"CoDSP.exe");
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

// Candidates: the folder this setup runs from, the Activision install key, Steam.
static int FindGame(wchar_t *out)
{
    wchar_t p[MAX_PATH], *slash;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    if ((slash = wcsrchr(p, L'\\')) != NULL) *slash = 0;
    if (HasGame(p)) { wcscpy(out, p); return 1; }
    if (RegPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Activision\\Call of Duty", L"InstallPath", p) && HasGame(p)) { wcscpy(out, p); return 1; }
    if (RegPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", p)) {
        wcscat(p, L"\\steamapps\\common\\Call of Duty");
        if (HasGame(p)) { wcscpy(out, p); return 1; }
    }
    return 0;
}

static int PickFolder(wchar_t *out)
{
    BROWSEINFOW bi = { 0 };
    PIDLIST_ABSOLUTE id;
    bi.lpszTitle = T(L"Select the Call of Duty folder (the one with CoDSP.exe)",
                     L"Selecione a pasta do Call of Duty (a que tem o CoDSP.exe)");
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
    Join(p, game, HANDS_PACK);             DeleteFileW(p);
    Join(p, game, TEXT_PACK);              DeleteFileW(p);
    Join(p, game, GORE_PACK);              DeleteFileW(p);
    Join(p, game, FONT_PACK);              DeleteFileW(p);
    Join(p, game, SHOCK_PACK);             DeleteFileW(p);
    Join(p, game, LAUNCHER_EXE);           DeleteFileW(p);
    if (ShortcutPath(p)) DeleteFileW(p);
}

// Command line (no dialogs): /silent /dir="<game folder>" [/hands] [/shortcut] [/uninstall]
// Exit code 0 = done, 1 = failed, 2 = no game in that folder.
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
    if (wcsstr(cmd, L"/shortcut")) CreateShortcut(game);
    Join(pack, game, HANDS_PACK);
    if (wcsstr(cmd, L"/hands")) return BuildHandsPack(game, &models) ? 0 : 1;
    DeleteFileW(pack);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    wchar_t game[MAX_PATH] = L"", dll[MAX_PATH], pack[MAX_PATH];
    int r, keptIni = 0, models = 0, installed;

    pt = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE;
    CoInitialize(NULL);
    if (cmd && wcsstr(cmd, L"/silent")) return Silent(cmd);

    if (FindGame(game)) {
        r = Ask(MB_YESNOCANCEL | MB_ICONQUESTION,
                T(L"Call of Duty found in:\n\n%s\n\nUse this folder? (No = choose another one)",
                  L"Call of Duty encontrado em:\n\n%s\n\nUsar esta pasta? (Não = escolher outra)"), game);
        if (r == IDCANCEL) return 0;
        if (r == IDNO) game[0] = 0;
    }
    while (!game[0] || !HasGame(game)) {
        if (game[0])
            Ask(MB_OK | MB_ICONWARNING, T(L"CoDSP.exe was not found in:\n\n%s", L"CoDSP.exe não foi encontrado em:\n\n%s"), game);
        if (!PickFolder(game)) return 0;
    }

    Join(dll, game, L"opengl32.dll");
    installed = FileExists(dll) && IsOurDll(dll);
    if (installed) {
        r = Ask(MB_YESNOCANCEL | MB_ICONQUESTION,
                T(L"The VR mod is already installed.\n\nYes = update / reinstall\nNo = uninstall",
                  L"O mod de VR já está instalado.\n\nSim = atualizar / reinstalar\nNão = desinstalar"));
        if (r == IDCANCEL) return 0;
        if (r == IDNO) {
            Uninstall(game);
            Ask(MB_OK | MB_ICONINFORMATION, T(L"The VR mod was removed.", L"O mod de VR foi removido."));
            return 0;
        }
    }

    if (ExeTimestamp(game) != CODSP_TIMESTAMP &&
        Ask(MB_YESNO | MB_ICONWARNING,
            T(L"This CoDSP.exe is not version 1.5. The mod only works with 1.5 (it stays inactive otherwise).\n\nInstall anyway?",
              L"Este CoDSP.exe não é a versão 1.5. O mod só funciona com a 1.5 (nas outras ele fica inativo).\n\nInstalar mesmo assim?")) != IDYES)
        return 0;

    if (!Install(game, &keptIni)) {
        Ask(MB_OK | MB_ICONERROR,
            T(L"Could not write the files to:\n\n%s\n\nIs the game running?", L"Não foi possível gravar os arquivos em:\n\n%s\n\nO jogo está aberto?"), game);
        return 1;
    }

    BuildTextPack(game);   // English game only; the mod works without it
    BuildGorePack(game);   // used only when MODS > Gore is set to Gore
    BuildFontPack(game);   // used only when GRAPHICS > Text font is Modern
    BuildShockPack(game);  // explosions without the view shake
    Join(pack, game, HANDS_PACK);
    r = Ask(MB_YESNO | MB_ICONQUESTION,
            T(L"Hide the player's hands and show only the gun?\n\n(The models are made from your own game files.)",
              L"Esconder as mãos do personagem e mostrar só a arma?\n\n(Os modelos são gerados a partir dos arquivos do seu jogo.)"));
    if (r == IDYES) {
        if (!BuildHandsPack(game, &models))
            Ask(MB_OK | MB_ICONWARNING, T(L"Could not build the hands pack. The mod works without it.",
                                          L"Não foi possível gerar o pacote das mãos. O mod funciona sem ele."));
    } else {
        DeleteFileW(pack);
    }

    if (Ask(MB_YESNO | MB_ICONQUESTION,
            T(L"Create a desktop shortcut to the Call of Duty VR launcher?\n\n(The launcher starts the game and has the VR and graphics settings.)",
              L"Criar um atalho na área de trabalho para o launcher do Call of Duty VR?\n\n(O launcher abre o jogo e tem as configurações de VR e gráficos.)")) == IDYES &&
        !CreateShortcut(game))
        Ask(MB_OK | MB_ICONWARNING, T(L"Could not create the shortcut. The launcher is %s in the game folder.",
                                      L"Não foi possível criar o atalho. O launcher é o %s na pasta do jogo."), LAUNCHER_EXE);

    Ask(MB_OK | MB_ICONINFORMATION,
        T(L"Done!\n\nConnect your headset (Virtual Desktop or Quest Link: the game needs a 32-bit OpenXR runtime) and start the game "
          L"with the Call of Duty VR launcher (CoD-VR-Launcher.exe in the game folder), which also has the settings.%s%s\n\n"
          L"Run this setup again to update or uninstall.",
          L"Pronto!\n\nConecte o headset (Virtual Desktop ou Quest Link: o jogo precisa de um runtime OpenXR de 32 bits) e abra o jogo "
          L"pelo launcher do Call of Duty VR (CoD-VR-Launcher.exe na pasta do jogo), que também tem as configurações.%s%s\n\n"
          L"Execute este instalador de novo para atualizar ou desinstalar."),
        keptIni ? T(L"\nYour existing cod_vr.ini was kept (new defaults: cod_vr.ini.default).",
                    L"\nSeu cod_vr.ini foi mantido (novos padrões: cod_vr.ini.default).") : L"",
        models ? T(L"\nHands hidden.", L"\nMãos escondidas.") : L"");
    return 0;
}
