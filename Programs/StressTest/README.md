# StressTest

`Run-ConnectionStressTest.ps1` empirically verifies the M1 thread-safety
work by hammering a running `GameServer.exe` with bursts of concurrent
`RemoteServer.exe` client connections (real OS processes, so real
concurrency -- not simulated), while sampling the server's CPU/memory/handle
counts and parsing its network log for handshake throughput.

## Usage

Build Debug|x64 first, then:

```powershell
powershell -ExecutionPolicy Bypass -File .\Run-ConnectionStressTest.ps1 -Bursts 8 -ClientsPerBurst 10 -BurstIntervalMs 400 -SoakSeconds 15
```

Add `-UseAppVerifier` (requires an elevated shell) to additionally run the
server under Windows Application Verifier's Heaps/Handles/Locks providers
for the duration of the test.

See the script's own comment header (`Get-Help .\Run-ConnectionStressTest.ps1 -Full`)
for all parameters and output files. Each run writes to
`results\<timestamp>\` (git-ignored): `summary.json`, `server_profile.csv`,
and per-process stdout/stderr logs.

## Known finding

The first real run of this script (2026-08-21, 8x10 clients) reliably
crashed the server -- not from a data race (verified: the crashing code path
never leaves the per-connection Job-queue affinity M1 relies on), but a
genuine pre-existing packet/channel-layer reliability bug. See
[`docs/problem-solving.md`](../../docs/problem-solving.md) case 1 for the full
investigation -- the root cause was fixed (35% -> 0% reproduction) and the residual
desync was contained by a circuit breaker, so long soak runs complete normally now.
