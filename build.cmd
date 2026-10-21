@echo off
setlocal
cd /d "%~dp0"

rem roms\ contiene datos locales persistentes del usuario.
rem Git y la compilacion NO deben borrar ni reemplazar su contenido.
if not exist roms mkdir roms

git pull --ff-only || exit /b 1

rem Limpiar solo la salida de compilacion.
if exist build rmdir /s /q build

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target promptemon_demo || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
