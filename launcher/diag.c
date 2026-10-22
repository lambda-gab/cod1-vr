// Diagnostic report: everything about the PC's VR set-up that decides whether the game can start
// in VR, written to a text file a player can send to the developer. It asks the 32-bit OpenXR
// loader of the game folder what the game itself would be told (the game is a 32-bit program, so
// only a 32-bit OpenXR runtime counts), and adds the registry, the game's files, its settings and
// its log.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

int WriteDiagnostics(const char *gameDir, const wchar_t *version, char *outPath, int outLen);

static FILE *g_f;                 // the part of the report that is written first, for the details
static char  g_find[16][256];     // the conclusions, shown at the top
static int   g_nFind;

static void P(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_f, fmt, ap);
    va_end(ap);
    fputc('\n', g_f);
}

static void Find(const char *fmt, ...)
{
    va_list ap;
    if (g_nFind >= 16) return;
    va_start(ap, fmt);
    _vsnprintf(g_find[g_nFind], sizeof(g_find[0]) - 1, fmt, ap);
    va_end(ap);
    g_find[g_nFind++][sizeof(g_find[0]) - 1] = 0;
}

static const char *MachineName(int m)
{
    return m == 0x14c ? "32-bit (x86)" : m == 0x8664 ? "64-bit (x64)" : m == 0xaa64 ? "ARM64" : m < 0 ? "unreadable" : "unknown";
}

// The kind of program a DLL or EXE is: IMAGE_FILE_HEADER.Machine, or -1.
static int PeMachine(const char *path)
{
    IMAGE_DOS_HEADER dos;
    DWORD sig;
    IMAGE_FILE_HEADER fh;
    int m = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(&dos, sizeof(dos), 1, f) == 1 && dos.e_magic == IMAGE_DOS_SIGNATURE && fseek(f, dos.e_lfanew, SEEK_SET) == 0 &&
        fread(&sig, 4, 1, f) == 1 && sig == IMAGE_NT_SIGNATURE && fread(&fh, sizeof(fh), 1, f) == 1)
        m = fh.Machine;
    fclose(f);
    return m;
}

static int FileExists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

static void FileInfo(const char *dir, const char *name, int pe)
{
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    _snprintf(path, sizeof(path), "%s\\%s", dir, name);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) { P("  %-18s missing", name); return; }
    {
        SYSTEMTIME st;
        FileTimeToSystemTime(&fa.ftLastWriteTime, &st);
        P("  %-18s %9lu bytes  %04d-%02d-%02d %02d:%02d%s%s", name, fa.nFileSizeLow, st.wYear, st.wMonth, st.wDay, st.wHour,
          st.wMinute, pe ? "  " : "", pe ? MachineName(PeMachine(path)) : "");
    }
}

// Prints a text file (at most the last maxBytes of it).
static void DumpFile(const char *path, long maxBytes)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    if (!f) { P("  (cannot open %s)", path); return; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size > maxBytes) { fseek(f, size - maxBytes, SEEK_SET); size = maxBytes; P("  [... the start of the file is left out ...]"); }
    else fseek(f, 0, SEEK_SET);
    buf = malloc(size + 1);
    if (buf) {
        size = (long)fread(buf, 1, size, f);
        buf[size] = 0;
        fputs(buf, g_f);
        if (size && buf[size - 1] != '\n') fputc('\n', g_f);
        free(buf);
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------
static void SystemInfo(void)
{
    typedef LONG (WINAPI *PFN_RtlGetVersion)(OSVERSIONINFOW *);
    OSVERSIONINFOW vi;
    PFN_RtlGetVersion rtl = (PFN_RtlGetVersion)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    BOOL wow = FALSE;
    SYSTEM_INFO si;
    MEMORYSTATUSEX ms;
    DISPLAY_DEVICEA dd;
    int i;

    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (rtl) rtl(&vi);
    IsWow64Process(GetCurrentProcess(), &wow);
    GetNativeSystemInfo(&si);
    P("== System ==");
    P("Windows %lu.%lu build %lu, %s", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber, wow ? "64-bit" : "32-bit");
    P("Processors: %lu (architecture %u)", si.dwNumberOfProcessors, si.wProcessorArchitecture);
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) P("Memory: %lu MB", (unsigned long)(ms.ullTotalPhys >> 20));
    for (i = 0; i < 8; i++) {
        char key[512], ver[64] = "", desc[128] = "";
        HKEY k;
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesA(NULL, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) && i > 0 && !dd.DeviceString[0]) continue;
        // DeviceKey is "\Registry\Machine\System\...": the adapter's driver version is in it
        if (!_strnicmp(dd.DeviceKey, "\\Registry\\Machine\\", 18)) {
            _snprintf(key, sizeof(key), "%s", dd.DeviceKey + 18);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
                DWORD n = sizeof(ver);
                RegQueryValueExA(k, "DriverVersion", NULL, NULL, (BYTE *)ver, &n);
                n = sizeof(desc);
                RegQueryValueExA(k, "ProviderName", NULL, NULL, (BYTE *)desc, &n);
                RegCloseKey(k);
            }
        }
        P("Graphics adapter %d: %s%s  driver %s %s", i, dd.DeviceString, (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " (primary)" : "",
          ver[0] ? ver : "?", desc);
    }
    P("");
}

