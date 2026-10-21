@echo off
setlocal
cd /d "%~dp0"

rem Las ROMs locales viven dentro de build\roms.
rem Esta carpeta es persistente y NUNCA debe borrarse durante pull/compilacion.
if not exist build mkdir build
if not exist build\roms mkdir build\roms

git pull --ff-only || exit /b 1

rem NO borrar build: contiene build\roms con las ROMs del usuario.
rem CMake reconfigura y recompila de forma incremental sobre la misma carpeta.
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target promptemon_demo || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
