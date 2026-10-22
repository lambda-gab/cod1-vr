// OpenXR backend for cod_vr: instance/session with XR_KHR_opengl_enable on the game's own
// GL context, one swapchain per eye, frame pacing and eye poses.
// The loader (openxr_loader.dll, Win32 build) is loaded at runtime from the game folder, so a
// missing loader only disables VR instead of breaking opengl32.dll.
#define _CRT_SECURE_NO_WARNINGS
#include "cod_vr.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
#define XR_FUNCS(X) \
    X(xrDestroyInstance) X(xrGetSystem) X(xrGetInstanceProperties) X(xrGetOpenGLGraphicsRequirementsKHR) \
    X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession) X(xrPollEvent) \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrEnumerateViewConfigurationViews) \
    X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) \
    X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) \
    X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews) X(xrLocateSpace) X(xrResultToString) \
    X(xrStringToPath) X(xrCreateActionSet) X(xrCreateAction) X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) X(xrCreateActionSpace) X(xrSyncActions) X(xrGetActionStateFloat) \
    X(xrGetActionStateBoolean) X(xrGetActionStateVector2f) X(xrApplyHapticFeedback)
#define X(name) static PFN_##name name;
XR_FUNCS(X)
#undef X
static PFN_xrCreateInstance xrCreateInstance_;

#define MAX_IMAGES 8

static struct {
    int        failed;          // gave up for this context (logged once)
    DWORD      retryAt;         // nonzero: no headset yet, try xrGetSystem again at this tick
    int        warnedNoHmd;
    XrInstance instance;
    XrSystemId system;
    XrSession  session;
    HGLRC      ctx;
    XrSpace    local, view;
    XrSessionState state;
    int        running;         // between xrBeginSession and xrEndSession
    // 0 = left eye, 1 = right eye, 2 = HUD quad, 3 = scope lens (unused), 4/5 = laser per eye
    XrSwapchain swapchain[6];
    int        scW[6], scH[6];
    uint32_t   imageCount[6];
    XrSwapchainImageOpenGLKHR images[6][MAX_IMAGES];
    int        laserOn;
    int        renderW, renderH;   // eye buffers (the swapchains are the recommended size)
    int        progressOn;       // a progress bar: centre, half-size vectors (XR coordinates), 0..1
    float      progress[3], progressR[3], progressU[3], progressFrac;
    int        reticleOn;       // crosshair style (0 = none): centre, half-size vectors (XR coordinates), colour
    float      reticle[3], reticleR[3], reticleU[3], reticleColor[4];
    float      laser[64][3];    // polyline in XR coordinates
    int        laserCount;
    float      laserColor[4];
    float      laserRing;       // metres: horizontal ring around the last point (0 = a dot)
    int        hudW, hudH;      // HUD swapchain size requested by XR_Init
    int        scopeSize;
    GLuint     dstFbo;
    // current frame
    int        frameOpen;
    XrFrameState frameState;
    XrView     views[2];
    int        viewsValid;
    // controllers (action set and actions live with the instance, spaces with the session)
    XrActionSet actionSet;
    XrAction   aPose, aTrigger, aGrip, aStick, aStickClick, aA, aB, aX, aY, aMenu, aHaptic;
    XrPath     handPath[2];
    XrSpace    handSpace[2];
} xr;

static float xr_renderScale = 1.0f;   // eye swapchains: the recommended size times this
static int   xr_recW, xr_recH, xr_maxW, xr_maxH;   // the runtime's recommended / largest eye image

static const char *ResultStr(XrResult r)
{
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (xrResultToString && xr.instance && xrResultToString(xr.instance, r, buf) == XR_SUCCESS) return buf;
    sprintf(buf, "XrResult %d", r);
    return buf;
}

#define CHECK(call) do { XrResult r_ = (call); if (XR_FAILED(r_)) { Log("OpenXR: %s failed: %s", #call, ResultStr(r_)); return 0; } } while (0)

static int LoadLoader(void)
{
    char path[MAX_PATH];
    HMODULE dll;
    _snprintf(path, sizeof(path), "%s\\openxr_loader.dll", g_dir);
    dll = LoadLibraryA(path);
    if (!dll) { Log("OpenXR: %s not found (error %lu)", path, GetLastError()); return 0; }
    xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(dll, "xrGetInstanceProcAddr");
    if (!xrGetInstanceProcAddr) return 0;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&xrCreateInstance_);
    return xrCreateInstance_ != NULL;
}

