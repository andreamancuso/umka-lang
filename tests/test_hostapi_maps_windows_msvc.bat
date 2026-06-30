@echo off
setlocal

set ROOT=%~dp0..
set OUT=%ROOT%\umka_windows_msvc

if not exist "%OUT%\libumka.lib" (
    call "%ROOT%\build_windows_msvc.bat"
    if errorlevel 1 exit /b %errorlevel%
)

cl /nologo /W4 /WX /wd4201 /I"%ROOT%\src" /Fo"%OUT%\hostapi_maps.obj" /Fe"%OUT%\hostapi_maps.exe" "%~dp0hostapi_maps.c" "%OUT%\libumka.lib"
if errorlevel 1 exit /b %errorlevel%

set PATH=%OUT%;%PATH%
"%OUT%\hostapi_maps.exe"
