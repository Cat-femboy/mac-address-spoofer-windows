@echo off
setlocal
cd /d "%~dp0"

where g++ >nul 2>&1
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH. Install LLVM-MinGW / mingw-w64.
    exit /b 1
)
where windres >nul 2>&1
if errorlevel 1 (
    echo [ERROR] windres not found in PATH.
    exit /b 1
)

echo [*] Generating icon...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_icon.ps1"
if errorlevel 1 exit /b 1

echo [*] Compiling resources...
windres resource.rc -O coff -o resource.res
if errorlevel 1 (
    echo [ERROR] Resource compilation failed.
    exit /b 1
)

echo [*] Compiling native GUI...
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ spoof.cpp resource.res -o MAC-Spoofer.exe -ldwmapi -lshell32 -lshlwapi -lcomctl32 -ladvapi32 -luser32 -lgdi32
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

echo [SUCCESS] MAC-Spoofer.exe built.