// ---------------------------------------------------------------------------
// OpenXR runtime registration
// ---------------------------------------------------------------------------
static int RegString(const char *sub, const char *val, REGSAM view, char *out, int n)
{
    HKEY k;
    DWORD len = n - 1, type;
    int ok = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ | view, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExA(k, val, NULL, &type, (BYTE *)out, &len) == ERROR_SUCCESS && type == REG_SZ) { out[len] = 0; ok = 1; }
    RegCloseKey(k);
    return ok;
}

// Prints a runtime's manifest and checks what its library_path points to. Returns 1 when it is a
// usable 32-bit library.
static int DescribeRuntime(const char *json)
{
    char text[8192] = "", copy[8192], lib[MAX_PATH] = "", full[MAX_PATH] = "", *p;
    FILE *f = fopen(json, "rb");
    size_t n;
    int m, ok = 0;
    if (!f) { P("    manifest: cannot be opened (%s)", json); return 0; }
    n = fread(text, 1, sizeof(text) - 1, f);
    text[n] = 0;
    fclose(f);
    P("    manifest %s:", json);
    strcpy(copy, text);
    for (p = copy; *p; ) {   // one line at a time, on a copy: the text is parsed below
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        P("      %s", p);
        if (!e) break;
        p = e + 1;
    }
    p = strstr(text, "\"library_path\"");
    if (p && (p = strchr(p + 14, ':')) && (p = strchr(p, '"'))) {
        int i = 0;
        for (p++; *p && *p != '"' && i < MAX_PATH - 1; p++) {
            if (*p == '\\' && (p[1] == '\\' || p[1] == '/')) p++;
            lib[i++] = *p == '/' ? '\\' : *p;
        }
        lib[i] = 0;
    }
    if (!lib[0]) { P("    library_path not found in the manifest"); return 0; }
    if (lib[1] == ':' || (lib[0] == '\\' && lib[1] == '\\')) _snprintf(full, sizeof(full), "%s", lib);
    else {
        char dir[MAX_PATH];
        _snprintf(dir, sizeof(dir), "%s", json);
        if ((p = strrchr(dir, '\\')) != NULL) *p = 0;
        _snprintf(full, sizeof(full), "%s\\%s", dir, lib);
    }
    m = PeMachine(full);
    P("    library: %s", full);
    P("    library %s, %s", FileExists(full) ? "exists" : "is MISSING", FileExists(full) ? MachineName(m) : "");
    ok = FileExists(full) && m == 0x14c;
    return ok;
}

static int g_has32, g_ok32;

