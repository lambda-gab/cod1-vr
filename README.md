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

## Requirements

- Call of Duty (2003) for PC, single player, **version 1.5** (`CoDSP.exe`).
- A PC VR headset with controllers (tested with a Quest through Virtual Desktop; Air Link and other PC VR headsets
  should work too).
- A **32-bit** OpenXR runtime: Virtual Desktop (VDXR) or Oculus/Meta. The game is a 32-bit program.

## Installation

### Easiest: the installer (recommended)

1. Download `CoD-VR-Setup.exe` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Run it. It finds the game folder (or lets you pick it) and checks that the game is version 1.5.
3. Choose whether to hide the player's hands, so only the gun is in your hand (recommended).
4. Connect the headset and start `CoDSP.exe`.

Run the installer again to update or uninstall. Your `cod_vr.ini` settings are kept on updates.

### Manual

1. Extract `cod-vr-manual-install.zip` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest).
2. Copy `opengl32.dll`, `openxr_loader.dll` and `cod_vr.ini` to the game folder (where `CoDSP.exe` is).
3. Connect the headset and start `CoDSP.exe`.

To uninstall, delete those three files. Hiding the hands and the VR tutorial texts need the installer (or the steps in
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

Buttons (editable in `[Buttons]` of `cod_vr.ini`):

| Control | Action |
|---|---|
| Either trigger | Fire (with a grenade: hold to see its trajectory, release to throw). With a scoped rifle, the trigger of the hand not holding the gun zooms |
| Grip of an empty hand | Use / pick up (doors, weapons on the floor) |
| Other hand's grip, near the gun | Hold the gun with both hands |
| Left stick | Move (relative to the head) |
| Right stick | Turn (45° snap); up/down changes stance |
| A / B | Jump / reload |
| X / Y | Next weapon / melee |
| Left stick click | Objectives |
| Right stick click | Go prone |
| Menu | Game menu |

In menus, point with the right hand: trigger clicks, B goes back, A confirms.

## Settings

Everything is in `cod_vr.ini`, in the game folder, with a comment on each setting. If the game is under
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

## Releases

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

- Fix visual glitches (artifacts).
- **United Offensive** (`CoDUOSP.exe`) support.

## Notes

- **Tutorial, the signs at the start:** point the gun hand at each sign and move the hand up and down until the game
  marks the objective as done.

## Technical information

How the mod works, building from source, the reverse engineering notes and tools:
[docs/TECHNICAL.md](docs/TECHNICAL.md).
