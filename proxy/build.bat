@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /MT /W3 /LD /I..\deps\openxr\include cod_vr.c vr_xr.c postfx.c stubs.c /Feout\opengl32.dll /link /DEF:opengl32.def user32.lib kernel32.lib
copy /y ..\deps\openxr\Win32\bin\openxr_loader.dll out\ >nul
