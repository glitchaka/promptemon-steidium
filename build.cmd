@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

rem Las ROMs son datos externos elegidos por el usuario al ejecutar el juego.
rem Su ruta se guarda en %%LOCALAPPDATA%%\PromptemonSteidium\player.db.
rem Este script no crea, mueve ni borra carpetas de ROMs.

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
    echo Instala "Desktop development with C++" o los Visual Studio Build Tools con MSVC y Windows SDK.
    exit /b 1
)

echo Usando: %GENERATOR%

rem Si build fue generado con otra version de Visual Studio, hay que regenerarlo.
if exist build\CMakeCache.txt (
    set "CACHED_GENERATOR="
    for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" build\CMakeCache.txt') do set "CACHED_GENERATOR=%%B"
    if defined CACHED_GENERATOR if /I not "!CACHED_GENERATOR!"=="%GENERATOR%" (
        echo Regenerando build: !CACHED_GENERATOR! -^> %GENERATOR%
        rmdir /s /q build
    )
)

cmake -S . -B build -G "%GENERATOR%" -A x64 || exit /b 1
cmake --build build --config Release --target promptemon_demo || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
