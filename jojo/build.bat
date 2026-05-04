@echo off
REM Build script for JOJO OS

REM Set paths for Visual Studio 2026 Professional
set VS_PATH="C:\Program Files\Microsoft Visual Studio\18\Professional"
set MSVC_PATH=%VS_PATH%\VC\Tools\MSVC\14.50.35717
set WIN_KITS="C:\Program Files (x86)\Windows Kits\10"

REM Set environment
call %VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat x64

REM Compile
cl /std:c++17 /EHsc /I%MSVC_PATH%\include /I%WIN_KITS%\Include\10.0.22621.0\ucrt /I%WIN_KITS%\Include\10.0.22621.0\um /I%WIN_KITS%\Include\10.0.22621.0\shared src\main.cpp src\kernel.cpp src\console.cpp src\filesystem.cpp /Fe:jojo.exe /link /LIBPATH:%MSVC_PATH%\lib\x64 /LIBPATH:%WIN_KITS%\Lib\10.0.22621.0\ucrt\x64 /LIBPATH:%WIN_KITS%\Lib\10.0.22621.0\um\x64

echo Build complete!
