@echo off
setlocal
cd /d "%~dp0"
if not defined ZIG set "ZIG=zig"
if not exist build mkdir build
"%ZIG%" c++ -target x86_64-windows-gnu -std=c++17 -O2 -g -shared -DWIN32_LEAN_AND_MEAN -DNOMINMAX poise_damage.cpp -o build/poise-damage-modifier.dll
if errorlevel 1 exit /b %errorlevel%
echo Built build\poise-damage-modifier.dll
