@echo off
rem Builds CoD-VR-Setup.exe. Run ..\proxy\build.bat first (the setup embeds its output).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio with C++ tools not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
if not exist ..\proxy\out\opengl32.dll ( echo Build the proxy first: ..\proxy\build.bat & exit /b 1 )
if not exist out mkdir out
rc /nologo /fo out\setup.res setup.rc || exit /b 1
cl /nologo /O2 /MT /W3 /DMINIZ_NO_TIME setup.c ..\third_party\miniz\miniz.c out\setup.res /Foout\ /Feout\CoD-VR-Setup.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:setup.manifest user32.lib shell32.lib ole32.lib advapi32.lib || exit /b 1
