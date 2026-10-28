// CoDSP.exe (Call of Duty 1.3 SP, 2004-03-02, sha256 c755c7da411c...5c69f47)
// Image base 0x400000, no ASLR. Recovered with REA 6.5.0 + Ghidra 12.1.4.
// Every address below was found via a string anchor or table; see CoD_VR_RE.md.
#pragma once

// The addresses differ between CoDSP.exe 1.3 and CoDUOSP.exe (United Offensive): the values of every
// name below are in addr_values.h, one set per game, picked in DllMain (SelectGame).
typedef struct {
#define X(n, sp, uo) DWORD n;
#include "addr_values.h"
#undef X
} addr_t;
extern addr_t g_A;
extern int g_uo;   // 1 = CoDUOSP.exe

// ---- Renderer export table (GetRefAPI @ 0x4a94c0, REF_API_VERSION 0xe) ----
#define ADDR_GetRefAPI              g_A.ADDR_GetRefAPI
#define ADDR_refexport_renderer     g_A.ADDR_refexport_renderer  // table built by the renderer
#define ADDR_re_client              g_A.ADDR_re_client  // client's copy (cl `re`), 0xec bytes; this is what the game calls
#define RE_OFS_RegisterShader       g_A.RE_OFS_RegisterShader    // qhandle_t RE_RegisterShader(const char *name, int type); the client registers "white" with type 2 (SP 0x40e28e, UO 0x40f443)
#define RE_OFS_ClearScene           g_A.RE_OFS_ClearScene        // cgame trap 0x3d
#define RE_OFS_AddRefEntityToScene  g_A.RE_OFS_AddRefEntityToScene        // cgame trap 0x3e
#define RE_OFS_RenderScene          g_A.RE_OFS_RenderScene        // cgame trap 0x47 -> RE_RenderScene
#define RE_OFS_SetColor             g_A.RE_OFS_SetColor        // RE_SetColor(const float *rgba), RC_SET_COLOR
#define RE_OFS_DrawStretchPic       g_A.RE_OFS_DrawStretchPic        // RE_StretchPic(x, y, w, h, s1, t1, s2, t2, hShader), pixels
#define RE_OFS_BeginFrame           g_A.RE_OFS_BeginFrame        // RE_BeginFrame(stereoFrame_t)
#define RE_OFS_EndFrame             g_A.RE_OFS_EndFrame        // RE_EndFrame(int*, int*)

