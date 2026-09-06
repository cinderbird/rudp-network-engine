@echo off
setlocal EnableExtensions

rem 배치 파일이 있는 폴더 기준
set "targetFolder=%~dp0"
pushd "%targetFolder%" || (echo Failed to cd into "%targetFolder%" & exit /b 1)

echo Target Folder: %CD%
echo Converting .h, .cpp, .cc files to UTF-8 (no BOM)...
echo Excluding *.pb.h and *.pb.cc
echo.

rem 단일 PowerShell 실행으로 일괄 처리
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$utf8NoBom = New-Object System.Text.UTF8Encoding($false);" ^
  "Get-ChildItem -Path . -Recurse -File -Include *.h,*.cpp,*.cc |" ^
  "Where-Object { $_.Name -notmatch '\.pb\.(h|cc)$' } |" ^
  "ForEach-Object {" ^
  "  Write-Host ('Processing: \"{0}\"' -f $_.FullName);" ^
  "  $text = Get-Content -LiteralPath $_.FullName -Raw;" ^
  "  [System.IO.File]::WriteAllText($_.FullName, $text, $utf8NoBom)" ^
  "}"

if errorlevel 1 (
  echo.
  echo ERROR: Conversion failed. See messages above.
  popd
  exit /b 1
)

echo.
echo All conversions are complete.
popd
pause
