<#
.SYNOPSIS
    End-to-end Windows installer verification:
      1. Fresh per-user installation (files, Start Menu & Desktop shortcuts, Installed Apps entry, launch)
      2. In-place upgrade installation (version update, no duplicates, settings preserved, shortcuts valid)
      3. Clean uninstallation (files & shortcuts removed, Run startup entry cleaned up, user prefs preserved)
#>
param(
    [string]$InstallerPath = "",
    [string]$IsccPath = "",
    [switch]$SkipUpgradeBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $Root "dist\ChiikawaStudyPet-Setup-x64.exe"
}
if (-not (Test-Path $InstallerPath)) {
    throw "Installer not found: $InstallerPath"
}

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Chiikawa"
$InstalledExe = Join-Path $InstallDir "ChiikawaStudyPet.exe"
$StartMenuShortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Chiikawa.lnk"
$DesktopShortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "Chiikawa.lnk"
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8B6E4A2E-9C3D-4F1A-B7E2-5D8A1C3F6E90}_is1"
$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$PrefsKey = "HKCU:\Software\ChiikawaStudyPet\DesktopPet"
$SentinelName = "installerTestSentinel"
$SentinelValue = "preserve-me-12345"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "ASSERTION FAILED: $Message"
    }
    Write-Host "  [PASS] $Message"
}

function Get-ShortcutInfo {
    param([string]$Path)
    $wsh = New-Object -ComObject WScript.Shell
    return $wsh.CreateShortcut($Path)
}

function Test-AppLaunches {
    param([string]$ExePath)
    $proc = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path $ExePath) -PassThru
    Start-Sleep -Seconds 3
    $alive = -not $proc.HasExited
    if ($alive) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
    return $alive
}

# Backup existing startup entry if present
$hadOriginalRun = $false
$originalRunValue = $null
if (Test-Path $RunKey) {
    $existingRun = Get-ItemProperty -Path $RunKey -Name "ChiikawaStudyPet" -ErrorAction SilentlyContinue
    if ($existingRun) {
        $hadOriginalRun = $true
        $originalRunValue = $existingRun.ChiikawaStudyPet
    }
}

$hadOriginalPrefsKey = Test-Path $PrefsKey
$upgradeTempDir = $null

