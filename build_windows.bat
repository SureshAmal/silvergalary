@echo off
setlocal
echo ==============================================
echo   Building SilverGallery & SilverViewer for Windows
echo ==============================================

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo Visual Studio 2022 not found, trying MinGW / default...
    cmake ..
)

cmake --build . --config Release --parallel
if %ERRORLEVEL% EQU 0 (
    echo ==============================================
    echo  Build Successful!
    echo  Executables created in bin\ directory:
    echo    - bin\silver_viewer.exe
    echo    - bin\silver_gallery.exe
    echo ==============================================
) else (
    echo Build failed. Please ensure CMake and a C++17 compiler (MSVC or MinGW) are installed.
)
cd ..
pause
