# Packages ChiikawaStudyPet with windeployqt for redistribution.
param(
    [string]$ExePath = "",
    [string]$QtPrefix = $env:CMAKE_PREFIX_PATH
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) {
    $candidates = @(
        (Join-Path $Root "build\Release\ChiikawaStudyPet.exe"),
        (Join-Path $Root "build\ChiikawaStudyPet.exe")
    )
    foreach ($c in $candidates) { if (Test-Path $c) { $ExePath = $c; break } }
}
if (-not $ExePath -or -not (Test-Path $ExePath)) {
    throw "Executable not found. Build Release first."
}
$windeploy = $null
if ($QtPrefix) {
    $p = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (Test-Path $p) { $windeploy = $p }
}
if (-not $windeploy) {
    $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($cmd) { $windeploy = $cmd.Source }
}
if (-not $windeploy) { throw "windeployqt not found. Set -QtPrefix to your Qt kit." }
Write-Host "Deploying $ExePath"
& $windeploy --release --no-translations $ExePath
Write-Host "Done. Distribute the folder containing the exe and deployed Qt DLLs."