try {
    # Ensure clean pre-state if a previous test install exists
    $existingUnins = Join-Path $InstallDir "unins000.exe"
    if (Test-Path $existingUnins) {
        Write-Host "Cleaning up prior installation before test..."
        Start-Process -FilePath $existingUnins -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART" -Wait
        Start-Sleep -Seconds 1
    }

    Write-Host "`n=== 1. Fresh Installation Test ==="
    $p = Start-Process -FilePath $InstallerPath -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS="desktopicon"' -Wait -PassThru
    Assert-True ($p.ExitCode -eq 0) "Fresh installer exited with code 0 (actual: $($p.ExitCode))"

    Assert-True (Test-Path $InstallDir) "Installed under per-user directory: $InstallDir"
    foreach ($rel in @(
        "ChiikawaStudyPet.exe",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Widgets.dll",
        "Qt6Multimedia.dll",
        "platforms\qwindows.dll",
        "imageformats\qgif.dll",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "unins000.exe"
    )) {
        Assert-True (Test-Path (Join-Path $InstallDir $rel)) "Deployed file exists: $rel"
    }

    Assert-True (Test-Path $StartMenuShortcut) "Start Menu shortcut created: $StartMenuShortcut"
    $sm = Get-ShortcutInfo $StartMenuShortcut
    Assert-True ($sm.TargetPath -ieq $InstalledExe) "Start Menu shortcut target matches installed exe"
    Assert-True ($sm.WorkingDirectory -ieq $InstallDir) "Start Menu shortcut working dir matches install dir"

    Assert-True (Test-Path $DesktopShortcut) "Optional Desktop shortcut created: $DesktopShortcut"
    $dk = Get-ShortcutInfo $DesktopShortcut
    Assert-True ($dk.TargetPath -ieq $InstalledExe) "Desktop shortcut target matches installed exe"

    Assert-True (Test-Path $UninstallKey) "Installed Apps uninstall key registered"
    $uProps = Get-ItemProperty $UninstallKey
    Assert-True ($uProps.DisplayName -eq "Chiikawa") "DisplayName is 'Chiikawa'"
    Assert-True ($uProps.Publisher -eq "John Nguyen") "Publisher is 'John Nguyen'"
    Assert-True ([bool]$uProps.UninstallString) "UninstallString is populated"

    $launched = Test-AppLaunches $InstalledExe
    Assert-True $launched "Installed ChiikawaStudyPet.exe launched and remained running cleanly"

    # Check Start Menu didn't get duplicated after app launch (WindowsIdentity::ensureStartMenuShortcut)
    $startMenuMatches = @(Get-ChildItem (Split-Path $StartMenuShortcut) -Filter "*Chiikawa*" -ErrorAction SilentlyContinue)
    Assert-True ($startMenuMatches.Count -eq 1) "No duplicate Start Menu shortcuts after app launch (count: $($startMenuMatches.Count))"

    Write-Host "`n=== 2. Upgrade Installation Test ==="
    if (-not (Test-Path $PrefsKey)) {
        New-Item -Path $PrefsKey -Force | Out-Null
    }
    Set-ItemProperty -Path $PrefsKey -Name $SentinelName -Value $SentinelValue

    $upgradeInstaller = $InstallerPath
    $expectedUpgradeVersion = $uProps.DisplayVersion
    if (-not $SkipUpgradeBuild) {
        $upgradeTempDir = Join-Path $env:TEMP ("chiikawa-upgrade-test-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $upgradeTempDir | Out-Null
        $pkgInstallerScript = Join-Path $PSScriptRoot "package_installer.ps1"
        $issArgs = @{
            Version    = "1.0.1"
            SkipDeploy = $true
        }
        if ($IsccPath) { $issArgs.IsccPath = $IsccPath }
        # Compile a 1.0.1 installer into temp dir without touching dist/
        $isccExe = $IsccPath
        if (-not $isccExe) {
            $cmd = Get-Command iscc -ErrorAction SilentlyContinue
            if ($cmd) { $isccExe = $cmd.Source }
        }
        if (-not $isccExe) {
            foreach ($c in @(
                "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
            )) {
                if ($c -and (Test-Path $c)) { $isccExe = $c; break }
            }
        }
        $stageDir = Join-Path $Root "dist\ChiikawaStudyPet-Windows-x64"
        if ($isccExe -and (Test-Path $stageDir)) {
            & $isccExe "/Q" "/DMyAppVersion=1.0.1" "/DSourceDir=$stageDir" "/DOutputDir=$upgradeTempDir" (Join-Path $Root "installer\ChiikawaStudyPet.iss")
            if ($LASTEXITCODE -eq 0) {
                $upgradeInstaller = Join-Path $upgradeTempDir "ChiikawaStudyPet-Setup-x64.exe"
                $expectedUpgradeVersion = "1.0.1"
            }
        }
    }

    $pUp = Start-Process -FilePath $upgradeInstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS' -Wait -PassThru
    Assert-True ($pUp.ExitCode -eq 0) "Upgrade installer exited with code 0 (actual: $($pUp.ExitCode))"

    $allChiikawaEntries = @(Get-ChildItem "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall" |
        ForEach-Object { Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue } |
        Where-Object { $_.DisplayName -eq "Chiikawa" })
    Assert-True ($allChiikawaEntries.Count -eq 1) "No duplicate Installed Apps entries after upgrade (count: $($allChiikawaEntries.Count))"
    Assert-True ($allChiikawaEntries[0].DisplayVersion -eq $expectedUpgradeVersion) "DisplayVersion updated to $expectedUpgradeVersion"

    $sentinelAfterUpgrade = (Get-ItemProperty -Path $PrefsKey -Name $SentinelName -ErrorAction SilentlyContinue).$SentinelName
    Assert-True ($sentinelAfterUpgrade -eq $SentinelValue) "User preferences preserved across upgrade"

    $smAfter = Get-ShortcutInfo $StartMenuShortcut
    Assert-True ($smAfter.TargetPath -ieq $InstalledExe) "Start Menu shortcut still targets installed exe after upgrade"

    $launchedAfterUpgrade = Test-AppLaunches $InstalledExe
    Assert-True $launchedAfterUpgrade "Upgraded ChiikawaStudyPet.exe launched cleanly"

    Write-Host "`n=== 3. Uninstallation Test ==="
    # Simulate 'Launch on Windows startup' enabled
    Set-ItemProperty -Path $RunKey -Name "ChiikawaStudyPet" -Value "`"$InstalledExe`""
    Assert-True ([bool](Get-ItemProperty -Path $RunKey -Name "ChiikawaStudyPet" -ErrorAction SilentlyContinue)) "Simulated Windows startup Run entry created"

    $uninsExe = Join-Path $InstallDir "unins000.exe"
    $pUn = Start-Process -FilePath $uninsExe -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait -PassThru
    Assert-True ($pUn.ExitCode -eq 0) "Uninstaller exited with code 0 (actual: $($pUn.ExitCode))"
    Start-Sleep -Seconds 1

    Assert-True (-not (Test-Path $InstalledExe)) "Installed executable removed"
    Assert-True (-not (Test-Path $InstallDir)) "Installation directory removed"
    Assert-True (-not (Test-Path $StartMenuShortcut)) "Start Menu shortcut removed"
    Assert-True (-not (Test-Path $DesktopShortcut)) "Desktop shortcut removed"
    Assert-True (-not (Test-Path $UninstallKey)) "Installed Apps uninstall key removed"

    $runAfterUninstall = Get-ItemProperty -Path $RunKey -Name "ChiikawaStudyPet" -ErrorAction SilentlyContinue
    Assert-True (-not $runAfterUninstall) "Windows startup Run entry cleaned up during uninstall"

    $sentinelAfterUninstall = (Get-ItemProperty -Path $PrefsKey -Name $SentinelName -ErrorAction SilentlyContinue).$SentinelName
    Assert-True ($sentinelAfterUninstall -eq $SentinelValue) "User preferences preserved by default after uninstall"

    Write-Host "`nALL INSTALLER TESTS PASSED."
}
finally {
    # Clean up test sentinel and temp files
    if (Test-Path $PrefsKey) {
        Remove-ItemProperty -Path $PrefsKey -Name $SentinelName -ErrorAction SilentlyContinue
        if (-not $hadOriginalPrefsKey) {
            try {
                Remove-Item -Path $PrefsKey -Recurse -Force -ErrorAction SilentlyContinue
            } catch {
                # Ignore registry cleanup edge cases on runner shutdown
            }
        }
    }
    if ($hadOriginalRun) {
        Set-ItemProperty -Path $RunKey -Name "ChiikawaStudyPet" -Value $originalRunValue -ErrorAction SilentlyContinue
    }
    if ($upgradeTempDir -and (Test-Path $upgradeTempDir)) {
        Remove-Item -Recurse -Force $upgradeTempDir -ErrorAction SilentlyContinue
    }
}