static void OpenXRRegistry(void)
{
    static const struct { const char *name; REGSAM view; } views[2] = {
        { "64-bit view (HKLM\\SOFTWARE\\Khronos\\OpenXR\\1): used by 64-bit games", KEY_WOW64_64KEY },
        { "32-bit view (HKLM\\SOFTWARE\\WOW6432Node\\Khronos\\OpenXR\\1): used by THIS game", KEY_WOW64_32KEY },
    };
    char active[MAX_PATH], env[MAX_PATH];
    int v;
    P("== OpenXR runtimes registered in Windows ==");
    for (v = 0; v < 2; v++) {
        HKEY k;
        P("%s", views[v].name);
        if (RegString("SOFTWARE\\Khronos\\OpenXR\\1", "ActiveRuntime", views[v].view, active, sizeof(active))) {
            int ok;
            P("  ActiveRuntime = %s", active);
            ok = DescribeRuntime(active);
            if (v == 1) { g_has32 = 1; g_ok32 = ok; }
        } else {
            P("  no ActiveRuntime");
        }
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenXR\\1\\AvailableRuntimes", 0, KEY_READ | views[v].view, &k) == ERROR_SUCCESS) {
            char name[MAX_PATH];
            DWORD i, n;
            for (i = 0; ; i++) {
                n = sizeof(name);
                if (RegEnumValueA(k, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
                P("  available: %s", name);
            }
            RegCloseKey(k);
        }
    }
    if (GetEnvironmentVariableA("XR_RUNTIME_JSON", env, sizeof(env))) {
        P("Environment variable XR_RUNTIME_JSON = %s (this overrides the registry)", env);
        DescribeRuntime(env);
    }
    if (GetEnvironmentVariableA("XR_API_LAYER_PATH", env, sizeof(env))) P("Environment variable XR_API_LAYER_PATH = %s", env);
    P("");
}

// ---------------------------------------------------------------------------
// What the game's own OpenXR loader says
// ---------------------------------------------------------------------------
static const char *XrName(XrResult r)
{
#define N(x) case x: return #x
    switch (r) {
    N(XR_SUCCESS); N(XR_ERROR_VALIDATION_FAILURE); N(XR_ERROR_RUNTIME_FAILURE); N(XR_ERROR_OUT_OF_MEMORY);
    N(XR_ERROR_API_VERSION_UNSUPPORTED); N(XR_ERROR_INITIALIZATION_FAILED); N(XR_ERROR_FUNCTION_UNSUPPORTED);
    N(XR_ERROR_EXTENSION_NOT_PRESENT); N(XR_ERROR_LIMIT_REACHED); N(XR_ERROR_SIZE_INSUFFICIENT);
    N(XR_ERROR_HANDLE_INVALID); N(XR_ERROR_INSTANCE_LOST); N(XR_ERROR_FORM_FACTOR_UNSUPPORTED);
    N(XR_ERROR_FORM_FACTOR_UNAVAILABLE); N(XR_ERROR_API_LAYER_NOT_PRESENT); N(XR_ERROR_FILE_ACCESS_ERROR);
    N(XR_ERROR_FILE_CONTENTS_INVALID); N(XR_ERROR_RUNTIME_UNAVAILABLE); N(XR_ERROR_GRAPHICS_DEVICE_INVALID);
    N(XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING);
    default: return "(other error)";
    }
#undef N
}

typedef XrResult (XRAPI_PTR *PFN_gipa)(XrInstance, const char *, PFN_xrVoidFunction *);

static void OpenXRProbe(const char *gameDir)
{
    char path[MAX_PATH];
    HMODULE dll;
    PFN_gipa gipa;
    PFN_xrEnumerateApiLayerProperties enumLayers = NULL;
    PFN_xrEnumerateInstanceExtensionProperties enumExt = NULL;
    PFN_xrCreateInstance create = NULL;
    PFN_xrDestroyInstance destroy = NULL;
    PFN_xrGetInstanceProperties getProps = NULL;
    PFN_xrGetSystem getSystem = NULL;
    PFN_xrGetSystemProperties getSysProps = NULL;
    PFN_xrGetOpenGLGraphicsRequirementsKHR getGl = NULL;
    PFN_xrEnumerateViewConfigurationViews getViews = NULL;
    XrInstance inst = XR_NULL_HANDLE;
    XrResult r;
    uint32_t n = 0, i;
    int hasGl = 0;

    P("== What the game's OpenXR loader reports (the same calls the game makes) ==");
    _snprintf(path, sizeof(path), "%s\\openxr_loader.dll", gameDir);
    if (!FileExists(path)) { P("openxr_loader.dll is not in the game folder"); Find("openxr_loader.dll is missing from the game folder: reinstall the mod."); P(""); return; }
    if (PeMachine(path) != 0x14c) { P("openxr_loader.dll is %s: it must be 32-bit", MachineName(PeMachine(path))); Find("openxr_loader.dll in the game folder is not a 32-bit file: reinstall the mod."); P(""); return; }
    dll = LoadLibraryA(path);
    if (!dll) { P("openxr_loader.dll could not be loaded (error %lu)", GetLastError()); Find("openxr_loader.dll could not be loaded (error %lu).", GetLastError()); P(""); return; }
    gipa = (PFN_gipa)GetProcAddress(dll, "xrGetInstanceProcAddr");
    if (!gipa) { P("xrGetInstanceProcAddr not found in the loader"); P(""); return; }
    gipa(XR_NULL_HANDLE, "xrEnumerateApiLayerProperties", (PFN_xrVoidFunction *)&enumLayers);
    gipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction *)&enumExt);
    gipa(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&create);

    if (enumLayers && XR_SUCCEEDED(enumLayers(0, &n, NULL))) {
        XrApiLayerProperties *l = calloc(n ? n : 1, sizeof(*l));
        P("API layers: %u", n);
        if (l && n) {
            for (i = 0; i < n; i++) l[i].type = XR_TYPE_API_LAYER_PROPERTIES;
            if (XR_SUCCEEDED(enumLayers(n, &n, l))) for (i = 0; i < n; i++) P("  layer %s (%s)", l[i].layerName, l[i].description);
        }
        free(l);
    }
    n = 0;
    r = enumExt ? enumExt(NULL, 0, &n, NULL) : XR_ERROR_FUNCTION_UNSUPPORTED;
    if (XR_FAILED(r)) {
        P("Instance extensions: the runtime could not be asked (%s, %d)", XrName(r), (int)r);
        Find("The OpenXR loader could not list the runtime's extensions (%s): no working 32-bit OpenXR runtime.", XrName(r));
    } else {
        XrExtensionProperties *e = calloc(n ? n : 1, sizeof(*e));
        P("Instance extensions: %u", n);
        if (e) {
            for (i = 0; i < n; i++) e[i].type = XR_TYPE_EXTENSION_PROPERTIES;
            if (XR_SUCCEEDED(enumExt(NULL, n, &n, e))) {
                for (i = 0; i < n; i++) {
                    if (!strcmp(e[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) hasGl = 1;
                    P("  %s", e[i].extensionName);
                }
            }
            free(e);
        }
        P("XR_KHR_opengl_enable (needed by the game): %s", hasGl ? "yes" : "NO");
        if (!hasGl) Find("The OpenXR runtime does not offer XR_KHR_opengl_enable, which the game needs (OpenGL).");
    }

    if (create) {
        XrInstanceCreateInfo ci;
        const char *exts[1] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
        memset(&ci, 0, sizeof(ci));
        ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
        strcpy(ci.applicationInfo.applicationName, "CoD-VR diagnostic");
        strcpy(ci.applicationInfo.engineName, "CoD-VR");
        ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        if (hasGl) { ci.enabledExtensionCount = 1; ci.enabledExtensionNames = exts; }
        r = create(&ci, &inst);
        P("xrCreateInstance: %s (%d)", XrName(r), (int)r);
        if (XR_FAILED(r)) {
            Find("xrCreateInstance failed: %s (%d). The OpenXR runtime could not start: is it the active one, and is its 32-bit side installed?", XrName(r), (int)r);
            inst = XR_NULL_HANDLE;
        }
    }
    if (inst) {
        XrInstanceProperties ip;
        XrSystemGetInfo gi;
        XrSystemId sys = 0;
        gipa(inst, "xrDestroyInstance", (PFN_xrVoidFunction *)&destroy);
        gipa(inst, "xrGetInstanceProperties", (PFN_xrVoidFunction *)&getProps);
        gipa(inst, "xrGetSystem", (PFN_xrVoidFunction *)&getSystem);
        gipa(inst, "xrGetSystemProperties", (PFN_xrVoidFunction *)&getSysProps);
        gipa(inst, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getGl);
        gipa(inst, "xrEnumerateViewConfigurationViews", (PFN_xrVoidFunction *)&getViews);
        memset(&ip, 0, sizeof(ip));
        ip.type = XR_TYPE_INSTANCE_PROPERTIES;
        if (getProps && XR_SUCCEEDED(getProps(inst, &ip)))
            P("Runtime: %s, version %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
              XR_VERSION_PATCH(ip.runtimeVersion));
        memset(&gi, 0, sizeof(gi));
        gi.type = XR_TYPE_SYSTEM_GET_INFO;
        gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        r = getSystem ? getSystem(inst, &gi, &sys) : XR_ERROR_FUNCTION_UNSUPPORTED;
        P("xrGetSystem (a headset): %s (%d)", XrName(r), (int)r);
        if (XR_FAILED(r)) {
            Find("The OpenXR runtime started but reports no headset: %s (%d). Start the headset's PC software (Virtual Desktop streamer and connect, Link, SteamVR) BEFORE the game.", XrName(r), (int)r);
        } else {
            XrSystemProperties sp;
            memset(&sp, 0, sizeof(sp));
            sp.type = XR_TYPE_SYSTEM_PROPERTIES;
            if (getSysProps && XR_SUCCEEDED(getSysProps(inst, sys, &sp)))
                P("System: %s (vendor %u), max swapchain %ux%u, layers %u, tracking: orientation %d position %d", sp.systemName, sp.vendorId,
                  sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight, sp.graphicsProperties.maxLayerCount,
                  (int)sp.trackingProperties.orientationTracking, (int)sp.trackingProperties.positionTracking);
            if (getGl && hasGl) {
                XrGraphicsRequirementsOpenGLKHR req;
                memset(&req, 0, sizeof(req));
                req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
                r = getGl(inst, sys, &req);
                if (XR_SUCCEEDED(r))
                    P("OpenGL needed by the runtime: %u.%u to %u.%u", XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
                      XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported));
                else P("xrGetOpenGLGraphicsRequirementsKHR: %s (%d)", XrName(r), (int)r);
            }
            if (getViews) {
                uint32_t nv = 0;
                XrViewConfigurationView vv[4];
                for (i = 0; i < 4; i++) { memset(&vv[i], 0, sizeof(vv[i])); vv[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW; }
                if (XR_SUCCEEDED(getViews(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 4, &nv, vv)))
                    for (i = 0; i < nv; i++) P("Eye %u: recommended %ux%u, maximum %ux%u", i, vv[i].recommendedImageRectWidth, vv[i].recommendedImageRectHeight,
                                               vv[i].maxImageRectWidth, vv[i].maxImageRectHeight);
            }
            if (hasGl) Find("OpenXR answers as the game needs (runtime %s, headset found). If VR still does not start, the log below has the reason.", ip.runtimeName);
        }
        if (destroy) destroy(inst);
    }
    P("");
}

// ---------------------------------------------------------------------------
// Programs that matter, the game's files and settings
// ---------------------------------------------------------------------------
static void Processes(void)
{
    static const char *words[] = { "virtualdesktop", "vrserver", "vrmonitor", "vrcompositor", "vrstartup", "steam", "ovr", "oculus", "alvr",
                                   "wivrn", "mixedreality", "pico", "vive", "htc", "varjo", "openxr", "codsp", "cod_vr", "reality", "meta" };
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    int any = 0;
    P("== VR related programs running now ==");
    if (s == INVALID_HANDLE_VALUE) { P("(cannot list the programs)"); P(""); return; }
    pe.dwSize = sizeof(pe);
    if (Process32First(s, &pe)) do {
        char low[MAX_PATH];
        int i;
        for (i = 0; pe.szExeFile[i] && i < MAX_PATH - 1; i++) low[i] = (char)tolower((unsigned char)pe.szExeFile[i]);
        low[i] = 0;
        for (i = 0; i < (int)(sizeof(words) / sizeof(words[0])); i++)
            if (strstr(low, words[i]) && !strstr(low, "webhelper")) { P("  %s", pe.szExeFile); any = 1; break; }
    } while (Process32Next(s, &pe));
    CloseHandle(s);
    if (!any) P("  none: no headset software is running");
    P("");
}

static void GameFiles(const char *dir)
{
    char path[MAX_PATH];
    P("== Game folder ==");
    P("%s", dir);
    FileInfo(dir, "CoDSP.exe", 1);
    FileInfo(dir, "opengl32.dll", 1);
    FileInfo(dir, "openxr_loader.dll", 1);
    FileInfo(dir, "cod_vr.ini", 0);
    FileInfo(dir, "CoD-VR-Launcher.exe", 1);
    FileInfo(dir, "steam_api.dll", 0);
    FileInfo(dir, "main\\pakz_vr_text.pk3", 0);
    _snprintf(path, sizeof(path), "%s\\opengl32.dll", dir);
    if (!FileExists(path)) Find("opengl32.dll (the VR mod) is not in the game folder: run the setup again.");
    else if (PeMachine(path) != 0x14c) Find("opengl32.dll in the game folder is not the 32-bit mod file.");
    P("");
    P("== cod_vr.ini ==");
    _snprintf(path, sizeof(path), "%s\\cod_vr.ini", dir);
    DumpFile(path, 12000);
    P("");
}

// The game writes cod_vr.log next to itself, or, when Windows does not let it write in Program
// Files, in the user's VirtualStore: this takes the newest of the places it may be in.
static void GameLog(const char *dir)
{
    char cand[3][MAX_PATH], best[MAX_PATH] = "", local[MAX_PATH];
    FILETIME bestTime = { 0, 0 };
    int i;
    P("== cod_vr.log (the game's own log of its last run) ==");
    _snprintf(cand[0], MAX_PATH, "%s\\cod_vr.log", dir);
    cand[1][0] = 0;
    if (GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local)) && dir[1] == ':')
        _snprintf(cand[1], MAX_PATH, "%s\\VirtualStore%s\\cod_vr.log", local, dir + 2);
    GetTempPathA(sizeof(local), local);
    _snprintf(cand[2], MAX_PATH, "%scod_vr.log", local);
    for (i = 0; i < 3; i++) {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!cand[i][0] || !GetFileAttributesExA(cand[i], GetFileExInfoStandard, &fa)) continue;
        P("found: %s (%lu bytes)", cand[i], fa.nFileSizeLow);
        if (CompareFileTime(&fa.ftLastWriteTime, &bestTime) > 0) { bestTime = fa.ftLastWriteTime; strcpy(best, cand[i]); }
    }
    if (!best[0]) { P("no log found: the game has not been started with the mod yet"); Find("There is no cod_vr.log: the game was never started with the mod, or the mod is not loaded."); return; }
    P("newest: %s", best);
    P("--------");
    DumpFile(best, 40000);
}

