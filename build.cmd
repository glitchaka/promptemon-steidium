@echo off
setlocal
cd /d "%~dp0"

rem Las ROMs viven junto al ejecutable del juego:
rem build\Release\promptemon_demo.exe
rem build\Release\roms\
rem Esta carpeta es persistente y NUNCA debe borrarse durante pull/compilacion.
if not exist build mkdir build
if not exist build\Release mkdir build\Release
if not exist build\Release\roms mkdir build\Release\roms

git pull --ff-only || exit /b 1

rem NO borrar build ni build\Release: ahi vive la carpeta roms del usuario.
rem CMake reconfigura y recompila de forma incremental.
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target promptemon_demo || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
echo ROMs:  build\Release\roms\
