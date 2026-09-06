<#
.SYNOPSIS
  M16: busy-spin vs CV-wait JobSystem scheduler comparison, across a range
  of connection-load levels, repeated multiple times per (scheduler, load)
  combination for chartable data.

.DESCRIPTION
  Drives Run-ConnectionStressTest.ps1 once per (Scheduler x ClientCount x
  Repetition) combination, toggling the scheduler via the
  NET_FORCE_BUSYSPIN env var (see JobSystem.cpp's RunPendingTsak /
  ShouldUseCVWait -- unset/absent = CV-wait (default as of M18, no rebuild
  needed), set = busy-spin). Parses each run's summary.json (handshake
  rate, ISSUE-1 count, crash status) and server_profile.csv (CPU-seconds
  consumed, to measure the busy-spin/CV-wait CPU cost difference directly)
  and appends one row per run to a single CSV under
  Programs\StressTest\results\scheduler_comparison_<timestamp>.csv, plus a
  per-(scheduler,load) aggregated summary printed at the end.

  Uses ClientsPerBurst=5 always; each ClientCount must be a multiple of 5.

.PARAMETER ClientCounts
  Load levels to test (must each be a multiple of 5).

.PARAMETER Repetitions
  Runs per (scheduler, load) combination.

.PARAMETER SoakSeconds
  Soak duration for each individual run (kept short since this is a matrix
  of many runs).

.EXAMPLE
  .\Run-SchedulerComparison.ps1 -ClientCounts 5,10,20,40 -Repetitions 3 -SoakSeconds 20