#define ADDR_RE_AddRefEntityToScene g_A.ADDR_RE_AddRefEntityToScene  // void (refEntity_t*, int); trap 0x3e passes 0
#define ADDR_RE_SetColor            g_A.ADDR_RE_SetColor
#define ADDR_RE_DrawStretchPic      g_A.ADDR_RE_DrawStretchPic
#define ADDR_RE_RenderScene         g_A.ADDR_RE_RenderScene  // void (refdef_t*) - copies refdef into tr.refdef, calls R_RenderView
#define ADDR_R_RenderView           g_A.ADDR_R_RenderView  // void (viewParms_t*)
#define ADDR_RE_BeginFrame          g_A.ADDR_RE_BeginFrame  // stereo checks ("Stereo is enabled, but stereoFrame was %i")
#define ADDR_RE_EndFrame            g_A.ADDR_RE_EndFrame
#define ADDR_RB_SwapBuffers         g_A.ADDR_RB_SwapBuffers
#define ADDR_R_SetupProjection      g_A.ADDR_R_SetupProjection  // builds tr.viewParms.projectionMatrix from tr.refdef.fov_x/fov_y
#define ADDR_CALL_SetupProjection   g_A.ADDR_CALL_SetupProjection  // the only call site (E8 rel32), inside R_GenerateDrawSurfs-like 0x4cb210
#define ADDR_R_SetupFrustum         g_A.ADDR_R_SetupFrustum  // symmetric culling planes from viewParms.fovX/fovY
#define ADDR_tr_viewParms           g_A.ADDR_tr_viewParms
#define ADDR_projectionMatrix       g_A.ADDR_projectionMatrix  // float[16], column-major, infinite far plane
#define ADDR_projectionMatrixGun    g_A.ADDR_projectionMatrixGun  // copy used for the view weapon (r_znear_depthhack), same layout
#define ADDR_viewParms_lodBias      g_A.ADDR_viewParms_lodBias  // viewParms + 0x140: r_lodBias
#define ADDR_viewParms_lodScale     g_A.ADDR_viewParms_lodScale  // viewParms + 0x144: tan(fov_x/2) / tan(40) * r_lodScale (model LOD)
#define ADDR_RB_SetGL2D             g_A.ADDR_RB_SetGL2D  // backend: viewport/scissor/glOrtho for 2D; 9 callers (stretch pics, raw, swap)
#define ADDR_RB_BeginDrawingView    g_A.ADDR_RB_BeginDrawingView  // backend: start of a 3D view (clears depth, sets projection2D = 0)
#define ADDR_RB_SurfaceStaticModelCached g_A.ADDR_RB_SurfaceStaticModelCached  // static models (props, trees): appends indices to a 0x20000-short cache, no room check
#define ADDR_staticCacheCount       g_A.ADDR_staticCacheCount  // that cache's index counter, right after the cache (UO port.py: same 0x40000 gap)
#define ADDR_staticModelDet         g_A.ADDR_staticModelDet  // the pointer its first instruction reads (mov eax, [it]; mov ecx, [eax + 0x20]): the detour's prologue
#define ADDR_backEnd_projection2D   g_A.ADDR_backEnd_projection2D
#define ADDR_backEnd_refdef_rdflags g_A.ADDR_backEnd_refdef_rdflags  // byte; bit0 = RDF_NOWORLDMODEL
#define ADDR_backEnd_refdef         g_A.ADDR_backEnd_refdef  // trRefdef_t: x, y, width, height first (rdflags at +0x4c)
// RE_RenderScene's "first item of this scene" counters: a scene uses the entities, lights and
// polys added since the previous RenderScene. Restoring them lets a second view reuse them.
#define ADDR_r_firstSceneEntity     g_A.ADDR_r_firstSceneEntity
#define ADDR_r_firstSceneDlight     g_A.ADDR_r_firstSceneDlight
#define ADDR_r_firstScenePoly       g_A.ADDR_r_firstScenePoly
// Animated model (xmodel) surfaces of a scene: 16-byte items at backEndData + 0x1dd700 (8192 of them,
// R_AddXModelSurfaces checks the room at 0x506a31), added from index ADDR_xsurfCount (0x506070).
// RE_RenderScene starts it at 0 for every scene (0x4df0cf); the draw surfaces of a scene point at
// these items, so a second scene in the same frame rewrote the first one's before it was drawn.
#define ADDR_xsurfCount             g_A.ADDR_xsurfCount
#define ADDR_RenderScene_xsurfReset g_A.ADDR_RenderScene_xsurfReset  // mov dword ptr [0x1536498], 0 (10 bytes)
#define ADDR_r_firstSceneList6      g_A.ADDR_r_firstSceneList6  // a sixth scene list (0x30-byte items at backEndData + 0x81100,
                                                // count at 0x154d710, probably coronas): RE_ClearScene 0x4de950
#define ADDR_r_numEntities          g_A.ADDR_r_numEntities  // added this frame (limit 2045)
#define ADDR_r_numDlights           g_A.ADDR_r_numDlights
#define ADDR_r_numPolys             g_A.ADDR_r_numPolys
// 0x0154d724 is where the next scene's draw surfaces start. It must NOT be restored: the views
// queued so far still own the surfaces before it until the backend runs.
#define ADDR_GLimp_EndFrame        g_A.ADDR_GLimp_EndFrame  // calls gdi32!SwapBuffers(hDC @ 0x01830cc4)
#define ADDR_GLimp_PixelFormat      g_A.ADDR_GLimp_PixelFormat  // adds PFD_STEREO when stereo requested
#define ADDR_g_hDC                  g_A.ADDR_g_hDC
#define ADDR_IAT_SwapBuffers        g_A.ADDR_IAT_SwapBuffers  // CoDSP.exe import slot for gdi32!SwapBuffers

// PE header TimeDateStamp of the CoDSP.exe these addresses belong to
#define CODSP_TIMESTAMP             g_A.CODSP_TIMESTAMP

