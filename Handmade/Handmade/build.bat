@echo off
 
taskkill /IM win32_handmade.exe /F >nul 2>&1
timeout /t 1 /nobreak >nul
IF NOT EXIST ..\build mkdir ..\build
pushd ..\build
cl -DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1 -GR- -MT -nologo -EHa- -Od -Oi -W4 -WX -wd4201 -wd4100 -wd4189 -FC -Fmwin32_handmade.map -Z7 /I ..\Handmade ..\Handmade\win32_handmade.cpp /link -opt:ref user32.lib gdi32.lib
popd
