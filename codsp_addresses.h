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
// Animated model (xmodel) surfaces of a scene: 16-byte items at backEndData + 0x1dd700 (8192 of them,
// R_AddXModelSurfaces checks the room at 0x506a31), added from index ADDR_xsurfCount (0x506070).
// RE_RenderScene starts it at 0 for every scene (0x4df0cf); the draw surfaces of a scene point at
// these items, so a second scene in the same frame rewrote the first one's before it was drawn.
#define ADDR_xsurfCount             0x01536498
#define ADDR_RenderScene_xsurfReset 0x004df0cf  // mov dword ptr [0x1536498], 0 (10 bytes)
#define ADDR_r_firstSceneList6      0x0154d728  // a sixth scene list (0x30-byte items at backEndData + 0x81100,
                                                // count at 0x154d710, probably coronas): RE_ClearScene 0x4de950
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
#define ADDR_CM_BoxTrace            0x0041d380  // trace_t* in EAX, brushmask in EDX, capsule flag in ECX;
                                                // stack: start, end, mins, maxs, model (0 = world).
                                                // trace_t starts with fraction (set to 1 first). Reached from
                                                // cgame traps 0x27 / 0x29 (CL_CgameSystemCalls 0x402164 / 0x4021be).
#define CONTENTS_SOLID              1
#define ADDR_cm_numSubModels        0x014bd6e0  // models of the loaded map; 0 = no map: a trace would raise
                                                // "CM_ClipHandleToModel: bad handle" (0x412b1e)
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
// Module VMs (native DLLs here). A DLL's traps go through VM_DllSyscall (0x44f040), which calls
// currentVM->systemCall: currentVM is only set while the engine is inside a VM_Call into that
// module, so calling game code from outside needs it set to the game's vm_t meanwhile.
#define ADDR_currentVM              0x013f9db4  // vm_t*
#define ADDR_VM_DllSyscall          0x0044f040  // int (int cmd, ...): currentVM->systemCall(&cmd)
#define G_TRAP_GetConfigstring      0x2e        // (index, char *buf, int size); 0x2d sets one
#define CS_SERVERINFO               0           // \\key\\value... with mapname
#define CS_OBJECTIVES               15          // 16 objectives: \\state\\N... (objective_state 0x200341e0)
#define OBJST_DONE                  3           // empty, active, invisible, done, current, failed
#define ADDR_vmTable                0x0152e960  // vm_t vms[3], 0x4c bytes each: systemCall at +0, name at +4
#define VM_SIZE                     0x4c
#define VM_COUNT                    3
#define VM_OFS_name                 4

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
#define CGAME_TIMESTAMP             0x40443fb6  // PE TimeDateStamp of the cgamex86.dll analysed
#define CG_OFS_currentWeaponDef     0x000d0584  // weaponDef_t* set only while the crosshair is drawn (unused)
#define CG_OFS_weaponDefs           0x000ceb8c  // module offset: weaponDef_t **weaponDefs (index = weapon number)
#define CG_OFS_numWeaponDefs        0x000ceb88  // module offset: highest valid weapon number
#define CG_OFS_KeyBindingText       0x00039810  // module offset: int (const char *cmd /* stack */), key names out
                                                // through char** in EAX and ECX; returns how many keys (0..2).
                                                // Used by vmMain(14) (script getKeyBinding) and the cgame's own hints.
#define WEAPDEF_OFS_internalName    0x00        // char*, weapon file name (field table at 0x30058260 starts at +8)
#define WEAPDEF_OFS_displayName     0x08        // char*
#define WEAPDEF_OFS_weaponType      0x70        // 0 bullet, 1 grenade, 2 projectile
#define WEAPDEF_OFS_fuseTime        0x210       // int, msec
#define WEAPDEF_OFS_projectileSpeed 0x314       // int, units/s along the view
#define WEAPDEF_OFS_projectileSpeedUp 0x318     // int, units/s added upwards
#define WEAPTYPE_GRENADE            1
#define WEAPDEF_OFS_hipSpreadStandMin 0x23c     // float; then DuckedMin, ProneMin, Max (same layout in game and cgame)
#define WEAPDEF_OFS_adsSpread       0x38c       // float
#define WEAPDEF_OFS_dropTime        0x200       // int msec; then raiseTime, altDropTime, altRaiseTime
// recoil ranges (float degrees: pitch min, pitch max, yaw min, yaw max) of the gun model and the view
#define WEAPDEF_OFS_adsGunKick      0x350
#define WEAPDEF_OFS_adsViewKick     0x370
#define WEAPDEF_OFS_hipGunKick      0x390
#define WEAPDEF_OFS_hipViewKick     0x3b0
#define WEAPDEF_OFS_adsOverlayShader 0x224      // char*: set for scoped weapons
#define WEAPDEF_OFS_ammoIndex       0x1a0       // int: index into ps.ammo (the reserve)
#define WEAPDEF_OFS_clipIndex       0x1a8       // int: index into ps.ammoclip (Drop / pickup of weapons, 0x2001bca0)
#define ADDR_cl_cgameUserCmdValue   0x0142f2d8  // selected weapon index (cgame trap 0x57), copied to usercmd + 6
#define CG_ADDR_vmMain              0x30018cd0

// ---- gamex86.dll (base 0x20000000): the single player game, runs in the same process ----
#define GAME_TIMESTAMP              0x40443fb0  // PE TimeDateStamp of the gamex86.dll analysed
#define G_OFS_entities              0x0012fa80  // module offset: gentity_t g_entities[] (0 = the player)
#define G_OFS_itemlist              0x000a7148  // module offset: gitem_t bg_itemlist[], 0x30 bytes, weapons by number
#define G_ITEM_SIZE                 0x30        // +8 world model name (char*), +0x20 giType
#define G_OFS_weaponDefs            0x00125650  // module offset: weaponDef_t **weaponDefs (index = weapon number)
#define G_OFS_numWeaponDefs         0x0012564c  // module offset: highest valid weapon number
#define GCL_OFS_ammo                0xf4        // int[]: reserve ammunition, by weaponDef ammoIndex
#define GCL_OFS_ammoclip            0x1f4       // int[]: rounds in the clip, by weaponDef clipIndex
#define GENT_OFS_client             0x104       // gclient_t*
#define GENT_OFS_posBase            0x20        // s.pos.trBase (s.pos at +0x14: trType, trTime, trDuration, base, delta)
#define GENT_OFS_posDelta           0x2c        // s.pos.trDelta
#define GENT_OFS_currentOrigin      0xe0        // r.currentOrigin
#define GENT_OFS_health             0x1d8       // int (spawn field table "health"; "maxhealth" at 0x1dc)
#define GENT_OFS_health             0x1d8       // int (spawn field table "health"; "maxhealth" at 0x1dc)
#define GCL_OFS_weapon              0xa0        // ps.weapon: the weapon in use
#define GCL_OFS_slots               0x2fc       // BYTE[6]: weapon in slot 1 primary, 2 primaryb, 3 pistol,
                                                // 4 grenade, 5 smokegrenade (0 = empty)
#define G_OFS_DropWeapon            0x0001bca0  // gentity_t *(int weapon, const char *tag): player gentity in EAX,
                                                // cdecl. Takes the weapon and its ammo from the player and
                                                // launches it as an item (Drop_Item 0x1bb30 -> LaunchItem 0x1b980);
                                                // the pickup swap (0x1ac50) calls it with tag 0.
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
