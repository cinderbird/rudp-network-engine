<#
.SYNOPSIS
  IOCP 스레드 수 스케일링 곡선 -- M25 실험 B(1회씩만 돌려 결론 못 냄)를
  반복 측정으로 다시 시도.

.DESCRIPTION
  JobSystem 워커 수와 부하는 고정하고 NET_IO_THREADS만 스윕(재빌드 불필요,
  NetDriver.cpp의 ResolveIoThreadCountForTelemetry 참고), 각 값에서 jobs/s,
  IO 완료/타임아웃, 커넥션 성립률을 기록한다.

  M25 실험 B(docs/decisions/0008-server-iocp-default.md 참고)는 같은 조합(750커넥션/
  150Hz, 워커 8, IO 스레드 1/2/4)을 값당 딱 1회만 돌렸는데, 성립 커넥션 수가
  577~742/750으로 크게 흔들려 jobs/s를 서로 비교할 수 없었다 -- "결론 낼 수
  없음"으로 끝났고, 반복을 늘려 재설계하는 게 다음 단계로 명시됐다. 이
  스크립트가 그 재설계다: 반복 3회(기본값), 성립률(EstablishedFraction)을
  CSV에 눈으로 찾아야 하는 값이 아니라 별도 열로 기록, 그룹 중앙값 대비
  튀는 rep은 즉시 경고.

  본 스윕 전에 NET_IO_THREADS=1로 사전 확인 1회 -- 이 부하가 IO 스레드
  하나라도 실제로 포화시키는지 확인(MedianIoTimeoutsPerSec이 0에 가까우면
  포화, IpNetDriver.cpp의 PumpIO 주석 참고). M22 시절 하네스는 이 질문
  자체에 도달을 못 했었다 -- 이 확인을 생략하면 같은 실수를 반복할 위험.

  참고: TickLoop 스레드 수는 이 스크립트의 매개변수가 아니다 -- 항상 1개로
  고정(ThreadManager::Run(), M3/M5의 중복 틱 버그가 이유). "최적값"을 물을
  수 있는 건 JobWorker(M25 실험 A가 이미 답함: 8이 최고)와 IOCP(이 스크립트)
  뿐이다.

.PARAMETER IoThreads
  스윕할 NET_IO_THREADS 값들.

.PARAMETER Connections
  모든 지점에 공통으로 쓰는 고정 커넥션 수. 기본 750(M25 실험 A/B와 동일해
  직접 비교 가능).

.PARAMETER RatePerConnHz
  커넥션당 SyntheticTrafficHz = MaxTickRate. 기본 150(M25와 동일).

.PARAMETER FixedWorkers
  스윕 내내 고정할 NET_JOBSYSTEM_WORKERS. 기본 8(M25 실험 A의 최고 jobs/s
  지점) -- JobWorker 용량이 병목이 되지 않게.

.PARAMETER Repetitions
  IO 스레드 값당 반복 횟수. 기본 3(M25 실험 B는 1이라 결론을 못 냈다).

.PARAMETER SoakSeconds
  회당 소크 시간.

.PARAMETER ConnectionsPerProcess
  M24/M25의 RemoteServer.exe <N> 프로세스당 커넥션 수. 기본 125.

.PARAMETER ClientsPerBurst
  버스트당 프로세스 수. ConnectionsPerProcess와 곱해 Connections를 나눠
  떨어뜨려야 함. 기본 6(6×125=750, 버스트 1회).

.PARAMETER SkipSanityCheck
  사전 확인(NET_IO_THREADS=1 포화 검사)을 건너뛰고 바로 본 스윕 실행.

.EXAMPLE
  .\Run-IoThreadScaling.ps1 -IoThreads 1,2,4 -Connections 750 -RatePerConnHz 150 -FixedWorkers 8 -Repetitions 3
