// CoD VR setup: installs the mod into a Call of Duty folder, optionally builds the "no hands"
// data pack from the player's own pak files, and uninstalls.
//
// The mod files (opengl32.dll, openxr_loader.dll, cod_vr.ini) are embedded as resources. No game
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
#include "setup_res.h"

#define CODSP_TIMESTAMP   0x40443fda              // CoDSP.exe 1.5
#define BACKUP_SUFFIX     L".cod_vr_backup"
#define HANDS_PACK        L"main\\pakz_vr_nohands.pk3"
#define TEXT_PACK         L"main\\pakz_vr_text.pk3"
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
    int count = 0, cap = 0, i, t, written = 0, ok = 1;
    char utf8[MAX_PATH * 3];
    const char *file = g_vrTexts[0].file;   // all texts live in one file today
    void *data = NULL;
    size_t size = 0;
    mz_zip_archive zw;

    _snwprintf(pattern, MAX_PATH, L"%s\\main\\localized_english_pak*.pk3", game);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;   // not an English game: nothing to change
    do {
        if (count == cap) { cap = cap ? cap * 2 : 16; paks = realloc(paks, cap * sizeof(*paks)); }
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
    if (!data) return 0;

    for (t = 0; t < (int)(sizeof(g_vrTexts) / sizeof(g_vrTexts[0])); t++) {
        size_t newSize;
        char *changed = ReplaceString(data, size, g_vrTexts[t].ref, g_vrTexts[t].text, &newSize);
        if (changed) { free(data); data = changed; size = newSize; written++; }
    }
    if (!written) { free(data); return 0; }

    Join(out, game, TEXT_PACK);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, utf8, sizeof(utf8), NULL, NULL);
    memset(&zw, 0, sizeof(zw));
    if (!mz_zip_writer_init_file(&zw, utf8, 0)) { free(data); return 0; }
    ok = mz_zip_writer_add_mem(&zw, file, data, size, MZ_DEFAULT_COMPRESSION);
    ok = ok && mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    free(data);
    if (!ok) { DeleteFileW(tmp); return 0; }
    return MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------
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
}

// Command line (no dialogs): /silent /dir="<game folder>" [/hands] [/uninstall]
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
    if (!Install(game, &keptIni)) return 1;
    BuildTextPack(game);
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

    Ask(MB_OK | MB_ICONINFORMATION,
        T(L"Done!\n\nConnect your headset (Virtual Desktop or Quest Link: the game needs a 32-bit OpenXR runtime) and start CoDSP.exe.\n\n"
          L"Settings: cod_vr.ini in the game folder.%s%s\n\nRun this setup again to update or uninstall.",
          L"Pronto!\n\nConecte o headset (Virtual Desktop ou Quest Link: o jogo precisa de um runtime OpenXR de 32 bits) e abra o CoDSP.exe.\n\n"
          L"Configurações: cod_vr.ini na pasta do jogo.%s%s\n\nExecute este instalador de novo para atualizar ou desinstalar."),
        keptIni ? T(L"\nYour existing cod_vr.ini was kept (new defaults: cod_vr.ini.default).",
                    L"\nSeu cod_vr.ini foi mantido (novos padrões: cod_vr.ini.default).") : L"",
        models ? T(L"\nHands hidden.", L"\nMãos escondidas.") : L"");
    return 0;
}
