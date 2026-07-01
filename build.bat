@echo off
chcp 65001 >nul
setlocal

set "NO_PAUSE="
if /i "%~1"=="--no-pause" set "NO_PAUSE=1"

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build"
set "OUTPUT_EXE=%PROJECT_DIR%\codex-tray-gauge.exe"

echo ========================================
echo   Codex Tray Gauge - Build
echo ========================================
echo.

cd /d "%PROJECT_DIR%" || goto :fail

rem MSYS2 UCRT64 toolchain
set "MSYS2_ROOT="
if exist "C:\msys64\ucrt64\bin\g++.exe" set "MSYS2_ROOT=C:\msys64"
if not defined MSYS2_ROOT if exist "D:\msys64\ucrt64\bin\g++.exe" set "MSYS2_ROOT=D:\msys64"
if not defined MSYS2_ROOT (
    echo [ERROR] MSYS2 UCRT64 compiler not found.
    echo Install MSYS2, then install mingw-w64-ucrt-x86_64-gcc.
    goto :fail
)

set "UCRT_BIN=%MSYS2_ROOT%\ucrt64\bin"
set "CXX=%UCRT_BIN%\g++.exe"

rem Prefer installed CMake, then PATH.
set "CMAKE_EXE="
if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CMAKE_EXE for %%X in (cmake.exe) do set "CMAKE_EXE=%%~$PATH:X"
if not defined CMAKE_EXE (
    echo [ERROR] CMake not found.
    echo Install: winget install Kitware.CMake
    goto :fail
)

rem Prefer Ninja from MSYS2, then PATH. Fall back to MinGW Makefiles.
set "GENERATOR=Ninja"
set "NINJA_EXE="
if exist "%UCRT_BIN%\ninja.exe" set "NINJA_EXE=%UCRT_BIN%\ninja.exe"
if not defined NINJA_EXE for %%X in (ninja.exe) do set "NINJA_EXE=%%~$PATH:X"
if not defined NINJA_EXE set "GENERATOR=MinGW Makefiles"

rem Important: g++ needs UCRT_BIN in PATH while compiling, not just for --version.
set "PATH=%UCRT_BIN%;%PATH%"

echo [INFO] Compiler : %CXX%
echo [INFO] CMake    : %CMAKE_EXE%
echo [INFO] Generator: %GENERATOR%
echo.

tasklist /FI "IMAGENAME eq codex-tray-gauge.exe" 2>nul | find /I "codex-tray-gauge.exe" >nul
if not errorlevel 1 (
    echo [INFO] Closing running codex-tray-gauge.exe
    taskkill /IM codex-tray-gauge.exe /F >nul 2>&1
)

echo [STEP 1/3] Configure
"%CMAKE_EXE%" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="%CXX%"
if errorlevel 1 goto :fail

echo.
echo [STEP 2/3] Build
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 goto :fail

echo.
echo [STEP 3/3] Copy exe
copy /Y "%BUILD_DIR%\codex-tray-gauge.exe" "%OUTPUT_EXE%" >nul
if errorlevel 1 (
    echo [ERROR] Could not replace codex-tray-gauge.exe.
    echo Close the running tray app, then run build.bat again.
    goto :fail
)

echo.
echo ========================================
echo   BUILD SUCCESS
echo   %OUTPUT_EXE%
echo ========================================
goto :done

:fail
echo.
echo ========================================
echo   BUILD FAILED
echo ========================================
if not defined NO_PAUSE pause
exit /b 1

:done
if not defined NO_PAUSE pause
exit /b 0
