<#
.SYNOPSIS
  워커 스케일링 곡선 -- "JobSystem 워커 수를 늘리면 처리량이 실제로 오르는가"
  를 답하는 핵심 실험.

.DESCRIPTION
  커넥션 수와 패킷 레이트는 고정, NET_JOBSYSTEM_WORKERS만 스윕(재빌드
  불필요, NetDriver::InitBase의 ResolveWorkerCount 참고), 각 값에서 jobs/s,
  워커별 분포, 총 CPU를 기록.

  레이트는 RELIABLE_BUFFER 트리거 지점 아래로 고정: 커넥션당 지속
  300-350Hz 이상이면(5커넥션, 로컬호스트, 손실 0 기준) 512개 unacked-bunch
  버퍼가 넘쳐 커넥션이 스스로 닫힌다(ReliableBufferOverflow, M10이 만든
  의도된 동작이지 이 실험이 재려는 건 아님) -- 기본 RatePerConnHz는 그
  경계보다 여유 있게 잡았다.

.PARAMETER Workers
  스윕할 워커 수들.

.PARAMETER Connections
  모든 지점 공통 고정 커넥션 수.

.PARAMETER RatePerConnHz
  커넥션당 SyntheticTrafficHz = MaxTickRate. RELIABLE_BUFFER 트리거 아래로
  유지.

.PARAMETER Repetitions
  워커 수당 반복 횟수.

.PARAMETER SoakSeconds
  회당 소크 시간.

.EXAMPLE
  .\Run-WorkerScaling.ps1 -Workers 1,2,4,8 -Connections 40 -RatePerConnHz 200 -Repetitions 2
