@echo off
setlocal enabledelayedexpansion

rem Fetches and bootstraps a repo-local copy of vcpkg at .\vcpkg so that
rem building Toska.sln does not depend on any machine-wide vcpkg setup
rem (no "vcpkg integrate install", no VCPKG_ROOT env var, no VS "vcpkg
rem package manager" individual component required). Run this once after
rem cloning the repository, then just open/build Toska.sln as usual.

SET REPO_ROOT=%~dp0
SET VCPKG_DIR=%REPO_ROOT%vcpkg

where git >nul 2>nul
if not %errorlevel% equ 0 (
    echo.
    echo !!!!!!! ERROR: git was not found on PATH. Install Git for Windows first. !!!!!!!
    exit /b 1
)

if not exist "%VCPKG_DIR%\.git" (
    echo Cloning vcpkg into "%VCPKG_DIR%"...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
    if not %errorlevel% equ 0 (
        echo.
        echo !!!!!!! ERROR: Failed to clone vcpkg. Check your network connection. !!!!!!!
        exit /b 1
    )
) else (
    echo Found existing vcpkg checkout at "%VCPKG_DIR%", skipping clone.
)

echo Bootstrapping vcpkg...
call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
if not %errorlevel% equ 0 (
    echo.
    echo !!!!!!! ERROR: vcpkg bootstrap failed. !!!!!!!
    exit /b 1
)

echo.
echo vcpkg is ready at "%VCPKG_DIR%". You can now open/build Toska.sln normally --
echo Directory.Build.props/.targets wire this checkout into every project automatically.
endlocal