// ---- Stereo flags ----
#define ADDR_r_glConfig_stereo      g_A.ADDR_r_glConfig_stereo  // renderer glConfig.stereoEnabled
#define ADDR_r_glConfig_vidWidth    g_A.ADDR_r_glConfig_vidWidth
#define ADDR_r_glConfig_vidHeight   g_A.ADDR_r_glConfig_vidHeight
#define ADDR_cls_glconfig_stereo    g_A.ADDR_cls_glconfig_stereo  // client copy; SCR_UpdateScreen draws twice when != 0

// ---- Client frame ----
#define ADDR_SCR_UpdateScreen       g_A.ADDR_SCR_UpdateScreen  // calls SCR_DrawScreenField with ESI = 1 (left), 2 (right) or 0 (mono)
#define ADDR_SCR_DrawScreenField    g_A.ADDR_SCR_DrawScreenField  // push esi; call re.BeginFrame; ... (non-standard: arg in ESI)
#define ADDR_CL_CgameSystemCalls    g_A.ADDR_CL_CgameSystemCalls  // switch on args[0]; "Bad cgame system trap"
#define ADDR_CL_InitRef             g_A.ADDR_CL_InitRef
#define ADDR_CL_Init                g_A.ADDR_CL_Init

// ---- Input / view angles ----
#define ADDR_CL_AdjustAngles        g_A.ADDR_CL_AdjustAngles  // keyboard turn (cl_yawspeed / cl_pitchspeed)
#define ADDR_CL_MouseMove           g_A.ADDR_CL_MouseMove  // mouse turn (sensitivity, m_pitch/m_yaw)
#define ADDR_cl_viewangles          g_A.ADDR_cl_viewangles  // float[3]: PITCH, YAW, ROLL
#define ADDR_CL_JoystickMove        g_A.ADDR_CL_JoystickMove  // usercmd_t* in ECX; adds cl.joystickAxis to the move
#define ADDR_cl_joystickAxis        g_A.ADDR_cl_joystickAxis  // int[6]: SIDE (turns unless +strafe), FORWARD, UP, ...
#define USERCMD_OFS_buttons         g_A.USERCMD_OFS_buttons
#define USERCMD_OFS_forwardmove     g_A.USERCMD_OFS_forwardmove        // signed char
#define USERCMD_OFS_rightmove       g_A.USERCMD_OFS_rightmove        // signed char
#define ADDR_Cbuf_AddText           g_A.ADDR_Cbuf_AddText  // text in EAX (register convention)
#define ADDR_CM_BoxTrace            g_A.ADDR_CM_BoxTrace  // trace_t* in EAX, brushmask in EDX, capsule flag in ECX;
                                                // stack: start, end, mins, maxs, model (0 = world).
                                                // trace_t starts with fraction (set to 1 first). Reached from
                                                // cgame traps 0x27 / 0x29 (CL_CgameSystemCalls 0x402164 / 0x4021be).
#define CONTENTS_SOLID              1
#define ADDR_cm_numSubModels        g_A.ADDR_cm_numSubModels  // models of the loaded map; 0 = no map: a trace would raise
                                                // "CM_ClipHandleToModel: bad handle" (0x412b1e)
#define ADDR_cls_keyCatchers        g_A.ADDR_cls_keyCatchers  // bit 1 = UI (menus), bit 3 = cgame
// engine event queue read by Com_EventLoop (0x42f1d0): 256 x { time, type, value, value2, ptrLength, ptr }
#define ADDR_eventQue               g_A.ADDR_eventQue
#define ADDR_eventHead              g_A.ADDR_eventHead
#define ADDR_eventTail              g_A.ADDR_eventTail
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
#define ADDR_currentVM              g_A.ADDR_currentVM  // vm_t*
#define ADDR_VM_DllSyscall          g_A.ADDR_VM_DllSyscall  // int (int cmd, ...): currentVM->systemCall(&cmd)
#define G_TRAP_GetConfigstring      0x2e        // (index, char *buf, int size); 0x2d sets one
#define G_OFS_syscall               g_A.G_OFS_syscall  // module offset: the game's engine call pointer (set by dllEntry)
#define G_TRAP_KeyBinding           0x98        // (const char *cmd, char **key1, char **key2) -> keys: the
                                                // script's getKeyBinding (0x30f10), for its "Press &&1" hints
