# Troubleshooting

**English** | [Português](TROUBLESHOOTING.pt-BR.md)

Something does not work? Start here. If nothing below fixes it, [send a diagnostic report](#5-still-not-working-send-a-diagnostic-report):
it tells me what is wrong with your VR set-up.

## The game opens as two flat images side by side (VR does not start)

This is what the game does when OpenXR could not start. Go through these steps:

### 1. Update to the latest version

Download `CoD-VR-Setup.exe` from the [latest release](https://github.com/lambda-gab/cod1-vr/releases/latest) and run it
again. Your settings are kept.

Version 1.6.2 fixes a common cause: other programs register OpenXR "API layers" (overlays and helpers of other VR
games) that are loaded into every OpenXR program, and one with a missing or wrong file stopped OpenXR from starting
(`XR_ERROR_FILE_ACCESS_ERROR`). The mod now turns those layers off for the game and tries again.

### 2. Connect the headset before starting the game

- **Virtual Desktop:** open the streamer on the PC and connect the headset.
- **Quest Link / Air Link:** start the link first.
- **SteamVR:** start SteamVR first.

Then start the game with the **Call of Duty VR launcher** (`CoD-VR-Launcher.exe`, or the desktop shortcut).

### 3. The game is 32-bit: it needs a 32-bit OpenXR runtime

- **Virtual Desktop** and **Meta/Oculus** include one.
- **SteamVR:** its 32-bit OpenXR runtime only comes with the **SteamVR beta** (Steam > SteamVR > Properties >
  Betas). After installing it, set SteamVR as the OpenXR runtime in the SteamVR settings.

### 4. One runtime must be set as the *active* OpenXR runtime

Having a runtime installed is not enough: Windows needs one marked as the active one. If none is, the game cannot
find the headset (`XR_ERROR_RUNTIME_UNAVAILABLE`, and the diagnostic report says "no ActiveRuntime"). Set it in the
program you use for VR (the exact wording changes between versions):

- **SteamVR:** Settings (turn on advanced settings) > **Developer** > **Set SteamVR as OpenXR runtime**.
- **Meta Quest Link / Oculus app:** Settings > General > set it as the active OpenXR runtime.
- **Virtual Desktop streamer:** its settings have an option to set it as the active OpenXR runtime.

Then connect the headset and start the game again.

### 5. Still not working? Send a diagnostic report

1. Open the launcher (`CoD-VR-Launcher.exe`) and go to **ABOUT**.
2. Click **"Problem starting in VR?"**. It saves `CoD-VR-diagnostic.txt` to your desktop and shows where.
3. Send that file to me: on [Discord](https://discord.gg/SRsK2x5Tn) (in a private message) or in a
   [GitHub issue](https://github.com/lambda-gab/cod1-vr/issues). Say which headset you have and how you connect it
   (Virtual Desktop, Link, SteamVR).

The report lists the OpenXR runtimes registered in Windows for 32-bit programs, asks the game's own OpenXR loader what
it finds (runtime, headset, graphics support), lists the OpenXR API layers, the VR programs running, your graphics
card and driver, the game's files, `cod_vr.ini` and the game's last log (`cod_vr.log`). It starts with a summary of
the likely causes. It contains file paths (for example your Windows user name in `C:\Users\...`) and nothing else
personal; read it before you send it if you like, it is plain text.

## Other problems

### The picture in the headset is stretched, zoomed in or black (SteamVR and others)

Fixed in version 1.5.0: the mod now hands the eye images to the runtime at the size the headset asks for. Update to
the latest version (step 1 above).

### The other tabs of the launcher do not respond after opening the VR tab

Fixed in version 1.6.1. Update to the latest version.

### The game is slow or stutters

In the launcher, **GRAPHICS**: lower **Headset resolution** (for example to Medium, 85%) and **Antialiasing**.
Changes need a game restart. Close other programs that use the graphics card.

### "not CoDSP.exe 1.5" in `cod_vr.log`, or the mod does nothing

The mod is made for **Call of Duty 1.5** (`CoDSP.exe` from March 2004). Other versions of the game put things
elsewhere, so the mod stays off. Update the game to 1.5 first. The installer checks the version for you.

### The Steam version does not start

Start it with the launcher: for the Steam version it starts the game through Steam, which its `CoDSP.exe` needs. Steam
has to be running and logged in.

### The controllers or buttons do nothing

Check that the headset's controllers are on and tracked before the game starts. Buttons are listed in the [README](README.md#controls) and can be changed in `[Buttons]` of `cod_vr.ini`.

### Something else

Ask on [Discord](https://discord.gg/SRsK2x5Tn) or [open an issue](https://github.com/lambda-gab/cod1-vr/issues), and
attach a diagnostic report if the problem is about VR not starting.
