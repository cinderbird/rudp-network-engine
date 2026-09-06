@echo off
setlocal enabledelayedexpansion

rem All paths are resolved relative to this script's own location so the
rem build works regardless of where the repository is cloned to.
rem This intentionally has no dependency on Python (or any other external
rem interpreter) being installed/on PATH -- it only needs protoc.exe, which
rem is vendored in the repo under Common/.
SET SCRIPT_DIR=%~dp0
SET REPO_ROOT=%SCRIPT_DIR%..\..

SET PROTO_SOURCE_ROOT=%REPO_ROOT%\Project\Network\Protos\GameProto
SET COPY_DEST_DIR=%REPO_ROOT%\Project\Network\include

rem Prefer the vendored protoc (matches the exact version generated code was
rem written against). If it isn't present -- e.g. it was stripped by an
rem overzealous .gitignore rule on a fresh clone -- fall back to the protoc
rem that vcpkg installs alongside the protobuf package, which is available
rem once the manifest has been restored at least once. The exact nesting of
rem vcpkg's installed tree (tools\protobuf\protoc.exe) has changed between
rem vcpkg tool versions, so search for it instead of hardcoding one layout.
SET PROTOC_PATH=%REPO_ROOT%\Common\protoc-21.12-win64\bin\protoc.exe
if not exist "%PROTOC_PATH%" (
    SET PROTOC_PATH=
    if exist "%REPO_ROOT%\vcpkg_installed\x64-windows-static-md" (
        for /f "delims=" %%P in ('dir /s /b "%REPO_ROOT%\vcpkg_installed\x64-windows-static-md\protoc.exe" 2^>nul') do (
            if "!PROTOC_PATH!"=="" set "PROTOC_PATH=%%P"
        )
    )
)

if not defined PROTOC_PATH (
    echo.
    echo !!!!!!! ERROR: protoc.exe was not found. !!!!!!!
    echo !!!!!!! Looked in Common\protoc-21.12-win64\bin\ and under vcpkg_installed\x64-windows-static-md\. !!!!!!!
    echo !!!!!!! If this is a fresh clone, build once so vcpkg can restore the manifest, then rebuild. !!!!!!!
    exit /b 1
)
if not exist "%PROTOC_PATH%" (
    echo.
    echo !!!!!!! ERROR: protoc.exe was not found. !!!!!!!
    echo !!!!!!! Looked in Common\protoc-21.12-win64\bin\ and under vcpkg_installed\x64-windows-static-md\. !!!!!!!
    echo !!!!!!! If this is a fresh clone, build once so vcpkg can restore the manifest, then rebuild. !!!!!!!
    exit /b 1
)

echo Starting Protobuf code generation...

FOR /R "%PROTO_SOURCE_ROOT%" %%F IN (*.proto) DO (
    echo Processing file: "%%~nxF"

    pushd "%PROTO_SOURCE_ROOT%"
    "%PROTOC_PATH%" -I=. --cpp_out=. "%%~nxF"
    set "PROTOC_ERR=!errorlevel!"
    popd

    if !PROTOC_ERR! neq 0 (
        echo.
        echo !!!!!!! ERROR: protoc failed to process %%F. Aborting build event. !!!!!!!
        exit /b !PROTOC_ERR!
    )

    if not exist "%PROTO_SOURCE_ROOT%\%%~nF.pb.h" (
        echo.
        echo !!!!!!! ERROR: Expected "%PROTO_SOURCE_ROOT%\%%~nF.pb.h" was not generated. !!!!!!!
        exit /b 1
    )
    if not exist "%PROTO_SOURCE_ROOT%\%%~nF.pb.cc" (
        echo.
        echo !!!!!!! ERROR: Expected "%PROTO_SOURCE_ROOT%\%%~nF.pb.cc" was not generated. !!!!!!!
        exit /b 1
    )

    echo Copying %%~nF.pb.h and %%~nF.pb.cc to "%COPY_DEST_DIR%"
    MOVE /Y "%PROTO_SOURCE_ROOT%\%%~nF.pb.h" "%COPY_DEST_DIR%\" >nul
    MOVE /Y "%PROTO_SOURCE_ROOT%\%%~nF.pb.cc" "%COPY_DEST_DIR%\" >nul
)

echo ...Protobuf code generation finished successfully.
endlocal
