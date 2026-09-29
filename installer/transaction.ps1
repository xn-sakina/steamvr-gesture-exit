[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Preflight", "Install", "Commit", "Rollback", "Recover", "Uninstall")]
    [string]$Action,
    [string]$PayloadArchive,
    [string]$PayloadManifest,
    [string]$Target,
    [string]$Version,
    [long]$PayloadInstallSize,
    [long]$PayloadExpandedSize,
    [long]$PayloadArchiveSize,
    [long]$PayloadLargestFileSize,
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
$script:JournalBackupPath = "$($script:JournalPath).backup"
$script:OwnershipMarkerName = ".steamvr-gesture-exit-owner.json"
$script:ManifestRelativePath = "manifest/steamvr_gesture_exit.vrmanifest"
$script:ExecutableRelativePath = "bin/win64/steamvr_gesture_exit.exe"
$script:Messages = @{
    en = @{
        steamvr_missing = "SteamVR was not found. Install SteamVR first."
        package_invalid = "The installer package is incomplete or damaged."
        running_failed = "This app could not be stopped. Close SteamVR and try again."
        registration_unknown = "SteamVR could not be found. Try again."
        registration_failed = "This app could not be added to SteamVR."
        rollback_failed = "The operation failed. Run this installer again."
        uninstall_failed = "This app could not be removed from SteamVR."
        operation_busy = "Another installer or uninstaller for this app is already running."
        steamvr_running = "Close SteamVR before uninstalling."
        cleanup_unverified = "It could not be confirmed whether this app was removed from SteamVR. The uninstall may be incomplete."
        unsafe_target = "Choose a dedicated installation folder."
        disk_space = "There is not enough free space at the target installation location."
        write_failed = "Files could not be written. Check permissions and antivirus settings."
        unexpected = "The operation failed."
        ok = "OK"
    }
    zh = @{
        steamvr_missing = "未找到 SteamVR，请先安装 SteamVR。"
        package_invalid = "安装包不完整或已损坏。"
        running_failed = "无法停止本软件，请关闭 SteamVR 后重试。"
        registration_unknown = "无法找到 SteamVR，请重试。"
        registration_failed = "向 SteamVR 添加本软件失败。"
        rollback_failed = "执行失败，请再次运行此安装程序。"
        uninstall_failed = "无法从 SteamVR 移除本软件。"
        operation_busy = "本软件的另一个安装或卸载程序正在运行。"
        steamvr_running = "请先关闭 SteamVR，再卸载本软件。"
        cleanup_unverified = "无法确认是否从 SteamVR 移除了本软件，卸载可能不完整。"
        unsafe_target = "请选择一个独立的安装目录。"
        disk_space = "目标安装位置可用空间不足。"
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

function Get-TextSha256([string]$Text) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Read-JournalSlots {
    $records = [Collections.Generic.List[object]]::new()
    $found = $false
    foreach ($path in @($script:JournalPath, $script:JournalBackupPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $found = $true
        try {
            $value = Read-Json $path
            if ($value.PSObject.Properties["journal_format"] -and
                [int]$value.journal_format -eq 2 -and
                $value.PSObject.Properties["revision"] -and
                $value.PSObject.Properties["payload_json"] -and
                $value.PSObject.Properties["checksum"]) {
                $payloadJson = [string]$value.payload_json
                if ((Get-TextSha256 $payloadJson) -ne ([string]$value.checksum).ToLowerInvariant()) {
                    throw "Journal checksum mismatch."
                }
                $payload = $payloadJson | ConvertFrom-Json
                $records.Add([pscustomobject]@{
                    Path = $path
                    Revision = [long]$value.revision
                    Payload = $payload
                })
            } elseif ($value.PSObject.Properties["phase"]) {
                # Journals from earlier releases had no envelope or checksum.
                $records.Add([pscustomobject]@{ Path = $path; Revision = 0L; Payload = $value })
            } else {
                throw "Unrecognized journal format."
            }
        } catch {
            # A damaged slot is ignored only here; Read-Journal fails
            # conservatively when no checksum-valid slot remains.
        }
    }
    return [pscustomobject]@{ Found = $found; Records = @($records) }
}

function Read-Journal {
    $slots = Read-JournalSlots
    if (-not $slots.Found) { return $null }
    if (@($slots.Records).Count -eq 0) {
        throw [InvalidOperationException]::new("rollback_failed")
    }
    return @($slots.Records | Sort-Object Revision -Descending)[0].Payload
}

function Write-Journal([object]$Journal) {
    $slots = Read-JournalSlots
    $revision = 1L
    if (@($slots.Records).Count -ne 0) {
        $revision = [long](@($slots.Records | Measure-Object Revision -Maximum).Maximum) + 1L
    }
    $payloadJson = $Journal | ConvertTo-Json -Depth 20 -Compress
    $envelope = [ordered]@{
        journal_format = 2
        revision = $revision
        payload_json = $payloadJson
        checksum = Get-TextSha256 $payloadJson
    }
    # Alternating slots ensure an interrupted replacement always leaves one
    # checksum-verified prior revision available for conservative recovery.
    $destination = if (($revision % 2) -eq 0) { $script:JournalPath } else { $script:JournalBackupPath }
    Write-JsonAtomic $destination $envelope
    $written = Read-Json $destination
    if (-not $written.PSObject.Properties["revision"] -or [long]$written.revision -ne $revision -or
        -not $written.PSObject.Properties["payload_json"] -or [string]$written.payload_json -cne $payloadJson -or
        -not $written.PSObject.Properties["checksum"] -or
        (Get-TextSha256 ([string]$written.payload_json)) -ne ([string]$written.checksum).ToLowerInvariant()) {
        throw [InvalidOperationException]::new("rollback_failed")
    }
}

function Remove-Journal {
    Remove-Item -LiteralPath $script:JournalPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:JournalBackupPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$($script:JournalPath).tmp" -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$($script:JournalBackupPath).tmp" -Force -ErrorAction SilentlyContinue
}

function Get-FullPath([string]$Path) {
    if (-not $Path) { return $null }
    return [IO.Path]::GetFullPath($Path)
}

function Test-SamePath([string]$Left, [string]$Right) {
    if (-not $Left -or -not $Right) { return $false }
    try { return (Get-FullPath $Left) -ieq (Get-FullPath $Right) } catch { return $false }
}

function Test-PathInside([string]$Path, [string]$Directory) {
    if (-not $Path -or -not $Directory) { return $false }
    try {
        $fullPath = (Get-FullPath $Path).TrimEnd('\')
        $fullDirectory = (Get-FullPath $Directory).TrimEnd('\')
        return $fullPath.StartsWith($fullDirectory + "\", [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

function Assert-NoReparsePoint([string]$Path) {
    $current = Get-FullPath $Path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw [InvalidOperationException]::new("unsafe_target")
            }
        }
        $parent = Split-Path $current -Parent
        if (-not $parent -or (Test-SamePath $parent $current)) { break }
        $current = $parent
    }
}

function Assert-SafeTarget([string]$Path) {
    $full = Get-FullPath $Path
    $root = [IO.Path]::GetPathRoot($full)
    $blocked = @($root, $env:SystemRoot, $env:USERPROFILE, $env:LOCALAPPDATA, $env:APPDATA)
    foreach ($entry in $blocked) {
        if ($entry -and (Test-SamePath $full $entry)) { throw [InvalidOperationException]::new("unsafe_target") }
    }
    $blockedTrees = [Collections.Generic.List[string]]::new()
    foreach ($entry in @(
        $env:SystemRoot,
        $env:ProgramData,
        $env:ProgramFiles,
        ${env:ProgramFiles(x86)},
        $env:CommonProgramFiles,
        ${env:CommonProgramFiles(x86)},
        [Environment]::GetFolderPath([Environment+SpecialFolder]::Desktop),
        [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments),
        [Environment]::GetFolderPath([Environment+SpecialFolder]::MyMusic),
        [Environment]::GetFolderPath([Environment+SpecialFolder]::MyPictures),
        [Environment]::GetFolderPath([Environment+SpecialFolder]::MyVideos)
    )) {
        if ($entry) { $blockedTrees.Add([string]$entry) }
    }
    foreach ($entry in @($blockedTrees | Select-Object -Unique)) {
        if ((Test-SamePath $full $entry) -or (Test-PathInside $full $entry)) {
            throw [InvalidOperationException]::new("unsafe_target")
        }
    }
    Assert-NoReparsePoint $full
}

function Get-NormalizedRelativePath([string]$Path) {
    if (-not $Path -or [IO.Path]::IsPathRooted($Path) -or $Path.Contains("..") -or $Path.Contains(":")) {
        throw [InvalidOperationException]::new("unsafe_target")
    }
    return $Path.Replace('/', '\').TrimStart('\')
}

function Get-RelativePathUnder([string]$Root, [string]$Path) {
    $fullRoot = (Get-FullPath $Root).TrimEnd('\')
    $fullPath = Get-FullPath $Path
    $prefix = $fullRoot + "\"
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw [InvalidOperationException]::new("unsafe_target")
    }
    return $fullPath.Substring($prefix.Length)
}

function Test-InnoOwnedPath([string]$Relative) {
    return $Relative -imatch '^installer\\uninstall\\unins[0-9]+\.(exe|dat|msg)$'
}

function Assert-TargetOwnership([string]$Path, [object]$Payload, [object]$OldState) {
    $full = Get-FullPath $Path
    if (-not (Test-Path -LiteralPath $full)) { return @() }
    if (-not (Test-Path -LiteralPath $full -PathType Container)) {
        throw [InvalidOperationException]::new("unsafe_target")
    }
    Assert-NoReparsePoint $full
    $entries = @(Get-ChildItem -LiteralPath $full -Force)
    if ($entries.Count -eq 0) { return @() }

    $ownedFiles = [Collections.Generic.List[string]]::new()
    $markerPath = Join-Path $full $script:OwnershipMarkerName
    if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        try {
            $marker = Read-Json $markerPath
            if ([int]$marker.schema -ne 1 -or [string]$marker.app_key -ne $script:AppKey -or
                -not (Test-SamePath ([string]$marker.install_path) $full) -or
                @($marker.files).Count -eq 0) {
                throw "Invalid ownership marker."
            }
            foreach ($relative in @($marker.files)) {
                $normalized = Get-NormalizedRelativePath ([string]$relative)
                if ($normalized -ieq $script:OwnershipMarkerName -or (Test-InnoOwnedPath $normalized)) {
                    throw "Invalid ownership marker."
                }
                if (-not ($ownedFiles | Where-Object { $_ -ieq $normalized })) {
                    $ownedFiles.Add($normalized)
                }
            }
        } catch {
            throw [InvalidOperationException]::new("unsafe_target")
        }
    } elseif ($OldState -and $OldState.PSObject.Properties["install_path"] -and
        (Test-SamePath ([string]$OldState.install_path) $full)) {
        # One-time migration for installations made before ownership markers.
        foreach ($record in @($Payload.files)) {
            $ownedFiles.Add((Get-NormalizedRelativePath ([string]$record.path)))
        }
    } else {
        throw [InvalidOperationException]::new("unsafe_target")
    }

    $allowedDirectories = [Collections.Generic.List[string]]::new()
    $allowedDirectories.Add("installer\uninstall")
    foreach ($relative in @($ownedFiles)) {
        $parent = Split-Path $relative -Parent
        while ($parent) {
            if (-not ($allowedDirectories | Where-Object { $_ -ieq $parent })) {
                $allowedDirectories.Add($parent)
            }
            $next = Split-Path $parent -Parent
            if (-not $next -or $next -eq $parent) { break }
            $parent = $next
        }
    }

    $pendingDirectories = [Collections.Generic.Queue[string]]::new()
    $pendingDirectories.Enqueue($full)
    while ($pendingDirectories.Count -ne 0) {
        $directory = $pendingDirectories.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            # Inspect one level at a time so a junction is rejected before
            # PowerShell has any opportunity to enumerate through it.
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw [InvalidOperationException]::new("unsafe_target")
            }
            $relative = Get-RelativePathUnder $full $item.FullName
            if ($item.PSIsContainer) {
                if (-not ($allowedDirectories | Where-Object { $_ -ieq $relative })) {
                    throw [InvalidOperationException]::new("unsafe_target")
                }
                $pendingDirectories.Enqueue($item.FullName)
            } elseif ($relative -ine $script:OwnershipMarkerName -and
                -not ($ownedFiles | Where-Object { $_ -ieq $relative }) -and
                -not (Test-InnoOwnedPath $relative)) {
                throw [InvalidOperationException]::new("unsafe_target")
            }
        }
    }
    return @($ownedFiles)
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
        if ($entry) {
            $installLocation = $entry.PSObject.Properties["InstallLocation"]
            if ($installLocation -and $installLocation.Value) {
                $paths.Add([string]$installLocation.Value)
            }
        }
    }
    $steamRoots = [Collections.Generic.List[string]]::new()
    foreach ($location in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam")) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if (-not $entry) { continue }
        foreach ($property in @("SteamPath", "InstallPath")) {
            $value = $entry.PSObject.Properties[$property]
            if ($value -and $value.Value) { $steamRoots.Add([string]$value.Value) }
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
            $value = $entry.PSObject.Properties[$property]
            if ($value -and $value.Value) {
                $directories.Add((Join-Path (Get-FullPath ([string]$value.Value)) "config"))
            }
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

function Get-OpenVrSnapshotRecords([string]$BackupRoot) {
    $records = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.List[string]]::new()
    $index = 0
    foreach ($directory in @(Get-OpenVrConfigDirectories)) {
        foreach ($path in @(
            (Join-Path $directory "appconfig.json"),
            (Join-Path (Join-Path $directory "vrappconfig") "$($script:AppKey).vrappconfig"),
            (Join-Path (Join-Path $directory "vrappconfig") $script:AppKey)
        )) {
            $full = Get-FullPath $path
            if ($seen | Where-Object { $_ -ieq $full }) { continue }
            $seen.Add($full)
            $existed = Test-Path -LiteralPath $full -PathType Leaf
            $records.Add([ordered]@{
                path = $full
                existed = $existed
                backup = Join-Path $BackupRoot ("openvr\$index.json")
                backup_ready = -not $existed
                sha256 = $null
            })
            $index++
        }
    }
    return $records
}

function Restore-OpenVrSnapshot([object]$Journal) {
    if (-not $Journal.PSObject.Properties["openvr_files"] -or @($Journal.openvr_files).Count -eq 0) {
        throw [InvalidOperationException]::new("cleanup_unverified")
    }
    Assert-SteamVrStopped
    foreach ($record in @($Journal.openvr_files)) {
        if (-not [bool]$record.backup_ready) {
            throw [InvalidOperationException]::new("rollback_failed")
        }
        $path = [string]$record.path
        if ([bool]$record.existed) {
            $backup = [string]$record.backup
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf) -or
                (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash.ToLowerInvariant() -ne
                    ([string]$record.sha256).ToLowerInvariant()) {
                throw [InvalidOperationException]::new("rollback_failed")
            }
            New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
            $temporary = "$path.steamvr-gesture-exit-restore"
            try {
                Copy-Item -LiteralPath $backup -Destination $temporary -Force
                Move-Item -LiteralPath $temporary -Destination $path -Force
            } finally {
                Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
            }
        } else {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        }
    }
    Assert-SteamVrStopped
}

function Add-OfflineManifest([string]$ManifestPath) {
    if (-not $ManifestPath -or -not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw [InvalidOperationException]::new("rollback_failed")
    }
    Assert-SteamVrStopped
    $directories = @(Get-OpenVrConfigDirectories)
    foreach ($directory in $directories) {
        $configPath = Join-Path $directory "appconfig.json"
        if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { continue }
        $config = Read-Json $configPath
        $paths = @()
        if ($config.PSObject.Properties["manifest_paths"]) { $paths = @($config.manifest_paths) }
        if (-not ($paths | Where-Object { Test-SamePath ([string]$_) $ManifestPath })) {
            $paths += (Get-FullPath $ManifestPath)
            if ($config.PSObject.Properties["manifest_paths"]) {
                $config.manifest_paths = $paths
            } else {
                $config | Add-Member -NotePropertyName manifest_paths -NotePropertyValue $paths
            }
            Write-JsonAtomic $configPath $config
        }
        Assert-SteamVrStopped
        return
    }
    throw [InvalidOperationException]::new("cleanup_unverified")
}

function Invoke-OfflineRollback([object]$Journal) {
    if ($Journal.PSObject.Properties["openvr_files"] -and @($Journal.openvr_files).Count -ne 0) {
        Restore-OpenVrSnapshot $Journal
        return
    }

    # Compatibility path for an interrupted transaction from an older
    # installer that did not persist an OpenVR snapshot.
    Remove-OfflineRegistration @([string]$Journal.new_manifest)
    if ($Journal.old_manifest) { Add-OfflineManifest ([string]$Journal.old_manifest) }
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

function Assert-PayloadManifest([object]$Manifest, [string]$ExpectedVersion) {
    if (-not $Manifest -or $Manifest.schema -ne 1 -or $Manifest.version -ne $ExpectedVersion -or
        @($Manifest.files).Count -eq 0) {
        throw [InvalidOperationException]::new("package_invalid")
    }
    $seen = [Collections.Generic.List[string]]::new()
    [long]$totalSize = 0
    [long]$largestSize = 0
    foreach ($record in @($Manifest.files)) {
        if (-not $record.path -or $record.path.Contains("..") -or ([string]$record.path).Contains(":") -or
            [IO.Path]::IsPathRooted([string]$record.path) -or -not $record.PSObject.Properties["size"] -or
            [long]$record.size -lt 0) {
            throw [InvalidOperationException]::new("package_invalid")
        }
        $relative = Get-NormalizedRelativePath ([string]$record.path)
        if ($seen | Where-Object { $_ -ieq $relative }) {
            throw [InvalidOperationException]::new("package_invalid")
        }
        $seen.Add($relative)
        $totalSize = [long]($totalSize + [long]$record.size)
        if ([long]$record.size -gt $largestSize) { $largestSize = [long]$record.size }
    }
    return [pscustomobject]@{
        Manifest = $Manifest
        TotalSize = $totalSize
        LargestSize = $largestSize
    }
}

function Assert-Payload([string]$Root, [string]$ExpectedVersion) {
    $manifestPath = Join-Path $Root "payload.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw [InvalidOperationException]::new("package_invalid") }
    $validation = Assert-PayloadManifest (Read-Json $manifestPath) $ExpectedVersion
    foreach ($record in @($validation.Manifest.files)) {
        $path = Join-Path $Root ([string]$record.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw [InvalidOperationException]::new("package_invalid") }
        if ([long]((Get-Item -LiteralPath $path).Length) -ne [long]$record.size) {
            throw [InvalidOperationException]::new("package_invalid")
        }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne ([string]$record.sha256).ToLowerInvariant()) { throw [InvalidOperationException]::new("package_invalid") }
    }
    return $validation.Manifest
}

function Add-RequiredDiskSpace([hashtable]$Volumes, [string]$Path, [long]$Bytes, [string]$Purpose) {
    if ($Bytes -lt 0) { throw [InvalidOperationException]::new("package_invalid") }
    $root = [IO.Path]::GetPathRoot((Get-FullPath $Path))
    if (-not $root) { throw [InvalidOperationException]::new("disk_space") }
    if (-not $Volumes.ContainsKey($root)) {
        $Volumes[$root] = [pscustomobject]@{
            Root = $root
            Required = 0L
            Purposes = [Collections.Generic.List[string]]::new()
        }
    }
    $Volumes[$root].Required = [long]([long]$Volumes[$root].Required + $Bytes)
    $Volumes[$root].Purposes.Add($Purpose)
}

function Invoke-Preflight {
    if (-not $Target -or -not $Version -or -not $PayloadManifest -or
        $PayloadInstallSize -le 0 -or $PayloadExpandedSize -le 0 -or
        $PayloadArchiveSize -le 0 -or $PayloadLargestFileSize -le 0) {
        throw [InvalidOperationException]::new("package_invalid")
    }
    if (-not (Test-Path -LiteralPath $PayloadManifest -PathType Leaf)) {
        throw [InvalidOperationException]::new("package_invalid")
    }

    Assert-SafeTarget $Target
    Assert-SteamVrInstalled
    $manifestFile = Get-Item -LiteralPath $PayloadManifest
    $validation = Assert-PayloadManifest (Read-Json $PayloadManifest) $Version
    if ([long]$validation.TotalSize -ne $PayloadInstallSize -or
        [long]$validation.LargestSize -ne $PayloadLargestFileSize -or
        [long]($PayloadInstallSize + [long]$manifestFile.Length) -ne $PayloadExpandedSize) {
        throw [InvalidOperationException]::new("package_invalid")
    }

    $oldState = Read-Json $script:StatePath
    $previousOwnedFiles = @(Assert-TargetOwnership $Target $validation.Manifest $oldState)
    $backupRelativePaths = [Collections.Generic.List[string]]::new()
    foreach ($record in @($validation.Manifest.files)) {
        $relative = Get-NormalizedRelativePath ([string]$record.path)
        if (-not ($backupRelativePaths | Where-Object { $_ -ieq $relative })) {
            $backupRelativePaths.Add($relative)
        }
    }
    foreach ($relativeValue in $previousOwnedFiles) {
        $relative = Get-NormalizedRelativePath ([string]$relativeValue)
        if (-not ($backupRelativePaths | Where-Object { $_ -ieq $relative })) {
            $backupRelativePaths.Add($relative)
        }
    }
    $backupRelativePaths.Add($script:OwnershipMarkerName)

    [long]$rollbackBytes = 0
    foreach ($relative in $backupRelativePaths) {
        $existing = Join-Path (Get-FullPath $Target) $relative
        if (Test-Path -LiteralPath $existing -PathType Leaf) {
            $rollbackBytes = [long]($rollbackBytes + [long]((Get-Item -LiteralPath $existing).Length))
        }
    }
    foreach ($record in @(Get-OpenVrSnapshotRecords (Join-Path $script:DataDirectory "preflight"))) {
        if ([bool]$record.existed) {
            $rollbackBytes = [long]($rollbackBytes + [long]((Get-Item -LiteralPath ([string]$record.path)).Length))
        }
    }
    if (Test-Path -LiteralPath $script:StatePath -PathType Leaf) {
        $rollbackBytes = [long]($rollbackBytes + [long]((Get-Item -LiteralPath $script:StatePath).Length))
    }

    # Include one MiB per role for journal envelopes, ownership metadata,
    # directory allocation, and the small extracted coordinator files.
    [long]$metadataReserve = 1MB
    $volumes = @{}
    Add-RequiredDiskSpace $volumes (Get-FullPath $Target) `
        ([long]($PayloadInstallSize + $PayloadLargestFileSize + $metadataReserve)) "target"
    Add-RequiredDiskSpace $volumes ([IO.Path]::GetTempPath()) `
        ([long]($PayloadArchiveSize + $PayloadExpandedSize + $metadataReserve)) "temporary"
    Add-RequiredDiskSpace $volumes $script:DataDirectory `
        ([long]($rollbackBytes + $metadataReserve)) "rollback"

    foreach ($volume in $volumes.Values) {
        try {
            $available = [long]([IO.DriveInfo]::new([string]$volume.Root).AvailableFreeSpace)
        } catch {
            throw [InvalidOperationException]::new("disk_space", $_.Exception)
        }
        if ($available -lt [long]$volume.Required) {
            throw [InvalidOperationException]::new("disk_space")
        }
    }
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
    $journal = Read-Journal
    if ($null -eq $journal) { return }
    if ($journal.phase -in @("committed", "rolled_back")) {
        Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Journal
        return
    }
    $errors = [Collections.Generic.List[string]]::new()
    $registrationMayHaveChanged = $journal.phase -in @(
        "unregistering",
        "old_unregistered",
        "files_installed",
        "registering_new",
        "new_registered",
        "pending_inno_commit",
        # Backward compatibility for journals written before the explicit
        # Inno commit boundary and before durable backup preparation.
        "installed"
    )
    if ($registrationMayHaveChanged) {
        $helperSucceeded = $false
        $helperCandidates = [Collections.Generic.List[string]]::new()
        if ($journal.helper) { $helperCandidates.Add([string]$journal.helper) }
        $backupHelper = Join-Path ([string]$journal.backup) "installer\steamvr_gesture_exit_cli.exe"
        if (-not ($helperCandidates | Where-Object { Test-SamePath $_ $backupHelper })) {
            $helperCandidates.Add($backupHelper)
        }
        foreach ($helper in $helperCandidates) {
            if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { continue }
            try {
                $newMayBeRegistered = $journal.phase -in @(
                    "registering_new", "new_registered", "pending_inno_commit", "installed"
                )
                $registeredNewPath = $false
                if ($newMayBeRegistered) {
                    $registeredNewPath = @(Get-OwnedManifestPaths @([string]$journal.new_manifest) | Where-Object {
                        Test-SamePath $_ ([string]$journal.new_manifest)
                    }).Count -ne 0
                }
                if ($newMayBeRegistered -and ((Test-ManifestInstalled $helper) -or $registeredNewPath)) {
                    Invoke-Helper -Cli $helper -Arguments @("--unregister-manifest", [string]$journal.new_manifest) | Out-Null
                }
                if ($journal.old_manifest -and
                    (Test-Path -LiteralPath ([string]$journal.old_manifest) -PathType Leaf)) {
                    Invoke-Helper -Cli $helper -Arguments @("--register-manifest", [string]$journal.old_manifest) | Out-Null
                    Invoke-Helper -Cli $helper -Arguments @("--set-autolaunch", $script:AppKey, "true") | Out-Null
                }
                $helperSucceeded = $true
                break
            } catch { }
        }
        if (-not $helperSucceeded) {
            try { Invoke-OfflineRollback $journal }
            catch { $errors.Add($_.Exception.Message) }
        }
    }

    try {
        $backupPreparationOnly = $journal.phase -eq "preparing_backup"
        if (-not $backupPreparationOnly) {
            foreach ($file in @($journal.files)) {
                $destination = Join-Path ([string]$journal.target) ([string]$file.path)
                $backup = Join-Path ([string]$journal.backup) ([string]$file.path)
                Remove-Item -LiteralPath "$destination.steamvr-gesture-exit-new" -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath "$destination.tmp" -Force -ErrorAction SilentlyContinue
                if ([bool]$file.existed) {
                    $ready = -not $file.PSObject.Properties["backup_ready"] -or [bool]$file.backup_ready
                    if (-not $ready -or -not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                        throw [InvalidOperationException]::new("rollback_failed")
                    }
                    if ($file.PSObject.Properties["sha256"] -and $file.sha256 -and
                        (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash.ToLowerInvariant() -ne
                            ([string]$file.sha256).ToLowerInvariant()) {
                        throw [InvalidOperationException]::new("rollback_failed")
                    }
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
        }
    } catch { $errors.Add($_.Exception.Message) }

    try { Restore-State $journal.old_state } catch { $errors.Add($_.Exception.Message) }
    if ($errors.Count -ne 0) { throw ($errors -join " ") }
    $journal.phase = "rolled_back"
    Write-Journal $journal
    Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Journal
}

function Remove-OrphanedRollbackDirectories {
    if ((Test-Path -LiteralPath $script:JournalPath -PathType Leaf) -or
        (Test-Path -LiteralPath $script:JournalBackupPath -PathType Leaf)) { return }
    foreach ($directory in @(Get-ChildItem -LiteralPath $script:DataDirectory -Directory -Filter "installer-rollback-*" -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $directory.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-Recover {
    if (-not (Test-Path -LiteralPath $script:JournalPath -PathType Leaf) -and
        -not (Test-Path -LiteralPath $script:JournalBackupPath -PathType Leaf)) {
        Remove-Journal
        Remove-OrphanedRollbackDirectories
        return
    }
    $journal = Read-Journal
    if ($journal.phase -in @("committed", "rolled_back")) {
        Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Journal
        Remove-OrphanedRollbackDirectories
        return
    }
    Undo-Journal
    Remove-OrphanedRollbackDirectories
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
        $previousOwnedFiles = @(Assert-TargetOwnership $Target $payload $oldState)
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
        $payloadRelativePaths = [Collections.Generic.List[string]]::new()
        foreach ($record in @($payload.files)) {
            $relative = Get-NormalizedRelativePath ([string]$record.path)
            $payloadRelativePaths.Add($relative)
            $destination = Join-Path (Get-FullPath $Target) $relative
            $existed = Test-Path -LiteralPath $destination -PathType Leaf
            $files.Add([ordered]@{
                path = $relative
                existed = $existed
                backup_ready = -not $existed
                sha256 = $null
                remove_on_install = $false
            })
        }
        foreach ($relative in $previousOwnedFiles) {
            if ($payloadRelativePaths | Where-Object { $_ -ieq $relative }) { continue }
            $destination = Join-Path (Get-FullPath $Target) $relative
            $existed = Test-Path -LiteralPath $destination -PathType Leaf
            $files.Add([ordered]@{
                path = $relative
                existed = $existed
                backup_ready = -not $existed
                sha256 = $null
                remove_on_install = $true
            })
        }
        $markerPath = Join-Path (Get-FullPath $Target) $script:OwnershipMarkerName
        $markerExisted = Test-Path -LiteralPath $markerPath -PathType Leaf
        $files.Add([ordered]@{
            path = $script:OwnershipMarkerName
            existed = $markerExisted
            backup_ready = -not $markerExisted
            sha256 = $null
            remove_on_install = $false
        })
        $openVrFiles = Get-OpenVrSnapshotRecords $backup
        $installedHelper = Join-Path (Get-FullPath $Target) "installer\steamvr_gesture_exit_cli.exe"
        $journal = [ordered]@{
            schema = 2
            phase = "preparing_backup"
            target = Get-FullPath $Target
            target_existed = Test-Path -LiteralPath (Get-FullPath $Target) -PathType Container
            backup = $backup
            helper = $installedHelper
            old_state = $oldState
            old_manifest = $oldManifest
            new_manifest = $newManifest
            files = $files
            openvr_files = $openVrFiles
        }
        # Persist every intended backup before creating it. A kill at any point
        # therefore leaves no unreferenced rollback directory.
        Write-Journal $journal

        try {
            foreach ($file in @($journal.files)) {
                if (-not [bool]$file.existed) { continue }
                $source = Join-Path ([string]$journal.target) ([string]$file.path)
                $backupPath = Join-Path ([string]$journal.backup) ([string]$file.path)
                New-Item -ItemType Directory -Path (Split-Path $backupPath -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $source -Destination $backupPath -Force
                $hash = (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($hash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()) {
                    throw [InvalidOperationException]::new("rollback_failed")
                }
                $file.sha256 = $hash
                $file.backup_ready = $true
                Write-Journal $journal
            }
            foreach ($file in @($journal.openvr_files)) {
                if (-not [bool]$file.existed) { continue }
                New-Item -ItemType Directory -Path (Split-Path ([string]$file.backup) -Parent) -Force | Out-Null
                Copy-Item -LiteralPath ([string]$file.path) -Destination ([string]$file.backup) -Force
                $hash = (Get-FileHash -LiteralPath ([string]$file.backup) -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($hash -ne (Get-FileHash -LiteralPath ([string]$file.path) -Algorithm SHA256).Hash.ToLowerInvariant()) {
                    throw [InvalidOperationException]::new("rollback_failed")
                }
                $file.sha256 = $hash
                $file.backup_ready = $true
                Write-Journal $journal
            }
            $journal.phase = "prepared"
            Write-Journal $journal
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
            Write-Journal $journal
            if (Test-ManifestInstalled $helper) {
                Invoke-Helper -Cli $helper -Arguments @("--unregister-manifest", $oldManifest) | Out-Null
            }
            $journal.phase = "old_unregistered"
            Write-Journal $journal
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
            foreach ($file in @($journal.files)) {
                if ($file.PSObject.Properties["remove_on_install"] -and [bool]$file.remove_on_install) {
                    $obsoletePath = Join-Path ([string]$journal.target) ([string]$file.path)
                    if (Test-Path -LiteralPath $obsoletePath -PathType Leaf) {
                        Remove-Item -LiteralPath $obsoletePath -Force -ErrorAction Stop
                    }
                }
            }
            $obsoleteDirectories = [Collections.Generic.List[string]]::new()
            foreach ($file in @($journal.files)) {
                if (-not $file.PSObject.Properties["remove_on_install"] -or -not [bool]$file.remove_on_install) {
                    continue
                }
                $directory = Split-Path (Join-Path ([string]$journal.target) ([string]$file.path)) -Parent
                while ($directory -and -not (Test-SamePath $directory ([string]$journal.target))) {
                    if (-not ($obsoleteDirectories | Where-Object { Test-SamePath $_ $directory })) {
                        $obsoleteDirectories.Add($directory)
                    }
                    $directory = Split-Path $directory -Parent
                }
            }
            foreach ($directory in @($obsoleteDirectories | Sort-Object { $_.Length } -Descending)) {
                if ((Test-Path -LiteralPath $directory -PathType Container) -and
                    @((Get-ChildItem -LiteralPath $directory -Force)).Count -eq 0) {
                    Remove-Item -LiteralPath $directory -Force -ErrorAction Stop
                }
            }
            $ownershipMarker = [ordered]@{
                schema = 1
                app_key = $script:AppKey
                install_path = Get-FullPath $Target
                files = @($payload.files | ForEach-Object {
                    Get-NormalizedRelativePath ([string]$_.path)
                } | Sort-Object -Unique)
            }
            Write-JsonAtomic $markerPath $ownershipMarker
            $journal.phase = "files_installed"
            Write-Journal $journal
            Invoke-FailureInjection "files-installed"
            $journal.phase = "registering_new"
            Write-Journal $journal
            Invoke-Helper -Cli (Join-Path (Get-FullPath $Target) "installer\steamvr_gesture_exit_cli.exe") -Arguments @("--register-manifest", $newManifest) | Out-Null
            if (-not (Test-ManifestInstalled $installedHelper)) { throw [InvalidOperationException]::new("registration_failed") }
            $journal.phase = "new_registered"
            Write-Journal $journal
            Invoke-FailureInjection "new-registered"

            $newState = [ordered]@{
                schema = 1
                version = $Version
                install_path = Get-FullPath $Target
                manifest_path = $newManifest
                ownership_marker = $markerPath
            }
            Write-JsonAtomic $script:StatePath $newState
            Invoke-FailureInjection "state-written"
            # The payload and SteamVR registration are ready, but Inno Setup
            # has not created its uninstall log/ARP entry yet. Recovery must
            # compensate this phase unless ssDone explicitly commits it.
            $journal.phase = "pending_inno_commit"
            $journal.helper = $installedHelper
            Write-Journal $journal
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
    $journal = Read-Journal
    if ($null -eq $journal) { throw "Transaction is not ready to commit." }
    if ($journal.phase -ne "pending_inno_commit") { throw "Transaction is not ready to commit." }
    $journal.phase = "committed"
    Write-Journal $journal
    Remove-Item -LiteralPath ([string]$journal.backup) -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Journal
    Remove-OrphanedRollbackDirectories
}

function Invoke-Uninstall {
    if (-not $Target) { throw [InvalidOperationException]::new("uninstall_failed") }
    Assert-SteamVrStopped
    Invoke-Recover
    $state = Read-Json $script:StatePath
    if ($state -and -not (Test-SamePath ([string]$state.install_path) $Target)) {
        return
    }
    $markerPath = Join-Path (Get-FullPath $Target) $script:OwnershipMarkerName
    if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        try {
            $marker = Read-Json $markerPath
            if ([int]$marker.schema -ne 1 -or [string]$marker.app_key -ne $script:AppKey -or
                -not (Test-SamePath ([string]$marker.install_path) $Target)) {
                throw "Invalid ownership marker."
            }
        } catch {
            throw [InvalidOperationException]::new("unsafe_target")
        }
    } elseif (-not $state -or -not (Test-SamePath ([string]$state.install_path) $Target)) {
        throw [InvalidOperationException]::new("unsafe_target")
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
    Remove-Journal
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
        "Preflight" { Invoke-Preflight }
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