#>
param(
    [int[]]$Workers = @(1, 2, 4, 8),
    [int]$Connections = 40,
    [int]$RatePerConnHz = 200,
    [int]$Repetitions = 2,
    [int]$SoakSeconds = 20,

    # RemoteServer.exe 프로세스 하나가 호스팅할 커넥션 수(M24). 기본 1은
    # 기존 프로세스당 커넥션 1개 방식 그대로. 1보다 크면 프로세스 수를 안
    # 늘리고도 $Connections를 수백~수천까지 올릴 수 있다.
    [int]$ConnectionsPerProcess = 1,

    # 버스트당 프로세스 수. ConnectionsPerProcess와 곱해 $Connections를
    # 나눠 떨어뜨려야 함.
    [int]$ClientsPerBurst = 5
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$RunScript = Join-Path $ScriptDir "Run-ConnectionStressTest.ps1"

$ConnectionsPerBurst = $ClientsPerBurst * $ConnectionsPerProcess
if ($Connections % $ConnectionsPerBurst -ne 0) {
    throw "Connections must be a multiple of ClientsPerBurst x ConnectionsPerProcess ($ConnectionsPerBurst)."
}
$Bursts = $Connections / $ConnectionsPerBurst

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutCsv = Join-Path $ScriptDir "results\worker_scaling_$Timestamp.csv"
New-Item -ItemType Directory -Force -Path (Join-Path $ScriptDir "results") | Out-Null

"Workers,Repetition,ConnectionsEstablished,TotalConnections,JobsPerSec,DatagramsRecvPerSec,IoCompletionsPerSec,TotalCpuCores,W0Jobs,W0Cpu,W1Jobs,W1Cpu,W2Jobs,W2Cpu,W3Jobs,W3Cpu,W4Jobs,W4Cpu,W5Jobs,W5Cpu,W6Jobs,W6Cpu,W7Jobs,W7Cpu" |
    Out-File -FilePath $OutCsv -Encoding utf8

$AllRows = @()
$TelemetryPortBase = 15000

foreach ($W in $Workers) {
    $env:NET_JOBSYSTEM_WORKERS = "$W"
    for ($Rep = 1; $Rep -le $Repetitions; $Rep++) {
        $Port = $TelemetryPortBase + ($W * 100) + $Rep
        Write-Host "=== workers=$W rep=$Rep/$Repetitions ===" -ForegroundColor Cyan

        $Summary = & $RunScript -Configuration Release `
            -Bursts $Bursts -ClientsPerBurst $ClientsPerBurst -ConnectionsPerProcess $ConnectionsPerProcess -BurstIntervalMs 300 -SoakSeconds $SoakSeconds `
            -ServerTelemetryPort $Port `
            -ClientSyntheticTrafficHz $RatePerConnHz -ClientMaxTickRate $RatePerConnHz `
            -ServerSyntheticTrafficHz $RatePerConnHz -ServerMaxTickRate $RatePerConnHz `
            -LogLevel "warn" *>&1 |
            Where-Object { $_ -is [string] } | Out-Null

        # Run-ConnectionStressTest.ps1은 객체를 반환하지 않고 summary.json을
        # 자기 results\<timestamp> 폴더에 쓴다 -- 방금 생긴 걸 다시 읽는다.
        $ResultsRoot = Join-Path $ScriptDir "results"
        $LatestDir = Get-ChildItem $ResultsRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d{8}_\d{6}$' } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $LatestDir) { Write-Warning "No results dir found for workers=$W rep=$Rep"; continue }

        $SummaryPath = Join-Path $LatestDir.FullName "summary.json"
        if (-not (Test-Path $SummaryPath)) { Write-Warning "No summary.json in $($LatestDir.FullName)"; continue }
        $S = Get-Content $SummaryPath -Raw | ConvertFrom-Json

        $T = $S.Telemetry
        $WorkerCells = @("", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "")
        if ($T -and $T.Workers) {
            for ($i = 0; $i -lt $T.Workers.Count -and $i -lt 8; $i++) {
                $WorkerCells[$i * 2] = $T.Workers[$i].MedianJobsPerSec
                $WorkerCells[$i * 2 + 1] = $T.Workers[$i].CpuCores
            }
        }

        $Row = [PSCustomObject]@{
            Workers                  = $W
            Repetition                = $Rep
            ConnectionsEstablished    = $S.AuthoritativeConnections
            TotalConnections          = $S.TotalClientsLaunched
            JobsPerSec                = if ($T) { $T.MedianJobsPerSec } else { $null }
            DatagramsRecvPerSec       = if ($T) { $T.MedianDatagramsRecvPerSec } else { $null }
            IoCompletionsPerSec       = if ($T) { $T.MedianIoCompletionsPerSec } else { $null }
            TotalCpuCores             = if ($T) { $T.TotalCpuCores } else { $null }
        }
        $AllRows += $Row

        "$W,$Rep,$($S.AuthoritativeConnections),$($S.TotalClientsLaunched),$($Row.JobsPerSec),$($Row.DatagramsRecvPerSec),$($Row.IoCompletionsPerSec),$($Row.TotalCpuCores),$($WorkerCells -join ',')" |
            Out-File -FilePath $OutCsv -Append -Encoding utf8

        Write-Host ("  established={0}/{1} jobs/s={2} cpu={3} cores" -f $S.AuthoritativeConnections, $S.TotalClientsLaunched, $Row.JobsPerSec, $Row.TotalCpuCores)
    }
    Remove-Item Env:\NET_JOBSYSTEM_WORKERS -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "=== Worker scaling summary (median across repetitions) ===" -ForegroundColor Cyan
$AllRows | Group-Object Workers | Sort-Object { [int]$_.Name } | ForEach-Object {
    $jobsVals = @($_.Group | ForEach-Object { $_.JobsPerSec } | Where-Object { $_ -ne $null })
    $cpuVals  = @($_.Group | ForEach-Object { $_.TotalCpuCores } | Where-Object { $_ -ne $null })
    $connVals = @($_.Group | ForEach-Object { $_.ConnectionsEstablished })
    $jobsMed = if ($jobsVals.Count -gt 0) { ($jobsVals | Sort-Object)[[int]([math]::Floor($jobsVals.Count / 2))] } else { $null }
    $cpuMed  = if ($cpuVals.Count -gt 0)  { ($cpuVals  | Sort-Object)[[int]([math]::Floor($cpuVals.Count / 2))] }  else { $null }
    Write-Host ("workers={0,-3} conns_established={1}  jobs/s={2,-10:N1}  cpu={3,-8:N4} cores  jobs/s-per-core={4:N0}" -f `
        $_.Name, ($connVals -join '/'), $jobsMed, $cpuMed, $(if ($cpuMed -gt 0) { $jobsMed / $cpuMed } else { 0 }))
}

Write-Host ""
Write-Host "CSV: $OutCsv"
