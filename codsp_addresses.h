// CoDSP.exe (Call of Duty 1.5 SP, 2004-03-02, sha256 c755c7da411c...5c69f47)
// Image base 0x400000, no ASLR. Recovered with REA 6.5.0 + Ghidra 12.1.4.
// Every address below was found via a string anchor or table; see CoD_VR_RE.md.
#pragma once

// ---- Renderer export table (GetRefAPI @ 0x4a94c0, REF_API_VERSION 0xe) ----
#define ADDR_GetRefAPI              0x004a94c0
#define ADDR_refexport_renderer     0x00c950b8  // table built by the renderer
#define ADDR_re_client              0x01426d60  // client's copy (cl `re`), 0xec bytes; this is what the game calls
#define RE_OFS_ClearScene           0x3c        // cgame trap 0x3d
#define RE_OFS_AddRefEntityToScene  0x40        // cgame trap 0x3e
#define RE_OFS_RenderScene          0x64        // cgame trap 0x47 -> RE_RenderScene
#define RE_OFS_SetColor             0x6c        // RE_SetColor(const float *rgba), RC_SET_COLOR
#define RE_OFS_DrawStretchPic       0x70        // RE_StretchPic(x, y, w, h, s1, t1, s2, t2, hShader), pixels
#define RE_OFS_BeginFrame           0x88        // RE_BeginFrame(stereoFrame_t)
#define RE_OFS_EndFrame             0x8c        // RE_EndFrame(int*, int*)

#define ADDR_RE_AddRefEntityToScene 0x004dec10  // void (refEntity_t*, int); trap 0x3e passes 0
#define ADDR_RE_SetColor            0x004d42a0
#define ADDR_RE_DrawStretchPic      0x004d4330
#define ADDR_RE_RenderScene         0x004deec0  // void (refdef_t*) - copies refdef into tr.refdef, calls R_RenderView
#define ADDR_R_RenderView           0x004cb270  // void (viewParms_t*)
#define ADDR_RE_BeginFrame          0x004d4680  // stereo checks ("Stereo is enabled, but stereoFrame was %i")
#define ADDR_RE_EndFrame            0x004d4a60
#define ADDR_RB_SwapBuffers         0x004ce730
#define ADDR_R_SetupProjection      0x004c9c00  // builds tr.viewParms.projectionMatrix from tr.refdef.fov_x/fov_y
#define ADDR_CALL_SetupProjection   0x004cb216  // the only call site (E8 rel32), inside R_GenerateDrawSurfs-like 0x4cb210
#define ADDR_R_SetupFrustum         0x004c9d30  // symmetric culling planes from viewParms.fovX/fovY
#define ADDR_tr_viewParms           0x01536030
#define ADDR_projectionMatrix       0x01536178  // float[16], column-major, infinite far plane
#define ADDR_projectionMatrixGun    0x015361b8  // copy used for the view weapon (r_znear_depthhack), same layout
#define ADDR_viewParms_lodBias      0x01536170  // viewParms + 0x140: r_lodBias
#define ADDR_viewParms_lodScale     0x01536174  // viewParms + 0x144: tan(fov_x/2) / tan(40) * r_lodScale (model LOD)
#define ADDR_RB_SetGL2D             0x004ccc10  // backend: viewport/scissor/glOrtho for 2D; 9 callers (stretch pics, raw, swap)
#define ADDR_RB_BeginDrawingView    0x004cc050  // backend: start of a 3D view (clears depth, sets projection2D = 0)
#define ADDR_backEnd_projection2D   0x01549b50
#define ADDR_backEnd_refdef_rdflags 0x015496ec  // byte; bit0 = RDF_NOWORLDMODEL
#define ADDR_backEnd_refdef         0x015496a0  // trRefdef_t: x, y, width, height first (rdflags at +0x4c)
// RE_RenderScene's "first item of this scene" counters: a scene uses the entities, lights and
// polys added since the previous RenderScene. Restoring them lets a second view reuse them.
#define ADDR_r_firstSceneEntity     0x0154d72c
#define ADDR_r_firstSceneDlight     0x0154d718
#define ADDR_r_firstScenePoly       0x0154d720
#define ADDR_r_numEntities          0x0154d70c  // added this frame (limit 2045)
#define ADDR_r_numDlights           0x0154d714
#define ADDR_r_numPolys             0x0154d730
// 0x0154d724 is where the next scene's draw surfaces start. It must NOT be restored: the views
// queued so far still own the surfaces before it until the backend runs.
#define ADDR_GLimp_EndFrame        0x004ffb40  // calls gdi32!SwapBuffers(hDC @ 0x01830cc4)
#define ADDR_GLimp_PixelFormat      0x004fd300  // adds PFD_STEREO when stereo requested
#define ADDR_g_hDC                  0x01830cc4
#define ADDR_IAT_SwapBuffers        0x0053302c  // CoDSP.exe import slot for gdi32!SwapBuffers

// PE header TimeDateStamp of the CoDSP.exe these addresses belong to
#define CODSP_TIMESTAMP             0x40443fda