static int CreateInstance(void)
{
    const char *ext[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };

    strcpy(ci.applicationInfo.applicationName, "Call of Duty VR");
    strcpy(ci.applicationInfo.engineName, "cod_vr");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = ext;
    CHECK(xrCreateInstance_(&ci, &xr.instance));

#define X(name) xrGetInstanceProcAddr(xr.instance, #name, (PFN_xrVoidFunction *)&name);
    XR_FUNCS(X)
#undef X
    if (xrGetInstanceProperties(xr.instance, &ip) == XR_SUCCESS)
        Log("OpenXR runtime: %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
            XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    return 1;
}

// The headset may connect later (Virtual Desktop streaming not started yet): keep asking.
static int GetSystem(void)
{
    XrSystemGetInfo si = { XR_TYPE_SYSTEM_GET_INFO };
    XrResult r;
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(xr.instance, &si, &xr.system);
    if (XR_SUCCEEDED(r)) return 1;
    if (!xr.warnedNoHmd) { Log("OpenXR: no headset yet (%s), retrying every 2 s", ResultStr(r)); xr.warnedNoHmd = 1; }
    xr.retryAt = GetTickCount() + 2000;
    return 0;
}

static int64_t PickFormat(void)
{
    int64_t formats[64], pick = 0;
    uint32_t n = 0, i;
    if (XR_FAILED(xrEnumerateSwapchainFormats(xr.session, 64, &n, formats))) return 0;
    // The game writes gamma-encoded colors without GL_FRAMEBUFFER_SRGB. An sRGB swapchain tells
    // the compositor those values are already gamma encoded, so they are shown unchanged.
    for (i = 0; i < n; i++) if (formats[i] == GL_SRGB8_ALPHA8) return formats[i];
    for (i = 0; i < n; i++) if (formats[i] == GL_RGBA8) pick = formats[i];
    return pick ? pick : (n ? formats[0] : 0);
}

static int CreateSwapchain(int i, int64_t format, int w, int h)
{
    XrSwapchainCreateInfo sc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    uint32_t k;
    sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    sc.format = format;
    sc.sampleCount = 1;
    sc.width = w;
    sc.height = h;
    sc.faceCount = 1;
    sc.arraySize = 1;
    sc.mipCount = 1;
    CHECK(xrCreateSwapchain(xr.session, &sc, &xr.swapchain[i]));
    xr.scW[i] = w;
    xr.scH[i] = h;
    for (k = 0; k < MAX_IMAGES; k++) xr.images[i][k].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    CHECK(xrEnumerateSwapchainImages(xr.swapchain[i], MAX_IMAGES, &xr.imageCount[i], (XrSwapchainImageBaseHeader *)xr.images[i]));
    Log("OpenXR: %s swapchain %dx%d, %u images", i >= 4 ? "laser" : i == 3 ? "scope" : i == 2 ? "HUD" : i ? "right eye" : "left eye", w, h, xr.imageCount[i]);
    return 1;
}

static XrPath Path(const char *s)
{
    XrPath p = XR_NULL_PATH;
    xrStringToPath(xr.instance, s, &p);
    return p;
}

static int CreateAction(XrAction *out, const char *name, const char *localized, XrActionType type, int perHand)
{
    XrActionCreateInfo ai = { XR_TYPE_ACTION_CREATE_INFO };
    strcpy(ai.actionName, name);
    strcpy(ai.localizedActionName, localized);
    ai.actionType = type;
    if (perHand) { ai.countSubactionPaths = 2; ai.subactionPaths = xr.handPath; }
    CHECK(xrCreateAction(xr.actionSet, &ai, out));
    return 1;
}

typedef struct { XrAction *action; const char *path; } binding_t;

static void Suggest(const char *profile, const binding_t *b, int n)
{
    XrActionSuggestedBinding sb[32];
    XrInteractionProfileSuggestedBinding ip = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    XrResult r;
    int i;
    for (i = 0; i < n; i++) { sb[i].action = *b[i].action; sb[i].binding = Path(b[i].path); }
    ip.interactionProfile = Path(profile);
    ip.suggestedBindings = sb;
    ip.countSuggestedBindings = n;
    r = xrSuggestInteractionProfileBindings(xr.instance, &ip);
    if (XR_FAILED(r)) Log("OpenXR: bindings for %s rejected: %s", profile, ResultStr(r));
}

// Once per instance: the action set becomes immutable when a session attaches it.
static int CreateActions(void)
{
    XrActionSetCreateInfo si = { XR_TYPE_ACTION_SET_CREATE_INFO };
    static const binding_t touch[] = {
        { &xr.aPose, "/user/hand/left/input/aim/pose" },          { &xr.aPose, "/user/hand/right/input/aim/pose" },
        { &xr.aTrigger, "/user/hand/left/input/trigger/value" },  { &xr.aTrigger, "/user/hand/right/input/trigger/value" },
        { &xr.aGrip, "/user/hand/left/input/squeeze/value" },     { &xr.aGrip, "/user/hand/right/input/squeeze/value" },
        { &xr.aStick, "/user/hand/left/input/thumbstick" },       { &xr.aStick, "/user/hand/right/input/thumbstick" },
        { &xr.aStickClick, "/user/hand/left/input/thumbstick/click" }, { &xr.aStickClick, "/user/hand/right/input/thumbstick/click" },
        { &xr.aA, "/user/hand/right/input/a/click" },             { &xr.aB, "/user/hand/right/input/b/click" },
        { &xr.aX, "/user/hand/left/input/x/click" },              { &xr.aY, "/user/hand/left/input/y/click" },
        { &xr.aMenu, "/user/hand/left/input/menu/click" },
        { &xr.aHaptic, "/user/hand/left/output/haptic" },         { &xr.aHaptic, "/user/hand/right/output/haptic" },
    };
    static const binding_t simple[] = {
        { &xr.aPose, "/user/hand/left/input/aim/pose" },          { &xr.aPose, "/user/hand/right/input/aim/pose" },
        { &xr.aTrigger, "/user/hand/left/input/select/click" },   { &xr.aTrigger, "/user/hand/right/input/select/click" },
        { &xr.aMenu, "/user/hand/left/input/menu/click" },
        { &xr.aHaptic, "/user/hand/left/output/haptic" },         { &xr.aHaptic, "/user/hand/right/output/haptic" },
    };

    if (xr.actionSet) return 1;
    xr.handPath[0] = Path("/user/hand/left");
    xr.handPath[1] = Path("/user/hand/right");
    strcpy(si.actionSetName, "gameplay");
    strcpy(si.localizedActionSetName, "Gameplay");
    CHECK(xrCreateActionSet(xr.instance, &si, &xr.actionSet));
    if (!CreateAction(&xr.aPose, "aim", "Aim", XR_ACTION_TYPE_POSE_INPUT, 1) ||
        !CreateAction(&xr.aTrigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, 1) ||
        !CreateAction(&xr.aGrip, "grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, 1) ||
        !CreateAction(&xr.aStick, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, 1) ||
        !CreateAction(&xr.aStickClick, "thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, 1) ||
        !CreateAction(&xr.aA, "button_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aB, "button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aX, "button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aY, "button_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aMenu, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
        !CreateAction(&xr.aHaptic, "haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, 1))
        return 0;
    Suggest("/interaction_profiles/oculus/touch_controller", touch, sizeof(touch) / sizeof(touch[0]));
    Suggest("/interaction_profiles/khr/simple_controller", simple, sizeof(simple) / sizeof(simple[0]));
    return 1;
}

// Per session: attach the action set and create a space for each hand's aim pose.
static void AttachActions(void)
{
    XrSessionActionSetsAttachInfo ai = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    XrResult r;
    int i;
    if (!xr.actionSet) return;
    ai.countActionSets = 1;
    ai.actionSets = &xr.actionSet;
    r = xrAttachSessionActionSets(xr.session, &ai);
    if (XR_FAILED(r)) { Log("OpenXR: controllers unavailable: %s", ResultStr(r)); return; }
    for (i = 0; i < 2; i++) {
        XrActionSpaceCreateInfo sp = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        sp.action = xr.aPose;
        sp.subactionPath = xr.handPath[i];
        sp.poseInActionSpace.orientation.w = 1;
        xrCreateActionSpace(xr.session, &sp, &xr.handSpace[i]);
    }
    Log("OpenXR: controller actions attached");
}

static int CreateSession(void)
{
    XrGraphicsRequirementsOpenGLKHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    XrGraphicsBindingOpenGLWin32KHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    XrViewConfigurationView vcv[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    uint32_t n = 0;
    int64_t format;
    int i;

    CHECK(xrGetOpenGLGraphicsRequirementsKHR(xr.instance, xr.system, &req));   // mandatory before xrCreateSession
    binding.hDC = gl.GetCurrentDC();
    binding.hGLRC = gl.GetCurrentContext();
    sci.next = &binding;
    sci.systemId = xr.system;
    CHECK(xrCreateSession(xr.instance, &sci, &xr.session));
    xr.ctx = binding.hGLRC;

    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;   // seated: origin at the head when recentered
    CHECK(xrCreateReferenceSpace(xr.session, &rs, &xr.local));
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    CHECK(xrCreateReferenceSpace(xr.session, &rs, &xr.view));
    AttachActions();

    CHECK(xrEnumerateViewConfigurationViews(xr.instance, xr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv));
    format = PickFormat();
    if (!format) { Log("OpenXR: no usable swapchain format"); return 0; }
    Log("OpenXR: swapchain format 0x%llx", format);

    for (i = 0; i < 2; i++) {
        // The eyes are rendered at the runtime's recommended resolution scaled by RenderScale
        // (within what it accepts), but handed over at the recommended size: SteamVR's 32-bit
        // runtime shows an eye image of any other size wrong (a part of it zoomed in, or black),
        // whatever the image rectangle says. The copy into the swapchain scales it.
        int ew = (int)(vcv[i].recommendedImageRectWidth * xr_renderScale + 0.5f);
        int eh = (int)(vcv[i].recommendedImageRectHeight * xr_renderScale + 0.5f);
        if (i == 0) {
            xr_recW = vcv[0].recommendedImageRectWidth; xr_recH = vcv[0].recommendedImageRectHeight;
            xr_maxW = vcv[0].maxImageRectWidth; xr_maxH = vcv[0].maxImageRectHeight;
        }
        if (ew > (int)vcv[i].maxImageRectWidth) ew = vcv[i].maxImageRectWidth;
        if (eh > (int)vcv[i].maxImageRectHeight) eh = vcv[i].maxImageRectHeight;
        if (i == 0) { xr.renderW = ew; xr.renderH = eh; }
        if (!CreateSwapchain(i, format, vcv[i].recommendedImageRectWidth, vcv[i].recommendedImageRectHeight)) return 0;
    }
    Log("OpenXR: eyes rendered at %dx%d (RenderScale %.2f), sent at %ux%u", xr.renderW, xr.renderH, xr_renderScale,
        vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight);
    if (xr.hudW > 0 && xr.hudH > 0 && !CreateSwapchain(2, format, xr.hudW, xr.hudH)) return 0;
    if (xr.scopeSize > 0 && !CreateSwapchain(3, format, xr.scopeSize, xr.scopeSize)) return 0;
    // lines over the world (menu beam, grenade line, crosshair): full resolution, so thin lines stay crisp
    for (i = 0; i < 2; i++)
        if (!CreateSwapchain(4 + i, format, vcv[i].recommendedImageRectWidth, vcv[i].recommendedImageRectHeight)) return 0;
    gl.GenFramebuffers(1, &xr.dstFbo);
    return 1;
}

void XR_SetRenderScale(float s) { xr_renderScale = s < 0.5f ? 0.5f : s > 2.0f ? 2.0f : s; }

void XR_RecommendedSize(int *w, int *h, int *maxW, int *maxH)
{
    *w = xr_recW; *h = xr_recH; *maxW = xr_maxW; *maxH = xr_maxH;
}

int XR_EyeSize(int *w, int *h)
{
    if (!xr.swapchain[0] || !xr.renderW) return 0;
    *w = xr.renderW;
    *h = xr.renderH;
    return 1;
}

int XR_Init(int hudW, int hudH, int scopeSize)
{
    if (xr.session) return 1;
    xr.hudW = hudW;
    xr.hudH = hudH;
    xr.scopeSize = scopeSize;
    if (xr.retryAt && (int)(GetTickCount() - xr.retryAt) < 0) return 0;
    if (xr.failed && !xr.retryAt) return 0;
    xr.failed = 1;   // until proven otherwise; no retry every frame
    xr.retryAt = 0;
    if (!xrGetInstanceProcAddr && !LoadLoader()) return 0;
    if (!xr.instance && !CreateInstance()) return 0;
    if (!xr.system && !GetSystem()) return 0;
    if (!CreateActions()) Log("OpenXR: controller actions could not be created");
    if (!CreateSession()) { XR_Shutdown(); xr.failed = 1; return 0; }
    xr.failed = 0;
    Log("OpenXR: session created, waiting for the headset");
    return 1;
}

int XR_Ready(void) { return xr.session != XR_NULL_HANDLE; }
int XR_FrameOpen(void) { return xr.frameOpen; }

void XR_Shutdown(void)
{
    int i;
    if (xr.session) {
        if (xr.running) xrEndSession(xr.session);
        for (i = 0; i < 6; i++) if (xr.swapchain[i]) xrDestroySwapchain(xr.swapchain[i]);
        for (i = 0; i < 2; i++) if (xr.handSpace[i]) xrDestroySpace(xr.handSpace[i]);
        if (xr.local) xrDestroySpace(xr.local);
        if (xr.view) xrDestroySpace(xr.view);
        xrDestroySession(xr.session);
        if (xr.dstFbo && gl.GetCurrentContext() == xr.ctx) gl.DeleteFramebuffers(1, &xr.dstFbo);
        Log("OpenXR: session destroyed");
    }
    {
        XrInstance keep = xr.instance;
        XrSystemId sys = xr.system;
        XrActionSet set = xr.actionSet;
        XrAction actions[11];
        XrPath hands[2];
        memcpy(actions, &xr.aPose, sizeof(actions));
        memcpy(hands, xr.handPath, sizeof(hands));
        memset(&xr, 0, sizeof(xr));
        xr.instance = keep;   // instance, system and actions survive vid_restart
        xr.system = sys;
        xr.actionSet = set;
        memcpy(&xr.aPose, actions, sizeof(actions));
        memcpy(xr.handPath, hands, sizeof(hands));
    }
}

static void PollEvents(void)
{
    XrEventDataBuffer ev;
    for (;;) {
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        ev.next = NULL;
        if (xrPollEvent(xr.instance, &ev) != XR_SUCCESS) break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged *sc = (XrEventDataSessionStateChanged *)&ev;
            xr.state = sc->state;
            Log("OpenXR: session state %d", sc->state);
            if (sc->state == XR_SESSION_STATE_READY && !xr.running) {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(xr.session, &bi))) { xr.running = 1; Log("OpenXR: session running"); }
            } else if (sc->state == XR_SESSION_STATE_STOPPING && xr.running) {
                xrEndSession(xr.session);
                xr.running = 0;
            } else if (sc->state == XR_SESSION_STATE_EXITING || sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                xr.running = 0;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            Log("OpenXR: instance loss pending");
            xr.running = 0;
        }
    }
}

// XR (x right, y up, -z forward) -> Quake 3 (x forward, y left, z up)
static void ToQ3(const float v[3], float out[3]) { out[0] = -v[2]; out[1] = -v[0]; out[2] = v[1]; }

static void Rotate(const XrQuaternionf *q, const float v[3], float out[3])
{
    // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
    float t[3], c[3];
    t[0] = q->y * v[2] - q->z * v[1] + q->w * v[0];
    t[1] = q->z * v[0] - q->x * v[2] + q->w * v[1];
    t[2] = q->x * v[1] - q->y * v[0] + q->w * v[2];
    c[0] = q->y * t[2] - q->z * t[1];
    c[1] = q->z * t[0] - q->x * t[2];
    c[2] = q->x * t[1] - q->y * t[0];
    out[0] = v[0] + 2 * c[0]; out[1] = v[1] + 2 * c[1]; out[2] = v[2] + 2 * c[2];
}

static void PoseToQ3(const XrPosef *p, vr_pose_t *out)
{
    static const float fwd[3] = { 0, 0, -1 }, left[3] = { -1, 0, 0 }, up[3] = { 0, 1, 0 };
    float pos[3] = { p->position.x, p->position.y, p->position.z }, t[3];
    ToQ3(pos, out->pos);
    Rotate(&p->orientation, fwd, t);  ToQ3(t, out->axis[0]);
    Rotate(&p->orientation, left, t); ToQ3(t, out->axis[1]);
    Rotate(&p->orientation, up, t);   ToQ3(t, out->axis[2]);
}

int XR_BeginFrame(vr_pose_t eyes[2], vr_pose_t *head)
{
    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    XrSpaceLocation hl = { XR_TYPE_SPACE_LOCATION };
    uint32_t n = 0;
    int i;

    if (!xr.session) return 0;
    if (xr.frameOpen) XR_EndFrame(NULL, 0, 0, 0, 0, NULL, NULL);   // previous frame never reached SwapBuffers
    xr.laserOn = 0;
    xr.reticleOn = 0;
    xr.progressOn = 0;
    PollEvents();
    if (!xr.running) return 0;

    xr.frameState.type = XR_TYPE_FRAME_STATE;
    xr.frameState.next = NULL;
    if (XR_FAILED(xrWaitFrame(xr.session, &wi, &xr.frameState))) return 0;
    if (XR_FAILED(xrBeginFrame(xr.session, &bi))) return 0;
    xr.frameOpen = 1;

    xr.views[0].type = xr.views[1].type = XR_TYPE_VIEW;
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = xr.frameState.predictedDisplayTime;
    li.space = xr.local;
    if (XR_FAILED(xrLocateViews(xr.session, &li, &vs, 2, &n, xr.views)) ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        xr.viewsValid = 0;
        return 0;   // frame is still open; XR_EndFrame submits nothing
    }
    xr.viewsValid = 1;
    {
        // once: what the runtime asks for (a wrong-looking headset view starts here)
        static int logged;
        if (!logged) {
            logged = 1;
            for (i = 0; i < 2; i++)
                Log("OpenXR: %s eye fov left %.1f right %.1f up %.1f down %.1f deg, swapchain %dx%d",
                    i ? "right" : "left", xr.views[i].fov.angleLeft * 57.29578f, xr.views[i].fov.angleRight * 57.29578f,
                    xr.views[i].fov.angleUp * 57.29578f, xr.views[i].fov.angleDown * 57.29578f, xr.scW[i], xr.scH[i]);
        }
    }

    for (i = 0; i < 2; i++) {
        PoseToQ3(&xr.views[i].pose, &eyes[i]);
        // OpenGL eye space: x right, y up. XR angles: left/down negative.
        eyes[i].tanLeft  = tanf(xr.views[i].fov.angleLeft);
        eyes[i].tanRight = tanf(xr.views[i].fov.angleRight);
        eyes[i].tanUp    = tanf(xr.views[i].fov.angleUp);
        eyes[i].tanDown  = tanf(xr.views[i].fov.angleDown);
    }
    if (XR_SUCCEEDED(xrLocateSpace(xr.view, xr.local, xr.frameState.predictedDisplayTime, &hl)) &&
        (hl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        PoseToQ3(&hl.pose, head);
    } else {
        *head = eyes[0];
    }
    return xr.frameState.shouldRender ? 1 : 0;
}

// Diagnostics ([Debug] DumpEyes): the left eye's buffer and the swapchain image made from it,
// a quarter of the size, as TGA files next to the log.
static char g_dumpDir[MAX_PATH];
void XR_RequestDump(const char *dir) { strncpy(g_dumpDir, dir, MAX_PATH - 1); }

static void DumpFramebuffer(GLuint fbo, int w, int h, const char *name)
{
    char path[MAX_PATH];
    unsigned char *px, hdr[18] = { 0 };
    int ow = w / 4, oh = h / 4, x, y;
    FILE *f;
    if (!gl.ReadPixels || w < 4 || h < 4 || !(px = malloc((size_t)w * h * 4))) return;
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl.ReadPixels(0, 0, w, h, 0x1908, 0x1401, px);   // GL_RGBA, GL_UNSIGNED_BYTE
    _snprintf(path, sizeof(path), "%s\\%s", g_dumpDir, name);
    if ((f = fopen(path, "wb"))) {
        hdr[2] = 2;
        hdr[12] = (unsigned char)ow; hdr[13] = (unsigned char)(ow >> 8);
        hdr[14] = (unsigned char)oh; hdr[15] = (unsigned char)(oh >> 8);
        hdr[16] = 24;
        fwrite(hdr, 1, 18, f);
        for (y = 0; y < oh; y++)
            for (x = 0; x < ow; x++) {
                const unsigned char *p = px + ((size_t)(y * 4) * w + x * 4) * 4;
                unsigned char bgr[3] = { p[2], p[1], p[0] };
                fwrite(bgr, 1, 3, f);
            }
        fclose(f);
        Log("dump: %s (%dx%d of %dx%d)", path, ow, oh, w, h);
    }
    free(px);
}

static int CopySwapchainImage(int eye, GLuint srcFbo, int sx, int sy, int w, int h)
{
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    uint32_t index;

    if (XR_FAILED(xrAcquireSwapchainImage(xr.swapchain[eye], &ai, &index))) return 0;
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(xr.swapchain[eye], &wi))) return 0;

    gl.BindFramebuffer(GL_FRAMEBUFFER, xr.dstFbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xr.images[eye][index].image, 0);
    // An eye was drawn with the eye's projection into a window-sized buffer: stretching it to
    // the swapchain keeps the geometry right (projection maps the frustum to the whole target).
    GL_BlitRect(srcFbo, sx, sy, w, h, xr.dstFbo, xr.scW[eye], xr.scH[eye]);
    if (g_dumpDir[0] && eye == 0) {
        DumpFramebuffer(srcFbo, w, h, "dump_eye_rendered.tga");
        DumpFramebuffer(xr.dstFbo, xr.scW[eye], xr.scH[eye], "dump_eye_sent.tga");
        Log("dump: eye buffer %dx%d from (%d,%d), swapchain %dx%d", w, h, sx, sy, xr.scW[eye], xr.scH[eye]);
        g_dumpDir[0] = 0;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, xr.dstFbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

    xrReleaseSwapchainImage(xr.swapchain[eye], &ri);
    return 1;
}

// Quake 3 axes (x forward, y left, z up) -> XR (x right, y up, -z forward)
static void FromQ3(const float v[3], float out[3]) { out[0] = -v[1]; out[1] = v[2]; out[2] = -v[0]; }

void XR_SetLine(const float (*pts)[3], int n, const float rgba[4], float ring)
{
    int i;
    if (n < 2) return;
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) FromQ3(pts[i], xr.laser[i]);
    xr.laserCount = n;
    memcpy(xr.laserColor, rgba, sizeof(xr.laserColor));
    xr.laserRing = ring;
    xr.laserOn = 1;
}

// A crosshair in the world: four arms from gap to the end of right / up (tracking space, Quake axes).
void XR_SetProgress(const float center[3], const float right[3], const float up[3], float fraction)
{
    FromQ3(center, xr.progress);
    FromQ3(right, xr.progressR);
    FromQ3(up, xr.progressU);
    xr.progressFrac = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    xr.progressOn = 1;
}

void XR_SetReticle(int style, const float center[3], const float right[3], const float up[3], const float rgba[4])
{
    if (style <= RETICLE_NONE || style >= RETICLE_COUNT) return;
    FromQ3(center, xr.reticle);
    FromQ3(right, xr.reticleR);
    FromQ3(up, xr.reticleU);
    memcpy(xr.reticleColor, rgba, sizeof(xr.reticleColor));
    xr.reticleOn = style;
}

void XR_SetLaser(const float from[3], const float to[3])
{
    static const float cyan[4] = { 0.2f, 0.9f, 1.0f, 1.0f };
    float pts[2][3];
    memcpy(pts[0], from, sizeof(pts[0]));
    memcpy(pts[1], to, sizeof(pts[1]));
    XR_SetLine((const float (*)[3])pts, 2, cyan, 0);
}

// Draws the beam into eye's laser swapchain with that eye's view and projection (fixed function,
// all state saved and restored around it).
static int DrawLaser(int eye)
{
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    const XrView *v = &xr.views[eye];
    const XrQuaternionf *q = &v->pose.orientation;
    float proj[16] = { 0 }, view[16] = { 0 }, r[9], n = 0.05f, f = 100.0f;
    float l = tanf(v->fov.angleLeft), rt = tanf(v->fov.angleRight), u = tanf(v->fov.angleUp), d = tanf(v->fov.angleDown);
    float px = v->pose.position.x, py = v->pose.position.y, pz = v->pose.position.z;
    int sc = 4 + eye, i, units = 4;
    uint32_t index;

    if (!gl.Begin || XR_FAILED(xrAcquireSwapchainImage(xr.swapchain[sc], &ai, &index))) return 0;
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(xr.swapchain[sc], &wi))) return 0;

    proj[0] = 2 / (rt - l); proj[5] = 2 / (u - d); proj[8] = (rt + l) / (rt - l); proj[9] = (u + d) / (u - d);
    proj[10] = -(f + n) / (f - n); proj[11] = -1; proj[14] = -2 * f * n / (f - n);
    // rotation matrix of the eye (row-major r), then the inverse pose as a column-major matrix
    r[0] = 1 - 2 * (q->y * q->y + q->z * q->z); r[1] = 2 * (q->x * q->y - q->z * q->w); r[2] = 2 * (q->x * q->z + q->y * q->w);
    r[3] = 2 * (q->x * q->y + q->z * q->w); r[4] = 1 - 2 * (q->x * q->x + q->z * q->z); r[5] = 2 * (q->y * q->z - q->x * q->w);
    r[6] = 2 * (q->x * q->z - q->y * q->w); r[7] = 2 * (q->y * q->z + q->x * q->w); r[8] = 1 - 2 * (q->x * q->x + q->y * q->y);
    for (i = 0; i < 3; i++) { view[i * 4 + 0] = r[i * 3 + 0]; view[i * 4 + 1] = r[i * 3 + 1]; view[i * 4 + 2] = r[i * 3 + 2]; }
    // transpose: view = [R^T | -R^T p]
    {
        float t[16] = { 0 };
        t[0] = r[0]; t[4] = r[3]; t[8] = r[6];
        t[1] = r[1]; t[5] = r[4]; t[9] = r[7];
        t[2] = r[2]; t[6] = r[5]; t[10] = r[8];
        t[12] = -(r[0] * px + r[3] * py + r[6] * pz);
        t[13] = -(r[1] * px + r[4] * py + r[7] * pz);
        t[14] = -(r[2] * px + r[5] * py + r[8] * pz);
        t[15] = 1;
        memcpy(view, t, sizeof(view));
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, xr.dstFbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xr.images[sc][index].image, 0);
    gl.PushAttrib(0x000FFFFF);   // GL_ALL_ATTRIB_BITS
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(0x0B71); gl.Disable(0x0BE2); gl.Disable(0x0BC0); gl.Disable(0x0B44);   // depth, blend, alpha test, cull
    gl.Disable(0x0B60); gl.Disable(0x0B50); gl.Disable(0x0B90);                       // fog, lighting, stencil
    if (gl.ActiveTexture) {
        for (i = units - 1; i >= 0; i--) { gl.ActiveTexture(0x84C0 + i); gl.Disable(GL_TEXTURE_2D); }
    } else {
        gl.Disable(GL_TEXTURE_2D);
    }
    gl.ColorMask(1, 1, 1, 1);
    gl.Viewport(0, 0, xr.scW[sc], xr.scH[sc]);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.MatrixMode(0x1701); gl.PushMatrix(); gl.LoadMatrixf(proj);   // GL_PROJECTION
    gl.MatrixMode(0x1700); gl.PushMatrix(); gl.LoadMatrixf(view);   // GL_MODELVIEW
    gl.LineWidth(3.0f);
    if (xr.reticleOn) {
        // the crosshair, thin and smooth like the game's own: arms around a gap, a dot, a ring
        static const float dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        const float *c = xr.reticle, *r = xr.reticleR, *u = xr.reticleU, a = xr.reticleColor[3];
        int style = xr.reticleOn, k, j;
        int cross = style == RETICLE_CROSS || style == RETICLE_CROSS_DOT;
        int dot = style == RETICLE_DOT || style == RETICLE_CIRCLE_DOT || style == RETICLE_CROSS_DOT;
        int ring = style == RETICLE_CIRCLE || style == RETICLE_CIRCLE_DOT;
        gl.Enable(0x0B20);                         // GL_LINE_SMOOTH
        gl.Enable(0x0BE2);                         // GL_BLEND, premultiplied
        if (gl.BlendFunc) gl.BlendFunc(1, 0x0303); // ONE, ONE_MINUS_SRC_ALPHA
        gl.LineWidth(1.6f);
        gl.Color4f(xr.reticleColor[0] * a, xr.reticleColor[1] * a, xr.reticleColor[2] * a, a);
        if (cross) {
            gl.Begin(0x0001);   // GL_LINES
            for (k = 0; k < 4; k++)
                for (j = 0; j < 2; j++) {
                    float t = j ? 1.0f : 0.45f;   // from the gap to the tip
                    gl.Vertex3f(c[0] + (r[0] * dirs[k][0] + u[0] * dirs[k][1]) * t,
                                c[1] + (r[1] * dirs[k][0] + u[1] * dirs[k][1]) * t,
                                c[2] + (r[2] * dirs[k][0] + u[2] * dirs[k][1]) * t);
                }
            gl.End();
        }
        if (ring) {
            gl.Begin(0x0002);   // GL_LINE_LOOP
            for (k = 0; k < 40; k++) {
                float t = k * (6.2831853f / 40), cs = cosf(t) * 0.7f, sn = sinf(t) * 0.7f;
                gl.Vertex3f(c[0] + r[0] * cs + u[0] * sn, c[1] + r[1] * cs + u[1] * sn, c[2] + r[2] * cs + u[2] * sn);
            }
            gl.End();
        }
        if (dot) {
            gl.Begin(0x0006);   // GL_TRIANGLE_FAN
            gl.Vertex3f(c[0], c[1], c[2]);
            for (k = 0; k <= 16; k++) {
                float t = k * (6.2831853f / 16), cs = cosf(t) * 0.12f, sn = sinf(t) * 0.12f;
                gl.Vertex3f(c[0] + r[0] * cs + u[0] * sn, c[1] + r[1] * cs + u[1] * sn, c[2] + r[2] * cs + u[2] * sn);
            }
            gl.End();
        }
        gl.Disable(0x0BE2);
        gl.Disable(0x0B20);
    }
    if (xr.progressOn) {
        // a dark frame, filled from the left in the game's gold as the time goes by
        const float *c = xr.progress, *r = xr.progressR, *u = xr.progressU;
        float f = xr.progressFrac * 2.0f - 1.0f;   // the fill's right end, -1..1 along r
        static const float corner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        int k;
        gl.Enable(0x0BE2);                          // GL_BLEND, premultiplied
        if (gl.BlendFunc) gl.BlendFunc(1, 0x0303);
        gl.Begin(0x0007);   // GL_QUADS: background
        gl.Color4f(0.0f, 0.0f, 0.0f, 0.55f);
        for (k = 0; k < 4; k++)
            gl.Vertex3f(c[0] + r[0] * corner[k][0] + u[0] * corner[k][1], c[1] + r[1] * corner[k][0] + u[1] * corner[k][1],
                        c[2] + r[2] * corner[k][0] + u[2] * corner[k][1]);
        gl.Color4f(0.85f, 0.65f, 0.25f, 1.0f);       // fill
        for (k = 0; k < 4; k++) {
            float x = corner[k][0] > 0 ? f : -1.0f, y = corner[k][1] * 0.7f;
            gl.Vertex3f(c[0] + r[0] * x + u[0] * y, c[1] + r[1] * x + u[1] * y, c[2] + r[2] * x + u[2] * y);
        }
        gl.End();
        gl.LineWidth(2.0f);
        gl.Begin(0x0002);   // GL_LINE_LOOP: frame
        gl.Color4f(0.9f, 0.9f, 0.9f, 0.9f);
        for (k = 0; k < 4; k++)
            gl.Vertex3f(c[0] + r[0] * corner[k][0] + u[0] * corner[k][1], c[1] + r[1] * corner[k][0] + u[1] * corner[k][1],
                        c[2] + r[2] * corner[k][0] + u[2] * corner[k][1]);
        gl.End();
        gl.Disable(0x0BE2);
    }
    gl.LineWidth(5.0f);   // the beam and the grenade line (the layer is full resolution)
    if (xr.laserOn) {
    gl.Begin(0x0003);   // GL_LINE_STRIP
    gl.Color4f(xr.laserColor[0] * xr.laserColor[3], xr.laserColor[1] * xr.laserColor[3],
               xr.laserColor[2] * xr.laserColor[3], xr.laserColor[3]);   // premultiplied
    for (i = 0; i < xr.laserCount; i++) gl.Vertex3f(xr.laser[i][0], xr.laser[i][1], xr.laser[i][2]);
    gl.End();
    }
    if (xr.laserOn) {
        const float *e = xr.laser[xr.laserCount - 1];
        if (xr.laserRing > 0) {
            // landing marker: a ring lying on the ground (XR y is up)
            gl.Begin(0x0002);   // GL_LINE_LOOP, same color
            for (i = 0; i < 24; i++) {
                float a = i * (6.2831853f / 24);
                gl.Vertex3f(e[0] + cosf(a) * xr.laserRing, e[1], e[2] + sinf(a) * xr.laserRing);
            }
            gl.End();
        } else {
            gl.PointSize(16.0f);   // full-resolution layer
            gl.Begin(0x0000);   // GL_POINTS: the spot where it hits the menu
            gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
            gl.Vertex3f(e[0], e[1], e[2]);
            gl.End();
        }
    }
    gl.MatrixMode(0x1701); gl.PopMatrix();
    gl.MatrixMode(0x1700); gl.PopMatrix();
    gl.PopAttrib();
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    xrReleaseSwapchainImage(xr.swapchain[sc], &ri);
    return 1;
}


static XrQuaternionf MatrixToQuat(const float c0[3], const float c1[3], const float c2[3])
{
    // columns c0, c1, c2 are the rotated x, y, z axes
    XrQuaternionf q;
    float tr = c0[0] + c1[1] + c2[2], s;
    if (tr > 0) {
        s = sqrtf(tr + 1.0f) * 2;
        q.w = 0.25f * s; q.x = (c1[2] - c2[1]) / s; q.y = (c2[0] - c0[2]) / s; q.z = (c0[1] - c1[0]) / s;
    } else if (c0[0] > c1[1] && c0[0] > c2[2]) {
        s = sqrtf(1.0f + c0[0] - c1[1] - c2[2]) * 2;
        q.w = (c1[2] - c2[1]) / s; q.x = 0.25f * s; q.y = (c1[0] + c0[1]) / s; q.z = (c2[0] + c0[2]) / s;
    } else if (c1[1] > c2[2]) {
        s = sqrtf(1.0f + c1[1] - c0[0] - c2[2]) * 2;
        q.w = (c2[0] - c0[2]) / s; q.x = (c1[0] + c0[1]) / s; q.y = 0.25f * s; q.z = (c2[1] + c1[2]) / s;
    } else {
        s = sqrtf(1.0f + c2[2] - c0[0] - c1[1]) * 2;
        q.w = (c0[1] - c1[0]) / s; q.x = (c2[0] + c0[2]) / s; q.y = (c2[1] + c1[2]) / s; q.z = 0.25f * s;
    }
    return q;
}

void XR_EndFrame(const GLuint eyeFbo[2], int w, int h, int hudW, int hudH, const vr_hud_t *hud, const vr_scope_t *scope)
{
    XrCompositionLayerQuad lens = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
    XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrCompositionLayerProjection beam = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView bv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
    const XrCompositionLayerBaseHeader *layers[4];
    int i, n = 0, render;

    if (!xr.frameOpen) return;
    xr.frameOpen = 0;
    render = eyeFbo && xr.viewsValid && xr.frameState.shouldRender;

    if (render && hud && hud->world && CopySwapchainImage(0, eyeFbo[0], 0, 0, w, h) && CopySwapchainImage(1, eyeFbo[1], 0, 0, w, h)) {
        for (i = 0; i < 2; i++) {
            pv[i].pose = xr.views[i].pose;   // the poses the eyes were rendered with
            pv[i].fov = xr.views[i].fov;
            pv[i].subImage.swapchain = xr.swapchain[i];
            pv[i].subImage.imageRect.extent.width = xr.scW[i];
            pv[i].subImage.imageRect.extent.height = xr.scH[i];
        }
        proj.space = xr.local;
        proj.viewCount = 2;
        proj.views = pv;
        layers[n++] = (const XrCompositionLayerBaseHeader *)&proj;
    }

    // HUD / menus on a flat screen in front of the player. The game draws it with ordinary
    // alpha blending into a transparent buffer, so its colors are already multiplied by alpha.
    // Scope lens: on top of the world, under the HUD. Its +z faces the eye (back along the gun).
    if (render && scope && scope->visible && xr.swapchain[3] &&
        CopySwapchainImage(3, scope->fbo, scope->x, scope->y, scope->size, scope->size)) {
        float right[3], back[3], c0[3], c1[3], c2[3];
        int i;
        for (i = 0; i < 3; i++) { right[i] = -scope->lens.axis[1][i]; back[i] = -scope->lens.axis[0][i]; }
        FromQ3(right, c0);
        FromQ3(scope->lens.axis[2], c1);
        FromQ3(back, c2);
        FromQ3(scope->lens.pos, &lens.pose.position.x);   // XrVector3f is three packed floats
        lens.pose.orientation = MatrixToQuat(c0, c1, c2);
        lens.space = xr.local;
        lens.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        lens.subImage.swapchain = xr.swapchain[3];
        lens.subImage.imageRect.extent.width = xr.scW[3];
        lens.subImage.imageRect.extent.height = xr.scH[3];
        lens.size.width = lens.size.height = scope->width;
        layers[n++] = (const XrCompositionLayerBaseHeader *)&lens;
    }

    if (render && hud && hud->fbo && xr.swapchain[2] && CopySwapchainImage(2, hud->fbo, 0, 0, hudW, hudH)) {
        float width = hud->world ? hud->width : hud->menuWidth;
        quad.layerFlags = hud->opaque ? 0 : XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.space = hud->headLocked ? xr.view : xr.local;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = xr.swapchain[2];
        quad.subImage.imageRect.extent.width = xr.scW[2];
        quad.subImage.imageRect.extent.height = xr.scH[2];
        if (hud->posed) {
            float right[3], back[3], c0[3], c1[3], c2[3];
            for (i = 0; i < 3; i++) { right[i] = -hud->pose.axis[1][i]; back[i] = -hud->pose.axis[0][i]; }
            FromQ3(right, c0);
            FromQ3(hud->pose.axis[2], c1);
            FromQ3(back, c2);
            FromQ3(hud->pose.pos, &quad.pose.position.x);
            quad.pose.orientation = MatrixToQuat(c0, c1, c2);
            quad.space = xr.local;
        } else {
            quad.pose.orientation.w = 1;
            quad.pose.position.y = hud->offsetY;
            quad.pose.position.z = -hud->distance;
        }
        quad.size.width = width;
        quad.size.height = width * xr.scH[2] / xr.scW[2];
        layers[n++] = (const XrCompositionLayerBaseHeader *)&quad;
    }

    // Menu pointer beam, above the menu screen
    if (render && (xr.laserOn || xr.reticleOn || xr.progressOn) && xr.swapchain[4] && DrawLaser(0) && DrawLaser(1)) {
        for (i = 0; i < 2; i++) {
            bv[i].pose = xr.views[i].pose;
            bv[i].fov = xr.views[i].fov;
            bv[i].subImage.swapchain = xr.swapchain[4 + i];
            bv[i].subImage.imageRect.extent.width = xr.scW[4 + i];
            bv[i].subImage.imageRect.extent.height = xr.scH[4 + i];
        }
        beam.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        beam.space = xr.local;
        beam.viewCount = 2;
        beam.views = bv;
        layers[n++] = (const XrCompositionLayerBaseHeader *)&beam;
    }

    ei.displayTime = xr.frameState.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = n;
    ei.layers = layers;
    {
        XrResult r = xrEndFrame(xr.session, &ei);
        if (XR_FAILED(r)) Log("OpenXR: xrEndFrame failed: %s", ResultStr(r));
    }
}

// A vibration on one controller: amplitude 0..1, for ms milliseconds (runtime's frequency).
void XR_Haptic(int hand, float amplitude, int ms)
{
    XrHapticVibration vib = { XR_TYPE_HAPTIC_VIBRATION };
    XrHapticActionInfo hi = { XR_TYPE_HAPTIC_ACTION_INFO };
    if (!xr.session || !xr.aHaptic || !xrApplyHapticFeedback || hand < 0 || hand > 1 || amplitude <= 0) return;
    vib.duration = (XrDuration)ms * 1000000;
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    vib.amplitude = amplitude > 1 ? 1 : amplitude;
    hi.action = xr.aHaptic;
    hi.subactionPath = xr.handPath[hand];
    xrApplyHapticFeedback(xr.session, &hi, (const XrHapticBaseHeader *)&vib);
}

static float GetFloat(XrAction a, int hand)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
    gi.action = a;
    gi.subactionPath = hand >= 0 ? xr.handPath[hand] : XR_NULL_PATH;
    return XR_SUCCEEDED(xrGetActionStateFloat(xr.session, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
}

static int GetBool(XrAction a, int hand)
{
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
    gi.action = a;
    gi.subactionPath = hand >= 0 ? xr.handPath[hand] : XR_NULL_PATH;
    return XR_SUCCEEDED(xrGetActionStateBoolean(xr.session, &gi, &st)) && st.isActive && st.currentState;
}

void XR_GetInput(vr_input_t *in)
{
    XrActiveActionSet active = { 0 };
    XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
    int i;

    memset(in, 0, sizeof(*in));
    if (!xr.session || !xr.running || !xr.actionSet || !xr.handSpace[1] || xr.state != XR_SESSION_STATE_FOCUSED) return;
    active.actionSet = xr.actionSet;
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (xrSyncActions(xr.session, &si) != XR_SUCCESS) return;   // XR_SESSION_NOT_FOCUSED etc.
    in->active = 1;

    for (i = 0; i < 2; i++) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;

        if (XR_SUCCEEDED(xrLocateSpace(xr.handSpace[i], xr.local, xr.frameState.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & need) == need) {
            PoseToQ3(&loc.pose, &in->hand[i]);
            in->handValid[i] = 1;
        }
        in->trigger[i] = GetFloat(xr.aTrigger, i);
        in->grip[i] = GetFloat(xr.aGrip, i);
        gi.action = xr.aStick;
        gi.subactionPath = xr.handPath[i];
        if (XR_SUCCEEDED(xrGetActionStateVector2f(xr.session, &gi, &st)) && st.isActive) {
            in->stick[i][0] = st.currentState.x;
            in->stick[i][1] = st.currentState.y;
        }
        if (GetBool(xr.aStickClick, i)) in->buttons |= i ? VRB_RSTICK : VRB_LSTICK;
    }
    if (GetBool(xr.aA, -1)) in->buttons |= VRB_A;
    if (GetBool(xr.aB, -1)) in->buttons |= VRB_B;
    if (GetBool(xr.aX, -1)) in->buttons |= VRB_X;
    if (GetBool(xr.aY, -1)) in->buttons |= VRB_Y;
    if (GetBool(xr.aMenu, -1)) in->buttons |= VRB_MENU;
}
