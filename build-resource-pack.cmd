@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "RESOURCES=%~1"
if not defined RESOURCES set "RESOURCES=%~dp0..\resources-tx"

if not exist "%RESOURCES%\files" (
    echo No encuentro resources-tx en:
    echo   %RESOURCES%
    echo.
    echo Uso: build-resource-pack.cmd "C:\ruta\a\resources-tx"
    exit /b 1
)

if not exist build\CMakeCache.txt (
    echo Primero configura/compila el proyecto con build.cmd.
    exit /b 1
)

cmake --build build --config Release --target promptemon_resource_packager promptemon_resource_sfx || exit /b 1

set "PACKAGER=build\Release\promptemon_resource_packager.exe"
set "STUB=build\Release\promptemon_resource_sfx.exe"
set "OUTPUT=build\Release\PromptemonResources.exe"

"%PACKAGER%" "%RESOURCES%" "config\required_resources.txt" "%STUB%" "%OUTPUT%" || exit /b 1

echo.
echo Autoextraible: %OUTPUT%
echo Al ejecutarlo extrae solo el subset usado a assets\resources-tx junto al EXE.
