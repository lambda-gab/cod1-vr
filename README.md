![Call of Duty VR](docs/images/banner.jpg)

# CALL OF DUTY (2003) VR

**English** | [Português](README.pt-BR.md)

A PC VR mod for the **Call of Duty 1 single player campaign** (`CoDSP.exe` 1.5). Play the original 2003 game in your
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

## Requirements

- Call of Duty (2003) for PC, single player, **version 1.5** (`CoDSP.exe`).
- A PC VR headset with controllers (tested with a Quest through Virtual Desktop; Air Link and other PC VR headsets
  should work too).
- A **32-bit** OpenXR runtime: Virtual Desktop (VDXR) or Oculus/Meta. The game is a 32-bit program.

## Installation

### Easiest: the installer (recommended)

1. Download `CoD-VR-Setup.exe` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Run it. It finds the game folder (or lets you pick it) and checks that the game is version 1.5.
3. Choose whether to hide the player's hands, so only the gun is in your hand (recommended), and whether to create
   a desktop shortcut to the launcher.
4. Connect the headset and start the game with the **Call of Duty VR launcher** (the shortcut, or
   `CoD-VR-Launcher.exe` in the game folder).

Run the installer again to update or uninstall. Your settings are kept on updates.

### Manual

1. Extract `cod-vr-manual-install.zip` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Copy `opengl32.dll`, `openxr_loader.dll`, `cod_vr.ini` and `CoD-VR-Launcher.exe` to the game folder (where
   `CoDSP.exe` is).
3. Connect the headset and start `CoD-VR-Launcher.exe` (or `CoDSP.exe` directly).

To uninstall, delete those four files. Hiding the hands and the VR tutorial texts need the installer (or the steps in
the [technical notes](docs/TECHNICAL.md)).

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
| Other hand's grip, near the gun | Hold the gun with both hands |
| Left stick | Move (relative to the head) |
| Right stick | Turn (45° snap); up/down changes stance |
| A / B | Jump / reload |
| X | Objectives (shown above your left hand) |
| Y | Melee |
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
- **VR:** comfort vignette, snap or smooth turning, dominant hand, roomscale, physical crouch, menus fixed or
  following the head, grenade trajectory, dropping the weapon on release, two-hand steadiness and recoil, objectives
  on the hand, and vibration strength for shots, hits and holsters.
- **CONTROLS:** the controller layout.

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
- `[Comfort]`: the vignette (on/off, strength, when it starts closing).
- `[HUD]` `MenuFollowsHead`, `ObjectivesOnHand`, `ObjectivesWidth`, `ObjectivesBackground`: menus and the
  objectives screen.
- `[VR]` `MSAA`, `Anisotropy`, `CullMargin`, `HeightOffset`; `[PostFX]`: image quality and effects.
- `[Controllers]` `HapticsShots`, `HapticsDamage`, `HapticsHolsters`, `PhysicalCrouch`, `CrouchDepth`,
  `TwoHandStabilization`, `TwoHandRecoilReduction`.

## Releases

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

## Next steps

- Melee by swinging the gun, and reloading by hand (magazines and clips).
- Fix the visual glitches that are still reported.
- **United Offensive** (`CoDUOSP.exe`) support.

## Notes

- **Tutorial, the signs at the start:** point your right hand (a line shows where) at each sign and move the hand
  up and down until the game marks the objective as done.

## Technical information

How the mod works, building from source, the reverse engineering notes and tools:
[docs/TECHNICAL.md](docs/TECHNICAL.md).
