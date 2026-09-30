$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'snapshot.json') -Raw | ConvertFrom-Json
$failures = @()
foreach ($entry in $manifest.files) {
    $target = Join-Path $PSScriptRoot $entry.path
    if (!(Test-Path -LiteralPath $target -PathType Leaf)) {
        $failures += "Missing: $($entry.path)"
    } elseif ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.sha256) {
        $failures += "Changed: $($entry.path)"
    }
}
if ($failures.Count) { throw ($failures -join [Environment]::NewLine) }
Write-Host "PASS: $($manifest.files.Count) baseline files match snapshot."
Write-Host 'Local keys/configuration and Python/Arduino installations are not covered by this manifest.'