#>
param(
    [int[]]$IoThreads = @(1, 2, 4),
    [int]$Connections = 750,
    [int]$RatePerConnHz = 150,
    [int]$FixedWorkers = 8,
    [int]$Repetitions = 3,
    [int]$SoakSeconds = 25,
    [int]$ConnectionsPerProcess = 125,
    [int]$ClientsPerBurst = 6,
    [switch]$SkipSanityCheck
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$RunScript = Join-Path $ScriptDir "Run-ConnectionStressTest.ps1"

$ConnectionsPerBurst = $ClientsPerBurst * $ConnectionsPerProcess
if ($Connections % $ConnectionsPerBurst -ne 0) {
    throw "Connections must be a multiple of ClientsPerBurst x ConnectionsPerProcess ($ConnectionsPerBurst)."
}
$Bursts = $Connections / $ConnectionsPerBurst

$ResultsRoot = Join-Path $ScriptDir "results"
New-Item -ItemType Directory -Force -Path $ResultsRoot | Out-Null

function Invoke-OneRun {
    param([int]$T, [int]$Rep, [int]$Port)

    $env:NET_IO_THREADS = "$T"
    $env:NET_JOBSYSTEM_WORKERS = "$FixedWorkers"

    & $RunScript -Configuration Release `
        -Bursts $Bursts -ClientsPerBurst $ClientsPerBurst -ConnectionsPerProcess $ConnectionsPerProcess -BurstIntervalMs 300 -SoakSeconds $SoakSeconds `
        -ServerTelemetryPort $Port `
        -ClientSyntheticTrafficHz $RatePerConnHz -ClientMaxTickRate $RatePerConnHz `
        -ServerSyntheticTrafficHz $RatePerConnHz -ServerMaxTickRate $RatePerConnHz `
        -LogLevel "warn" *>&1 |
        Where-Object { $_ -is [string] } | Out-Null

    $LatestDir = Get-ChildItem $ResultsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d{8}_\d{6}$' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $LatestDir) { Write-Warning "No results dir found for io_threads=$T rep=$Rep"; return $null }

    $SummaryPath = Join-Path $LatestDir.FullName "summary.json"
    if (-not (Test-Path $SummaryPath)) { Write-Warning "No summary.json in $($LatestDir.FullName)"; return $null }
    return Get-Content $SummaryPath -Raw | ConvertFrom-Json
}

# --- 사전 확인 -----------------------------------------------------------
# 이 부하 수준(750커넥션/150Hz 등)이 IO 스레드 하나를 실제로 포화시키는지
# 본 스윕 전에 먼저 확인.
if (-not $SkipSanityCheck) {
    Write-Host "=== Sanity check: does $Connections conn / ${RatePerConnHz}Hz stress a single IO thread? ===" -ForegroundColor Yellow
    $SanityPort = 16000
    $S0 = Invoke-OneRun -T 1 -Rep 0 -Port $SanityPort
    Remove-Item Env:\NET_IO_THREADS -ErrorAction SilentlyContinue
    Remove-Item Env:\NET_JOBSYSTEM_WORKERS -ErrorAction SilentlyContinue

    if ($S0 -and $S0.Telemetry) {
        $Timeouts = $S0.Telemetry.MedianIoTimeoutsPerSec
        $Established = $S0.AuthoritativeConnections
        Write-Host ("  io_threads=1: established={0}/{1} jobs/s={2} MedianIoTimeoutsPerSec={3}" -f `
            $Established, $S0.TotalClientsLaunched, $S0.Telemetry.MedianJobsPerSec, $Timeouts)
        if ($Timeouts -lt 5) {
            Write-Warning "IO timeouts/sec is near zero at IO_THREADS=1 -- this load level already saturates a single IO thread. Any effect the main sweep finds is meaningful, not noise."
        } else {
            Write-Warning "IO timeouts/sec is $Timeouts (not near zero) at IO_THREADS=1 -- this load level does NOT appear to stress a single IO thread. If the main sweep shows no effect, that may just mean the load wasn't high enough to reach the IO ceiling, not that more IO threads never help -- consider raising -Connections/-RatePerConnHz."
        }
    } else {
        Write-Warning "Sanity pass produced no usable telemetry -- proceeding to main sweep anyway, but interpret results cautiously."
    }
    Write-Host ""
}

# --- 본 스윕 ---------------------------------------------------------------
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutCsv = Join-Path $ResultsRoot "io_thread_scaling_$Timestamp.csv"
"IoThreads,Repetition,ConnectionsEstablished,TotalConnections,EstablishedFraction,JobsPerSec,DatagramsRecvPerSec,IoCompletionsPerSec,IoTimeoutsPerSec,TotalCpuCores" |
    Out-File -FilePath $OutCsv -Encoding utf8

$AllRows = @()
$TelemetryPortBase = 17000

