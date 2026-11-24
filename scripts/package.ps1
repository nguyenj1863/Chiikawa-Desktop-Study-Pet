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

# Copy app-local MSVC runtime DLLs when available so end users do not need VC++ Redistributable preinstalled
$crtDlls = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll")
$crtSearchDirs = @()
if ($env:VCToolsRedistDir) {
    $crtSearchDirs += (Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT")
}
$vsBase = "C:\Program Files (x86)\Microsoft Visual Studio\2022"
foreach ($edition in @("BuildTools", "Community", "Professional", "Enterprise")) {
    $redistRoot = Join-Path $vsBase "$edition\VC\Redist\MSVC"
    if (Test-Path $redistRoot) {
        Get-ChildItem $redistRoot -Directory | Sort-Object Name -Descending | ForEach-Object {
            $crtSearchDirs += (Join-Path $_.FullName "x64\Microsoft.VC143.CRT")
        }
    }
}
$crtSearchDirs += (Join-Path $env:SystemRoot "System32")

foreach ($dll in $crtDlls) {
    $dst = Join-Path $AppDir $dll
    if (-not (Test-Path $dst)) {
        foreach ($dir in $crtSearchDirs) {
            $src = Join-Path $dir $dll
            if (Test-Path $src) {
                Copy-Item -Force $src $dst
                break
            }
        }
    }
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

# Include license and third-party notices in the distributable package
foreach ($doc in @("LICENSE", "THIRD_PARTY_NOTICES.md")) {
    $docPath = Join-Path $Root $doc
    if (Test-Path $docPath) {
        Copy-Item -Force $docPath (Join-Path $AppDir $doc)
    }
}

if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}
Compress-Archive -Path $AppDir -DestinationPath $ZipPath -Force

Write-Host "Packaged: $ZipPath"
Write-Host ("Size: {0:N1} MB" -f ((Get-Item $ZipPath).Length / 1MB))
