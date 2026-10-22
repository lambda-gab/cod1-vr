@echo off
setlocal
rem Finds the newest Visual Studio with the C++ tools and sets up the x86 compiler.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio with C++ tools not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /MT /W3 /LD /I..\deps\openxr\include cod_vr.c vr_xr.c postfx.c stubs.c /Feout\opengl32.dll /link /DEF:opengl32.def user32.lib kernel32.lib advapi32.lib
copy /y ..\deps\openxr\Win32\bin\openxr_loader.dll out\ >nul
