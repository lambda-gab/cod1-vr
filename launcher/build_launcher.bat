@echo off
setlocal
rem Builds CoD-VR-Launcher.exe (goes in the game folder, next to CoDSP.exe).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio with C++ tools not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
rc /nologo /fo out\launcher.res launcher.rc || exit /b 1
cl /nologo /O2 /MT /W3 /DUNICODE /D_UNICODE /I..\deps\openxr\include launcher.c diag.c out\launcher.res /Foout\ /Feout\CoD-VR-Launcher.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:launcher.manifest user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib msimg32.lib advapi32.lib winhttp.lib || exit /b 1
