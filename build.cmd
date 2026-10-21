@echo off
setlocal
cd /d "%~dp0"

rem Las ROMs son datos externos elegidos por el usuario al ejecutar el juego.
rem Su ruta se guarda en %%LOCALAPPDATA%%\PromptemonSteidium\player.db.
rem Este script no crea, mueve ni borra carpetas de ROMs.

git pull --ff-only || exit /b 1

rem Compilacion incremental. No se borra build para preservar cualquier dato local.
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target promptemon_demo || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