#define G_TRAP_SendServerCommand    0x2c        // (clientNum or -1, const char *cmd): "gmb \"text\"" = iprintlnbold (0x305e0)
#define CS_SERVERINFO               0           // \\key\\value... with mapname
#define CS_OBJECTIVES               15          // 16 objectives: \\state\\N... (objective_state 0x200341e0)
#define OBJST_DONE                  3           // empty, active, invisible, done, current, failed
#define ADDR_vmTable                g_A.ADDR_vmTable  // vm_t vms[3], 0x4c bytes each: systemCall at +0, name at +4
#define VM_SIZE                     g_A.VM_SIZE
#define VM_COUNT                    g_A.VM_COUNT
#define VM_OFS_name                 g_A.VM_OFS_name

#define ADDR_det_bdv                g_A.ADDR_det_bdv   // RB_BeginDrawingView: its first bytes (push ecx; mov eax, [here])
#define ADDR_det_joy                g_A.ADDR_det_joy   // CL_JoystickMove: its first bytes (mov eax, [here])

#define GENT_OFS_item               g_A.GENT_OFS_item      // gitem_t* of an item entity (byte at +0 is 2 for items)
#define GENT_OFS_useIdx             g_A.GENT_OFS_useIdx    // index into the use-function table (items: 7)
#define G_OFS_useTable              g_A.G_OFS_useTable     // module offset: void (*)(gentity_t *item, gentity_t *user, int) [], called when "use" takes an item
#define GITEM_OFS_giType            0x20                   // gitem_t: 1 = weapon

// refEntity_t (0x9c bytes): same field offsets as Quake 3 up to oldorigin
#define REFENT_SIZE                 g_A.REFENT_SIZE
#define REFENT_OFS_renderfx         g_A.REFENT_OFS_renderfx
#define REFENT_OFS_lightingOrigin   g_A.REFENT_OFS_lightingOrigin
#define REFENT_OFS_axis             g_A.REFENT_OFS_axis
#define REFENT_OFS_origin           g_A.REFENT_OFS_origin
#define REFENT_OFS_oldorigin        g_A.REFENT_OFS_oldorigin
#define RF_DEPTHHACK                0x08        // view weapon / hands / muzzle flash
#define REFENT_TYPE_MODEL           1           // reType of xmodels (the view weapon); flashes are 4 and 13
#define RF_LIGHTING_ORIGIN          0x80

// ---- cgamex86.dll (base 0x30000000) ----
#define CGAME_TIMESTAMP             g_A.CGAME_TIMESTAMP  // PE TimeDateStamp of the cgamex86.dll analysed
#define CG_OFS_currentWeaponDef     g_A.CG_OFS_currentWeaponDef  // weaponDef_t* set only while the crosshair is drawn (unused)
#define CG_OFS_weaponDefs           g_A.CG_OFS_weaponDefs  // module offset: weaponDef_t **weaponDefs (index = weapon number)
#define CG_OFS_numWeaponDefs        g_A.CG_OFS_numWeaponDefs  // module offset: highest valid weapon number
#define CG_OFS_KeyBindingText       g_A.CG_OFS_KeyBindingText  // module offset: int (const char *cmd /* stack */), key names out
                                                // through char** in EAX and ECX; returns how many keys (0..2).
                                                // Used by vmMain(14) (script getKeyBinding) and the cgame's own hints.