// ---- Stereo flags ----
#define ADDR_r_glConfig_stereo      0x015347d8  // renderer glConfig.stereoEnabled
#define ADDR_r_glConfig_vidWidth    0x015347c4
#define ADDR_r_glConfig_vidHeight   0x015347c8
#define ADDR_cls_glconfig_stereo    0x01439440  // client copy; SCR_UpdateScreen draws twice when != 0

// ---- Client frame ----
#define ADDR_SCR_UpdateScreen       0x00410590  // calls SCR_DrawScreenField with ESI = 1 (left), 2 (right) or 0 (mono)
#define ADDR_SCR_DrawScreenField    0x00410410  // push esi; call re.BeginFrame; ... (non-standard: arg in ESI)
#define ADDR_CL_CgameSystemCalls    0x00401a50  // switch on args[0]; "Bad cgame system trap"
#define ADDR_CL_InitRef             0x0040e460
#define ADDR_CL_Init                0x0040edc0

// ---- Input / view angles ----
#define ADDR_CL_AdjustAngles        0x0040a5b0  // keyboard turn (cl_yawspeed / cl_pitchspeed)
#define ADDR_CL_MouseMove           0x0040aa10  // mouse turn (sensitivity, m_pitch/m_yaw)
#define ADDR_cl_viewangles          0x0142f304  // float[3]: PITCH, YAW, ROLL
#define ADDR_CL_JoystickMove        0x0040a900  // usercmd_t* in ECX; adds cl.joystickAxis to the move
#define ADDR_cl_joystickAxis        0x0142f2c0  // int[6]: SIDE (turns unless +strafe), FORWARD, UP, ...
#define USERCMD_OFS_buttons         0x04
#define USERCMD_OFS_forwardmove     0x14        // signed char
#define USERCMD_OFS_rightmove       0x15        // signed char
#define ADDR_Cbuf_AddText           0x004209d0  // text in EAX (register convention)
#define ADDR_cls_keyCatchers        0x01439264  // bit 1 = UI (menus), bit 3 = cgame
// engine event queue read by Com_EventLoop (0x42f1d0): 256 x { time, type, value, value2, ptrLength, ptr }
#define ADDR_eventQue               0x0089e478
#define ADDR_eventHead              0x013f9e14
#define ADDR_eventTail              0x013f9e18
#define SE_KEY                      1           // value = key, value2 = down
#define SE_MOUSE                    3           // value = dx, value2 = dy (to the UI while it catches keys)
#define K_ENTER                     13
#define K_ESCAPE                    27
#define K_UPARROW                   154
#define K_DOWNARROW                 155
#define K_MOUSE1                    200
#define ADDR_cls_keyCatchers        0x01439264  // bit 1 = UI (menus), bit 3 = cgame
// engine event queue read by Com_EventLoop (0x42f1d0): 256 x { time, type, value, value2, ptrLength, ptr }
#define ADDR_eventQue               0x0089e478
#define ADDR_eventHead              0x013f9e14
#define ADDR_eventTail              0x013f9e18
#define SE_KEY                      1           // value = key, value2 = down
#define SE_MOUSE                    3           // value = dx, value2 = dy (to the UI while it catches keys)
#define K_ENTER                     13
#define K_ESCAPE                    27
#define K_UPARROW                   154
#define K_DOWNARROW                 155
#define K_MOUSE1                    200
#define ADDR_cls_keyCatchers        0x01439264  // bit 1 = UI (menus), bit 3 = cgame
// engine event queue read by Com_EventLoop (0x42f1d0): 256 x { time, type, value, value2, ptrLength, ptr }
#define ADDR_eventQue               0x0089e478
#define ADDR_eventHead              0x013f9e14
#define ADDR_eventTail              0x013f9e18
#define SE_KEY                      1           // value = key, value2 = down
#define SE_MOUSE                    3           // value = dx, value2 = dy (to the UI while it catches keys)
#define K_ENTER                     13
#define K_ESCAPE                    27
#define K_UPARROW                   154
#define K_DOWNARROW                 155
#define K_MOUSE1                    200

// refEntity_t (0x9c bytes): same field offsets as Quake 3 up to oldorigin
#define REFENT_SIZE                 0x9c
#define REFENT_OFS_renderfx         0x04
#define REFENT_OFS_lightingOrigin   0x0c
#define REFENT_OFS_axis             0x1c
#define REFENT_OFS_origin           0x44
#define REFENT_OFS_oldorigin        0x54
#define RF_DEPTHHACK                0x08        // view weapon / hands / muzzle flash
#define REFENT_TYPE_MODEL           1           // reType of xmodels (the view weapon); flashes are 4 and 13
#define RF_LIGHTING_ORIGIN          0x80

// ---- cgamex86.dll (base 0x30000000) ----
#define CG_ADDR_vmMain              0x30018cd0
#define CG_ADDR_dllEntry            0x30022540

// refdef_t as read by RE_RenderScene (same layout as Q3/RTCW):
typedef struct {
    int   x, y, width, height;   // [0..3]
    float fov_x, fov_y;          // [4..5]
    float vieworg[3];            // [6..8]
    float viewaxis[3][3];        // [9..17]
    int   time;                  // [18]
    int   rdflags;               // [19]  bit0 = RDF_NOWORLDMODEL (2D/UI scenes)
    // ... areamask etc. follows, not needed for VR
} cod_refdef_t;
