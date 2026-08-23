@echo off
if not exist "bin\silver_viewer.exe" (
    echo Building first...
    call build.bat
)
start "" "bin\silver_viewer.exe" %*
