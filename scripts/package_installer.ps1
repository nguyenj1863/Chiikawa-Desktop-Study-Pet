<#
.SYNOPSIS
    Build the Windows Inno Setup installer (ChiikawaStudyPet-Setup-x64.exe)
    from the verified windeployqt deployment directory produced by scripts/package.ps1.
#>
param(
    [string]$QtPrefix = $env:CMAKE_PREFIX_PATH,
    [string]$SourceDir = "",
    [string]$Version = "",
    [string]$IsccPath = "",
    [string]$SignCommand = $env:WINDOWS_SIGN_COMMAND,
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$DistRoot = Join-Path $Root "dist"
$IssPath = Join-Path $Root "installer\ChiikawaStudyPet.iss"

if (-not (Test-Path $IssPath)) {
    throw "Inno Setup script not found at $IssPath"
}

# Read project VERSION from CMakeLists.txt unless explicitly overridden
if (-not $Version) {
    $cmakeFile = Join-Path $Root "CMakeLists.txt"
    $cmakeContent = Get-Content -Raw $cmakeFile
    if ($cmakeContent -match "project\s*\(\s*ChiikawaStudyPet[\s\S]*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)") {
        $Version = $Matches[1]
    } else {
        throw "Could not determine project VERSION from CMakeLists.txt"
    }
}

# Reuse scripts/package.ps1 deployment directory so Qt/CRT bundling logic is never duplicated
if (-not $SourceDir) {
    $SourceDir = Join-Path $DistRoot "ChiikawaStudyPet-Windows-x64"
}

if (-not $SkipDeploy -and -not (Test-Path (Join-Path $SourceDir "ChiikawaStudyPet.exe"))) {
    Write-Host "Deployment directory not found; running scripts/package.ps1 first..."
    $pkgScript = Join-Path $PSScriptRoot "package.ps1"
    if ($QtPrefix) {
        & $pkgScript -QtPrefix $QtPrefix
    } else {
        & $pkgScript
    }
}

# Verify required runtime files exist in the staging directory before compiling the installer
$required = @(
    "ChiikawaStudyPet.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Multimedia.dll",
    "platforms\qwindows.dll",
    "imageformats\qgif.dll",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md"
)
foreach ($rel in $required) {
    $full = Join-Path $SourceDir $rel
    if (-not (Test-Path $full)) {
        throw "Missing required deployment file for installer: $full"
    }
}

# Locate Inno Setup compiler (ISCC.exe)
if (-not $IsccPath) {
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) {
        $IsccPath = $cmd.Source
    }
}
if (-not $IsccPath) {
    foreach ($candidate in @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )) {
        if ($candidate -and (Test-Path $candidate)) {
            $IsccPath = $candidate
            break
        }
    }
}
if (-not $IsccPath -or -not (Test-Path $IsccPath)) {
    throw "Inno Setup 6 (ISCC.exe) not found. Install Inno Setup 6 or pass -IsccPath."
}

$resolvedSource = (Resolve-Path $SourceDir).Path
New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
$resolvedOutput = (Resolve-Path $DistRoot).Path

$isccArgs = @(
    "/DMyAppVersion=$Version",
    "/DSourceDir=$resolvedSource",
    "/DOutputDir=$resolvedOutput"
)

if ($SignCommand) {
    Write-Host "Signing enabled via configured SignCommand."
    $isccArgs += "/DSIGN_ENABLED=1"
    $isccArgs += "/Ssigntool=$SignCommand"
} else {
    Write-Host "No Windows signing command configured; building unsigned installer."
}

$isccArgs += $IssPath

Write-Host "Compiling installer with $IsccPath (version $Version)..."
& $IsccPath @isccArgs
if ($LASTEXITCODE -ne 0) {
    throw "ISCC.exe failed with exit code $LASTEXITCODE"
}

$installerPath = Join-Path $resolvedOutput "ChiikawaStudyPet-Setup-x64.exe"
if (-not (Test-Path $installerPath)) {
    throw "Expected installer was not generated: $installerPath"
}

$size = (Get-Item $installerPath).Length
if ($size -lt 5000000) {
    throw "Generated installer is unexpectedly small: $size bytes"
}

Write-Host "Built installer: $installerPath"
Write-Host ("Size: {0:N1} MB ({1} bytes)" -f ($size / 1MB), $size)
