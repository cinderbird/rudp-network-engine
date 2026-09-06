<#
.SYNOPSIS
  실행 중인 엔진 프로세스의 TelemetrySink 포트에 붙어, 줄바꿈 구분 JSON
  이벤트를 그대로 파일에 이어쓴다.

.DESCRIPTION
  TelemetrySink(Project/Network/src/telemetry/TelemetrySink.h)는 로컬 전용
  TCP 서버로 줄바꿈 구분 JSON을 내보낸다. 부하 테스트의 측정 채널로 쓰는
  이유는 ISSUE-8의 손실 모드에 안 걸리는 유일한 계측이기 때문 -- 프로세스별
  전용 소켓이라, 엔진이 빨라질수록 핸드셰이크 수를 더 과소보고하던 공유
  spdlog 파일(overrun_oldest)과 다르다.

  스트레스 실행과 함께 별도 프로세스로 띄우고 종료 시 같이 죽이는 용도 --
  연결이 끊기거나 -MaxSeconds가 지날 때까지 반복.

.PARAMETER Port
  엔진의 TelemetryPort(config.json "TelemetryPort").

.PARAMETER OutFile
  원본 JSON 줄을 이어쓸 파일(이벤트당 한 줄).

.PARAMETER MaxSeconds
  강제 종료 시각 -- 고아 프로세스가 실행보다 오래 살아남지 않게.

.EXAMPLE
  .\Collect-Telemetry.ps1 -Port 9100 -OutFile telemetry.jsonl -MaxSeconds 120
#>
param(
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [int]$MaxSeconds = 600
)

$ErrorActionPreference = "Stop"
$Deadline = (Get-Date).AddSeconds($MaxSeconds)

# 엔진이 드라이버 초기화 중 텔레메트리 리스너를 여는 타이밍과 이 스크립트
# 실행이 경합하므로 -- 바로 실패시키지 않고 잠깐 재시도.
$Client = $null
while ((Get-Date) -lt $Deadline) {
    try {
        $Client = New-Object System.Net.Sockets.TcpClient
        $Client.Connect("127.0.0.1", $Port)
        break
    }
    catch {
        if ($Client) { $Client.Dispose() }
        $Client = $null
        Start-Sleep -Milliseconds 200
    }
}

if (-not $Client) {
    "ERROR: could not connect to telemetry port $Port" | Out-File -FilePath $OutFile -Encoding utf8
    exit 1
}

try {
    $Stream = $Client.GetStream()
    # ReadLine은 블로킹 -- 타임아웃을 걸어야 엔진이 조용할 때도 아래 데드라인
    # 체크가 실제로 돈다.
    $Stream.ReadTimeout = 2000
    $Reader = New-Object System.IO.StreamReader($Stream, [System.Text.Encoding]::UTF8)
    $Writer = New-Object System.IO.StreamWriter($OutFile, $false, [System.Text.Encoding]::UTF8)
    $Writer.AutoFlush = $true

    while ((Get-Date) -lt $Deadline -and $Client.Connected) {
        try {
            $Line = $Reader.ReadLine()
            if ($null -eq $Line) { break }   # peer closed
            $Writer.WriteLine($Line)
        }
        catch [System.IO.IOException] {
            # ReadTimeout 발생 -- 엔진이 잠깐 조용한 정상 상황. 데드라인 체크를
            # 위해 계속 반복.
            continue
        }
    }
}
finally {
    if ($Writer) { $Writer.Dispose() }
    if ($Reader) { $Reader.Dispose() }
    if ($Client) { $Client.Dispose() }
}
