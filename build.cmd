@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

git pull --ff-only || exit /b 1

set "GENERATOR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_VERSION=%%V"
    if defined VS_VERSION (
        for /f "tokens=1 delims=." %%M in ("!VS_VERSION!") do set "VS_MAJOR=%%M"
        if "!VS_MAJOR!"=="18" set "GENERATOR=Visual Studio 18 2026"
        if "!VS_MAJOR!"=="17" set "GENERATOR=Visual Studio 17 2022"
        if "!VS_MAJOR!"=="16" set "GENERATOR=Visual Studio 16 2019"
    )
)

if not defined GENERATOR (
    echo No se encontro una instalacion de Visual Studio/Build Tools con C++.
    exit /b 1
)

echo Usando: %GENERATOR%

rem El linker trabaja siempre en un directorio temporal limpio. Asi nunca intenta
rem enlazar encima de los ejecutables que estan en build\Release.
if exist build-link rmdir /s /q build-link
cmake -S . -B build-link -G "%GENERATOR%" -A x64 || exit /b 1
cmake --build build-link --config Release --target promptemon_demo promptemon_resource_packager promptemon_resource_sfx || exit /b 1

if not exist build\Release mkdir build\Release

rem Elimina la salida temporal absurda usada por builds anteriores.
if exist build\Release\promptemon_demo_*.exe del /q build\Release\promptemon_demo_*.exe >nul 2>nul
if exist build\Release\LATEST_PROMPTEMON.txt del /q build\Release\LATEST_PROMPTEMON.txt >nul 2>nul

copy /Y build-link\Release\promptemon_demo.exe build\Release\promptemon_demo.exe >nul || exit /b 1
copy /Y build-link\Release\promptemon_resource_packager.exe build\Release\promptemon_resource_packager.exe >nul || exit /b 1
copy /Y build-link\Release\promptemon_resource_sfx.exe build\Release\promptemon_resource_sfx.exe >nul || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
echo Listo: build\Release\promptemon_resource_packager.exe
echo Listo: build\Release\promptemon_resource_sfx.exe