#define WEAPDEF_OFS_internalName    g_A.WEAPDEF_OFS_internalName        // char*, weapon file name (field table at 0x30058260 starts at +8)
#define WEAPDEF_OFS_displayName     g_A.WEAPDEF_OFS_displayName        // char*
#define WEAPDEF_OFS_weaponType      g_A.WEAPDEF_OFS_weaponType        // 0 bullet, 1 grenade, 2 projectile
#define WEAPDEF_OFS_fuseTime        g_A.WEAPDEF_OFS_fuseTime       // int, msec
#define WEAPDEF_OFS_projectileSpeed g_A.WEAPDEF_OFS_projectileSpeed       // int, units/s along the view
#define WEAPDEF_OFS_projectileSpeedUp g_A.WEAPDEF_OFS_projectileSpeedUp     // int, units/s added upwards
#define WEAPTYPE_GRENADE            1
#define WEAPDEF_OFS_hipSpreadStandMin g_A.WEAPDEF_OFS_hipSpreadStandMin     // float; then DuckedMin, ProneMin, Max (same layout in game and cgame)
#define WEAPDEF_OFS_adsSpread       g_A.WEAPDEF_OFS_adsSpread       // float
#define WEAPDEF_OFS_dropTime        g_A.WEAPDEF_OFS_dropTime       // int msec; then raiseTime, altDropTime, altRaiseTime
// recoil ranges (float degrees: pitch min, pitch max, yaw min, yaw max) of the gun model and the view
#define WEAPDEF_OFS_adsGunKick      g_A.WEAPDEF_OFS_adsGunKick
#define WEAPDEF_OFS_adsViewKick     g_A.WEAPDEF_OFS_adsViewKick
#define WEAPDEF_OFS_hipGunKick      g_A.WEAPDEF_OFS_hipGunKick
#define WEAPDEF_OFS_hipViewKick     g_A.WEAPDEF_OFS_hipViewKick
#define WEAPDEF_OFS_adsOverlayShader g_A.WEAPDEF_OFS_adsOverlayShader      // char*: set for scoped weapons
#define WEAPDEF_OFS_adsTransInTime  g_A.WEAPDEF_OFS_adsTransInTime  // int ms: the time to aim down the sights (adsTransOutTime follows)
#define WEAPDEF_OFS_adsZoomFov      g_A.WEAPDEF_OFS_adsZoomFov      // float: the fov when fully zoomed in (scoped rifles 10)
#define WEAPDEF_OFS_adsOverlayHeight g_A.WEAPDEF_OFS_adsOverlayHeight  // float: the scope overlay's height in 640x480 units (400: its circle)
#define WEAPDEF_OFS_lmgDeployAnim   g_A.WEAPDEF_OFS_lmgDeployAnim      // char*, UO only (0 in SP): set for the LMGs set up with aim down the sights (.30 cal, MG34, DP28)
#define WEAPDEF_OFS_ammoIndex       g_A.WEAPDEF_OFS_ammoIndex       // int: index into ps.ammo (the reserve)
#define WEAPDEF_OFS_clipIndex       g_A.WEAPDEF_OFS_clipIndex       // int: index into ps.ammoclip (Drop / pickup of weapons, 0x2001bca0)
#define WEAPDEF_OFS_maxAmmo         g_A.WEAPDEF_OFS_maxAmmo       // int (game field table "maxAmmo")
#define WEAPDEF_OFS_damage          g_A.WEAPDEF_OFS_damage       // int (game field table "damage"; "meleeDamage" at 0x1c4)
#define WEAPDEF_OFS_meleeDamage     g_A.WEAPDEF_OFS_meleeDamage
#define WEAPDEF_OFS_clipSize        g_A.WEAPDEF_OFS_clipSize       // int (game field table "clipSize")
#define ADDR_cl_cgameUserCmdValue   g_A.ADDR_cl_cgameUserCmdValue  // selected weapon index (cgame trap 0x57), copied to usercmd + 6
#define CG_ADDR_vmMain              g_A.CG_ADDR_vmMain

