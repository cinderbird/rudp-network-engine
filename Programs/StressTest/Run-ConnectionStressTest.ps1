<#
.SYNOPSIS
  Connection load/chaos testing tool. Originally built for M1's
  thread-safety verification (hammers GameServer with concurrent client
  connections to stress the IpNetDriver/NetDriver ConnectionsMutex path --
  AddClient_Internal writes vs FindConnection/TickDispatch/TickFlush reads --
  while profiling the server process). -EnablePacketSim additionally turns
  on M2's recv-side packet loss/duplicate/reorder simulation on the server,
  for exercising the channel/packet reliability layer (M3/M4) under
  controllable, reproducible network conditions.

.DESCRIPTION
  Launches one GameServer.exe, then launches ClientsPerBurst RemoteServer.exe
  instances (as separate OS processes, each getting its own ephemeral UDP
  port) per burst, Bursts times, with BurstIntervalMs between bursts. Then
  holds everything alive for SoakSeconds so steady-state Tick traffic keeps
  running. Samples the server's CPU/memory/handle/thread counts after every
  burst, and at the end parses Bins\Logs\Server\network_log_*.txt for
  handshake completion lines (same "3-way handshake end" marker used in
  POST_MORTEM.md) to report per-connection handshake latency and overall
  connect throughput.

  Everything is written under Programs\StressTest\results\<timestamp>\:
    summary.json         - pass/fail, crash counts, throughput/latency stats
    server_profile.csv   - CPU%/working-set/handles/threads sampled over time
    gameserver_stdout/stderr.log
    client_survivors.txt - PIDs of client processes still alive at end (if any)

.PARAMETER Bursts
  Number of connection bursts to fire.

.PARAMETER ClientsPerBurst
  Number of RemoteServer.exe client processes launched per burst.

.PARAMETER BurstIntervalMs
  Delay between the start of one burst and the next.

.PARAMETER SoakSeconds
  How long to keep the server + all clients running after the last burst,
  before tearing everything down.

.PARAMETER UseAppVerifier
  If set, enables Windows Application Verifier (Heaps/Handles/Locks) for
  GameServer.exe for the duration of the run, then disables it again
  afterward. Requires an elevated (Administrator) PowerShell session.
  Verifier stops are surfaced via the Application event log.

.PARAMETER EnablePacketSim
  If set, GameServer.exe is launched against a temporary copy of its config
  (Project\GameServer\config\DefaultNetworkEngine.json) with the M2 recv-side
  packet simulation fields (PacketDropPermille/PacketDuplicatePermille/
  PacketReorderWindow) overridden to SimDropPermille/SimDuplicatePermille/
  SimReorderWindow, via the existing LADELTA_CONFIG_PATH override. Look for
  "[PacketSimulator]" trace lines in the resulting network_log_*.txt to
  confirm it actually fired.

.PARAMETER SimDropPermille
  Parts-per-thousand chance (0-1000) a received datagram is dropped. Only
  used when -EnablePacketSim is set.

.PARAMETER SimDuplicatePermille
  Parts-per-thousand chance (0-1000) a surviving datagram is delivered
  twice. Only used when -EnablePacketSim is set.

.PARAMETER SimReorderWindow
  Reorder window size (0 disables reordering). Only used when
  -EnablePacketSim is set.

.PARAMETER SimDropPermilleOutbound
  서버가 보내는(server->client) 데이터그램을 드롭할 확률(퍼밀, 0-1000) --
  이전엔 recv 방향만 시뮬레이션됐던 갭을 메움. 드롭만 지원, 이 방향엔
  중복/재정렬 없음. -EnablePacketSim일 때만 적용.

.PARAMETER ConnectionsPerProcess
  RemoteServer.exe 프로세스 하나가 호스팅할 커넥션 수(M24의
  `RemoteServer.exe <N>`). 기본 1 = 프로세스당 커넥션 1개(기존 동작 그대로).
  $Bursts * $ClientsPerBurst는 프로세스 수, 여기에 $ConnectionsPerProcess를
  곱한 값(= $TotalClientsLaunched)이 총 커넥션 수 -- 프로세스를 그만큼
  안 띄우고도 수백~수천 커넥션에 도달하려면 후자를 쓴다.

.EXAMPLE
  .\Run-ConnectionStressTest.ps1 -Bursts 10 -ClientsPerBurst 15 -BurstIntervalMs 500 -SoakSeconds 20 -UseAppVerifier

.EXAMPLE
  .\Run-ConnectionStressTest.ps1 -Bursts 2 -ClientsPerBurst 5 -EnablePacketSim -SimDropPermille 100 -SimDuplicatePermille 20 -SimReorderWindow 3

.EXAMPLE
  # 프로세스 500개 대신 4개(각 125커넥션)로 500커넥션
  .\Run-ConnectionStressTest.ps1 -Bursts 1 -ClientsPerBurst 4 -ConnectionsPerProcess 125 -Configuration Release -ServerTelemetryPort 9200