#>
param(
    [int[]]$ClientCounts = @(5, 10, 20, 40),
    [int]$Repetitions = 3,
    [int]$SoakSeconds = 20
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$RunScript = Join-Path $ScriptDir "Run-ConnectionStressTest.ps1"

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutCsv = Join-Path $ScriptDir "results\scheduler_comparison_$Timestamp.csv"
New-Item -ItemType Directory -Force -Path (Join-Path $ScriptDir "results") | Out-Null

"Scheduler,ClientCount,Repetition,HandshakesObserved,TotalClients,HandshakeRate,ConnectsPerSecond,Issue1AssertCount,ServerCrashed,CpuSecondsConsumed,ElapsedSecondsProfiled,AvgCpuPercent,ResultsDir" |
    Out-File -FilePath $OutCsv -Encoding utf8

# M18: CV-wait became the shipped default (JobSystem.cpp's
# ShouldUseCVWait()) once M18 fixed the handshake-permanence gap M16/M17
# found. The toggle env var is now NET_FORCE_BUSYSPIN (opt back into
# busy-spin), inverted from M16's original M16_SCHEDULER_CVWAIT (opt into
# CV-wait) -- polarity flipped, same purpose: drive this comparison matrix
# without a rebuild between runs.
$Schedulers = @(
    @{ Name = "BusySpin"; EnvValue = "1" },
    @{ Name = "CVWait";   EnvValue = $null }
)

$AllRows = @()

foreach ($Sched in $Schedulers) {
    if ($null -eq $Sched.EnvValue) {
        Remove-Item Env:\NET_FORCE_BUSYSPIN -ErrorAction SilentlyContinue
    } else {
        $env:NET_FORCE_BUSYSPIN = $Sched.EnvValue
    }

    foreach ($ClientCount in $ClientCounts) {
        if ($ClientCount % 5 -ne 0) { throw "ClientCount $ClientCount must be a multiple of 5 (ClientsPerBurst is fixed at 5)." }
        $Bursts = $ClientCount / 5

        for ($Rep = 1; $Rep -le $Repetitions; $Rep++) {
            Write-Host "=== Scheduler=$($Sched.Name) ClientCount=$ClientCount Rep=$Rep/$Repetitions ===" -ForegroundColor Cyan

            & $RunScript -Bursts $Bursts -ClientsPerBurst 5 -BurstIntervalMs 500 -SoakSeconds $SoakSeconds `
                -EnablePacketSim -SimDropPermille 100 -SimDuplicatePermille 20 -SimReorderWindow 3 | Out-Null

            # Run-ConnectionStressTest.ps1 always writes to the newest results\<timestamp> dir
            $LatestResultsDir = Get-ChildItem (Join-Path $ScriptDir "results") -Directory |
                Where-Object { $_.Name -match '^\d{8}_\d{6}$' } |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1

            $SummaryPath = Join-Path $LatestResultsDir.FullName "summary.json"
            $ProfilePath = Join-Path $LatestResultsDir.FullName "server_profile.csv"

            $Summary = Get-Content $SummaryPath -Raw | ConvertFrom-Json
            $Profile = Import-Csv $ProfilePath

            $FirstRow = $Profile | Select-Object -First 1
            $LastRow = $Profile | Select-Object -Last 1
            $CpuDelta = [double]$LastRow.CpuTotalSec - [double]$FirstRow.CpuTotalSec
            $ElapsedDelta = [double]$LastRow.ElapsedSec - [double]$FirstRow.ElapsedSec
            $AvgCpuPercent = if ($ElapsedDelta -gt 0) { [math]::Round(($CpuDelta / $ElapsedDelta) * 100, 1) } else { 0 }

            # 클라이언트 로그 집계 대신 신뢰할 수 있는 서버 쪽 수치를 쓴다
            # (자세한 배경은 Run-ConnectionStressTest.ps1, ISSUE-8) -- 클라이언트
            # 로그는 여러 프로세스가 공유하고 큐 오버런으로 유실되어
            # 과소보고하며, 핸드셰이크가 빠를수록 더 심하다. 이걸 안 고치면
            # 여기 0.600으로 나온 실행도 실제론 38/40이 성립돼 있었다.
            $HandshakeRate = if ($Summary.TotalClientsLaunched -gt 0) {
                [math]::Round($Summary.ServerVerifiedConnections / $Summary.TotalClientsLaunched, 3)
            } else { 0 }

            $Row = [PSCustomObject]@{
                Scheduler           = $Sched.Name
                ClientCount         = $ClientCount
                Repetition          = $Rep
                HandshakesObserved  = $Summary.ServerVerifiedConnections
                TotalClients        = $Summary.TotalClientsLaunched
                HandshakeRate       = $HandshakeRate
                ConnectsPerSecond   = $Summary.HandshakeStats.ConnectsPerSecond
                Issue1AssertCount   = $Summary.Issue1AssertCount
                ServerCrashed       = $Summary.ServerCrashed
                CpuSecondsConsumed  = [math]::Round($CpuDelta, 3)
                ElapsedSecondsProfiled = [math]::Round($ElapsedDelta, 2)
                AvgCpuPercent       = $AvgCpuPercent
                ResultsDir          = $LatestResultsDir.Name
            }
            $AllRows += $Row

            "$($Row.Scheduler),$($Row.ClientCount),$($Row.Repetition),$($Row.HandshakesObserved),$($Row.TotalClients),$($Row.HandshakeRate),$($Row.ConnectsPerSecond),$($Row.Issue1AssertCount),$($Row.ServerCrashed),$($Row.CpuSecondsConsumed),$($Row.ElapsedSecondsProfiled),$($Row.AvgCpuPercent),$($Row.ResultsDir)" |
                Out-File -FilePath $OutCsv -Encoding utf8 -Append

            Write-Host ("  handshakes={0}/{1} rate={2} connSec={3} issue1={4} cpu%={5} crashed={6}" -f `
                $Row.HandshakesObserved, $Row.TotalClients, $Row.HandshakeRate, $Row.ConnectsPerSecond, $Row.Issue1AssertCount, $Row.AvgCpuPercent, $Row.ServerCrashed)

            Start-Sleep -Seconds 3
        }
    }
}

Remove-Item Env:\NET_FORCE_BUSYSPIN -ErrorAction SilentlyContinue

Write-Host "`n=== Aggregated (mean per Scheduler x ClientCount) ===" -ForegroundColor Yellow
$AllRows | Group-Object Scheduler, ClientCount | ForEach-Object {
    $Rate = ($_.Group | Measure-Object HandshakeRate -Average).Average
    $Cpu = ($_.Group | Measure-Object AvgCpuPercent -Average).Average
    $Issue1 = ($_.Group | Measure-Object Issue1AssertCount -Sum).Sum
    [PSCustomObject]@{
        Scheduler        = $_.Group[0].Scheduler
        ClientCount      = $_.Group[0].ClientCount
        MeanHandshakeRate = [math]::Round($Rate, 3)
        MeanCpuPercent   = [math]::Round($Cpu, 1)
        TotalIssue1Hits  = $Issue1
    }
} | Sort-Object Scheduler, ClientCount | Format-Table -AutoSize

Write-Host "Full per-run data -> $OutCsv" -ForegroundColor Green