foreach ($T in $IoThreads) {
    $Fractions = @()
    for ($Rep = 1; $Rep -le $Repetitions; $Rep++) {
        $Port = $TelemetryPortBase + ($T * 100) + $Rep
        Write-Host "=== io_threads=$T rep=$Rep/$Repetitions ===" -ForegroundColor Cyan

        $S = Invoke-OneRun -T $T -Rep $Rep -Port $Port
        if (-not $S) { continue }

        $Established = $S.AuthoritativeConnections
        $Total = $S.TotalClientsLaunched
        $Fraction = if ($Total -gt 0) { $Established / $Total } else { 0 }
        $Fractions += $Fraction

        $T2 = $S.Telemetry
        $Row = [PSCustomObject]@{
            IoThreads              = $T
            Repetition             = $Rep
            ConnectionsEstablished = $Established
            TotalConnections       = $Total
            EstablishedFraction    = [math]::Round($Fraction, 3)
            JobsPerSec             = if ($T2) { $T2.MedianJobsPerSec } else { $null }
            DatagramsRecvPerSec    = if ($T2) { $T2.MedianDatagramsRecvPerSec } else { $null }
            IoCompletionsPerSec    = if ($T2) { $T2.MedianIoCompletionsPerSec } else { $null }
            IoTimeoutsPerSec       = if ($T2) { $T2.MedianIoTimeoutsPerSec } else { $null }
            TotalCpuCores          = if ($T2) { $T2.TotalCpuCores } else { $null }
        }
        $AllRows += $Row

        "$T,$Rep,$Established,$Total,$($Row.EstablishedFraction),$($Row.JobsPerSec),$($Row.DatagramsRecvPerSec),$($Row.IoCompletionsPerSec),$($Row.IoTimeoutsPerSec),$($Row.TotalCpuCores)" |
            Out-File -FilePath $OutCsv -Append -Encoding utf8

        Write-Host ("  established={0}/{1} ({2:P0}) jobs/s={3} io_completions/s={4} io_timeouts/s={5}" -f `
            $Established, $Total, $Fraction, $Row.JobsPerSec, $Row.IoCompletionsPerSec, $Row.IoTimeoutsPerSec)
    }

    # 그룹 중앙값보다 성립률이 많이 낮은 rep을 즉시 경고 -- M25 실험 B를
    # 망친 바로 그 교란을 나중이 아니라 바로 표시.
    if ($Fractions.Count -gt 0) {
        $MedFraction = ($Fractions | Sort-Object)[[int]([math]::Floor($Fractions.Count / 2))]
        for ($i = 0; $i -lt $Fractions.Count; $i++) {
            if (($MedFraction - $Fractions[$i]) -gt 0.05) {
                Write-Warning ("io_threads=$T rep=$($i+1): established fraction {0:P0} is more than 5pp below this group's median {1:P0} -- likely confounded, treat with suspicion." -f $Fractions[$i], $MedFraction)
            }
        }
    }

    Remove-Item Env:\NET_IO_THREADS -ErrorAction SilentlyContinue
    Remove-Item Env:\NET_JOBSYSTEM_WORKERS -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "=== IO thread scaling summary (median across repetitions, fixed workers=$FixedWorkers) ===" -ForegroundColor Cyan
$AllRows | Group-Object IoThreads | Sort-Object { [int]$_.Name } | ForEach-Object {
    $jobsVals    = @($_.Group | ForEach-Object { $_.JobsPerSec } | Where-Object { $_ -ne $null })
    $timeoutVals = @($_.Group | ForEach-Object { $_.IoTimeoutsPerSec } | Where-Object { $_ -ne $null })
    $fracVals    = @($_.Group | ForEach-Object { $_.EstablishedFraction })
    $jobsMed    = if ($jobsVals.Count -gt 0)    { ($jobsVals    | Sort-Object)[[int]([math]::Floor($jobsVals.Count / 2))] }    else { $null }
    $timeoutMed = if ($timeoutVals.Count -gt 0) { ($timeoutVals | Sort-Object)[[int]([math]::Floor($timeoutVals.Count / 2))] } else { $null }
    $fracRange  = if ($fracVals.Count -gt 0) { "{0:P0}-{1:P0}" -f ($fracVals | Measure-Object -Minimum).Minimum, ($fracVals | Measure-Object -Maximum).Maximum } else { "n/a" }
    Write-Host ("io_threads={0,-3} established_fraction_range={1,-9} jobs/s={2,-10:N1}  io_timeouts/s={3:N1}" -f `
        $_.Name, $fracRange, $jobsMed, $timeoutMed)
}

Write-Host ""
Write-Host "CSV: $OutCsv"
