@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   Silver Suite - Windows Standalone Build System
echo ===================================================

if not exist "bin" mkdir "bin"

:: 1. Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [*] CMake detected. Building with CMake...
    if not exist "build" mkdir "build"
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    if %ERRORLEVEL% NEQ 0 (
        cmake .. -G "MinGW Makefiles"
    )
    cmake --build . --config Release --parallel
    cd ..
    goto finish
)

:: 2. Check if MinGW g++ is available
where g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [*] MinGW G++ detected. Compiling directly...
    
    echo [*] Compiling embedded SQLite...
    gcc -O3 -c include\sqlite3.c -o bin\sqlite3.o -Iinclude

    echo [*] Building silver_viewer.exe...
    g++ -std=c++17 -O3 -Iinclude -Iinclude\easyexif -Iinclude\GLFW -Iviewer_linux viewer_linux\main.cpp include\easyexif\exif.cpp -lopengl32 -lglfw3 -lgdi32 -luser32 -lshell32 -o bin\silver_viewer.exe

    echo [*] Building silver_gallery.exe...
    g++ -std=c++17 -O3 -Iinclude -Iinclude\easyexif -Iinclude\GLFW -Iviewer_linux -Igallery_linux gallery_linux\main.cpp include\easyexif\exif.cpp bin\sqlite3.o -lopengl32 -lglfw3 -lgdi32 -luser32 -lshell32 -o bin\silver_gallery.exe

    goto finish
)

:: 3. Check for Visual Studio MSVC compiler
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [*] MSVC cl.exe detected. Compiling directly...
    
    cl /nologo /O2 /std:c++17 /EHsc /Iinclude /Iinclude\easyexif /Iinclude\GLFW /Iviewer_linux viewer_linux\main.cpp include\easyexif\exif.cpp /link opengl32.lib glfw3.lib user32.lib gdi32.lib shell32.lib /OUT:bin\silver_viewer.exe

    cl /nologo /O2 /std:c++17 /EHsc /Iinclude /Iinclude\easyexif /Iinclude\GLFW /Iviewer_linux /Igallery_linux gallery_linux\main.cpp include\sqlite3.c include\easyexif\exif.cpp /link opengl32.lib glfw3.lib user32.lib gdi32.lib shell32.lib /OUT:bin\silver_gallery.exe

    goto finish
)

:: 4. Try locating Visual Studio vcvarsall.bat
set "VS_PATH="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if defined VS_PATH (
    echo [*] Setting up Visual Studio environment...
    call "!VS_PATH!"
    
    cl /nologo /O2 /std:c++17 /EHsc /Iinclude /Iinclude\easyexif /Iinclude\GLFW /Iviewer_linux viewer_linux\main.cpp include\easyexif\exif.cpp /link opengl32.lib glfw3.lib user32.lib gdi32.lib shell32.lib /OUT:bin\silver_viewer.exe

    cl /nologo /O2 /std:c++17 /EHsc /Iinclude /Iinclude\easyexif /Iinclude\GLFW /Iviewer_linux /Igallery_linux gallery_linux\main.cpp include\sqlite3.c include\easyexif\exif.cpp /link opengl32.lib glfw3.lib user32.lib gdi32.lib shell32.lib /OUT:bin\silver_gallery.exe

    goto finish
)

echo [ERROR] No supported C++ compiler found (CMake, MinGW g++, or Visual Studio MSVC).
echo Please install Visual Studio with "Desktop development with C++" or MinGW-w64.
pause
exit /b 1

:finish
echo.
echo ===================================================
if exist "bin\silver_viewer.exe" (
    echo  [OK] Successfully built: bin\silver_viewer.exe
)
if exist "bin\silver_gallery.exe" (
    echo  [OK] Successfully built: bin\silver_gallery.exe
)
echo ===================================================
pause
