param(
    [Parameter(Mandatory = $true)][string]$DeviceIp,
    [string]$TrustKey = 'host/trusted_device.pub',
    [ValidateRange(1, 65535)][int]$TcpPort = 9000,
    [ValidateRange(1, 2147483647)][int]$Seconds = 60,
    [ValidateRange(1, 100000)][int]$RekeyEvery = 10,
    [switch]$NoDisplay,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'verify_snapshot.ps1')
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runName = Get-Date -Format 'yyyyMMdd_HHmmss_fffffff'
$runDirectory = Join-Path $projectRoot "diagnostics/single-device-wifi-v1/$runName"
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
$arguments = @('-B', '-u', '-m', 'host.pqc_camera_demo', '--host', $DeviceIp,
    '--tcp-port', "$TcpPort", '--trust-key', $TrustKey, '--mode', 'record',
    '--seconds', "$Seconds", '--rekey-every', "$RekeyEvery", '--memory-every', '10',
    '--profile', '--diagnostics', (Join-Path $runDirectory 'trace.jsonl'))
if (!$NoDisplay) { $arguments += '--display' }
Write-Host "Baseline: $PSScriptRoot"
Write-Host "Diagnostics: $runDirectory"
Push-Location $PSScriptRoot
try {
    & $Python @arguments
    $resultCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $resultCode
