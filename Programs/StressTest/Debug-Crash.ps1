<#
One-off forensic script (not part of the reusable stress tool): launches
GameServer.exe under cdb.exe with packet-reorder simulation enabled, fires
a couple of client connections, and lets cdb catch the access violation and
print a stack trace + faulting instruction context.
#>
param(
    [int]$SimReorderWindow = 3
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$GameServerExe = Join-Path $RepoRoot "Bins\Debug\GameServer.exe"
$RemoteClientExe = Join-Path $RepoRoot "Bins\Debug\RemoteServer.exe"
$ExeWorkingDir = Split-Path $GameServerExe -Parent
$Cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"

if (-not (Test-Path $Cdb)) { throw "cdb.exe not found at $Cdb" }

$OutDir = Join-Path $PSScriptRoot "results\crash_debug_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$BaseConfigPath = Join-Path $RepoRoot "Project\GameServer\config\DefaultNetworkEngine.json"
$ConfigJson = Get-Content $BaseConfigPath -Raw | ConvertFrom-Json
$ConfigJson.Network.PacketDropPermille = 0
$ConfigJson.Network.PacketDuplicatePermille = 0
$ConfigJson.Network.PacketReorderWindow = $SimReorderWindow
$ConfigOverridePath = Join-Path $OutDir "GameServer_PacketSim.json"
$ConfigJson | ConvertTo-Json -Depth 6 | Out-File -FilePath $ConfigOverridePath -Encoding utf8

Remove-Item (Join-Path $RepoRoot "Logs") -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Launching GameServer.exe under cdb.exe (reorderWindow=$SimReorderWindow)..." -ForegroundColor Cyan
$env:LADELTA_CONFIG_PATH = $ConfigOverridePath
$env:_NO_DEBUG_HEAP = "0"  # keep the debug heap active so corruption is caught close to the write, not later

$cdbOut = Join-Path $OutDir "cdb_output.log"
$bpCmd = 'bp GameServer!Network::Simulation::PacketSimulator::Process+0x0'
$cdbCommandLine = '-g -G -c "bu `PacketSimulator.cpp:71`;g;?? PickIndex;?? ReorderWindow;?? ReorderWindow.size();g;g;g;g;g;g;g;g;g;g;q" "' + $GameServerExe + '"'
$cdbProc = Start-Process -FilePath $Cdb -ArgumentList $cdbCommandLine -PassThru -WorkingDirectory $ExeWorkingDir `
    -RedirectStandardOutput $cdbOut -RedirectStandardError (Join-Path $OutDir "cdb_stderr.log")
Remove-Item Env:\LADELTA_CONFIG_PATH -ErrorAction SilentlyContinue

Start-Sleep -Seconds 2

Write-Host "Launching 2 client connections..."
$clientProcs = @()
for ($i = 1; $i -le 2; $i++) {
    $p = Start-Process -FilePath $RemoteClientExe -PassThru -WorkingDirectory $ExeWorkingDir `
        -RedirectStandardOutput (Join-Path $OutDir "client_$i.out.log") -RedirectStandardError (Join-Path $OutDir "client_$i.err.log")
    $clientProcs += $p
}

Write-Host "Waiting up to 30s for cdb to catch a crash and exit..."
$cdbProc.WaitForExit(30000) | Out-Null

foreach ($p in $clientProcs) {
    if (-not $p.HasExited) { try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {} }
}
if (-not $cdbProc.HasExited) {
    Write-Warning "cdb did not exit within 30s (no crash caught, or still running); killing it."
    try { Stop-Process -Id $cdbProc.Id -Force -ErrorAction SilentlyContinue } catch {}
}

Write-Host "cdb output -> $cdbOut" -ForegroundColor Yellow
Get-Content $cdbOut -Tail 80
