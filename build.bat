@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo   Codex Tray Gauge - Build Script
echo ========================================
echo.

:: ── Locate MSYS2 UCRT64 ──
set "MSYS2_ROOT="
if exist "C:\msys64\ucrt64\bin\g++.exe" (
    set "MSYS2_ROOT=C:\msys64"
) else if exist "D:\msys64\ucrt64\bin\g++.exe" (
    set "MSYS2_ROOT=D:\msys64"
)

:: ── Locate CMake ──
set "CMAKE_BIN="
if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set "CMAKE_BIN=C:\Program Files\CMake\bin"
)
:: Also check PATH
for %%X in (cmake.exe) do if not defined CMAKE_BIN set "CMAKE_BIN=%%~dpX"

:: ── Locate Ninja ──
set "NINJA_DIR="
if exist "%MSYS2_ROOT%\ucrt64\bin\ninja.exe" (
    set "NINJA_DIR=%MSYS2_ROOT%\ucrt64\bin"
)
:: Also check PATH / winget install
for %%X in (ninja.exe) do if not defined NINJA_DIR set "NINJA_DIR=%%~dpX"

:: ── Validate ──
if "%MSYS2_ROOT%"=="" (
    echo [ERROR] MSYS2 with UCRT64 toolchain not found.
    echo         Install: winget install MSYS2.MSYS2
    echo         Then run in MSYS2 terminal: pacman -S mingw-w64-ucrt-x86_64-gcc
    pause
    exit /b 1
)
if "%CMAKE_BIN%"=="" (
    echo [ERROR] CMake not found.
    echo         Install: winget install Kitware.CMake
    pause
    exit /b 1
)

echo [INFO] MSYS2 : %MSYS2_ROOT%
echo [INFO] CMake  : %CMAKE_BIN%
echo [INFO] Ninja  : %NINJA_DIR%
echo.

:: ── Build PATH ──
set "PATH=%MSYS2_ROOT%\ucrt64\bin;%CMAKE_BIN%;%NINJA_DIR%;%PATH%"

:: ── Verify tools ──
g++.exe --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] g++ not working. Check MSYS2 installation.
    pause
    exit /b 1
)
cmake.exe --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not working.
    pause
    exit /b 1
)

:: ── Check for Ninja, fall back to MinGW Makefiles ──
set "GENERATOR=Ninja"
ninja.exe --version >nul 2>&1
if errorlevel 1 (
    echo [WARN]  Ninja not found, using MinGW Makefiles (slower)
    set "GENERATOR=MinGW Makefiles"
)

:: ── Build directory ──
set "BUILD_DIR=build"
set "PROJECT_DIR=%~dp0"

cd /d "%PROJECT_DIR%"

echo [STEP 1] Configuring CMake...
echo          Generator: %GENERATOR%
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++.exe
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

echo.
echo [STEP 2] Building...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo [STEP 3] Copying executable...
copy /Y "%BUILD_DIR%\codex-tray-gauge.exe" "codex-tray-gauge.exe" >nul
if errorlevel 1 (
    echo [WARN]  Could not copy exe (maybe it's running, kill it first)
)

echo.
echo ========================================
echo   BUILD SUCCESS
echo   %PROJECT_DIR%codex-tray-gauge.exe
echo ========================================
pause
