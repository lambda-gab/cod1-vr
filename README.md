![Call of Duty VR](docs/images/banner.jpg)

# CALL OF DUTY (2003) VR

**English** | [Português](README.pt-BR.md)

A PC VR mod for the **Call of Duty 1 single player campaign** (`CoDSP.exe` 1.3) and its expansion **United Offensive** (`CoDUOSP.exe` 1.51). Play the original 2003 game in your
headset: you look around and move with your own body, the gun is in your hand, you aim with the controller and you see
the HUD and menus on a floating screen.

It is made by a Call of Duty fan. CoD 1 was the first FPS I ever played and it has a special place in my heart, so I
wanted to experience it again in VR.

> **Disclaimer:** unofficial fan project, not affiliated with or endorsed by Activision. Call of Duty is a
> trademark of Activision Publishing, Inc. A legitimate copy of the game is required; no game code or game files
> are included or distributed (the optional packs are generated on your PC from your own copy).

> [!WARNING]
> **This project is in development.** Bugs and visual glitches can happen. Please report them on
> [Discord](https://discord.gg/SRsK2x5Tn).

## Support the developer

> [!TIP]
> **Would you like to help the dev? $2 keeps the Claude Pro plan running.**
>
> Every ported game is and always will be free for everyone. Your support pays for the AI plan and lets me spend a
> few hours of my day bringing joy to gamers.
>
> [**Support on Patreon**](https://www.patreon.com/cw/LambdaGab)

### Patreon supporters

<table>
  <tr>
    <td valign="middle"><img src="docs/images/supporters/master.png" width="64" alt="Master"></td>
    <td valign="middle"><b>Dmitriy Melnikov</b><br>Master</td>
  </tr>
</table>

## Requirements

- Call of Duty (2003) for PC, single player, **version 1.3** (`CoDSP.exe`), and/or Call of Duty: United Offensive,
  single player, **version 1.51** (`CoDUOSP.exe`).
- A PC VR headset with controllers (tested with a Quest through Virtual Desktop; Air Link and other PC VR headsets
  should work too).
- A **32-bit** OpenXR runtime: Virtual Desktop (VDXR), Oculus/Meta, or SteamVR (its 32-bit OpenXR runtime comes
  with the **SteamVR beta**: Steam > SteamVR > Properties > Betas). The game is a 32-bit program.

> [!NOTE]
> **Tested with the Quest 3, where it works perfectly.** If you have a problem with another headset, please
> [open an issue](https://github.com/lambda-gab/cod1-vr/issues) so I can look into it.

## Installation

> [!IMPORTANT]
> **VR does not start, or the game opens as two flat images side by side?** Read the
> **[Troubleshooting guide](TROUBLESHOOTING.md)**. It also explains how to save a **diagnostic report** (launcher >
> ABOUT > "Problem starting in VR?") to send to me.

### Easiest: the installer (recommended)

1. Download `CoD-VR-Setup.exe` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Run it. It finds the game folder (or lets you pick it) and checks that the game is version 1.3 (United Offensive: 1.51).
3. Choose whether to hide the player's hands, so only the gun is in your hand (recommended), and whether to create
   a desktop shortcut to the launcher.
4. Connect the headset and start the game with the **Call of Duty VR launcher** (the shortcut, or
   `CoD-VR-Launcher.exe` in the game folder).

Run the installer again to update or uninstall. Your settings are kept on updates.

### Manual

1. Extract `cod-vr-manual-install.zip` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Copy `opengl32.dll`, `openxr_loader.dll`, `cod_vr.ini`, `supporters.ini` and `CoD-VR-Launcher.exe` to the game folder (where
   `CoDSP.exe` or `CoDUOSP.exe` is).
3. Start `CoD-VR-Launcher.exe`, open the **VR** tab and set **Player hands** to **Hidden** (the soldier's arms are shown
   until you do; the launcher makes the file from your own game, and Windows may ask for administrator rights).
4. Connect the headset and press PLAY (or start `CoDSP.exe` directly).

To uninstall, delete those five files (and `main\pakz_vr_nohands.pk3` if you hid the hands). The VR tutorial texts
need the installer (or the steps in the [technical notes](docs/TECHNICAL.md)).

## Controls

Weapons are taken from your body, with either hand, and held while the grip is pressed:

| Put the hand here and press the grip | You take |
|---|---|
| Over the right shoulder (hand behind the head) | Weapon 1 (primary) |
| Over the left shoulder | Weapon 2 (secondary) |
| At the hip, on that hand's side | Pistol |
| Behind you, from the waist line to the lower back | Grenade |

- Let go of the grip where the weapon belongs (long guns over a shoulder, the pistol at the hip, grenades at the
  back) to put it away.
- Let go anywhere else and the gun **falls to the floor**. Grab it with an empty hand to pick it up: it goes to
  that hand.
- A gun in the left hand is mirrored.
- Crouch for real and the soldier crouches. Stand up straight and click the left stick to recenter (your height
  and the roomscale centre).
- Hold the gun with both hands for a steadier aim and less recoil.
- The controllers vibrate when you reach a holster, take, put away or drop a weapon, fire and get hit.

Buttons (editable in `[Buttons]` of `cod_vr.ini`):

| Control | Action |
|---|---|
| Either trigger | Fire (with a grenade: hold to see its trajectory, release to throw). With a scoped rifle, the trigger of the hand not holding the gun zooms |
| Grip of an empty hand | Use / pick up (doors, weapons on the floor) |
| Trigger of the free hand, with the hand on a bomb or switch | Use (plant explosives) while the other hand holds a gun |
| Free hand's grip on the gun's handguard | Hold the gun with both hands |
| Left stick | Move (relative to the head). With Blink or Shift (beta): push forward to aim a jump, let go to go |
| Right stick | Turn (45° snap); up/down changes stance |
| A / B (your gun hand's buttons) | Jump / reload (left-handed: X / Y) |
| X, Y (the other hand's buttons; left-handed: A, B) | Wildcards: a tap runs the button's own action (none by default), holding one and pressing another button runs a shortcut (weapon switch on Y + A). Editable in the launcher |
| Back of the hand without the gun, turned to your eyes | Objectives, like looking at a watch. Melee: swing the gun |
| Left stick click | Recenter (stand up straight) |
| Right stick click | Go prone |
| Menu | Game menu |

In menus, point with the right hand: trigger clicks, B goes back, A confirms.

## Settings

### The launcher

The launcher has the settings most players want, saved as soon as you change them:

- **GRAPHICS:** headset resolution (from *Potato* 50% to *GPU Furnace* 200%, with the size per eye), antialiasing
  (MSAA), texture filtering (anisotropic), texture quality, brightness, and post-processing test options (ambient
  occlusion, sharpening, colour style, bloom: leave them off if the image looks worse).
- **VR:** movement (continuous, or Blink / Shift jumps, beta), comfort vignette, snap or smooth turning, roomscale, physical crouch, menus fixed or
  following the head, what of the HUD stays on screen, grenade trajectory, crosshair style, melee by swinging the
  gun, dropping the weapon on release, two-hand steadiness and recoil, objectives on the hand, and vibration
  strength for shots, hits and holsters.
- **MODS:** things the original game does not have: custom difficulty (*Real life*: one shot kills, for you and
  for them; *Perma death*: dying ends the run; both; *God mode*), infinite ammo, and blood (off, on, or *Gore*).
- **CONTROLS:** the controller layout.

With the Steam version of the game, PLAY starts it through Steam (its `CoDSP.exe` only runs when Steam starts it).

Its choices go to `%APPDATA%\CoD-VR\settings.ini` and take precedence over `cod_vr.ini`.

### cod_vr.ini

Everything else is in `cod_vr.ini`, in the game folder, with a comment on each setting. If the game is under
`Program Files`, open the editor (Notepad) as administrator to save it. The most used ones:

- `[Controllers]` `WeaponOffset*`, `WeaponPitch`, `WeaponYaw`: gun position and angle in the hand.
- `[Controllers]` `TwoHandGripHeight`, `AimSmoothing`: two-handed grip and aim smoothing.
- `[Controllers]` `MuzzleOffsetRight/Up` and `[MuzzleFlash]`: muzzle flash position, also per weapon
  (`thompson=forward right up`). These are read again while you play: save and fire again.
- `[VR]` `RoomScale`, `PositionalTracking`, `WorldScale`.
- `[HUD]` and `[Scope]`: size and distance of the floating screens, scope smoothing.
- `[Grenade]`: trajectory on/off and its opacity.
- `[Controllers]` `HipFireAccuracy`: shots as accurate as aimed ones (on by default, since in VR you aim with the
  gun's own sights); `RightHanded`: which stick turns.
- `[Holsters]`: weapon over each shoulder (left-handed players may want them swapped), holster heights, dropping
  the gun when you let go, draw time.
- `[VR]` `RenderScale`: eye resolution, times the headset's recommended size.
- `[Comfort]`: the vignette (on/off, strength, when it starts closing), `VehicleTurnSmooth`, `ShakeKeep`.
- `[HUD]` `MenuFollowsHead`, `ObjectivesOnHand`, `ObjectivesWidth`, `ObjectivesBackground`: menus and the
  objectives screen.
- `[VR]` `MSAA`, `Anisotropy`, `CullMargin`, `HeightOffset`; `[PostFX]`: image quality and effects.
- `[Controllers]` `HapticsShots`, `HapticsDamage`, `HapticsHolsters`, `PhysicalCrouch`, `CrouchDepth`,
  `TwoHandStabilization`, `TwoHandRecoilReduction`.
- `[Controllers]` `Crosshair`, `CrosshairSize`, `SwingMelee`, `MeleeSpeed`; `[HUD]` `HudMode`.
- `[Mods]` `Difficulty`, `InfiniteAmmo`, `Gore`.

## Releases

### v1.7.4

- **Weapons on your body (holsters):** the pistol at your chest is no longer swapped for a rifle, and a hand at your neck
  is no longer taken for a shoulder. The chest zone is shallower, so arms held out in front of you while walking no longer
  make the controllers vibrate, and the waist holsters need the hand closer to your waist.
- **A softer holster signal:** reaching a holster now gives two quick taps instead of a second of vibration.
- **Taking a weapon from the floor:** a weapon lying near your hand is taken before any holster (crouching for it used to
  draw a weapon from your body). The reach is longer, and the controller has to point at the weapon unless it is right at
  your hand.
- **Only the gun hand's trigger fires:** the other hand's trigger fires only while that hand holds the handguard.
- **Immersive HUD:** the ammo count no longer shows.
- **Objectives on a button:** hold the button to see them (a quick tap only made the screen flash). The objectives on the
  back of your hand are a little bigger.

### v1.7.3

- **No more white flash and white haze after explosions:** the game's shellshock effect (an explosion close to you)
  blends a copy of the screen over the picture; in VR that copy was not the headset's image and it covered the eyes in
  white for a few seconds. The blend is now left out in VR (the shellshock's sound and slowdown stay).
  `[Comfort] ShellshockBlend=1` brings it back.
- **Trees and props no longer go missing in busy scenes (United Offensive forests, big explosions):** the game's list
  for static models, sized for a flat screen of 2003, filled up with the wide VR view and some models were left out.
  It is now 8 times bigger (Call of Duty and United Offensive).

### v1.7.2

- **Using things with the free hand is now its trigger:** with a gun in one hand, put the other hand on the bomb (a switch, a
  door) and press its trigger; no need to look at it or to see the use hint. Away from such things that trigger still
  aims and fires with the rifle. The free hand's grip no longer plants: it only holds the gun with both hands.
- **Always armed (gunstock) mode:** CONTROLS > Weapon handling. The weapon stays in your dominant hand, it is never put away
  or dropped, and there are no holsters. The free hand's X (A if left-handed) switches weapon by default, through
  everything you carry. For gunstock controllers. "Free hands" is still the default.
- **Binoculars (United Offensive)** are a holster item, at the chest by default; any zone of the body can hold them.
- **Menu action** for any button (CONTROLS), for controllers without a menu button. The same button closes the menu.
- **Tap action of X and Y** (A and B if left-handed) in CONTROLS: they can now be given an action that runs on a tap,
  without pressing another button with them.
- **Tanks:** the gun follows the controller of your dominant hand (Call of Duty and United Offensive); United Offensive's
  Kursk tank is driven like Call of Duty's, with the head free of the tank's movement.
- **Fire and smoke no longer vanish** when your gun points somewhere else (a burning tank while aiming at the floor):
  the game dropped the effects outside the view of the aim. `[Graphics] FxAlways`.
- **Scripted scenes and getting into a car or truck keep your view direction** instead of jumping (the ammunition scene and
  the truck in Trenches). `[Comfort] KeepLook`.
- **Scoped rifles and binoculars** zoom at once, with the scope picture full size from the first frame (`[Scope] InstantAds`).
- **Trucks:** the first seconds are calmer (less camera shake, no vignette flicker), and the right stick turns you in your
  seat in cars and trucks.
- **United Offensive jeep gun:** follows the controller without lagging behind quick turns.
- **Chest holster** starts closer to the body, so a weapon pressed to the chest counts. **Immersive HUD** hides the whole
  health bar.
- Pimax: unchanged since 1.7.0. Diagnostics added for the white flash after damage (full-screen pictures in `cod_vr.log`).

### v1.7.1

- **Patreon Supporters panel:** wider, so the whole name fits, and now beside the game's main menu (on its left,
  outside the menu picture) instead of over it, so the Load and Save lists and the other menus never cover it or are
  covered by it. Same in Call of Duty and United Offensive.

### v1.7.0

- **Call of Duty: United Offensive** (`CoDUOSP.exe` 1.51) works, single player campaign. The installer and the launcher
  accept it (the launcher's home page has a card for each game you have), and everything below is for both games.
  United Offensive extras: the jeep's machine gun is aimed with the controller; the bipod machine guns (.30 cal, MG34,
  DP28) are set up with the trigger of your free hand (press again to take it down); the Foy ladder, the Noville tank
  start and the bomber's turrets work; Perma death erases United Offensive's own saves.
- **The bomber's gun turrets** (dorsal and tail): you stay at the gun's seat while it turns, the controller you point
  aims it (the head looks around freely), the seat is the same wherever the controller points when you get on, the
  recenter button brings you back to it, a grip press gets you off, and the plain reticle is hidden on turrets.
- **Movement** (VR tab): *Continuous* walks with the stick, as before (the default). **Blink (beta)** and **Shift
  (beta)** move by jumps: push the move stick forward to aim an arc with that stick's hand (your gun hand stays free),
  let go to go there. The destination has to be floor where you fit. Blink fades the screen out and in; Shift glides.
- **Buttons.** The two buttons of the hand that does not hold the gun (X and Y, or A and B if you are left-handed) are
  wildcards: a tap runs the button's own action, holding one and pressing another button runs a shortcut (weapon
  switch on Y + A by default). The two buttons of your gun hand act at once: A = Jump, B = Reload (left-handed: X =
  Jump, Y = Reload), so you can reload while the thumb stays on the move stick.
- **CONTROLS page:** your dominant hand is the first choice and the defaults follow it (choose Left and "Default" is the
  left-handed layout, so the separate Leftie profile is gone). The page has no more long texts, and the launcher is a
  little taller and its home page centred.
- **Plant explosives and use things with your free hand while a gun is in the other:** reach the free hand into the
  bomb (or switch, door) and hold its grip. Before, the weapon had to be put away.
- **Objectives** appear on the back of the hand that does not hold the gun, like a watch: turn it to your eyes. The
  screen is bigger and its background more opaque (the subtitles' box too).
- **Patreon Supporters:** the people who support the project on Patreon are listed on a panel beside the game's main
  menu in the headset (the list is `supporters.ini` in the game folder; the launcher draws the panel).
- **Holsters:** reaching a holster you can use gives one vibration of a second, not a continuous one.
- **Scopes:** a round mask closes the view while you zoom.
- **VR tab:** new *Vehicle turns* and *Camera shake* options (how the view follows a vehicle's bends, how much it shakes
  in explosions).
- **Fixes:** the "lean out of the window" hint only on the car ride; the trees' flicker in United Offensive (the
  static model cache); Pimax: VR starts when the runtime registered its 64-bit file for 32-bit programs.
### v1.6.9

- **The boat missions** ("ship", the crossing to Stalingrad, and the start of "Stalingrad", which begins in a boat): no
  vignette, no "hold both triggers to skip the mission" and none of the ride messages. The missions need you to
  crouch, so the free stick's up and down are the stance buttons again there. The free head and the view following the
  boat stay as they were. The cars and trucks (carride, truckride, ...) keep all of it.
- **The game version is 1.3, not 1.5:** 1.3 is the last Call of Duty single player patch (1.5 is the multiplayer one).
  The README, the troubleshooting guide, the technical notes, the installer's messages and the log said 1.5. The mod
  itself is unchanged: it still recognises the same `CoDSP.exe`.

### v1.6.8

- **Hide the player's arms from the launcher** (VR tab > **Player hands**: Shown / Hidden). With the manual install
  there is no installer to make the "no hands" file, so the arms stayed visible; the launcher now makes it (or
  removes it) from your own game files. Windows may ask for administrator rights. The manual installation steps in
  this page say to do it.
- The last row of the VR tab's left column (Subtitle size) was drawn under the hint line at the bottom: the rows are
  closer together now.

### v1.6.7

- **Crash in the mission "rocket"** when fighting the enemies at the start: the game's drawing of static models
  (props, trees, rubble) fills a fixed-size list and wrote past its end when a lot of them were in view. The mod now
  leaves out the models that do not fit for that frame instead of letting the game crash (a log line,
  `static models: the index cache is full`, tells when it happens).

### v1.6.6

- **The gun swayed in your hand like an elastic while you walked or strafed** (v1.6.4 and v1.6.5): the camera shake
  reduction moved the point the gun is placed from. The gun now stays steady in the hand.
- **No weapon name or picture when you stood near a weapon** with the HUD on "Immersive", "Subtitles only" or
  "Compass only" (v1.6.5): the game's use hints are shown in every HUD mode again.
- **Menus and loading screens no longer follow your head** for their first seconds (it made some players dizzy):
  they appear in front of you and stay there; they move again only if your head ends up well above or below them
  (you sat down or stood up), or when you recenter.
- **The view no longer goes into walls and door frames** when you lean or walk into them with roomscale: it stops a
  little before the wall. `[VR]` `HeadCollision=0` turns it off. Doors are not detected yet.
- **Mounted guns (the MG42, the PTRS41 anti-tank rifles) no longer push you** when you aim down: the gun is not solid for
  you while you are on it. `[Turret]` `NoCollision`, and `OffsetX/Y/Z` (cm) to move where you stand.
- **Dropped weapon pick-up:** with only a rifle and a grenade, the dropped rifle can be picked up again.
- **Subtitle size** in the launcher's VR tab (Small, Medium, Large, Extra Large; `[HUD]` `SubtitleSize`).
- **OpenXR runtime:** a 64-bit runtime named in `XR_RUNTIME_JSON` (SteamVR) is ignored for this 32-bit game, and the
  start-up log has more steps to find crashes in a headset's runtime.
- The diagnostic report (ABOUT) now also shows whether `main\pakz_vr_nohands.pk3` (the hidden hands) is installed.

### v1.6.5

- **The image shook when you turned your head** (v1.6.4): the camera shake reduction also smoothed the game's view
  turn, which holds your own head turns, so the picture lagged behind and wobbled by a few degrees while you
  turned. It now only tones down the shake of the camera's position (earthquakes, bombing runs) and leaves turning
  alone.

### v1.6.4

- **Vibration when firing mounted guns and the Panzerfaust:** the MG42, the Flak 88, the Flak Vierling and the tank
  now shake both controllers on every shot (a cannon harder and longer); the Panzerfaust gives one hard push to
  both hands. The grenade line no longer shows while you are on a gun.
- **Truckride, the Panzerfaust Waters hands over:** inside a vehicle, the grip or the trigger of an empty hand now
  takes the weapon held out to you.
- **Gentler turns in vehicles:** the view follows a truck or car round a bend more slowly (and no faster than 50
  degrees a second), so a sharp turn no longer swings the whole view round at once. `[Comfort]` `VehicleTurnSmooth`
  (seconds; 0.15 was the old behaviour, 0.5 is the new default).
- **Briefings and slides** (the pictures between missions, such as the Russian campaign's): they are shown on a screen
  in front of you, like the menus, instead of one copy in each eye (which looked doubled and out of line), and the
  trigger skips them ("fire to skip").
- **Stalingrad, the queue for ammunition:** the scripted walk along the line no longer turns you round (it was taken
  for a vehicle seat), and the head is free during it.
- **Less camera shake** in earthquakes and explosions (the bombing at the end of Stalingrad): the shake is cut to 40%.
  `[Comfort]` `ShakeKeep` (1 = as the game shakes, 0 = none).

### v1.6.3

- **VR did not start when Windows has no active OpenXR runtime** (SteamVR, Meta or other VR software installed, but none
  chosen as the active one): the game could not find the headset. The mod now picks the runtime of the VR software
  that is running (SteamVR, then Virtual Desktop, then Meta/Oculus), or the only one installed.
- **Diagnostic report:** says clearly when no runtime is set as the active one and how to choose one.

### v1.6.2

- **VR did not start for some players** (the game opened as two flat images side by side): other programs register
  OpenXR "API layers" (overlays and helpers of other VR games) that are loaded into every OpenXR program, and one
  with a missing or wrong file stopped OpenXR from starting (`XR_ERROR_FILE_ACCESS_ERROR`). The mod now turns those
  layers off for the game and tries again.
- **Diagnostic report** (launcher > ABOUT): lists the API layers and tries the same fix, no longer wipes the game's
  last log, lists the VR programs running correctly, and says when SteamVR has no 32-bit OpenXR runtime (it comes
  with the SteamVR beta) or is not the one the game uses.

### v1.6.1

- **Launcher:** fixed the other tabs not responding after opening the VR tab.
- **Sticks** now says what it does: "Left: move, Right: turn" or "Left: turn, Right: move", whichever hand holds
  the gun (it replaces "Swapped"). The default is the left stick moving.

### v1.6.0

- **Vehicles** (car, truck, Flak 88, tank, the Dawnville car): your head is free inside them, the view follows the
  vehicle without its jolts, and the shots go where you aim, so you can hit enemies from a car or truck. A separate
  vignette strength for vehicles (launcher), and a hint to skip the mission if you feel sick (hold both triggers for
  4 seconds). The Flak 88 is aimed with the head, and held or let go with the grips.
- **Tank:** the left stick drives it, gently (heavy, slow to start and stop); the gun follows where your head looks.
- **MG42 turrets:** both grips to mount (the hands then hold it and aim it), let go of a grip to get off.
- **Ladders:** push the stick into the ladder and point your gun hand up to climb. Training signs count when you
  just look at them.
- **Modern font** (GRAPHICS tab): the game's text in Segoe UI, made by the installer from the one in Windows.
- **Controllers:** "Sticks: Swapped" in the launcher (move with the gun hand's stick, for left-handers who keep
  moving on the left), and the game's hints name your controller buttons.
- **No view shake** from explosions and shell shock (made by the installer), and the Panzerfaust always shows a
  reticle; the gun no longer goes through the floor when you go prone.
- **Launcher:** it tells you when a new release is out, and ABOUT has **"Problem starting in VR?"**: it saves a
  diagnostic report (the OpenXR runtimes registered for 32-bit programs, what the game's loader reports, your
  headset software, the game's settings and log) to the Desktop, to send on Discord.

### v1.5.0

- **SteamVR and other runtimes:** the view in the headset was stretched and zoomed in (fine on the monitor), and
  black at low resolutions, with SteamVR's 32-bit runtime and others. The eyes are now handed over at the size the
  headset asks for, whatever resolution you choose.
- **Steam version:** PLAY starts the game through Steam, which its `CoDSP.exe` needs.
- **Melee by swinging the gun:** hit someone with any part of it, stock to muzzle. Only a real swing counts (its
  speed is set in the launcher), not resting the gun on them.
- **HUD modes:** full, immersive (none), subtitles only, compass only, or health and ammo only. Holding the
  Objectives button shows all of it.
- **Crosshair** (off by default), in six styles shown in the launcher: cross, dot, circle, circle + dot,
  cross + dot.
- **Walking and running** follow the stick's tilt, and are no longer slower when the gun points away from where
  you walk.
- **MODS tab:** custom difficulty (Real life, Perma death, both, God mode), infinite ammo, and blood (off, on, or
  Gore: more blood that sprays and falls, made by the installer from your game's own effect).
- The lines over the world (menu pointer, grenade trajectory, crosshair) are sharper.

### v1.4.0

- **Vibration:** when a hand reaches a holster, when you take, put away or drop a weapon, on every shot (scoped
  rifles too) and when you get hit. Each kind has its own strength in the launcher.
- **Physical crouch:** crouch for real and the soldier crouches, with the view at your real height.
- **Recenter** with the left stick click: your height and the roomscale centre.
- **Two-hand steadiness:** holding the gun with both hands damps hand tremor and reduces recoil.
- **Image quality:** antialiasing (MSAA), anisotropic texture filtering, 32-bit textures, and post-processing test
  options (ambient occlusion, sharpening, colour styles, bloom).
- The objectives on the hand have a dark rounded background, easier to read.
- The launcher stays open while you play, to try settings; the VR page is in two columns.
- **Fix:** soldiers drawn with parts of other models (gear, heads) in the left eye.
- **Fix:** textures flashing at the edges of the view on quick head turns.

### v1.3.0

- **Launcher** with PLAY, CONTROLS, GRAPHICS, VR and ABOUT; the installer can create a desktop shortcut to it.
- **Sharper image:** each eye is rendered at the headset's own resolution (it was the game window's, stretched),
  adjustable from 50% to 200%.
- **Comfort vignette** while moving with the stick or turning smoothly (Off / Low / Medium / High).
- Menus and loading screens stay fixed in front of you, so you can look around them.
- X opens the objectives, shown above your left hand.
- Training: with empty hands, a line shows where the right hand points, until the signs are done.
- The controls are now always the body-holster ones (the Arcade mode was removed).

Thanks to [lasmith1689-sys/cod1-vr](https://github.com/lasmith1689-sys/cod1-vr) for the vignette and headset
resolution ideas.

### v1.2.0

- Weapons are taken from the body: the shoulders (weapon 1 on the right, weapon 2 on the left), the hip (pistol) and
  the back (grenades), with either hand. Letting go away from the holster drops the gun on the floor, and you can
  pick it up again.
- Quick draw: no raise animation, and the previous weapon no longer shows during a switch.
- A gun in the left hand is mirrored.
- Better aim: shots go where the sights point, without the game's hip fire spread (`HipFireAccuracy`).
- Fix: the world no longer turns when the aim passes between the head and the hand.

### v1.1.1

- Fix: the muzzle flash adjustments for the Thompson, M1A1 Carbine and Springfield are now included in the
  default settings (v1.1.0 shipped without them). Updating keeps your `cod_vr.ini`: copy the `[MuzzleFlash]`
  lines from `cod_vr.ini.default` into it.

### v1.1.0

- Grenade trajectory line: shows where the grenade flies and lands, stopping at walls and going through windows.
- On-screen hints show the controller buttons, and the tutorial texts that mention the mouse were rewritten for VR.
- The Menu button opens the game menu.
- Steadier scope aim (its own smoothing while zoomed in, no flicker).
- Muzzle flash on the gun barrel (Thompson, M1A1 Carbine, Springfield), adjustable per weapon.

### v1.0.0

- First release: the game is playable in VR (stereo with head tracking and roomscale, gun in the hand, controller
  aiming, HUD and menus on a floating screen, scope view, installer).

## Technical information

How the mod works, building from source, the reverse engineering notes and tools:
[docs/TECHNICAL.md](docs/TECHNICAL.md).
