@echo off
if not exist "bin\silver_gallery.exe" (
    echo Building first...
    call build.bat
)
start "" "bin\silver_gallery.exe" %*
