<#
.SYNOPSIS
    Build a portable ChiikawaStudyPet-Windows-x64.zip for GitHub Releases.
#>
param(
    [string]$QtPrefix = $env:CMAKE_PREFIX_PATH,
    [string]$ExePath = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$DistRoot = Join-Path $Root "dist"
$AppDirName = "ChiikawaStudyPet-Windows-x64"
$AppDir = Join-Path $DistRoot $AppDirName
$ZipPath = Join-Path $DistRoot "$AppDirName.zip"

if (-not $ExePath) {
    foreach ($c in @(
        (Join-Path $Root "build\Release\ChiikawaStudyPet.exe"),
        (Join-Path $Root "build\ChiikawaStudyPet.exe")
    )) {
        if (Test-Path $c) { $ExePath = $c; break }
    }
}
if (-not $ExePath -or -not (Test-Path $ExePath)) {
    throw "Executable not found. Build Release first."
}

$windeploy = $null
if ($QtPrefix) {
    $candidate = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (Test-Path $candidate) { $windeploy = $candidate }
}
if (-not $windeploy) {
    $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($cmd) { $windeploy = $cmd.Source }
}
if (-not $windeploy) {
    throw "windeployqt not found. Set -QtPrefix to your Qt kit."
}

if (Test-Path $DistRoot) {
    Remove-Item -Recurse -Force $DistRoot
}
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null

$targetExe = Join-Path $AppDir "ChiikawaStudyPet.exe"
Copy-Item -Force $ExePath $targetExe

Write-Host "Running windeployqt on $targetExe"
& $windeploy --release --no-translations --no-opengl-sw $targetExe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# Drop compiler/debug leftovers if any slipped in
Get-ChildItem $AppDir -Recurse -Include *.pdb,*.ilk,*.exp,*.lib,*.obj |
    Remove-Item -Force -ErrorAction SilentlyContinue

# Optional bulky files not required for Qt Widgets desktop pets
@(
    "dxcompiler.dll",
    "dxil.dll",
    "vc_redist.x64.exe",
    "opengl32sw.dll"
) | ForEach-Object {
    $p = Join-Path $AppDir $_
    if (Test-Path $p) { Remove-Item -Force $p }
}

# Soft-check required runtime pieces
$required = @(
    "ChiikawaStudyPet.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Multimedia.dll",
    "platforms\qwindows.dll",
    "imageformats\qgif.dll"
)
foreach ($rel in $required) {
    $path = Join-Path $AppDir $rel
    if (-not (Test-Path $path)) {
        throw "Missing required package file: $rel"
    }
}

if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}
Compress-Archive -Path $AppDir -DestinationPath $ZipPath -Force

Write-Host "Packaged: $ZipPath"
Write-Host ("Size: {0:N1} MB" -f ((Get-Item $ZipPath).Length / 1MB))
