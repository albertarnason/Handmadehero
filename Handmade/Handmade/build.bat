@echo off

taskkill /IM win32_handmade.exe /F >nul 2>&1
timeout /t 1 /nobreak >nul
IF NOT EXIST ..\build mkdir ..\build
pushd ..\build
cl -DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1 -FC -Zi /I ..\Handmade ..\Handmade\win32_handmade.cpp user32.lib gdi32.lib
popd