// ---- gamex86.dll (base 0x20000000): the single player game, runs in the same process ----
#define GAME_TIMESTAMP              g_A.GAME_TIMESTAMP  // PE TimeDateStamp of the gamex86.dll analysed
#define G_OFS_entities              g_A.G_OFS_entities  // module offset: gentity_t g_entities[] (0 = the player)
#define G_OFS_itemlist              g_A.G_OFS_itemlist  // module offset: gitem_t bg_itemlist[], 0x30 bytes, weapons by number
#define G_ITEM_SIZE                 g_A.G_ITEM_SIZE        // +8 world model name (char*), +0x20 giType
#define G_OFS_weaponDefs            g_A.G_OFS_weaponDefs  // module offset: weaponDef_t **weaponDefs (index = weapon number)
#define G_OFS_numWeaponDefs         g_A.G_OFS_numWeaponDefs  // module offset: highest valid weapon number
#define GCL_OFS_ammo                g_A.GCL_OFS_ammo        // int[]: reserve ammunition, by weaponDef ammoIndex
#define GCL_OFS_ammoclip            g_A.GCL_OFS_ammoclip       // int[]: rounds in the clip, by weaponDef clipIndex
#define GENT_OFS_client             g_A.GENT_OFS_client       // gclient_t*
#define GENT_OFS_posBase            g_A.GENT_OFS_posBase        // s.pos.trBase (s.pos at +0x14: trType, trTime, trDuration, base, delta)
#define GENT_OFS_posDelta           g_A.GENT_OFS_posDelta        // s.pos.trDelta
#define GENT_OFS_currentOrigin      g_A.GENT_OFS_currentOrigin        // r.currentOrigin
#define GENT_OFS_health             g_A.GENT_OFS_health       // int (spawn field table "health"; "maxhealth" at 0x1dc)
#define GENT_OFS_inuse              g_A.GENT_OFS_inuse       // char r.inuse (the AI debug walk 0x1e940 tests it)
#define GENT_OFS_flags              g_A.GENT_OFS_flags       // int: bit 1 = god mode (the "god" command, 0x17550)
#define GENT_OFS_tagInfo            g_A.GENT_OFS_tagInfo       // tagInfo_t*: set while linked (G_EntLinkTo 0x44b90); +0 = parent gentity
#define GENT_OFS_currentAngles      g_A.GENT_OFS_currentAngles        // r.currentAngles (pitch, yaw, roll)
#define GENT_OFS_ownerNum           g_A.GENT_OFS_ownerNum        // int r.ownerNum: who uses a turret (getturretowner 0x334c0)
#define GENT_OFS_turretInfo         g_A.GENT_OFS_turretInfo       // turret data, set on turrets (MG42, 30 cal, ...)
#define GENT_OFS_vehicle            g_A.GENT_OFS_vehicle       // vehicle data, set on vehicles (fireturret 0x40750)
#define GENT_OFS_weapon             g_A.GENT_OFS_weapon         // byte s.weapon: a vehicle's gun (the Flak 88's), 0 = none
#define GENT_OFS_turretInUse        g_A.GENT_OFS_turretInUse       // byte: someone is on the turret
#define GENT_OFS_contents           g_A.GENT_OFS_contents          // int r.contents (turrets: 0x200004 from their spawn code)
#define G_OFS_TurretStartUse        g_A.G_OFS_TurretStartUse  // void (gentity_t *turret, gentity_t *player), cdecl: puts the
                                                // player on it (the use check before it, 0x23180, also wants him
                                                // in the arc behind it and on the ground)
#define GENT_OFS_eType              g_A.GENT_OFS_eType         // int s.eType
#define G_ENTITY_SIZE               g_A.G_ENTITY_SIZE       // sizeof(gentity_t)
#define G_OFS_numEntities           g_A.G_OFS_numEntities  // module offset: int level.num_entities
#define GCL_OFS_weapon              g_A.GCL_OFS_weapon        // ps.weapon: the weapon in use
#define GCL_OFS_origin              g_A.GCL_OFS_origin        // float[3] ps.origin (velocity at 0x20, as in Quake 3)
#define GCL_OFS_viewangles          g_A.GCL_OFS_viewangles        // float[3] ps.viewangles: pitch, yaw, roll (what the shots follow)
#define GCL_OFS_pmFlags             g_A.GCL_OFS_pmFlags        // int ps.pm_flags: 0x10 = on a ladder (PM_CheckLadder 0x7b30)
#define PMF_LADDER                  0x10
#define TRACE_OFS_surfaceFlags      g_A.TRACE_OFS_surfaceFlags        // int in trace_t (after fraction +0 and the normal +0x10)
#define SURF_LADDER                 0x8
#define GCL_OFS_strafeSpeedScale    g_A.GCL_OFS_strafeSpeedScale       // float ps: sideways speed (0.8, set at spawn 0x14ad3; PM_CmdScale 0x3760)
#define GCL_OFS_backSpeedScale      g_A.GCL_OFS_backSpeedScale       // float ps: backwards speed (0.7)
#define GCL_OFS_slots               g_A.GCL_OFS_slots       // BYTE[6]: weapon in slot 1 primary, 2 primaryb, 3 pistol,
                                                // 4 grenade, 5 smokegrenade (0 = empty)
#define G_OFS_DropWeapon            g_A.G_OFS_DropWeapon  // gentity_t *(int weapon, const char *tag): player gentity in EAX,
                                                // cdecl. Takes the weapon and its ammo from the player and
                                                // launches it as an item (Drop_Item 0x1bb30 -> LaunchItem 0x1b980);
                                                // the pickup swap (0x1ac50) calls it with tag 0.
#define CG_ADDR_dllEntry            g_A.CG_ADDR_dllEntry

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
