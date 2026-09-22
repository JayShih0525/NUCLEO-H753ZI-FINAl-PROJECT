$ErrorActionPreference = 'Stop'
# Reuse the project's installed Python runtime; all source/configuration is local.
$python = Join-Path $PSScriptRoot '..\..\..\.venv\Scripts\python.exe'
if (!(Test-Path -LiteralPath $python)) { $python = 'python' }
Push-Location $PSScriptRoot
$previousBytecode = $env:PYTHONDONTWRITEBYTECODE
try {
    $env:PYTHONDONTWRITEBYTECODE = '1'
    & $python -B -u (Join-Path $PSScriptRoot 'run_camera.py')
    $result = $LASTEXITCODE
} finally {
    $env:PYTHONDONTWRITEBYTECODE = $previousBytecode
    Pop-Location
}
exit $result
