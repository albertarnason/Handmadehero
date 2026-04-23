@echo off

taskkill /IM win32_handmade.exe /F >nul 2>&1
timeout /t 1 /nobreak >nul
mkdir ..\build
pushd ..\build
cl -FC -Zi -Wall /I ..\Handmade ..\Handmade\win32_handmade.cpp user32.lib gdi32.lib
popd