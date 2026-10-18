@echo off
setlocal
rem Finds the newest Visual Studio with the C++ tools and sets up the x86 compiler.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo Visual Studio with C++ tools not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /W3 gltest.c user32.lib gdi32.lib >nul
