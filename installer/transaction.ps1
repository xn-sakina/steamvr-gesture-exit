[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Install", "Commit", "Rollback", "Recover", "Uninstall")]
    [string]$Action,
    [string]$PayloadArchive,
    [string]$Target,
    [string]$Version,
    [ValidateSet("en", "zh")]
    [string]$Language = "en",
    [string]$ResultFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:AppKey = "dev.kanamio.steamvrgestureexit"
$script:DataDirectory = Join-Path $env:LOCALAPPDATA "SteamVRGestureExit"
$script:StatePath = Join-Path $script:DataDirectory "installer-state.json"
$script:JournalPath = Join-Path $script:DataDirectory "installer-transaction.json"
$script:ManifestRelativePath = "manifest/steamvr_gesture_exit.vrmanifest"
$script:ExecutableRelativePath = "bin/win64/steamvr_gesture_exit.exe"
$script:Messages = @{
    en = @{
        steamvr_missing = "SteamVR was not found. Install SteamVR first."
        package_invalid = "The installer package is incomplete or damaged."
        running_failed = "This app could not be stopped. Close SteamVR and try again."
        registration_unknown = "SteamVR could not be found. Try again."
        registration_failed = "This app could not be registered with SteamVR."
        rollback_failed = "The operation failed. Run this installer again."
        uninstall_failed = "This app could not be removed from SteamVR."
        operation_busy = "Another installation or uninstallation is already running."
        steamvr_running = "Close SteamVR before uninstalling."
        cleanup_unverified = "SteamVR cleanup could not be fully verified. No files were removed."
        unsafe_target = "Choose a dedicated installation folder."
        write_failed = "Files could not be written. Check permissions and antivirus settings."
        unexpected = "The operation failed."
        ok = "OK"
    }
    zh = @{
        steamvr_missing = "未找到 SteamVR，请先安装 SteamVR。"
        package_invalid = "安装包不完整或已损坏。"
        running_failed = "无法停止本软件，请关闭 SteamVR 后重试。"
        registration_unknown = "无法找到 SteamVR，请重试。"
        registration_failed = "向 SteamVR 注册本软件失败。"
        rollback_failed = "执行失败，请再次运行此安装程序。"
        uninstall_failed = "无法从 SteamVR 移除本软件。"
        operation_busy = "另一个安装或卸载操作正在运行。"
        steamvr_running = "请先关闭 SteamVR，再卸载本软件。"
        cleanup_unverified = "无法完整确认 SteamVR 清理结果，未删除安装文件。"
        unsafe_target = "请选择一个独立的安装目录。"
        write_failed = "无法写入文件，请检查权限和杀毒软件拦截。"
        unexpected = "操作失败。"
        ok = "成功"
    }
}

function Get-Message([string]$Key) {
    $table = $script:Messages[$Language]
    if ($table.ContainsKey($Key)) { return [string]$table[$Key] }
    return [string]$script:Messages.en[$Key]
}

function Write-Result([string]$Text) {
    if (-not $ResultFile) { return }
    $parent = Split-Path $ResultFile -Parent
    if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Set-Content -LiteralPath $ResultFile -Value $Text -Encoding UTF8
}

function Write-JsonAtomic([string]$Path, [object]$Value) {
    $parent = Split-Path $Path -Parent
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporary = "$Path.tmp"
    try {
        $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding UTF8
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Read-Json([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Get-FullPath([string]$Path) {
    if (-not $Path) { return $null }
    return [IO.Path]::GetFullPath($Path)
}

function Test-SamePath([string]$Left, [string]$Right) {
    if (-not $Left -or -not $Right) { return $false }
    try { return (Get-FullPath $Left) -ieq (Get-FullPath $Right) } catch { return $false }
}

function Assert-SafeTarget([string]$Path) {
    $full = Get-FullPath $Path
    $root = [IO.Path]::GetPathRoot($full)
    $blocked = @($root, $env:SystemRoot, $env:USERPROFILE, $env:LOCALAPPDATA, $env:APPDATA)
    foreach ($entry in $blocked) {
        if ($entry -and (Test-SamePath $full $entry)) { throw [InvalidOperationException]::new("unsafe_target") }
    }
}

function Invoke-Helper {
    param(
        [Parameter(Mandatory = $true)][string]$Cli,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [int[]]$AllowedExitCodes = @(0)
    )
    $output = & $Cli @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($AllowedExitCodes -notcontains $exitCode) {
        $detail = ($output | Out-String).Trim()
        throw "Helper failed ($exitCode): $detail"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = ($output | Out-String).Trim() }
}

function Test-ManifestInstalled([string]$Cli) {
    $result = Invoke-Helper -Cli $Cli -Arguments @("--manifest-status", $script:AppKey) -AllowedExitCodes @(0, 2)
    return $result.ExitCode -eq 0
}

function Get-SteamInstallPaths {
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($location in @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820"
    )) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if ($entry -and $entry.InstallLocation) { $paths.Add([string]$entry.InstallLocation) }
    }
    $steamRoots = [Collections.Generic.List[string]]::new()
    foreach ($location in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam")) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if (-not $entry) { continue }
        foreach ($property in @("SteamPath", "InstallPath")) {
            if ($entry.$property) { $steamRoots.Add([string]$entry.$property) }
        }
    }
    if (${env:ProgramFiles(x86)}) { $steamRoots.Add((Join-Path ${env:ProgramFiles(x86)} "Steam")) }
    foreach ($steamRoot in ($steamRoots | Select-Object -Unique)) {
        $paths.Add((Join-Path $steamRoot "steamapps\common\SteamVR"))
        $libraries = Join-Path $steamRoot "steamapps\libraryfolders.vdf"
        if (-not (Test-Path -LiteralPath $libraries -PathType Leaf)) { continue }
        foreach ($line in Get-Content -LiteralPath $libraries -ErrorAction SilentlyContinue) {
            if ($line -match '^\s*"path"\s*"(.+)"\s*$') {
                $libraryRoot = $Matches[1] -replace '\\\\', '\'
                $paths.Add((Join-Path $libraryRoot "steamapps\common\SteamVR"))
            }
        }
    }
    return @($paths | ForEach-Object {
        try { Get-FullPath $_ } catch { $null }
    } | Where-Object { $_ } | Select-Object -Unique)
}

function Assert-SteamVrInstalled {
    foreach ($path in Get-SteamInstallPaths) {
        if (Test-Path -LiteralPath (Join-Path $path "bin\win64\vrserver.exe") -PathType Leaf) { return }
    }
    throw [InvalidOperationException]::new("steamvr_missing")
}

function Get-OpenVrConfigDirectories {
    $directories = [Collections.Generic.List[string]]::new()
    $registryPath = if ($env:VR_PATHREG_OVERRIDE) { $env:VR_PATHREG_OVERRIDE } else {
        Join-Path $env:LOCALAPPDATA "openvr\openvrpaths.vrpath"
    }
    if (Test-Path -LiteralPath $registryPath -PathType Leaf) {
        $registry = Get-Content -LiteralPath $registryPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($path in @($registry.config)) {
            if ($path) { $directories.Add((Get-FullPath ([string]$path))) }
        }
    }
    foreach ($location in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam")) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if (-not $entry) { continue }
        foreach ($property in @("SteamPath", "InstallPath")) {
            if ($entry.$property) { $directories.Add((Join-Path (Get-FullPath ([string]$entry.$property)) "config")) }
        }
    }
    if (${env:ProgramFiles(x86)}) { $directories.Add((Join-Path ${env:ProgramFiles(x86)} "Steam\config")) }
    return @($directories | Select-Object -Unique | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
}

function Test-OwnedManifest([string]$Path, [string[]]$KnownPaths) {
    foreach ($known in $KnownPaths) {
        if ($known -and (Test-SamePath $Path $known)) { return $true }
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return [IO.Path]::GetFileName($Path) -ieq "steamvr_gesture_exit.vrmanifest"
    }
    try {
        $manifest = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
        return @($manifest.applications).Count -eq 1 -and $manifest.applications[0].app_key -eq $script:AppKey
    } catch { return $false }
}

function Get-OwnedManifestPaths(
    [string[]]$KnownPaths = @(),
    [AllowNull()][string[]]$Directories = $null
) {
    if ($null -eq $Directories) { $Directories = @(Get-OpenVrConfigDirectories) }
    $found = [Collections.Generic.List[string]]::new()
    foreach ($directory in @($Directories)) {
        $configPath = Join-Path $directory "appconfig.json"
        if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { continue }
        $config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($manifestPath in @($config.manifest_paths)) {
            if ($manifestPath -and (Test-OwnedManifest ([string]$manifestPath) $KnownPaths)) {
                $full = Get-FullPath ([string]$manifestPath)
                if (-not ($found | Where-Object { $_ -ieq $full })) { $found.Add($full) }
            }
        }
    }
    return @($found)
}

function Assert-SteamVrStopped {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw [InvalidOperationException]::new("steamvr_running")
    }
}

function Remove-OfflineRegistration([string[]]$KnownPaths) {
    Assert-SteamVrStopped
    $directories = @(Get-OpenVrConfigDirectories)
    if ($directories.Count -eq 0) {
        throw [InvalidOperationException]::new("cleanup_unverified")
    }
    $backupRoot = Join-Path $script:DataDirectory ("uninstall-openvr-" + [guid]::NewGuid().ToString("N"))
    $backups = [Collections.Generic.List[object]]::new()
    $succeeded = $false
    try {
        $index = 0
        foreach ($directory in $directories) {
            Assert-SteamVrStopped
            $configPath = Join-Path $directory "appconfig.json"
            if (Test-Path -LiteralPath $configPath -PathType Leaf) {
                $configBackup = Join-Path $backupRoot ("appconfig-$index.json")
                New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
                Copy-Item -LiteralPath $configPath -Destination $configBackup -Force
                $backups.Add([ordered]@{ original = $configPath; backup = $configBackup })
                $index++

                $config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
                $kept = @($config.manifest_paths | Where-Object {
                    -not (Test-OwnedManifest ([string]$_) $KnownPaths)
                })
                if ($kept.Count -ne @($config.manifest_paths).Count) {
                    $config.manifest_paths = $kept
                    $temporary = "$configPath.steamvr-gesture-exit.tmp"
                    try {
                        $config | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $temporary -Encoding UTF8
                        Get-Content -LiteralPath $temporary -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null
                        Move-Item -LiteralPath $temporary -Destination $configPath -Force
                    } finally {
                        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
                    }
                }
            }
            foreach ($name in @("$($script:AppKey).vrappconfig", $script:AppKey)) {
                $ownedConfig = Join-Path (Join-Path $directory "vrappconfig") $name
                if (-not (Test-Path -LiteralPath $ownedConfig -PathType Leaf)) { continue }
                $ownedBackup = Join-Path $backupRoot ("vrappconfig-$index.json")
                New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
                Copy-Item -LiteralPath $ownedConfig -Destination $ownedBackup -Force
                $backups.Add([ordered]@{ original = $ownedConfig; backup = $ownedBackup })
                $index++
                Remove-Item -LiteralPath $ownedConfig -Force
            }
        }
        Assert-SteamVrStopped
        foreach ($directory in $directories) {
            if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
                throw [InvalidOperationException]::new("cleanup_unverified")
            }
        }
        $verificationDirectories = @(
            (@($directories) + @(Get-OpenVrConfigDirectories)) | Select-Object -Unique
        )
        if ($verificationDirectories.Count -eq 0) {
            throw [InvalidOperationException]::new("cleanup_unverified")
        }
        $remainingManifests = @(Get-OwnedManifestPaths $KnownPaths $verificationDirectories)
        $remainingAppConfigs = @(
            foreach ($directory in $verificationDirectories) {
                foreach ($name in @("$($script:AppKey).vrappconfig", $script:AppKey)) {
                    $path = Join-Path (Join-Path $directory "vrappconfig") $name
                    if (Test-Path -LiteralPath $path -PathType Leaf) { $path }
                }
            }
        )
        Assert-SteamVrStopped
        if ($remainingManifests.Count -ne 0 -or $remainingAppConfigs.Count -ne 0) {
            throw [InvalidOperationException]::new("cleanup_unverified")
        }
        $succeeded = $true
    } finally {
        if (-not $succeeded) {
            foreach ($record in $backups) {
                New-Item -ItemType Directory -Path (Split-Path ([string]$record.original) -Parent) -Force | Out-Null
                Copy-Item -LiteralPath ([string]$record.backup) -Destination ([string]$record.original) -Force
            }
        }
        if ($succeeded) { Remove-Item -LiteralPath $backupRoot -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

function Stop-OwnedProcesses([string[]]$AllowedPaths) {
    foreach ($process in @(Get-Process -Name "steamvr_gesture_exit" -ErrorAction SilentlyContinue)) {
        $actualPath = $null
        try { $actualPath = $process.Path } catch { }
        if (-not $actualPath) { continue }
        $owned = $false
        foreach ($allowed in $AllowedPaths) {
            if ($allowed -and (Test-SamePath $actualPath $allowed)) { $owned = $true; break }
        }
        if (-not $owned) { continue }
        try { $creationToken = $process.StartTime.ToFileTimeUtc() }
        catch { throw [InvalidOperationException]::new("running_failed") }
        try {
            $stopEvent = [Threading.EventWaitHandle]::OpenExisting("Local\SteamVRGestureExit.Stop.$($process.Id).$creationToken")
            try { [void]$stopEvent.Set() } finally { $stopEvent.Dispose() }
        } catch {
            if (-not $process.HasExited) { throw [InvalidOperationException]::new("running_failed") }
        }
        if (-not $process.WaitForExit(20000)) { throw [InvalidOperationException]::new("running_failed") }
    }
}

function Assert-Payload([string]$Root, [string]$ExpectedVersion) {
    $manifestPath = Join-Path $Root "payload.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw [InvalidOperationException]::new("package_invalid") }
    $manifest = Read-Json $manifestPath
    if ($manifest.schema -ne 1 -or $manifest.version -ne $ExpectedVersion -or @($manifest.files).Count -eq 0) {
        throw [InvalidOperationException]::new("package_invalid")
    }
    foreach ($record in @($manifest.files)) {
        if (-not $record.path -or $record.path.Contains("..") -or [IO.Path]::IsPathRooted([string]$record.path)) {
            throw [InvalidOperationException]::new("package_invalid")
        }
        $path = Join-Path $Root ([string]$record.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw [InvalidOperationException]::new("package_invalid") }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne ([string]$record.sha256).ToLowerInvariant()) { throw [InvalidOperationException]::new("package_invalid") }
    }
    return $manifest
}

function Invoke-FailureInjection([string]$Point) {
    if ($env:STEAMVR_GESTURE_EXIT_INSTALLER_FAIL_AFTER -and $env:STEAMVR_GESTURE_EXIT_INSTALLER_FAIL_AFTER -eq $Point) {
        throw "Injected installer failure after $Point."
    }
}

function Restore-State([object]$OldState) {
    if ($null -eq $OldState) {
        Remove-Item -LiteralPath $script:StatePath -Force -ErrorAction SilentlyContinue
    } else {
        Write-JsonAtomic $script:StatePath $OldState
    }
}

function Undo-Journal {
    $journal = Read-Json $script:JournalPath
    if ($null -eq $journal) { return }
    $errors = [Collections.Generic.List[string]]::new()
    $helper = [string]$journal.helper
    try {
        if (Test-Path -LiteralPath $helper -PathType Leaf) {
            $known = @([string]$journal.new_manifest)
            if ($journal.old_manifest) { $known += [string]$journal.old_manifest }
            $registered = @(Get-OwnedManifestPaths $known)
            $newMayBeRegistered = $journal.phase -in @(
                "registering_new",
                "new_registered",
                "pending_inno_commit",
                # Backward compatibility for journals written by installers
                # released before the Inno commit boundary was made explicit.
                "installed"
            )
            if ($newMayBeRegistered -and ((Test-ManifestInstalled $helper) -or
                ($registered | Where-Object { Test-SamePath $_ ([string]$journal.new_manifest) }))) {
                try { Invoke-Helper -Cli $helper -Arguments @("--unregister-manifest", [string]$journal.new_manifest) | Out-Null }
                catch { $errors.Add($_.Exception.Message) }
            }
        }
    } catch { $errors.Add($_.Exception.Message) }

    try {
        if ($journal.old_manifest -and (Test-Path -LiteralPath ([string]$journal.old_manifest) -PathType Leaf) -and
            (Test-Path -LiteralPath $helper -PathType Leaf)) {
            Invoke-Helper -Cli $helper -Arguments @("--register-manifest", [string]$journal.old_manifest) | Out-Null
            Invoke-Helper -Cli $helper -Arguments @("--set-autolaunch", $script:AppKey, "true") | Out-Null
        }
    } catch { $errors.Add($_.Exception.Message) }

    try {
        foreach ($file in @($journal.files)) {
            $destination = Join-Path ([string]$journal.target) ([string]$file.path)
            $backup = Join-Path ([string]$journal.backup) ([string]$file.path)
            Remove-Item -LiteralPath "$destination.steamvr-gesture-exit-new" -Force -ErrorAction SilentlyContinue
            if ([bool]$file.existed -and (Test-Path -LiteralPath $backup -PathType Leaf)) {
                New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $backup -Destination $destination -Force
            } else {
                Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
            }
        }
        $ownedDirectories = @($journal.files | ForEach-Object {
            Split-Path (Join-Path ([string]$journal.target) ([string]$_.path)) -Parent
        } | Sort-Object { $_.Length } -Descending -Unique)
        foreach ($directory in $ownedDirectories) {
            if ((Test-Path -LiteralPath $directory -PathType Container) -and
                @((Get-ChildItem -LiteralPath $directory -Force)).Count -eq 0) {
                Remove-Item -LiteralPath $directory -Force -ErrorAction SilentlyContinue
            }
        }
        if (-not [bool]$journal.target_existed -and
            (Test-Path -LiteralPath ([string]$journal.target) -PathType Container) -and
            @((Get-ChildItem -LiteralPath ([string]$journal.target) -Force)).Count -eq 0) {
            Remove-Item -LiteralPath ([string]$journal.target) -Force -ErrorAction SilentlyContinue
        }
    } catch { $errors.Add($_.Exception.Message) }

    try { Restore-State $journal.old_state } catch { $errors.Add($_.Exception.Message) }
    if ($errors.Count -ne 0) { throw ($errors -join " ") }
    Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:JournalPath -Force -ErrorAction SilentlyContinue
}

function Invoke-Recover {
    if (-not (Test-Path -LiteralPath $script:JournalPath -PathType Leaf)) { return }
    $journal = Read-Json $script:JournalPath
    if ($journal.phase -eq "committed") {
        Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $script:JournalPath -Force -ErrorAction SilentlyContinue
        return
    }
    Undo-Journal
}

function Invoke-Install {
    if (-not $PayloadArchive -or -not $Target -or -not $Version) { throw [InvalidOperationException]::new("package_invalid") }
    Assert-SafeTarget $Target
    Invoke-Recover
    Assert-SteamVrInstalled

    $work = Join-Path ([IO.Path]::GetTempPath()) ("steamvr-gesture-exit-" + [guid]::NewGuid().ToString("N"))
    try {
        New-Item -ItemType Directory -Path $work -Force | Out-Null
        Expand-Archive -LiteralPath $PayloadArchive -DestinationPath $work -Force
        $payload = Assert-Payload $work $Version
        $helper = Join-Path $work "installer\steamvr_gesture_exit_cli.exe"
        if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { throw [InvalidOperationException]::new("package_invalid") }
        [void](Test-ManifestInstalled $helper)

        $oldState = Read-Json $script:StatePath
        $oldManifest = $null
        $oldInstall = $null
        if ($oldState) {
            $oldManifest = [string]$oldState.manifest_path
            $oldInstall = [string]$oldState.install_path
        }
        if (-not $oldManifest) {
            $developmentReceipt = Join-Path $script:DataDirectory "registered-manifest.txt"
            if (Test-Path -LiteralPath $developmentReceipt -PathType Leaf) {
                $oldManifest = (Get-Content -LiteralPath $developmentReceipt -Raw -Encoding UTF8).Trim()
            }
        }
        $newManifest = Join-Path (Get-FullPath $Target) ($script:ManifestRelativePath -replace '/', '\')
        $known = @($newManifest)
        if ($oldManifest) { $known += $oldManifest }
        $ownedRegistrations = @(Get-OwnedManifestPaths $known)
        if (-not $oldManifest -and $ownedRegistrations.Count -eq 1) { $oldManifest = $ownedRegistrations[0] }
        if ((Test-ManifestInstalled $helper) -and -not $oldManifest) {
            throw [InvalidOperationException]::new("registration_unknown")
        }

        $backup = Join-Path $script:DataDirectory ("installer-rollback-" + [guid]::NewGuid().ToString("N"))
        $files = [Collections.Generic.List[object]]::new()
        foreach ($record in @($payload.files)) {
            $relative = ([string]$record.path).Replace('/', '\')
            $destination = Join-Path (Get-FullPath $Target) $relative
            $existed = Test-Path -LiteralPath $destination -PathType Leaf
            if ($existed) {
                $backupPath = Join-Path $backup $relative
                New-Item -ItemType Directory -Path (Split-Path $backupPath -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $destination -Destination $backupPath -Force
            }
            $files.Add([ordered]@{ path = $relative; existed = $existed })
        }
        $installedHelper = Join-Path (Get-FullPath $Target) "installer\steamvr_gesture_exit_cli.exe"
        $journal = [ordered]@{
            schema = 1
            phase = "prepared"
            target = Get-FullPath $Target
            target_existed = Test-Path -LiteralPath (Get-FullPath $Target) -PathType Container
            backup = $backup
            helper = $installedHelper
            old_state = $oldState
            old_manifest = $oldManifest
            new_manifest = $newManifest
            files = $files
        }
        Write-JsonAtomic $script:JournalPath $journal

        try {
            Invoke-FailureInjection "journal"
            # Install the recovery helper first. The durable journal always points
            # to this copy, so a power loss after unregistering the old manifest
            # still leaves enough machinery for the next run to compensate.
            foreach ($relative in @("installer\openvr_api.dll", "installer\steamvr_gesture_exit_cli.exe")) {
                $source = Join-Path $work $relative
                $destination = Join-Path (Get-FullPath $Target) $relative
                New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $source -Destination "$destination.steamvr-gesture-exit-new" -Force
                Move-Item -LiteralPath "$destination.steamvr-gesture-exit-new" -Destination $destination -Force
            }
            $journal.phase = "unregistering"
            Write-JsonAtomic $script:JournalPath $journal
            if (Test-ManifestInstalled $helper) {
                Invoke-Helper -Cli $helper -Arguments @("--unregister-manifest", $oldManifest) | Out-Null
            }
            $journal.phase = "old_unregistered"
            Write-JsonAtomic $script:JournalPath $journal
            Invoke-FailureInjection "old-unregistered"
            $allowedExecutables = @((Join-Path (Get-FullPath $Target) ($script:ExecutableRelativePath -replace '/', '\')))
            if ($oldInstall) { $allowedExecutables += Join-Path $oldInstall ($script:ExecutableRelativePath -replace '/', '\') }
            Stop-OwnedProcesses $allowedExecutables

            foreach ($record in @($payload.files)) {
                $relative = ([string]$record.path).Replace('/', '\')
                if ($relative -in @("installer\openvr_api.dll", "installer\steamvr_gesture_exit_cli.exe")) { continue }
                $source = Join-Path $work $relative
                $destination = Join-Path (Get-FullPath $Target) $relative
                New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
                $temporary = "$destination.steamvr-gesture-exit-new"
                Copy-Item -LiteralPath $source -Destination $temporary -Force
                if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
                    throw [InvalidOperationException]::new("write_failed")
                }
                Move-Item -LiteralPath $temporary -Destination $destination -Force
            }
            $journal.phase = "files_installed"
            Write-JsonAtomic $script:JournalPath $journal
            Invoke-FailureInjection "files-installed"
            $journal.phase = "registering_new"
            Write-JsonAtomic $script:JournalPath $journal
            Invoke-Helper -Cli (Join-Path (Get-FullPath $Target) "installer\steamvr_gesture_exit_cli.exe") -Arguments @("--register-manifest", $newManifest) | Out-Null
            if (-not (Test-ManifestInstalled $installedHelper)) { throw [InvalidOperationException]::new("registration_failed") }
            $journal.phase = "new_registered"
            Write-JsonAtomic $script:JournalPath $journal
            Invoke-FailureInjection "new-registered"

            $newState = [ordered]@{
                schema = 1
                version = $Version
                install_path = Get-FullPath $Target
                manifest_path = $newManifest
            }
            Write-JsonAtomic $script:StatePath $newState
            Invoke-FailureInjection "state-written"
            # The payload and SteamVR registration are ready, but Inno Setup
            # has not created its uninstall log/ARP entry yet. Recovery must
            # compensate this phase unless ssDone explicitly commits it.
            $journal.phase = "pending_inno_commit"
            $journal.helper = $installedHelper
            Write-JsonAtomic $script:JournalPath $journal
        } catch {
            $installError = $_
            try { Undo-Journal } catch {
                throw [InvalidOperationException]::new("rollback_failed", $installError.Exception)
            }
            throw $installError
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-Commit {
    $journal = Read-Json $script:JournalPath
    if ($null -eq $journal) { throw "Transaction is not ready to commit." }
    if ($journal.phase -ne "pending_inno_commit") { throw "Transaction is not ready to commit." }
    $journal.phase = "committed"
    Write-JsonAtomic $script:JournalPath $journal
    Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:JournalPath -Force -ErrorAction SilentlyContinue
}

function Invoke-Uninstall {
    if (-not $Target) { throw [InvalidOperationException]::new("uninstall_failed") }
    Assert-SteamVrStopped
    Invoke-Recover
    $state = Read-Json $script:StatePath
    if ($state -and -not (Test-SamePath ([string]$state.install_path) $Target)) {
        return
    }
    $manifest = Join-Path (Get-FullPath $Target) ($script:ManifestRelativePath -replace '/', '\')
    if ($state -and $state.manifest_path) { $manifest = [string]$state.manifest_path }
    $executable = Join-Path (Get-FullPath $Target) ($script:ExecutableRelativePath -replace '/', '\')
    Assert-SteamVrStopped
    Stop-OwnedProcesses @($executable)
    try {
        Remove-OfflineRegistration @($manifest)
    } catch {
        if ($script:Messages.en.ContainsKey($_.Exception.Message)) { throw }
        throw [InvalidOperationException]::new("uninstall_failed", $_.Exception)
    }

    Remove-Item -LiteralPath $script:StatePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:JournalPath -Force -ErrorAction SilentlyContinue
    try { Remove-Item -LiteralPath $script:DataDirectory -Recurse -Force -ErrorAction Stop } catch { }
}

$operationMutex = $null
$operationMutexAcquired = $false
$exitCode = 0
try {
    $userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    if (-not $userSid) { throw [InvalidOperationException]::new("operation_busy") }
    $mutexName = "Global\SteamVRGestureExit.Installer.Transaction.$userSid"
    try {
        $operationMutex = [Threading.Mutex]::new($false, $mutexName)
    } catch {
        throw [InvalidOperationException]::new("operation_busy", $_.Exception)
    }
    try {
        $operationMutexAcquired = $operationMutex.WaitOne([TimeSpan]::FromSeconds(30))
    } catch [Threading.AbandonedMutexException] {
        $operationMutexAcquired = $true
    }
    if (-not $operationMutexAcquired) {
        throw [InvalidOperationException]::new("operation_busy")
    }
    if ($ResultFile) { Remove-Item -LiteralPath $ResultFile -Force -ErrorAction SilentlyContinue }
    switch ($Action) {
        "Install" { Invoke-Install }
        "Commit" { Invoke-Commit }
        "Rollback" { Undo-Journal }
        "Recover" { Invoke-Recover }
        "Uninstall" { Invoke-Uninstall }
    }
    Write-Result (Get-Message "ok")
} catch {
    $key = $_.Exception.Message
    if (-not $script:Messages.en.ContainsKey($key)) {
        $key = switch -Regex ($_.Exception.ToString()) {
            "access|denied|permission|virus|antivirus|locked|being used" { "write_failed"; break }
            "SteamVR was not found" { "steamvr_missing"; break }
            "registration|manifest|OpenVR|Helper failed" { if ($Action -eq "Uninstall") { "uninstall_failed" } else { "registration_failed" }; break }
            default { "unexpected" }
        }
    }
    $message = Get-Message $key
    Write-Result $message
    Write-Error "$message $($_.Exception.Message)"
    $exitCode = 1
} finally {
    if ($operationMutexAcquired) { $operationMutex.ReleaseMutex() }
    if ($null -ne $operationMutex) { $operationMutex.Dispose() }
}
exit $exitCode
