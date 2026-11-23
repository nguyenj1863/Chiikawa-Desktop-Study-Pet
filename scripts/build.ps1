<#
.SYNOPSIS
    Configure, build, test, and optionally deploy Chiikawa Study Pet.
.PARAMETER Config
    Debug or Release (default: Release)
.PARAMETER Clean
    Remove the build directory before configuring
.PARAMETER Deploy
    Run windeployqt after a successful Release build
.PARAMETER QtPrefix
    Optional CMAKE_PREFIX_PATH pointing at the Qt 6 kit (e.g. C:\Qt\6.8.0\msvc2022_64)
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Deploy,
    [string]$QtPrefix = $env:CMAKE_PREFIX_PATH
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"

function Find-VsDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $devCmd = Join-Path $install "Common7\Tools\VsDevCmd.bat"
            if (Test-Path $devCmd) { return $devCmd }
        }
    }
    return $null
}

# Prefer a developer shell so cl.exe / link.exe are available
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $devCmd = Find-VsDevCmd
    if ($devCmd) {
        Write-Host "Importing MSVC environment from $devCmd"
        $temp = [System.IO.Path]::GetTempFileName()
        cmd /c "`"$devCmd`" -arch=amd64 >nul && set" | ForEach-Object {
            if ($_ -match "^(.*?)=(.*)$") {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
            }
        }
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $cmakeCandidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    )
    foreach ($c in $cmakeCandidates) {
        if (Test-Path $c) {
            $env:Path = "$(Split-Path $c);$env:Path"
            break
        }
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found. Install Kitware CMake and re-run."
}

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

# Prefer the VS generator when vswhere reports a complete install; otherwise Ninja/NMake
$generator = @("-G", "Visual Studio 17 2022", "-A", "x64")
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsReady = $false
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsPath) { $vsReady = $true }
}
if (-not $vsReady) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $generator = @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Config")
    } elseif (Test-Path (Join-Path $QtPrefix "bin\ninja.exe")) {
        $env:Path = "$(Join-Path $QtPrefix 'bin');$env:Path"
        $generator = @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Config")
    } else {
        $generator = @("-G", "NMake Makefiles", "-DCMAKE_BUILD_TYPE=$Config")
    }
}

$configureArgs = @("-S", $Root, "-B", $BuildDir) + $generator
if ($QtPrefix) {
    $configureArgs += "-DCMAKE_PREFIX_PATH=$QtPrefix"
}

Write-Host "Configuring..."
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "Building ($Config)..."
& cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "Running tests..."
& ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Some tests failed (exit $LASTEXITCODE)"
}

$exe = Join-Path $BuildDir "$Config\ChiikawaStudyPet.exe"
if (-not (Test-Path $exe)) {
    # Ninja / single-config generators
    $alt = Join-Path $BuildDir "ChiikawaStudyPet.exe"
    if (Test-Path $alt) { $exe = $alt }
}

Write-Host "Executable: $exe"

if ($Deploy) {
    if ($Config -ne "Release") {
        Write-Warning "Deploy is intended for Release builds."
    }
    $windeploy = $null
    if ($QtPrefix) {
        $candidate = Join-Path $QtPrefix "bin\windeployqt.exe"
        if (Test-Path $candidate) { $windeploy = $candidate }
    }
    if (-not $windeploy) {
        $windeploy = Get-Command windeployqt -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    }
    if ($windeploy) {
        Write-Host "Deploying with $windeploy"
        & $windeploy --release --no-translations $exe
    } else {
        Write-Warning "windeployqt not found. Set QtPrefix or PATH."
    }
}

Write-Host "Done."