// ---------------------------------------------------------------------------
int WriteDiagnostics(const char *gameDir, const wchar_t *version, char *outPath, int outLen)
{
    char desktop[MAX_PATH] = "";
    FILE *out, *body;
    SYSTEMTIME st;
    int i;
    char buf[4096];
    size_t n;

    {
        char tmp[MAX_PATH];
        GetTempPathA(sizeof(tmp), tmp);
        strcat(tmp, "cod_vr_diag.tmp");
        body = fopen(tmp, "w+bD");   // deleted when closed
    }
    if (!body) return 0;
    g_f = body;
    g_nFind = 0;
    g_has32 = g_ok32 = 0;

    SystemInfo();
    OpenXRRegistry();
    if (!g_has32) Find("No 32-bit OpenXR runtime is registered in Windows. The game is a 32-bit program and can only use a runtime registered for 32-bit programs.");
    else if (!g_ok32) Find("The active 32-bit OpenXR runtime points to a file that is missing or is not 32-bit.");
    OpenXRProbe(gameDir);
    Processes();
    GameFiles(gameDir);
    GameLog(gameDir);

    if (SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop) != S_OK || !desktop[0]) strcpy(desktop, gameDir);
    _snprintf(outPath, outLen, "%s\\CoD-VR-diagnostic.txt", desktop);
    out = fopen(outPath, "wb");
    if (!out) { fclose(body); return 0; }
    GetLocalTime(&st);
    fprintf(out, "CoD VR diagnostic report, %ls, made %04d-%02d-%02d %02d:%02d\r\n", version, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    fprintf(out, "Send this file to the developer.\r\n\r\n== Summary ==\r\n");
    if (!g_nFind) fprintf(out, "(nothing found wrong in the set-up: read the details below)\r\n");
    for (i = 0; i < g_nFind; i++) fprintf(out, "* %s\r\n", g_find[i]);
    fprintf(out, "\r\n");
    fflush(body);
    fseek(body, 0, SEEK_SET);
    while ((n = fread(buf, 1, sizeof(buf), body)) > 0) fwrite(buf, 1, n, out);
    fclose(body);
    fclose(out);
    return 1;
}