#>
param(
    [int]$Bursts = 10,
    [int]$ClientsPerBurst = 15,
    [int]$BurstIntervalMs = 500,
    [int]$SoakSeconds = 20,
    [switch]$UseAppVerifier,
    [switch]$EnablePacketSim,
    [int]$SimDropPermille = 100,
    [int]$SimDuplicatePermille = 20,
    [int]$SimReorderWindow = 3,
    [int]$SimDropPermilleOutbound = 0,

    # --- 부하 생성 관련 옵션(전부 기본값은 기존 엔진 동작 그대로) ---
    [int]$ClientSyntheticTrafficHz = 0,   # 0 = 설정 파일 기본값(1) 유지
    [int]$ClientMaxTickRate = 0,          # 0 = 설정 파일 기본값(60) 유지
    [int]$ServerSyntheticTrafficHz = 0,
    [int]$ServerMaxTickRate = 0,
    [string]$LogLevel = "",               # "" = 설정 파일 기본값(trace) 유지
    [int]$ServerTelemetryPort = 0,        # 0 = 텔레메트리 끔(측정 안 함)

    # Release가 처리량/스케일링 실측의 정직한 기준 -- Network.vcxproj의
    # protobuf 유니티 빌드 충돌을 고치기 전엔 안 빌드돼서 그전 측정은 전부
    # Debug였다.
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    # RemoteServer.exe 프로세스 하나가 호스팅할 커넥션 수(M24의
    # `RemoteServer.exe <N>`). 기본 1은 기존 프로세스당 커넥션 1개 동작
    # 그대로. 이 값을 올리면 프로세스 수를 안 늘리고도 수백~수천 커넥션까지
    # 부하를 올릴 수 있다 -- $Bursts * $ClientsPerBurst는 프로세스 수, 여기에
    # 이 값을 곱하면 총 커넥션 수($TotalClientsLaunched, 아래).
    [int]$ConnectionsPerProcess = 1
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$GameServerExe = Join-Path $RepoRoot "Bins\$Configuration\GameServer.exe"
$RemoteClientExe = Join-Path $RepoRoot "Bins\$Configuration\RemoteServer.exe"
$ExeWorkingDir = Split-Path $GameServerExe -Parent
# ServerLog::Init (Project/Network/src/utils/AyncyLog.h) derives the log
# directory from std::filesystem::current_path() -- the PROCESS's working
# directory, not the exe's own path -- as
# current_path().parent_path().parent_path() / "Logs". We always launch both
# exes with -WorkingDirectory $ExeWorkingDir (Bins\Debug) below, which
# resolves to <repo root>\Logs\{Server,Remote}. If that helper ever changes
# to use the exe path instead, update this to match.
$ServerLogDir = Join-Path $RepoRoot "Logs\Server"
$RemoteLogDir = Join-Path $RepoRoot "Logs\Remote"

if (-not (Test-Path $GameServerExe)) { throw "GameServer.exe not found at $GameServerExe -- build $Configuration|x64 first." }
if (-not (Test-Path $RemoteClientExe)) { throw "RemoteServer.exe not found at $RemoteClientExe -- build $Configuration|x64 first." }

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$ResultsDir = Join-Path $PSScriptRoot "results\$Timestamp"
New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

Write-Host "=== Connection stress test ===" -ForegroundColor Cyan
Write-Host "Bursts=$Bursts ClientsPerBurst=$ClientsPerBurst ConnectionsPerProcess=$ConnectionsPerProcess BurstIntervalMs=$BurstIntervalMs SoakSeconds=$SoakSeconds UseAppVerifier=$UseAppVerifier EnablePacketSim=$EnablePacketSim"
Write-Host "Results -> $ResultsDir"

# --- 설정 오버라이드 --------------------------------------------------------
# 기본값과 다른 옵션이 하나라도 있으면 오버라이드를 만든다. 클라이언트
# 설정에도 같은 방식 적용(이전엔 클라이언트가 항상 커밋된 기본값으로만
# 돌아서 부하를 못 올렸음).
#
# 둘 다 LADELTA_CONFIG_PATH(프로세스 전역 env var, GetConfigJsonPath가 읽음)
# 를 거친다. NetworkKitInitializeWithConfig의 ConfigOverride 매개변수는
# LaDelta::InitNetworkSetting이 조용히 무시하므로, 지금 실제로 동작하는
# 오버라이드 경로는 이 env var뿐이다.
function New-ConfigOverride {
    param(
        [string]$BaseConfigPath,
        [string]$OutPath,
        [hashtable]$Overrides
    )
    if (-not (Test-Path $BaseConfigPath)) { throw "Base config not found at $BaseConfigPath" }

    $ConfigJson = Get-Content $BaseConfigPath -Raw | ConvertFrom-Json
    foreach ($Key in $Overrides.Keys) {
        # 엔진의 JSON 바인딩은 필드마다 at()을 써서 키가 없으면 시작할 때
        # 던진다 -- 여기서 미리 명확한 메시지로 실패시킨다.
        if ($null -eq $ConfigJson.Network.PSObject.Properties[$Key]) {
            throw "Config key '$Key' not present in $BaseConfigPath -- the engine would reject the override."
        }
        $ConfigJson.Network.$Key = $Overrides[$Key]
    }
    $ConfigJson | ConvertTo-Json -Depth 6 | Out-File -FilePath $OutPath -Encoding utf8
    return $OutPath
}

$ServerOverrides = @{}
if ($EnablePacketSim) {
    $ServerOverrides["PacketDropPermille"] = $SimDropPermille
    $ServerOverrides["PacketDuplicatePermille"] = $SimDuplicatePermille
    $ServerOverrides["PacketReorderWindow"] = $SimReorderWindow
    $ServerOverrides["PacketDropPermilleOutbound"] = $SimDropPermilleOutbound
}
if ($ServerSyntheticTrafficHz -gt 0) { $ServerOverrides["SyntheticTrafficHz"] = $ServerSyntheticTrafficHz }
if ($ServerMaxTickRate -gt 0)        { $ServerOverrides["MaxTickRate"] = $ServerMaxTickRate }
if ($LogLevel -ne "")                { $ServerOverrides["LogLevel"] = $LogLevel }
if ($ServerTelemetryPort -gt 0)      { $ServerOverrides["TelemetryPort"] = $ServerTelemetryPort }

$ServerConfigOverridePath = $null
if ($ServerOverrides.Count -gt 0) {
    $ServerConfigOverridePath = New-ConfigOverride `
        -BaseConfigPath (Join-Path $RepoRoot "Project\GameServer\config\DefaultNetworkEngine.json") `
        -OutPath (Join-Path $ResultsDir "GameServer_Override.json") `
        -Overrides $ServerOverrides
    Write-Host "Server config override -> $ServerConfigOverridePath" -ForegroundColor Yellow
    Write-Host "  $(($ServerOverrides.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', ')"
}

$ClientOverrides = @{}
if ($ClientSyntheticTrafficHz -gt 0) { $ClientOverrides["SyntheticTrafficHz"] = $ClientSyntheticTrafficHz }
if ($ClientMaxTickRate -gt 0)        { $ClientOverrides["MaxTickRate"] = $ClientMaxTickRate }
if ($LogLevel -ne "")                { $ClientOverrides["LogLevel"] = $LogLevel }

$ClientConfigOverridePath = $null
if ($ClientOverrides.Count -gt 0) {
    $ClientConfigOverridePath = New-ConfigOverride `
        -BaseConfigPath (Join-Path $RepoRoot "Project\RemoteServer\config\DefaultNetworkEngine.json") `
        -OutPath (Join-Path $ResultsDir "RemoteServer_Override.json") `
        -Overrides $ClientOverrides
    Write-Host "Client config override -> $ClientConfigOverridePath" -ForegroundColor Yellow
    Write-Host "  $(($ClientOverrides.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', ')"
}

# Clean previous logs so this run's log lines are unambiguous.
foreach ($dir in @($ServerLogDir, $RemoteLogDir)) {
    if (Test-Path $dir) {
        Remove-Item (Join-Path $dir "*") -Force -ErrorAction SilentlyContinue
    }
}

$AppVerifierEnabled = $false
if ($UseAppVerifier) {
    try {
        & appverif.exe -enable Heaps Handles Locks -for GameServer.exe | Out-Null
        $AppVerifierEnabled = $true
        Write-Host "Application Verifier enabled for GameServer.exe (Heaps/Handles/Locks)." -ForegroundColor Yellow
    }
    catch {
        Write-Warning "Could not enable Application Verifier (needs an elevated shell). Continuing without it. $_"
    }
}

$EventLogCutoff = Get-Date

# --- Start server ---
# LADELTA_CONFIG_PATH is inherited by the child process at creation time, so
# it's safe to set it only around this one Start-Process call and clear it
# right after -- later Start-Process calls for RemoteServer.exe clients must
# NOT pick up the server's (GameServer-shaped) config override.
if ($ServerConfigOverridePath) {
    $env:LADELTA_CONFIG_PATH = $ServerConfigOverridePath
}
$ServerProc = Start-Process -FilePath $GameServerExe -PassThru -WorkingDirectory $ExeWorkingDir `
    -RedirectStandardOutput (Join-Path $ResultsDir "gameserver_stdout.log") `
    -RedirectStandardError  (Join-Path $ResultsDir "gameserver_stderr.log")
if ($ServerConfigOverridePath) {
    Remove-Item Env:\LADELTA_CONFIG_PATH -ErrorAction SilentlyContinue
}

Start-Sleep -Seconds 2  # let it bind the socket + spin up worker threads
if ($ServerProc.HasExited) {
    throw "GameServer.exe exited immediately (exit code $($ServerProc.ExitCode)). Check $ResultsDir\gameserver_stderr.log"
}

# --- 텔레메트리 수집기 ------------------------------------------------------
# -ServerTelemetryPort을 줬을 때만 시작. 전용 소켓이라 부하 중에도 spdlog
# 파일처럼 조용히 데이터를 흘리지 않는다(ISSUE-8).
$TelemetryFile = Join-Path $ResultsDir "server_telemetry.jsonl"
$TelemetryProc = $null
if ($ServerTelemetryPort -gt 0) {
    $CollectScript = Join-Path $PSScriptRoot "Collect-Telemetry.ps1"
    $TelemetryMaxSeconds = ($Bursts * $BurstIntervalMs / 1000) + $SoakSeconds + 60
    $TelemetryProc = Start-Process -FilePath "powershell.exe" -PassThru -WindowStyle Hidden `
        -ArgumentList @(
            "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$CollectScript`"",
            "-Port", $ServerTelemetryPort,
            "-OutFile", "`"$TelemetryFile`"",
            "-MaxSeconds", [int]$TelemetryMaxSeconds
        )
    Write-Host "Telemetry collector -> $TelemetryFile (port $ServerTelemetryPort)" -ForegroundColor Yellow
}

$StartTime = Get-Date
$Profile = New-Object System.Collections.Generic.List[object]
$ClientProcs = New-Object System.Collections.Generic.List[object]

function Sample-Server {
    param([string]$Label)
    if ($ServerProc.HasExited) { return }
    $ServerProc.Refresh()
    $Script:Profile.Add([PSCustomObject]@{
        ElapsedSec    = [math]::Round(((Get-Date) - $StartTime).TotalSeconds, 2)
        Label         = $Label
        CpuTotalSec   = [math]::Round($ServerProc.TotalProcessorTime.TotalSeconds, 3)
        WorkingSetMB  = [math]::Round($ServerProc.WorkingSet64 / 1MB, 2)
        HandleCount   = $ServerProc.HandleCount
        ThreadCount   = $ServerProc.Threads.Count
        LiveClients   = ($Script:ClientProcs | Where-Object { -not $_.HasExited }).Count
    })
}

Sample-Server -Label "baseline"

# --- Fire connection bursts ---
for ($b = 1; $b -le $Bursts; $b++) {
    Write-Host "Burst $b/$Bursts : launching $ClientsPerBurst client process(es), $ConnectionsPerProcess connection(s) each..."
    # 서버와 같은 LADELTA_CONFIG_PATH 방식을 클라이언트에도 적용. 실행 전후로
    # 세팅/해제해서 이후 코드가 RemoteServer용 설정을 잘못 물려받지 않게.
    if ($ClientConfigOverridePath) {
        $env:LADELTA_CONFIG_PATH = $ClientConfigOverridePath
    }
    for ($c = 1; $c -le $ClientsPerBurst; $c++) {
        $stdOut = Join-Path $ResultsDir "client_b${b}_c${c}_stdout.log"
        $stdErr = Join-Path $ResultsDir "client_b${b}_c${c}_stderr.log"
        # -ArgumentList는 M24의 RemoteServer.exe <connectionCount> 인자.
        # 기본 1은 인자 없이 실행하는 것과 동일(exe 내부 RemoteCount도 1).
        $p = Start-Process -FilePath $RemoteClientExe -ArgumentList @("$ConnectionsPerProcess") -PassThru -WorkingDirectory $ExeWorkingDir `
            -RedirectStandardOutput $stdOut -RedirectStandardError $stdErr
        $ClientProcs.Add($p) | Out-Null
    }
    if ($ClientConfigOverridePath) {
        Remove-Item Env:\LADELTA_CONFIG_PATH -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds $BurstIntervalMs
    Sample-Server -Label "after_burst_$b"
}

Write-Host "Soaking for $SoakSeconds s with $($ClientProcs.Count) client processes live..."
$soakSamples = 5
for ($i = 1; $i -le $soakSamples; $i++) {
    Start-Sleep -Seconds ([math]::Max(1, [int]($SoakSeconds / $soakSamples)))
    Sample-Server -Label "soak_$i"
}

# --- Collect crash/survival info before teardown ---
$ServerCrashed = $ServerProc.HasExited
$ServerExitCode = if ($ServerCrashed) { $ServerProc.ExitCode } else { $null }

$ClientExitInfo = $ClientProcs | ForEach-Object {
    $_.Refresh()
    if ($_.HasExited) {
        [PSCustomObject]@{ Pid = $_.Id; Exited = $true; ExitCode = $_.ExitCode }
    } else {
        [PSCustomObject]@{ Pid = $_.Id; Exited = $false; ExitCode = $null }
    }
}
$ClientCrashCount = @($ClientExitInfo | Where-Object { $_.Exited -and $_.ExitCode -ne 0 }).Count

# 부하 생성기 자신이 쓴 CPU 시간(초) vs 서버가 쓴 CPU 시간 -- 결과가
# 엔진의 한계를 재는 건지 하네스 자체의 한계를 재는 건지 가려주는 수치.
$ClientCpuSeconds = 0.0
foreach ($p in $ClientProcs) {
    try { $ClientCpuSeconds += $p.TotalProcessorTime.TotalSeconds } catch {}
}
$ServerCpuSeconds = 0.0
try { $ServerCpuSeconds = $ServerProc.TotalProcessorTime.TotalSeconds } catch {}
$RunWallSeconds = ((Get-Date) - $StartTime).TotalSeconds
$LogicalCores = [Environment]::ProcessorCount
$ClientStillAliveCount = @($ClientExitInfo | Where-Object { -not $_.Exited }).Count

# --- Teardown ---
foreach ($p in $ClientProcs) {
    if (-not $p.HasExited) {
        try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
}
Start-Sleep -Milliseconds 300
if (-not $ServerProc.HasExited) {
    try { Stop-Process -Id $ServerProc.Id -Force -ErrorAction SilentlyContinue } catch {}
}

# 수집기는 서버가 소켓을 닫으면 스스로 종료하지만, 혹시 안 그랬으면 여기서
# 정리.
if ($TelemetryProc) {
    Start-Sleep -Milliseconds 500
    if (-not $TelemetryProc.HasExited) {
        try { Stop-Process -Id $TelemetryProc.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
}

$VerifierStops = @()
if ($AppVerifierEnabled) {
    Start-Sleep -Milliseconds 500
    try {
        $VerifierStops = @(Get-WinEvent -FilterHashtable @{ LogName = "Application"; StartTime = $EventLogCutoff } -ErrorAction SilentlyContinue |
            Where-Object { $_.Message -match "GameServer" -or $_.ProviderName -match "Verifier|Application Error" } |
            Select-Object TimeCreated, Id, ProviderName, @{N="Message";E={$_.Message.Substring(0, [Math]::Min(500,$_.Message.Length))}})
    } catch {
        Write-Warning "Could not query event log for Verifier stops: $_"
    }
    & appverif.exe -disable * -for GameServer.exe | Out-Null
    Write-Host "Application Verifier disabled for GameServer.exe." -ForegroundColor Yellow
}

# --- Parse logs ---
# "3-way handshake end" is logged by the *client* side (UdpConnectionProcessor
# on RemoteServer.exe), not the server -- see POST_MORTEM.md's own example,
# which is from Logs/Remote/. The server log is used separately below for
# PacketSimulator trace lines, which are logged wherever simulation is
# enabled (the server, in -EnablePacketSim runs).
#
# 중요: 이 클라이언트 쪽 집계는 구조적으로 과소보고하고, 엔진이 빠를수록
# 더 그렇다. 원인 둘 다 ServerLog::Init(AyncyLog.h)에 있음:
#   1. RemoteServer.exe 프로세스들이 전부 같은 Logs/Remote 파일에 씀 --
#      daily_file_sink_mt는 프로세스 하나 안에서만 스레드 안전, 프로세스
#      간 락은 없어서 20개+ writer가 뒤섞인다.
#   2. 비동기 로거가 async_overflow_policy::overrun_oldest, 큐 10,000개 --
#      핸드셰이크가 몰리면 큐가 넘쳐 오래된 줄부터 조용히 버려진다(여기서
#      세는 "3-way handshake end" 마커도 포함).
# 실측 확인: 여기서 9/20으로 나온 실행도 서버 쪽엔 20개 전부
# "[M13][Login] ... result=VERIFIED"가 찍혀있었다. 아래
# ServerVerifiedConnections를 신뢰할 수치로, 이건 하한으로 취급.
#
# 이후 ServerLog::Init이 로그 파일명을 network_log_pid<PID>_<date>.txt로
# 바꿔서(ISSUE-8 엔진 쪽 수정) Logs/Remote가 클라이언트 프로세스마다 파일
# 하나씩으로 나뉜다 -- 그래서 전부 다 집계해야 한다(최신 파일 하나만 보면
# 클라이언트 하나 분량만 세게 됨).
#
# 이걸로 프로세스 간 뒤섞임 원인은 없어졌지만 overrun_oldest 원인은 여전 --
# 클라이언트 하나의 버스트만으로도 큐 1만 개가 넘칠 수 있다.
# ServerVerifiedConnections가 여전히 신뢰할 수치.
$HandshakeCount = 0
$HandshakeTimestamps = @()
$RemoteLogFiles = @(Get-ChildItem $RemoteLogDir -Filter "network_log_*.txt" -ErrorAction SilentlyContinue)

if ($RemoteLogFiles.Count -gt 0) {
    $lines = @(Select-String -Path $RemoteLogFiles.FullName -Pattern "3-way handshake end")
    $HandshakeCount = $lines.Count
    foreach ($l in $lines) {
        if ($l.Line -match '^\[(?<ts>[\d\-]+\s[\d:.]+)\]') {
            try { $HandshakeTimestamps += [datetime]::ParseExact($Matches.ts, "yyyy-MM-dd HH:mm:ss.fff", $null) } catch {}
        }
    }
}

$LogFile = Get-ChildItem $ServerLogDir -Filter "network_log_*.txt" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

# 서버 로그 기반의 신뢰할 수 있는 커넥션 수 -- 프로세스 하나만 쓰는 파일이라
# 위 두 손실 원인이 없다. "VERIFIED" 줄은 3-way 핸드셰이크와 자격 검증을 다
# 통과해야만 나오므로, 여기서 DISTINCT ConnectionId 수가 실제 성립 커넥션
# 수다.
$ServerVerifiedConnections = 0
if ($LogFile) {
    $ServerVerifiedConnections = @(
        Select-String -Path $LogFile.FullName -Pattern '\[M13\]\[Login\].*ConnectionId=(?<id>\d+).*result=VERIFIED' |
            ForEach-Object { $_.Matches[0].Groups['id'].Value } |
            Sort-Object -Unique
    ).Count
}

$HandshakeIntervalStats = $null
if ($HandshakeTimestamps.Count -ge 2) {
    $sorted = $HandshakeTimestamps | Sort-Object
    $span = ($sorted[-1] - $sorted[0]).TotalSeconds
    $HandshakeIntervalStats = [PSCustomObject]@{
        FirstHandshake     = $sorted[0]
        LastHandshake      = $sorted[-1]
        SpanSeconds        = [math]::Round($span, 2)
        ConnectsPerSecond  = if ($span -gt 0) { [math]::Round($HandshakeTimestamps.Count / $span, 2) } else { $null }
    }
}

# --- M2: count PacketSimulator trace lines to confirm it actually fired ---
$SimDroppedCount = 0
$SimDuplicatedCount = 0
$SimReorderedCount = 0
if ($EnablePacketSim -and $LogFile) {
    $SimDroppedCount    = @(Select-String -Path $LogFile.FullName -Pattern "\[PacketSimulator\] dropped").Count
    $SimDuplicatedCount = @(Select-String -Path $LogFile.FullName -Pattern "\[PacketSimulator\] duplicated").Count
    $SimReorderedCount  = @(Select-String -Path $LogFile.FullName -Pattern "\[PacketSimulator\] released|\[PacketSimulator\] flushed").Count
}

# --- M9: automated OutUnAckedBunches / OutSeq / ISSUE-1 instrumentation ---
# Previously these three were checked by hand (grep) after every run across
# M5-M8; pulling them into the script makes repeated/matrix runs (M9's
# hardened stress matrix) directly comparable without manual log-diving.
# 경로 배열을 받는다 -- Logs/Remote가 클라이언트 프로세스마다 파일이 나뉘어
# 있어서 전부에서 최댓값을 찾아야 함.
function Get-MaxIntFromLog {
    param([string[]]$Paths, [string]$Pattern)
    $existing = @($Paths | Where-Object { $_ -and (Test-Path $_) })
    if ($existing.Count -eq 0) { return 0 }
    $maxVal = 0
    foreach ($m in (Select-String -Path $existing -Pattern $Pattern -AllMatches)) {
        foreach ($g in $m.Matches) {
            $v = [int64]$g.Groups[1].Value
            if ($v -gt $maxVal) { $maxVal = $v }
        }
    }
    return $maxVal
}

$ServerOutUnAckedBunchesMax = 0
$RemoteOutUnAckedBunchesMax = 0
$RemoteOutSeqMax = 0
if ($LogFile) {
    $ServerOutUnAckedBunchesMax = Get-MaxIntFromLog -Paths $LogFile.FullName -Pattern 'OutUnAckedBunches\.size=(\d+)'
}
if ($RemoteLogFiles.Count -gt 0) {
    $RemoteOutUnAckedBunchesMax = Get-MaxIntFromLog -Paths $RemoteLogFiles.FullName -Pattern 'OutUnAckedBunches\.size=(\d+)'
    $RemoteOutSeqMax            = Get-MaxIntFromLog -Paths $RemoteLogFiles.FullName -Pattern 'OutSeq: (\d+)'
}

# ISSUE-1 (ChannelRecord.h:39 assert()) writes "Assertion failed: ..." to
# stderr before the process hangs on a modal CRT dialog -- so a non-empty
# client_*_stderr.log is the reliable signal, not the exit code (a hung
# process reports Exited=False, not a crash).
# 프로세스 하나가 멎으면(assert) $ConnectionsPerProcess개 커넥션 분량의
# 부하가 같이 사라진다 -- 프로세스 하나 = CRT 모달 다이얼로그 하나 = 그
# 프로세스 전체 정지. $Issue1AssertCount 자체는 여전히 프로세스 수(stderr
# 파일 검사가 그렇게 셈), ConnectionsLostToIssue1이 실제 부하 손실로 환산.
$Issue1AssertCount = @(Get-ChildItem $ResultsDir -Filter "client_*_stderr.log" -ErrorAction SilentlyContinue | Where-Object { $_.Length -gt 0 }).Count
$ConnectionsLostToIssue1 = $Issue1AssertCount * $ConnectionsPerProcess

# 이제 프로세스 수가 아니라 커넥션 수다 -- 아래에서 이 값을 읽는 곳(성립률
# 필터, summary.json, 콘솔 출력)이 전부 커넥션 수 기준이라 이 한 줄만
# 고치면 나머지가 다 맞다. 프로세스 수는 $ClientProcs.Count로 따로 있음.
$TotalClientsLaunched = $Bursts * $ClientsPerBurst * $ConnectionsPerProcess

# --- 텔레메트리 스트림 집계 --------------------------------------------------
# 평균이 아니라 안정 구간(steady-state) 중앙값을 낸다 -- 램프업(커넥션이
# 아직 붙는 중)과 종료 구간까지 평균에 섞으면 실제 유지된 처리량을 낮게
# 보이게 한다.
$TelemetrySummary = $null
if ($ServerTelemetryPort -gt 0 -and (Test-Path $TelemetryFile)) {
    $StatEvents = @(
        Get-Content $TelemetryFile -ErrorAction SilentlyContinue | ForEach-Object {
            try { $o = $_ | ConvertFrom-Json } catch { return }
            if ($o.type -eq "stats") { $o }
        }
    )

    if ($StatEvents.Count -gt 0) {
        function Get-Median {
            param([double[]]$Values)
            if ($Values.Count -eq 0) { return 0 }
            $s = @($Values | Sort-Object)
            $mid = [int][math]::Floor($s.Count / 2)
            if ($s.Count % 2 -eq 1) { return $s[$mid] }
            return ($s[$mid - 1] + $s[$mid]) / 2
        }

        # 클라이언트가 다 붙기 전(램프업 구간) 샘플은 제외.
        $SteadyEvents = @($StatEvents | Where-Object { $_.connections -ge $TotalClientsLaunched })
        if ($SteadyEvents.Count -eq 0) { $SteadyEvents = $StatEvents }

        # 워커별 job 처리율 -- 워커 간 분포를 보기 위함.
        #
        # CPU는 job과 다르게 처리한다: Windows는 스레드 CPU 시간을 스케줄러
        # 틱(15.625ms) 단위로 끊어서 주기 때문에, 초당 샘플 하나는 0이거나
        # 15625us뿐이지 실제 값이 아니다(실측 확인: cpuTimeUs가 전부
        # 15625의 배수). 그래서 CPU는 안정 구간 전체의 누적 카운터로
        # 계산해 양자화 오차를 상쇄시키고, job은(정확한 카운터라) 샘플별로
        # 그대로 쓴다.
        $WorkerTotals = @{}
        foreach ($e in $SteadyEvents) {
            foreach ($w in $e.workers) {
                if (-not $WorkerTotals.ContainsKey($w.worker)) {
                    $WorkerTotals[$w.worker] = [PSCustomObject]@{
                        Worker = $w.worker; OsThreadId = $w.osThreadId
                        JobRates = New-Object System.Collections.Generic.List[double]
                        FirstCpuUs = [double]$w.cpuTimeUs
                        LastCpuUs = [double]$w.cpuTimeUs
                    }
                }
                $WorkerTotals[$w.worker].JobRates.Add([double]$w.jobsPerSec)
                $WorkerTotals[$w.worker].LastCpuUs = [double]$w.cpuTimeUs
            }
        }

        # 안정 구간의 실제 경과 시간(sink 자체 타임스탬프 기준).
        $SteadySpanSec = 0
        if ($SteadyEvents.Count -ge 2) {
            $SteadySpanSec = ([double]$SteadyEvents[-1].t - [double]$SteadyEvents[0].t) / 1000.0
        }

        $TelemetrySummary = [PSCustomObject]@{
            SampleCount            = $StatEvents.Count
            SteadySampleCount      = $SteadyEvents.Count
            MedianDatagramsRecvPerSec = [math]::Round((Get-Median ([double[]]($SteadyEvents | ForEach-Object { $_.datagramsRecvPerSec }))), 1)
            MedianDatagramsSentPerSec = [math]::Round((Get-Median ([double[]]($SteadyEvents | ForEach-Object { $_.datagramsSentPerSec }))), 1)
            MedianJobsPerSec       = [math]::Round((Get-Median ([double[]]($SteadyEvents | ForEach-Object { $_.jobsPerSec }))), 1)
            MedianIoCompletionsPerSec = [math]::Round((Get-Median ([double[]]($SteadyEvents | ForEach-Object { $_.ioCompletionsPerSec }))), 1)
            MedianIoTimeoutsPerSec = [math]::Round((Get-Median ([double[]]($SteadyEvents | ForEach-Object { $_.ioTimeoutsPerSec }))), 1)
            MaxConnections         = ($StatEvents | Measure-Object -Property connections -Maximum).Maximum
            SteadySpanSec          = [math]::Round($SteadySpanSec, 1)
            Workers                = @(
                $WorkerTotals.Keys | Sort-Object | ForEach-Object {
                    $w = $WorkerTotals[$_]
                    $CpuCores = 0
                    if ($SteadySpanSec -gt 0) {
                        $CpuCores = (($w.LastCpuUs - $w.FirstCpuUs) / 1000000.0) / $SteadySpanSec
                    }
                    [PSCustomObject]@{
                        Worker            = $w.Worker
                        OsThreadId        = $w.OsThreadId
                        MedianJobsPerSec  = [math]::Round((Get-Median ([double[]]$w.JobRates)), 1)
                        # 안정 구간 동안 이 워커가 쓴 코어 1개 대비 비율.
                        # 1.0 = 포화.
                        CpuCores          = [math]::Round($CpuCores, 4)
                    }
                }
            )
            TotalCpuCores          = [math]::Round((@($WorkerTotals.Keys | ForEach-Object {
                $w = $WorkerTotals[$_]
                if ($SteadySpanSec -gt 0) { (($w.LastCpuUs - $w.FirstCpuUs) / 1000000.0) / $SteadySpanSec } else { 0 }
            }) | Measure-Object -Sum).Sum, 4)
        }
    }
}

# 커넥션별 RTT/레이턴시 percentile -- 위 드라이버 전역 "stats"와 달리 각
# 커넥션이 초당 자기만의 "gauge" 이벤트를 낸다. 전체 샘플을 한 리스트로
# 합쳐서 중앙값 내지 않고, 커넥션별 중앙값의 중앙값(median-of-medians)을
# 쓴다 -- 유난히 바쁘거나 한가한 커넥션 몇 개가 전체 수치를 왜곡하지
# 않도록.
$RttSummary = $null
if ($ServerTelemetryPort -gt 0 -and (Test-Path $TelemetryFile)) {
    $GaugeEvents = @(
        Get-Content $TelemetryFile -ErrorAction SilentlyContinue | ForEach-Object {
            try { $o = $_ | ConvertFrom-Json } catch { return }
            if ($o.type -eq "gauge" -and $null -ne $o.rttSampleCount -and $o.rttSampleCount -gt 0) { $o }
        }
    )

    if ($GaugeEvents.Count -gt 0) {
        if (-not (Get-Command Get-Median -ErrorAction SilentlyContinue)) {
            function Get-Median {
                param([double[]]$Values)
                if ($Values.Count -eq 0) { return 0 }
                $s = @($Values | Sort-Object)
                $mid = [int][math]::Floor($s.Count / 2)
                if ($s.Count % 2 -eq 1) { return $s[$mid] }
                return ($s[$mid - 1] + $s[$mid]) / 2
            }
        }

        # 커넥션별로 각 percentile의 중앙값을 낸 뒤, 그걸 커넥션 간에 다시
        # 중앙값.
        # @(...)로 배열을 강제 -- Group-Object가 그룹이 하나뿐이면 배열이
        # 아니라 GroupInfo 객체 하나만 반환해서, .Count가 "그룹 몇 개"가
        # 아니라 "그 그룹 안 항목 몇 개"로 읽힌다(실측: 진짜 1커넥션 실행이
        # ConnectionCount=15로 나온 적 있음).
        $ByConnection = @($GaugeEvents | Group-Object connectionId)
        $PerConnMedianP50 = @($ByConnection | ForEach-Object { Get-Median ([double[]]($_.Group | ForEach-Object { $_.rttP50Us })) })
        $PerConnMedianP95 = @($ByConnection | ForEach-Object { Get-Median ([double[]]($_.Group | ForEach-Object { $_.rttP95Us })) })
        $PerConnMedianP99 = @($ByConnection | ForEach-Object { Get-Median ([double[]]($_.Group | ForEach-Object { $_.rttP99Us })) })

        $RttSummary = [PSCustomObject]@{
            # 참고: 이건 전체 커넥션의 초당 gauge 스냅샷 개수지, RTT 측정
            # 개수가 아니다 -- gauge 이벤트마다 rttSampleCount 자체가 이미
            # 링버퍼 최대 256개를 반영(여러 스냅샷에 걸쳐 유지됨)해서, 이걸
            # 이벤트마다 합치면 같은 샘플을 중복해서 센다.
            GaugeSnapshotCount = $GaugeEvents.Count
            ConnectionCount    = $ByConnection.Count
            MedianP50Us        = [math]::Round((Get-Median ([double[]]$PerConnMedianP50)), 1)
            MedianP95Us        = [math]::Round((Get-Median ([double[]]$PerConnMedianP95)), 1)
            MedianP99Us        = [math]::Round((Get-Median ([double[]]$PerConnMedianP99)), 1)
            MinUs              = ($GaugeEvents | Measure-Object -Property rttMinUs -Minimum).Minimum
            MaxUs              = ($GaugeEvents | Measure-Object -Property rttMaxUs -Maximum).Maximum
        }
    }
}

# 어느 커넥션 수를 믿을지.
#
# ServerVerifiedConnections는 spdlog::info 줄 파싱이라 기본 trace 레벨에선
# 되지만, 부하 테스트가 꼭 필요로 하는 -LogLevel warn에서는 0으로 읽힌다
# (로거 자체가 측정 대상의 병목이 되는 걸 막으려면 낮춰야 함) -- 실측으로도
# 20커넥션/1,308datagrams초 실행이 0/20으로 나온 적 있다.
#
# TelemetrySink의 `connections` 게이지는 로그와 무관하니 텔레메트리가 켜져
# 있으면 이쪽을 우선한다 -- ISSUE-8과 같은 교훈: 엔진 자신의 로그 파일로
# 엔진을 측정하지 않는다.
$LogMetricsBlinded = ($LogLevel -ne "" -and $LogLevel -notin @("trace", "debug", "info"))
$AuthoritativeConnections = $ServerVerifiedConnections
$ConnectionSource = "server log"
if ($TelemetrySummary -and $null -ne $TelemetrySummary.MaxConnections) {
    $AuthoritativeConnections = [int]$TelemetrySummary.MaxConnections
    $ConnectionSource = "telemetry"
}

$Summary = [PSCustomObject]@{
    Timestamp              = $Timestamp
    Parameters             = [PSCustomObject]@{ Bursts=$Bursts; ClientsPerBurst=$ClientsPerBurst; BurstIntervalMs=$BurstIntervalMs; SoakSeconds=$SoakSeconds; AppVerifier=$AppVerifierEnabled; PacketSim=$EnablePacketSim }
    TotalClientsLaunched   = $TotalClientsLaunched
    ServerCrashed          = $ServerCrashed
    ServerExitCode         = $ServerExitCode
    ClientCrashCount       = $ClientCrashCount
    ClientStillAliveAtEnd  = $ClientStillAliveCount
    HandshakesObservedInLog= $HandshakeCount
    ServerVerifiedConnections = $ServerVerifiedConnections
    AuthoritativeConnections = $AuthoritativeConnections
    ConnectionSource       = $ConnectionSource
    LogMetricsBlinded      = $LogMetricsBlinded
    HandshakeStats         = $HandshakeIntervalStats
    PacketSimStats         = if ($EnablePacketSim) { [PSCustomObject]@{ Dropped=$SimDroppedCount; Duplicated=$SimDuplicatedCount; Reordered=$SimReorderedCount } } else { $null }
    OutUnAckedBunchesMax   = [PSCustomObject]@{ Server=$ServerOutUnAckedBunchesMax; Remote=$RemoteOutUnAckedBunchesMax }
    OutSeqMaxRemote        = $RemoteOutSeqMax
    Issue1AssertCount      = $Issue1AssertCount
    ConnectionsLostToIssue1 = $ConnectionsLostToIssue1
    ConnectionsPerProcess  = $ConnectionsPerProcess
    ProcessesLaunched      = $ClientProcs.Count
    LoadKnobs              = [PSCustomObject]@{
        ClientSyntheticTrafficHz = $ClientSyntheticTrafficHz
        ClientMaxTickRate        = $ClientMaxTickRate
        ServerSyntheticTrafficHz = $ServerSyntheticTrafficHz
        ServerMaxTickRate        = $ServerMaxTickRate
        LogLevel                 = $LogLevel
        JobSystemWorkers         = $env:NET_JOBSYSTEM_WORKERS
        IoThreads                = $env:NET_IO_THREADS
    }
    Telemetry              = $TelemetrySummary
    RttSummary             = $RttSummary
    LoadGeneratorCost      = [PSCustomObject]@{
        ClientCpuSeconds  = [math]::Round($ClientCpuSeconds, 1)
        ServerCpuSeconds  = [math]::Round($ServerCpuSeconds, 1)
        RunWallSeconds    = [math]::Round($RunWallSeconds, 1)
        LogicalCores      = $LogicalCores
        ClientCores       = [math]::Round($ClientCpuSeconds / [math]::Max($RunWallSeconds, 1), 2)
        ServerCores       = [math]::Round($ServerCpuSeconds / [math]::Max($RunWallSeconds, 1), 2)
        MachineUtilisation = [math]::Round((($ClientCpuSeconds + $ServerCpuSeconds) / [math]::Max($RunWallSeconds, 1)) / $LogicalCores, 3)
    }
    AppVerifierStops       = $VerifierStops
    Verdict                = if ($ServerCrashed -or $ClientCrashCount -gt 0 -or $Issue1AssertCount -gt 0 -or $VerifierStops.Count -gt 0) { "FAIL" } else { "PASS" }
}

$Summary | ConvertTo-Json -Depth 6 | Out-File (Join-Path $ResultsDir "summary.json") -Encoding utf8
$Profile | Export-Csv (Join-Path $ResultsDir "server_profile.csv") -NoTypeInformation
$ClientExitInfo | Export-Csv (Join-Path $ResultsDir "client_exit_info.csv") -NoTypeInformation

Write-Host ""
Write-Host "=== RESULT: $($Summary.Verdict) ===" -ForegroundColor $(if ($Summary.Verdict -eq "PASS") { "Green" } else { "Red" })
# $TotalClientsLaunched는 커넥션 수 -- 프로세스 수와 다를 때는 따로 표시해서
# "500개 연결"을 프로세스 500개로 착각하지 않게.
if ($ConnectionsPerProcess -gt 1) {
    Write-Host "Connections targeted:    $TotalClientsLaunched  ($($ClientProcs.Count) processes x $ConnectionsPerProcess conn/process)"
} else {
    Write-Host "Clients launched:        $TotalClientsLaunched"
}
Write-Host "Connections established: $AuthoritativeConnections / $TotalClientsLaunched  ($ConnectionSource -- authoritative)" -ForegroundColor Cyan
if ($LogMetricsBlinded) {
    Write-Host "NOTE: LogLevel='$LogLevel' suppresses the info/debug lines the log-derived" -ForegroundColor DarkYellow
    Write-Host "      metrics below are parsed from -- they read 0 by construction, not by failure." -ForegroundColor DarkYellow
}
Write-Host "  server log count:      $ServerVerifiedConnections"
Write-Host "Handshakes in log:       $HandshakeCount  (client log -- lossy lower bound, see script comment)"
if ($HandshakeIntervalStats) {
    Write-Host "Connects/sec (approx):   $($HandshakeIntervalStats.ConnectsPerSecond)"
}
Write-Host "Server crashed:          $ServerCrashed $(if($ServerCrashed){"(exit $ServerExitCode)"})"
# 크래시/assert 카운트는 원래 프로세스 단위(stderr 파일도, CRT 다이얼로그도
# 프로세스당 하나) -- 분모는 $ClientProcs.Count, 커넥션 수인
# $TotalClientsLaunched가 아니다.
Write-Host "Client crashes:          $ClientCrashCount / $($ClientProcs.Count) processes"
Write-Host "Verifier stops:          $($VerifierStops.Count)"
if ($EnablePacketSim) {
    Write-Host "Packet sim (drop/dup/reorder events): $SimDroppedCount / $SimDuplicatedCount / $SimReorderedCount"
}
Write-Host "OutUnAckedBunches max (server/remote): $ServerOutUnAckedBunchesMax / $RemoteOutUnAckedBunchesMax"
Write-Host "OutSeq max (remote):     $RemoteOutSeqMax"
Write-Host "ISSUE-1 asserts:         $Issue1AssertCount / $($ClientProcs.Count) processes"
if ($ConnectionsPerProcess -gt 1) {
    Write-Host "  -> connections lost:   $ConnectionsLostToIssue1 / $TotalClientsLaunched ($ConnectionsPerProcess conn/process)"
}
if ($TelemetrySummary) {
    Write-Host ""
    Write-Host "--- Telemetry (server, steady-state medians over $($TelemetrySummary.SteadySampleCount) samples) ---" -ForegroundColor Cyan
    Write-Host "Datagrams recv/sent per sec: $($TelemetrySummary.MedianDatagramsRecvPerSec) / $($TelemetrySummary.MedianDatagramsSentPerSec)"
    Write-Host "Jobs per sec:                $($TelemetrySummary.MedianJobsPerSec)"
    Write-Host "IO completions / timeouts:   $($TelemetrySummary.MedianIoCompletionsPerSec) / $($TelemetrySummary.MedianIoTimeoutsPerSec) per sec"
    Write-Host "Worker CPU total:            $($TelemetrySummary.TotalCpuCores) cores (over $($TelemetrySummary.SteadySpanSec)s)"
    foreach ($w in $TelemetrySummary.Workers) {
        Write-Host ("  w{0} (tid={1}): {2} jobs/s, {3} cores" -f $w.Worker, $w.OsThreadId, $w.MedianJobsPerSec, $w.CpuCores)
    }
}
if ($RttSummary) {
    Write-Host ""
    Write-Host ("--- RTT/latency (server, median-of-per-connection-medians over {0} connections) ---" -f $RttSummary.ConnectionCount) -ForegroundColor Cyan
    Write-Host ("p50/p95/p99: {0} / {1} / {2} us   (min={3}us max={4}us, {5} gauge snapshots)" -f `
        $RttSummary.MedianP50Us, $RttSummary.MedianP95Us, $RttSummary.MedianP99Us, `
        $RttSummary.MinUs, $RttSummary.MaxUs, $RttSummary.GaugeSnapshotCount)
}
Write-Host ""
Write-Host "--- Load generator cost ---" -ForegroundColor Cyan
Write-Host ("Clients: {0} cores   Server: {1} cores   Machine: {2}% of {3} logical cores" -f `
    [math]::Round($ClientCpuSeconds / [math]::Max($RunWallSeconds,1), 2), `
    [math]::Round($ServerCpuSeconds / [math]::Max($RunWallSeconds,1), 2), `
    [math]::Round(100 * (($ClientCpuSeconds + $ServerCpuSeconds) / [math]::Max($RunWallSeconds,1)) / $LogicalCores, 1), `
    $LogicalCores)
Write-Host "Full results:            $ResultsDir"
