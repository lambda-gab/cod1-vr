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
> [Discord](https://discord.com/channels/1561484693881225219/1561485586169331882).

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

Defaults, editable in `[Buttons]` of `cod_vr.ini`:

| Control | Action |
|---|---|
| Right trigger | Fire (hold with a grenade to see its trajectory, release to throw) |
| Right grip | Use / pick up |
| Left trigger | Aim down the sights (with a scope: zoom) |
| Left grip | Hold the gun with both hands |
| Left stick | Move (relative to the head) |
| Right stick | Turn (45° snap); up/down changes stance |
| A / B | Jump / reload |
| X / Y | Next weapon / melee |
| Left stick click | Objectives |
| Right stick click | Go prone |
| Menu | Game menu |

In menus, point with the gun hand: trigger clicks, B goes back, A confirms.

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

## Releases

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
