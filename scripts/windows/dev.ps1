[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("help", "configure", "build", "package", "register", "update", "status", "logs", "stop-orphans", "clean", "unregister")]
    [string]$Action = "help",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$SteamVrPath,
    [switch]$Follow,
    [switch]$KeepUserData
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$script:BuildDirectory = Join-Path $script:ProjectRoot "out\build\windows-x64"
$script:PackageDirectory = Join-Path $script:ProjectRoot "out\package\steamvr-gesture-exit"
$script:PackageStagingDirectory = "$($script:PackageDirectory).staging"
$script:PackagePreviousDirectory = "$($script:PackageDirectory).previous"
$script:ExecutablePath = Join-Path $script:PackageDirectory "bin\win64\steamvr_gesture_exit.exe"
$script:CliPath = Join-Path $script:PackageDirectory "bin\win64\steamvr_gesture_exit_cli.exe"
$script:ManifestPath = Join-Path $script:PackageDirectory "manifest\steamvr_gesture_exit.vrmanifest"
$script:PackageIntegrityFileName = ".package-integrity.json"
$script:PackageIntegrityCatalogPath = Join-Path (Split-Path $script:PackageDirectory -Parent) ".steamvr-gesture-exit-integrity.json"
$script:RuntimePackageFiles = @(
    "bin/win64/steamvr_gesture_exit.exe",
    "bin/win64/openvr_api.dll",
    "manifest/steamvr_gesture_exit.vrmanifest",
    "assets/brand/steamvr-gesture-exit.png"
)
$script:DebugOnlyPackageFiles = @("bin/win64/steamvr_gesture_exit_cli.exe")
$script:AppKey = "dev.kanamio.steamvrgestureexit"
$script:UserDataDirectory = Join-Path $env:LOCALAPPDATA "SteamVRGestureExit"
$script:LogDirectory = Join-Path $script:UserDataDirectory "logs"
$script:RegistrationReceiptPath = Join-Path $script:UserDataDirectory "registered-manifest.txt"
$script:PendingRegistrationReceiptPath = Join-Path $script:UserDataDirectory "registering-manifest.txt"

function Write-Step([string]$Message) {
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter()][string[]]$Arguments = @()
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

function Find-CMake {
    $command = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $vswhereCandidates = @()
    if (${env:ProgramFiles(x86)}) {
        $vswhereCandidates += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    }
    if ($env:ProgramFiles) {
        $vswhereCandidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"
    }
    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { continue }
        $installations = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($installation in $installations) {
            $bundledCMake = Join-Path $installation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $bundledCMake -PathType Leaf) { return $bundledCMake }
        }
    }
    throw @"
CMake was not found. Install Visual Studio 2022 or Build Tools 2022 with:
  - Desktop development with C++
  - MSVC v143 x64/x86 build tools
  - Windows 10/11 SDK
  - C++ CMake tools for Windows
Then reopen PowerShell.
"@
}

function Get-SteamInstallPaths {
    $paths = [Collections.Generic.List[string]]::new()
    if ($env:STEAMVR_PATH) { $paths.Add($env:STEAMVR_PATH) }
    if ($SteamVrPath) { $paths.Add($SteamVrPath) }

    foreach ($location in @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820"
    )) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if ($entry) {
            $installLocation = $entry.PSObject.Properties["InstallLocation"]
            if ($installLocation -and $installLocation.Value) { $paths.Add([string]$installLocation.Value) }
        }
    }

    $steamRoots = [Collections.Generic.List[string]]::new()
    foreach ($location in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam")) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if ($entry) {
            foreach ($property in @("SteamPath", "InstallPath")) {
                $value = $entry.PSObject.Properties[$property]
                if ($value -and $value.Value) { $steamRoots.Add([string]$value.Value) }
            }
        }
    }
    if (${env:ProgramFiles(x86)}) { $steamRoots.Add((Join-Path ${env:ProgramFiles(x86)} "Steam")) }

    foreach ($steamRoot in ($steamRoots | Select-Object -Unique)) {
        $paths.Add((Join-Path $steamRoot "steamapps\common\SteamVR"))
        $libraryFile = Join-Path $steamRoot "steamapps\libraryfolders.vdf"
        if (Test-Path -LiteralPath $libraryFile -PathType Leaf) {
            foreach ($line in Get-Content -LiteralPath $libraryFile -ErrorAction SilentlyContinue) {
                if ($line -match '^\s*"path"\s*"(.+)"\s*$') {
                    $libraryRoot = $Matches[1] -replace '\\\\', '\'
                    $paths.Add((Join-Path $libraryRoot "steamapps\common\SteamVR"))
                }
            }
        }
    }
    return $paths | ForEach-Object {
        try { [IO.Path]::GetFullPath($_) } catch { $null }
    } | Where-Object { $_ } | Select-Object -Unique
}

function Find-SteamVr {
    foreach ($candidate in Get-SteamInstallPaths) {
        if (Test-Path -LiteralPath (Join-Path $candidate "bin\win64\vrserver.exe") -PathType Leaf) { return $candidate }
    }
    throw "SteamVR was not found. Install SteamVR, pass -SteamVrPath, or set STEAMVR_PATH."
}

function Get-OpenVrConfigDirectories {
    $directories = [Collections.Generic.List[string]]::new()
    $registryPath = if ($env:VR_PATHREG_OVERRIDE) { $env:VR_PATHREG_OVERRIDE } else {
        Join-Path $env:LOCALAPPDATA "openvr\openvrpaths.vrpath"
    }
    if (Test-Path -LiteralPath $registryPath -PathType Leaf) {
        try { $registry = Get-Content -LiteralPath $registryPath -Raw -Encoding UTF8 | ConvertFrom-Json }
        catch { throw "Cannot inspect OpenVR path registry '$registryPath': $($_.Exception.Message)" }
        if (-not $registry -or -not $registry.PSObject.Properties["config"]) {
            throw "OpenVR path registry has no config list: $registryPath"
        }
        foreach ($path in @($registry.config)) {
            if ($path) { $directories.Add([IO.Path]::GetFullPath([string]$path)) }
        }
    }
    foreach ($location in @("HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam")) {
        $entry = Get-ItemProperty -LiteralPath $location -ErrorAction SilentlyContinue
        if (-not $entry) { continue }
        foreach ($property in @("SteamPath", "InstallPath")) {
            $value = $entry.PSObject.Properties[$property]
            if ($value -and $value.Value) {
                $directories.Add((Join-Path ([IO.Path]::GetFullPath([string]$value.Value)) "config"))
            }
        }
    }
    if (${env:ProgramFiles(x86)}) {
        $directories.Add((Join-Path ${env:ProgramFiles(x86)} "Steam\config"))
    }
    if ($env:ProgramFiles) {
        $directories.Add((Join-Path $env:ProgramFiles "Steam\config"))
    }
    return @($directories | Select-Object -Unique | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
}

function Test-OwnedManifestPath([string]$Path) {
    if (-not $Path) { return $false }
    if (-not [IO.Path]::IsPathRooted($Path)) { return $false }
    try { $fullPath = [IO.Path]::GetFullPath($Path) } catch { return $false }
    if ($fullPath -ieq [IO.Path]::GetFullPath($script:ManifestPath)) { return $true }
    foreach ($receipt in @($script:RegistrationReceiptPath, $script:PendingRegistrationReceiptPath)) {
        $recorded = Get-ReceiptValue $receipt
        if ($recorded -and $fullPath -ieq [IO.Path]::GetFullPath($recorded)) { return $true }
    }
    if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
        try {
            $manifest = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8 | ConvertFrom-Json
            $apps = @($manifest.applications)
            if ($apps.Count -eq 1 -and $apps[0].app_key -eq $script:AppKey) {
                return $true
            }
            return $false
        } catch { }
    }
    # A deleted or damaged old package can only be identified by its unique manifest name.
    return [IO.Path]::GetFileName($fullPath) -ieq "steamvr_gesture_exit.vrmanifest"
}

function Get-OpenVrResiduals {
    foreach ($directory in @(Get-OpenVrConfigDirectories)) {
        foreach ($name in @("$($script:AppKey).vrappconfig", $script:AppKey)) {
            $path = Join-Path (Join-Path $directory "vrappconfig") $name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                [pscustomobject]@{ Kind = "vrappconfig"; File = $path; Manifest = $null }
            }
        }
        $appConfig = Join-Path $directory "appconfig.json"
        if (-not (Test-Path -LiteralPath $appConfig -PathType Leaf)) { continue }
        try { $config = Get-Content -LiteralPath $appConfig -Raw -Encoding UTF8 | ConvertFrom-Json }
        catch { throw "Cannot inspect OpenVR app config '$appConfig': $($_.Exception.Message)" }
        if (-not $config) { throw "OpenVR app config is empty: $appConfig" }
        $paths = @()
        if ($config.PSObject.Properties["manifest_paths"]) { $paths = @($config.manifest_paths) }
        foreach ($path in $paths) {
            if (Test-OwnedManifestPath ([string]$path)) {
                [pscustomobject]@{ Kind = "manifest_path"; File = $appConfig; Manifest = [string]$path }
            }
        }
    }
}

function Remove-OpenVrResiduals {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "Close SteamVR before editing its on-disk application config."
    }
    $directories = @(Get-OpenVrConfigDirectories)
    if ($directories.Count -eq 0) {
        throw "No OpenVR configuration directory could be located; clean stopped before deleting the package."
    }
    foreach ($directory in $directories) {
        if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
            throw "SteamVR started during clean; no further config files were changed."
        }
        $appConfig = Join-Path $directory "appconfig.json"
        if (Test-Path -LiteralPath $appConfig -PathType Leaf) {
            try { $config = Get-Content -LiteralPath $appConfig -Raw -Encoding UTF8 | ConvertFrom-Json }
            catch { throw "Cannot clean OpenVR app config '$appConfig': $($_.Exception.Message)" }
            if (-not $config) { throw "OpenVR app config is empty: $appConfig" }
            $original = @()
            if ($config.PSObject.Properties["manifest_paths"]) { $original = @($config.manifest_paths) }
            $remaining = @($original | Where-Object { -not (Test-OwnedManifestPath ([string]$_)) })
            if ($remaining.Count -ne $original.Count) {
                $config.manifest_paths = $remaining
                $temporary = "$appConfig.steamvr-gesture-exit.tmp"
                try {
                    $json = $config | ConvertTo-Json -Depth 64
                    [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
                    $check = Get-Content -LiteralPath $temporary -Raw -Encoding UTF8 | ConvertFrom-Json
                    if (@($check.manifest_paths).Count -ne $remaining.Count) {
                        throw "OpenVR app config verification failed: $appConfig"
                    }
                    # PowerShell converts $null to an empty string for .NET string parameters.
                    [IO.File]::Replace($temporary, $appConfig, [NullString]::Value)
                    Write-Host "Removed $($original.Count - $remaining.Count) owned manifest path(s) from $appConfig"
                } finally {
                    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
                }
            }
        }
        foreach ($name in @("$($script:AppKey).vrappconfig", $script:AppKey)) {
            $path = Join-Path (Join-Path $directory "vrappconfig") $name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                Remove-Item -LiteralPath $path -Force
                Write-Host "Removed $path"
            }
        }
    }
    $remaining = @(Get-OpenVrResiduals)
    if ($remaining.Count -gt 0) {
        throw "OpenVR config still contains $($remaining.Count) add-on residue(s); no package files were removed."
    }
}

function Get-PackageFilePath([string]$PackageRoot, [string]$RelativePath) {
    return Join-Path $PackageRoot ($RelativePath -replace '/', '\')
}

function Get-FileSha256([string]$Path) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($Path)
        try {
            return [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace("-", "").ToLowerInvariant()
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha256.Dispose()
    }
}

function Get-RequiredPackageFiles([string]$PackageConfiguration) {
    $script:RuntimePackageFiles
    if ($PackageConfiguration -eq "Debug") { $script:DebugOnlyPackageFiles }
}

function Write-PackageIntegrityCatalog([object[]]$Records) {
    $catalog = [ordered]@{ schema_version = 1; records = $Records }
    $temporaryPath = "$($script:PackageIntegrityCatalogPath).tmp"
    try {
        $catalog | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
        if (Test-Path -LiteralPath $script:PackageIntegrityCatalogPath -PathType Leaf) {
            # PowerShell converts $null to an empty string for .NET string parameters.
            [IO.File]::Replace($temporaryPath, $script:PackageIntegrityCatalogPath, [NullString]::Value)
        } else {
            [IO.File]::Move($temporaryPath, $script:PackageIntegrityCatalogPath)
        }
    } finally {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
    }
}

function Write-PackageIntegrity([string]$PackageRoot) {
    $files = @()
    foreach ($relativePath in @(Get-RequiredPackageFiles $Configuration)) {
        $filePath = Get-PackageFilePath $PackageRoot $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Cannot complete the package because a required file is missing: $filePath"
        }
        $files += [ordered]@{
            path = $relativePath
            sha256 = (Get-FileSha256 $filePath)
        }
    }
    $integrity = [ordered]@{
        schema_version = 1
        package_id = [Guid]::NewGuid().ToString("D")
        configuration = $Configuration
        created_utc = [DateTime]::UtcNow.ToString("o")
        files = $files
    }
    if ($Configuration -eq "Release") {
        $records = @()
        if (Test-Path -LiteralPath $script:PackageIntegrityCatalogPath -PathType Leaf) {
            $catalog = Get-Content -LiteralPath $script:PackageIntegrityCatalogPath -Raw | ConvertFrom-Json
            if ($catalog.schema_version -ne 1) { throw "Package integrity catalog has an unsupported schema." }
            $records = @($catalog.records)
        }
        Write-PackageIntegrityCatalog @($records + $integrity)
    } else {
        $integrityPath = Join-Path $PackageRoot $script:PackageIntegrityFileName
        $integrity | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $integrityPath -Encoding UTF8
    }
}

function Get-PackageIntegrityRecords([string]$PackageRoot) {
    $integrityPath = Join-Path $PackageRoot $script:PackageIntegrityFileName
    if (Test-Path -LiteralPath $integrityPath -PathType Leaf) {
        Get-Content -LiteralPath $integrityPath -Raw | ConvertFrom-Json
        return
    }
    if (-not (Test-Path -LiteralPath $script:PackageIntegrityCatalogPath -PathType Leaf)) {
        throw "Package integrity record is missing for $PackageRoot. Run 'package' first."
    }
    $catalog = Get-Content -LiteralPath $script:PackageIntegrityCatalogPath -Raw | ConvertFrom-Json
    if ($catalog.schema_version -ne 1) { throw "Package integrity catalog has an unsupported schema." }
    foreach ($record in @($catalog.records)) { $record }
}

function Assert-PackageIntegrity([string]$PackageRoot, [object]$Integrity) {
    $integrity = $Integrity
    if ($integrity.schema_version -ne 1) {
        throw "Package integrity record has an unsupported schema."
    }
    $packageId = [Guid]::Empty
    if (-not [Guid]::TryParse([string]$integrity.package_id, [ref]$packageId)) {
        throw "Package integrity record has an invalid package id."
    }
    if (@("Debug", "Release") -notcontains [string]$integrity.configuration) {
        throw "Package integrity record has an invalid build configuration."
    }
    $requiredFiles = @(Get-RequiredPackageFiles ([string]$integrity.configuration))
    $recordedFiles = @($integrity.files)
    if ($recordedFiles.Count -ne $requiredFiles.Count) {
        throw "Package integrity record does not describe the complete required file set."
    }
    foreach ($relativePath in $requiredFiles) {
        $matchingRecords = @($recordedFiles | Where-Object { [string]$_.path -ceq $relativePath })
        if ($matchingRecords.Count -ne 1) {
            throw "Package integrity record does not uniquely identify '$relativePath'."
        }
        $expectedHash = [string]$matchingRecords[0].sha256
        if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Package integrity record has an invalid SHA-256 for '$relativePath'."
        }
        $filePath = Get-PackageFilePath $PackageRoot $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Required package file is missing: $filePath. Run 'package' first."
        }
        $actualHash = Get-FileSha256 $filePath
        if ($actualHash -ine $expectedHash) {
            throw "Package file does not match build '$packageId': $filePath. Run 'package' again."
        }
    }
    if ($integrity.configuration -eq "Release") {
        $actualFiles = @(Get-ChildItem -LiteralPath $PackageRoot -Recurse -File | ForEach-Object {
            $_.FullName.Substring($PackageRoot.Length).TrimStart('\') -replace '\\', '/'
        })
        if (@(Compare-Object $requiredFiles $actualFiles).Count -ne 0) {
            throw "Release package contains files outside the required runtime payload: $PackageRoot"
        }
    }
}

function Get-PackageIntegrityRecord([string]$PackageRoot) {
    $lastError = $null
    foreach ($record in @(Get-PackageIntegrityRecords $PackageRoot)) {
        try {
            Assert-PackageIntegrity $PackageRoot $record
            return $record
        } catch {
            $lastError = $_.Exception.Message
        }
    }
    throw "Package integrity verification failed for $PackageRoot. $lastError"
}

function Require-Package([string]$PackageRoot = $script:PackageDirectory) {
    [void](Get-PackageIntegrityRecord $PackageRoot)

    $executablePath = Get-PackageFilePath $PackageRoot "bin/win64/steamvr_gesture_exit.exe"
    $manifestPath = Get-PackageFilePath $PackageRoot "manifest/steamvr_gesture_exit.vrmanifest"
    try { $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json }
    catch { throw "Application manifest is not valid JSON: $manifestPath. $($_.Exception.Message)" }
    $application = @($manifest.applications) | Where-Object { $_.app_key -eq $script:AppKey } | Select-Object -First 1
    if (-not $application) { throw "Application manifest does not contain the expected app key '$script:AppKey'." }
    $expectedBinary = [IO.Path]::GetFullPath((Join-Path (Split-Path $manifestPath -Parent) ([string]$application.binary_path_windows)))
    if ($expectedBinary -ne [IO.Path]::GetFullPath($executablePath)) {
        throw "Manifest binary path resolves to '$expectedBinary', expected '$executablePath'."
    }
    $expectedImage = Get-PackageFilePath $PackageRoot "assets/brand/steamvr-gesture-exit.png"
    $manifestImage = [IO.Path]::GetFullPath((Join-Path (Split-Path $manifestPath -Parent) ([string]$application.image_path)))
    if ($manifestImage -ne [IO.Path]::GetFullPath($expectedImage)) {
        throw "Manifest image path resolves to '$manifestImage', expected '$expectedImage'."
    }
}

function Resolve-CliPath {
    $integrity = Get-PackageIntegrityRecord $script:PackageDirectory
    if ($integrity.configuration -eq "Debug") {
        $path = Join-Path $script:PackageDirectory "bin\win64\steamvr_gesture_exit_cli.exe"
    } else {
        $path = Join-Path $script:BuildDirectory "Release\steamvr_gesture_exit_cli.exe"
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Registration helper is missing: $path. Run 'package -Configuration $($integrity.configuration)' first."
    }
    return $path
}

function Get-ReceiptValue([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $value = Get-Content -LiteralPath $Path -Raw
    if ($null -eq $value) { return $null }
    $value = $value.Trim()
    if (-not $value) { return $null }
    return $value
}

function Set-ReceiptValue([string]$Path, [string]$Value) {
    New-Item -ItemType Directory -Path $script:UserDataDirectory -Force | Out-Null
    $temporaryPath = "$Path.tmp"
    try {
        Set-Content -LiteralPath $temporaryPath -Value $Value -Encoding UTF8
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    } finally {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
    }
}

function Remove-RegistrationReceipts {
    Remove-Item -LiteralPath $script:RegistrationReceiptPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $script:PendingRegistrationReceiptPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $script:UserDataDirectory "registering-autolaunch.txt") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$($script:RegistrationReceiptPath).tmp" -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$($script:PendingRegistrationReceiptPath).tmp" -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $script:UserDataDirectory "registering-autolaunch.txt.tmp") -Force -ErrorAction SilentlyContinue
}

function Get-PackagedAddOnProcesses {
    $expectedPath = [IO.Path]::GetFullPath($script:ExecutablePath)
    foreach ($process in @(Get-Process -Name "steamvr_gesture_exit" -ErrorAction SilentlyContinue)) {
        try {
            $actualPath = $process.Path
            if ($actualPath -and [IO.Path]::GetFullPath($actualPath) -ieq $expectedPath) {
                $process
            }
        } catch {
            # An inaccessible executable path cannot establish package ownership.
        }
    }
}

function Stop-AddOn {
    $processes = @(Get-PackagedAddOnProcesses)
    if ($processes.Count -eq 0) { return }
    Write-Step "Requesting graceful shutdown of the packaged add-on"
    foreach ($process in $processes) {
        if ($process.HasExited) { continue }
        try { $creationToken = $process.StartTime.ToFileTimeUtc() }
        catch { throw "Cannot identify packaged add-on PID $($process.Id); package was not modified." }
        $eventName = "Local\SteamVRGestureExit.Stop.$($process.Id).$creationToken"
        try { $stopEvent = [Threading.EventWaitHandle]::OpenExisting($eventName) }
        catch {
            if ($process.HasExited) { continue }
            throw "Packaged add-on PID $($process.Id) has no cooperative stop signal; package was not modified. $($_.Exception.Message)"
        }
        try { [void]$stopEvent.Set() }
        finally { $stopEvent.Dispose() }
        if (-not $process.WaitForExit(20000)) {
            throw "Packaged add-on PID $($process.Id) did not finish shutdown within 20 seconds; package was not modified."
        }
    }
}

function Invoke-Stop-Orphans {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "SteamVR is running. Close SteamVR normally before cleaning orphan add-on processes."
    }
    $orphans = @(Get-Process -Name "steamvr_gesture_exit" -ErrorAction SilentlyContinue)
    if ($orphans.Count -eq 0) {
        Write-Host "No orphan steamvr_gesture_exit.exe processes found." -ForegroundColor Green
        return
    }
    foreach ($orphan in $orphans) {
        try { $startToken = $orphan.StartTime.ToFileTimeUtc() }
        catch { throw "Cannot verify the identity of PID $($orphan.Id); no process was stopped for this PID." }
        $path = "<path unavailable>"
        try { if ($orphan.Path) { $path = $orphan.Path } } catch { }
        Write-Host "Stopping orphan PID $($orphan.Id): $path"
        if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
            throw "SteamVR started during orphan cleanup; stop it normally and retry."
        }
        $current = Get-Process -Id $orphan.Id -ErrorAction SilentlyContinue
        if (-not $current) { continue }
        try { $sameProcess = $current.ProcessName -eq "steamvr_gesture_exit" -and $current.StartTime.ToFileTimeUtc() -eq $startToken }
        catch { $sameProcess = $false }
        if (-not $sameProcess) { throw "PID $($orphan.Id) changed identity during orphan cleanup; it was not stopped." }
        Stop-Process -Id $orphan.Id -Force -ErrorAction Stop
    }
    Start-Sleep -Milliseconds 500
    if (Get-Process -Name "steamvr_gesture_exit" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "Some add-on processes remain. Run 'status' to inspect them."
    }
    Write-Host "All orphan steamvr_gesture_exit.exe processes were stopped." -ForegroundColor Green
}

function Repair-PackageSwap {
    $hasCurrent = Test-Path -LiteralPath $script:PackageDirectory -PathType Container
    $hasPrevious = Test-Path -LiteralPath $script:PackagePreviousDirectory -PathType Container
    if ($hasPrevious) {
        $currentIsComplete = $false
        $previousIsComplete = $false
        $stagingIsComplete = $false
        if ($hasCurrent) {
            try { Require-Package; $currentIsComplete = $true } catch { }
        }
        try { Require-Package $script:PackagePreviousDirectory; $previousIsComplete = $true } catch { }
        if (Test-Path -LiteralPath $script:PackageStagingDirectory -PathType Container) {
            try { Require-Package $script:PackageStagingDirectory; $stagingIsComplete = $true } catch { }
        }

        if ($currentIsComplete) {
            Remove-Item -LiteralPath $script:PackagePreviousDirectory -Recurse -Force
        } elseif ($stagingIsComplete) {
            Stop-AddOn
            if ($hasCurrent) {
                Remove-Item -LiteralPath $script:PackageDirectory -Recurse -Force
            }
            try {
                Move-Item -LiteralPath $script:PackageStagingDirectory -Destination $script:PackageDirectory
                Require-Package
            } catch {
                $recoveryError = $_
                if (Test-Path -LiteralPath $script:PackageDirectory) {
                    Remove-Item -LiteralPath $script:PackageDirectory -Recurse -Force
                }
                Move-Item -LiteralPath $script:PackagePreviousDirectory -Destination $script:PackageDirectory
                throw "Could not complete the interrupted package activation; the previous directory was restored. $($recoveryError.Exception.Message)"
            }
            Remove-Item -LiteralPath $script:PackagePreviousDirectory -Recurse -Force
            Write-Step "Completed the package activation interrupted between directory switches"
        } elseif ($previousIsComplete) {
            Stop-AddOn
            if ($hasCurrent) {
                Remove-Item -LiteralPath $script:PackageDirectory -Recurse -Force
            }
            Move-Item -LiteralPath $script:PackagePreviousDirectory -Destination $script:PackageDirectory
            Require-Package
            Write-Step "Restored the previous package after an incomplete switch"
        } else {
            throw "Cannot recover an interrupted package switch because no complete package copy remains. Run 'package' again; no executable was started."
        }
    }
    if (Test-Path -LiteralPath $script:PackageStagingDirectory) {
        Remove-Item -LiteralPath $script:PackageStagingDirectory -Recurse -Force
    }
}

function Publish-Package {
    Require-Package $script:PackageStagingDirectory
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "SteamVR started while packaging. Close it normally and retry; no package was switched."
    }
    Stop-AddOn
    $savedPrevious = $false
    $activatedStaging = $false
    try {
        if (Test-Path -LiteralPath $script:PackageDirectory) {
            Move-Item -LiteralPath $script:PackageDirectory -Destination $script:PackagePreviousDirectory
            $savedPrevious = $true
        }
        Move-Item -LiteralPath $script:PackageStagingDirectory -Destination $script:PackageDirectory
        $activatedStaging = $true
        Require-Package
    } catch {
        $publishError = $_
        if ($savedPrevious) {
            if (Test-Path -LiteralPath $script:PackageDirectory) {
                Remove-Item -LiteralPath $script:PackageDirectory -Recurse -Force
            }
            if (Test-Path -LiteralPath $script:PackagePreviousDirectory) {
                Move-Item -LiteralPath $script:PackagePreviousDirectory -Destination $script:PackageDirectory
            }
        } elseif ($activatedStaging -and (Test-Path -LiteralPath $script:PackageDirectory)) {
            Remove-Item -LiteralPath $script:PackageDirectory -Recurse -Force
        }
        throw "Package activation failed; the previous package was restored when available. $($publishError.Exception.Message)"
    }
    if (Test-Path -LiteralPath $script:PackagePreviousDirectory) {
        Remove-Item -LiteralPath $script:PackagePreviousDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $script:PackageIntegrityCatalogPath -PathType Leaf) {
        $currentIntegrity = Get-PackageIntegrityRecord $script:PackageDirectory
        if ($currentIntegrity.configuration -eq "Release") {
            Write-PackageIntegrityCatalog @($currentIntegrity)
        } else {
            Remove-Item -LiteralPath $script:PackageIntegrityCatalogPath -Force
        }
    }
}

function Wait-OpenVrApplicationsReady {
    $deadline = (Get-Date).AddSeconds(60)
    $lastOutput = ""
    Write-Step "Preparing the OpenVR applications interface"
    do {
        $probeOutput = & $script:CliPath "--manifest-status" $script:AppKey 2>&1
        $probeExitCode = $LASTEXITCODE
        if ($probeExitCode -eq 0 -or $probeExitCode -eq 2) { return }
        $lastOutput = ($probeOutput | Out-String).Trim()
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    if ($lastOutput) {
        throw "OpenVR applications interface did not become ready within 60 seconds: $lastOutput"
    }
    throw "OpenVR applications interface did not become ready within 60 seconds."
}

function Invoke-Configure {
    Write-Step "Configuring a Windows x64 Visual Studio build"
    $cmake = Find-CMake
    Invoke-Checked $cmake @(
        "-S", $script:ProjectRoot, "--preset", "windows-x64"
    )
}

function Invoke-Build {
    if (-not (Test-Path -LiteralPath (Join-Path $script:BuildDirectory "CMakeCache.txt") -PathType Leaf)) { Invoke-Configure }
    Write-Step "Building $Configuration"
    $cmake = Find-CMake
    Invoke-Checked $cmake @("--build", $script:BuildDirectory, "--config", $Configuration, "--parallel")
    $ctest = Join-Path (Split-Path $cmake -Parent) "ctest.exe"
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw "CTest was not found next to CMake: $ctest"
    }
    Write-Step "Running portable core tests"
    Invoke-Checked $ctest @("--test-dir", $script:BuildDirectory, "-C", $Configuration, "--output-on-failure")
}

function Invoke-Package {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "Close SteamVR normally before package/update. Packaging will not stop SteamVR or risk its auto-launching the add-on during replacement."
    }
    Repair-PackageSwap
    Invoke-Build
    Write-Step "Staging and validating the relocatable runtime package"
    try {
        Invoke-Checked (Find-CMake) @(
            "--install", $script:BuildDirectory, "--config", $Configuration,
            "--prefix", $script:PackageStagingDirectory
        )
        Write-PackageIntegrity $script:PackageStagingDirectory
        Require-Package $script:PackageStagingDirectory
        Write-Step "Activating the complete package"
        Publish-Package
    } finally {
        if (Test-Path -LiteralPath $script:PackageStagingDirectory) {
            Remove-Item -LiteralPath $script:PackageStagingDirectory -Recurse -Force
        }
    }
    Write-Host "Package ready: $script:PackageDirectory" -ForegroundColor Green
}

function Invoke-Register {
    Require-Package
    $script:CliPath = Resolve-CliPath
    [void](Find-SteamVr)
    Wait-OpenVrApplicationsReady
    Remove-Item -LiteralPath (Join-Path $script:UserDataDirectory "registering-autolaunch.txt") -Force -ErrorAction SilentlyContinue
    $confirmedManifest = Get-ReceiptValue $script:RegistrationReceiptPath
    $pendingManifest = Get-ReceiptValue $script:PendingRegistrationReceiptPath
    if ($confirmedManifest -eq $script:ManifestPath -and -not $pendingManifest) {
        & $script:CliPath "--manifest-status" $script:AppKey
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Manifest is already registered; SteamVR's startup setting was not touched." -ForegroundColor Green
            return
        }
    }
    if ($pendingManifest -and $pendingManifest -ne $script:ManifestPath) {
        & $script:CliPath "--manifest-status" $script:AppKey
        $manifestState = $LASTEXITCODE
        if ($manifestState -eq 0) {
            Write-Step "Recovering an interrupted manifest registration"
            Invoke-Checked $script:CliPath @("--unregister-manifest", $pendingManifest)
        } elseif ($manifestState -ne 2) {
            throw "Unable to recover the interrupted manifest registration (status exit code $manifestState)."
        }
        Remove-Item -LiteralPath $script:PendingRegistrationReceiptPath -Force
        $confirmedManifest = Get-ReceiptValue $script:RegistrationReceiptPath
        if ($confirmedManifest -eq $pendingManifest) {
            Remove-Item -LiteralPath $script:RegistrationReceiptPath -Force
        }
    }
    $previousManifest = Get-ReceiptValue $script:RegistrationReceiptPath
    if ($previousManifest -and $previousManifest -ne $script:ManifestPath) {
        # Move the confirmed receipt into the recovery slot before changing
        # SteamVR, so every interruption point retains the path to clean up.
        Set-ReceiptValue $script:PendingRegistrationReceiptPath $previousManifest
        Remove-Item -LiteralPath $script:RegistrationReceiptPath -Force
        Write-Step "Removing the manifest registration from the previous package location"
        Invoke-Checked $script:CliPath @("--unregister-manifest", $previousManifest)
        Remove-Item -LiteralPath $script:PendingRegistrationReceiptPath -Force
    }
    # Journal the new path before mutating SteamVR. If this command is interrupted,
    # unregister/clean can still remove a registration that may have succeeded.
    Set-ReceiptValue $script:PendingRegistrationReceiptPath $script:ManifestPath
    Write-Step "Registering the SteamVR application manifest"
    Invoke-Checked $script:CliPath @("--register-manifest", $script:ManifestPath)
    Invoke-Checked $script:CliPath @("--manifest-status", $script:AppKey)
    Set-ReceiptValue $script:RegistrationReceiptPath $script:ManifestPath
    Remove-Item -LiteralPath $script:PendingRegistrationReceiptPath -Force
    Write-Host "Registered: $script:ManifestPath" -ForegroundColor Green
}

function Invoke-Unregister {
    if (-not (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        Invoke-Stop-Orphans
        Write-Step "Removing this application's OpenVR configuration"
        Remove-OpenVrResiduals
        Remove-RegistrationReceipts
        Write-Host "Unregistered. SteamVR was not started; package and build files were kept." -ForegroundColor Green
        return
    }
    Require-Package
    $script:CliPath = Resolve-CliPath
    Wait-OpenVrApplicationsReady
    & $script:CliPath "--manifest-status" $script:AppKey
    $manifestState = $LASTEXITCODE
    if ($manifestState -eq 2) {
        Stop-AddOn
        Remove-RegistrationReceipts
        Write-Host "Manifest is already unregistered. Close SteamVR and run 'clean' for full disk cleanup." -ForegroundColor Green
        return
    }
    if ($manifestState -ne 0) {
        throw "Unable to query manifest state (exit code $manifestState); no files were removed."
    }
    $registeredManifest = $script:ManifestPath
    $pendingManifest = Get-ReceiptValue $script:PendingRegistrationReceiptPath
    $confirmedManifest = Get-ReceiptValue $script:RegistrationReceiptPath
    if ($pendingManifest) { $registeredManifest = $pendingManifest }
    elseif ($confirmedManifest) { $registeredManifest = $confirmedManifest }
    $candidates = [Collections.Generic.List[string]]::new()
    $manifestResiduals = @()
    try { $manifestResiduals = @(Get-OpenVrResiduals | Where-Object { $_.Kind -eq "manifest_path" }) }
    catch { Write-Warning "OpenVR config audit failed; trying recorded manifest paths: $($_.Exception.Message)" }
    foreach ($residual in $manifestResiduals) {
        if (-not ($candidates | Where-Object { $_ -ieq $residual.Manifest })) {
            $candidates.Add($residual.Manifest)
        }
    }
    if (-not ($candidates | Where-Object { $_ -ieq $registeredManifest })) {
        $candidates.Add($registeredManifest)
    }
    Write-Step "Disabling auto-launch and unregistering this application's manifest paths"
    foreach ($candidate in $candidates) {
        & $script:CliPath "--manifest-status" $script:AppKey
        if ($LASTEXITCODE -eq 2) { break }
        & $script:CliPath "--unregister-manifest" $candidate
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Could not remove candidate manifest path: $candidate"
        }
    }
    Stop-AddOn
    & $script:CliPath "--manifest-status" $script:AppKey
    if ($LASTEXITCODE -ne 2) {
        throw "Manifest removal could not be verified (status exit code $LASTEXITCODE); no build or user data was removed."
    }
    Remove-RegistrationReceipts
    Write-Host "Unregistered. Close SteamVR, then run 'clean' to remove the owned vrappconfig and any on-disk manifest-path residue." -ForegroundColor Green
}

function Invoke-Status {
    Write-Step "Development status"
    $packageReady = $false
    $packageProblem = $null
    try {
        Require-Package
        $packageReady = $true
    } catch {
        $packageProblem = $_.Exception.Message
    }
    $cliProblem = $null
    if ($packageReady) {
        try { $script:CliPath = Resolve-CliPath }
        catch { $cliProblem = $_.Exception.Message }
    }
    try { Write-Host "CMake:             $(Find-CMake)" } catch { Write-Host "CMake:             not found" -ForegroundColor Yellow }
    try { Write-Host "SteamVR:           $(Find-SteamVr)" } catch { Write-Host "SteamVR:           not found" -ForegroundColor Yellow }
    Write-Host "Build directory:   $script:BuildDirectory ($(if (Test-Path -LiteralPath $script:BuildDirectory) { 'present' } else { 'absent' }))"
    Write-Host "Package directory: $script:PackageDirectory ($(if (Test-Path -LiteralPath $script:PackageDirectory) { 'present' } else { 'absent' }))"
    Write-Host "Package integrity: $(if ($packageReady) { 'verified' } else { 'not verified' })$(if ($packageProblem) { " ($packageProblem)" })"
    Write-Host "Package recovery:  $(if ((Test-Path -LiteralPath $script:PackageStagingDirectory) -or (Test-Path -LiteralPath $script:PackagePreviousDirectory)) { 'pending; run package to recover or clean to remove' } else { 'none' })"
    Write-Host "Manifest:          $script:ManifestPath ($(if (Test-Path -LiteralPath $script:ManifestPath) { 'present' } else { 'absent' }))"
    $steamVrRunning = [bool](Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1)
    $addOnRunning = @(Get-PackagedAddOnProcesses).Count -gt 0
    $allAddOnProcesses = @(Get-Process -Name "steamvr_gesture_exit" -ErrorAction SilentlyContinue)
    Write-Host "SteamVR process:   $(if ($steamVrRunning) { 'running' } else { 'stopped' })"
    Write-Host "Add-on process:    $(if ($addOnRunning) { 'running' } else { 'stopped' })"
    if ($allAddOnProcesses.Count -gt 1) {
        Write-Host "Instance check:    $($allAddOnProcesses.Count) processes; possible startup handoff, recheck after 15 seconds" -ForegroundColor Yellow
    } else {
        Write-Host "Instance check:    $($allAddOnProcesses.Count) process(es)" -ForegroundColor Green
    }
    if (-not $steamVrRunning) {
        if ($allAddOnProcesses.Count -eq 0) {
            Write-Host "Orphan check:      clear" -ForegroundColor Green
        } else {
            Write-Host "Orphan check:      $($allAddOnProcesses.Count) process(es) remain; run stop-orphans" -ForegroundColor Red
        }
    }
    foreach ($process in $allAddOnProcesses) {
        $path = "<path unavailable>"
        try { if ($process.Path) { $path = $process.Path } } catch { }
        Write-Host "Add-on PID $($process.Id): $path"
    }
    Write-Host "User data:         $script:UserDataDirectory ($(if (Test-Path -LiteralPath $script:UserDataDirectory) { 'present' } else { 'absent' }))"
    if (Test-Path -LiteralPath $script:RegistrationReceiptPath -PathType Leaf) {
        Write-Host "Registration path: $((Get-Content -LiteralPath $script:RegistrationReceiptPath -Raw).Trim())"
    }
    if (Test-Path -LiteralPath $script:PendingRegistrationReceiptPath -PathType Leaf) {
        Write-Host "Pending registration: $((Get-Content -LiteralPath $script:PendingRegistrationReceiptPath -Raw).Trim())" -ForegroundColor Yellow
    }
    try {
        $configDirectories = @(Get-OpenVrConfigDirectories)
        if ($configDirectories.Count -eq 0) { throw "No OpenVR config directory was found." }
        foreach ($configDirectory in $configDirectories) {
            Write-Host "OpenVR config dir: $configDirectory"
        }
        $residuals = @(Get-OpenVrResiduals)
        Write-Host "OpenVR config residue: $($residuals.Count)"
        foreach ($residual in $residuals) {
            $detail = if ($residual.Manifest) { "$($residual.File) -> $($residual.Manifest)" } else { $residual.File }
            Write-Host "  $($residual.Kind): $detail" -ForegroundColor Yellow
        }
    } catch {
        Write-Host "OpenVR config audit: unavailable ($($_.Exception.Message))" -ForegroundColor Red
    }
    if ($packageReady -and (-not $cliProblem) -and $steamVrRunning) {
        & $script:CliPath "--manifest-status" $script:AppKey
        switch ($LASTEXITCODE) {
            0 { Write-Host "Manifest state:    registered" -ForegroundColor Green }
            2 { Write-Host "Manifest state:    not registered" -ForegroundColor Yellow }
            default { Write-Host "Manifest state:    unavailable (exit code $LASTEXITCODE)" -ForegroundColor Yellow }
        }
        & $script:CliPath "--autolaunch-status" $script:AppKey
        $autoLaunchState = $LASTEXITCODE
        switch ($autoLaunchState) {
            0 { Write-Host "Auto-launch:       enabled" -ForegroundColor Green }
            2 { Write-Host "Auto-launch:       disabled" -ForegroundColor Yellow }
            3 { Write-Host "Auto-launch:       not registered" -ForegroundColor Yellow }
            default { Write-Host "Auto-launch:       unavailable (exit code $autoLaunchState)" -ForegroundColor Yellow }
        }
        if ($autoLaunchState -eq 0) {
            if ($addOnRunning) { Write-Host "Startup check:     active" -ForegroundColor Green }
            else { Write-Host "Startup check:     enabled but add-on is not running" -ForegroundColor Red }
        } elseif ($autoLaunchState -eq 2) {
            if ($addOnRunning) { Write-Host "Startup check:     disabled but add-on is still running" -ForegroundColor Red }
            else { Write-Host "Startup check:     inactive" -ForegroundColor Green }
        }
    } else {
        $reason = if ($cliProblem) { $cliProblem } else { "SteamVR/package is not running/present" }
        Write-Host "Manifest state:    unavailable ($reason)" -ForegroundColor Yellow
        Write-Host "Auto-launch:       unavailable ($reason)" -ForegroundColor Yellow
    }
    $activeLog = Join-Path $script:LogDirectory "runtime.log"
    $backupLog = "$activeLog.1"
    if (Test-Path -LiteralPath $activeLog -PathType Leaf) {
        Write-Host "Latest log:        $activeLog"
    } elseif (Test-Path -LiteralPath $backupLog -PathType Leaf) {
        Write-Host "Latest log:        $backupLog"
    }
}

function Get-LogRotationMarker {
    $backup = Get-Item -LiteralPath (Join-Path $script:LogDirectory "runtime.log.1") -ErrorAction SilentlyContinue
    if (-not $backup) { return "none" }
    return "$($backup.Length):$($backup.LastWriteTimeUtc.Ticks)"
}

function Read-LogBytes([string]$Path, [long]$Position) {
    $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $sharing)
    try {
        if ($Position -gt $stream.Length) {
            return @{ Truncated = $true; Bytes = [byte[]]::new(0); Position = [long]0 }
        }
        [void]$stream.Seek($Position, [IO.SeekOrigin]::Begin)
        $available = [int]($stream.Length - $Position)
        $bytes = [byte[]]::new($available)
        $count = 0
        while ($count -lt $available) {
            $read = $stream.Read($bytes, $count, $available - $count)
            if ($read -eq 0) { break }
            $count += $read
        }
        if ($count -lt $available) {
            $complete = [byte[]]::new($count)
            [Array]::Copy($bytes, 0, $complete, 0, $count)
            $bytes = $complete
        }
        return @{ Truncated = $false; Bytes = $bytes; Position = ($Position + $count) }
    } finally {
        $stream.Dispose()
    }
}

function Split-CompleteLogLines([byte[]]$Pending, [byte[]]$Bytes) {
    $combined = [byte[]]::new($Pending.Length + $Bytes.Length)
    [Array]::Copy($Pending, 0, $combined, 0, $Pending.Length)
    [Array]::Copy($Bytes, 0, $combined, $Pending.Length, $Bytes.Length)
    $lastNewline = [Array]::LastIndexOf($combined, [byte]10)
    if ($lastNewline -lt 0) {
        return @{ Lines = [Collections.Generic.List[string]]::new(); Pending = $combined }
    }
    $text = [Text.Encoding]::UTF8.GetString($combined, 0, $lastNewline)
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in [regex]::Split($text, "`n")) {
        if ($line.EndsWith("`r")) { $lines.Add($line.Substring(0, $line.Length - 1)) }
        else { $lines.Add($line) }
    }
    $remaining = $combined.Length - $lastNewline - 1
    $pendingNext = [byte[]]::new($remaining)
    [Array]::Copy($combined, $lastNewline + 1, $pendingNext, 0, $remaining)
    return @{ Lines = $lines; Pending = $pendingNext }
}

function Invoke-Logs {
    $logPath = Join-Path $script:LogDirectory "runtime.log"
    $backupPath = "$logPath.1"
    if (-not $Follow -and -not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        if (Test-Path -LiteralPath $backupPath -PathType Leaf) { $logPath = $backupPath }
        else {
            Write-Host "No diagnostic log exists under $script:LogDirectory. Launch the add-on first."
            return
        }
    }
    Write-Step "Showing $logPath"
    if ($Follow -and -not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        Write-Host "Waiting for the add-on to create runtime.log..."
    }
    $position = [long]0
    $pending = [byte[]]::new(0)
    $rotationMarker = Get-LogRotationMarker
    $initialized = $false
    while ($true) {
        if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
            if (-not $Follow) { return }
            Start-Sleep -Milliseconds 500
            continue
        }
        try {
            $before = Get-LogRotationMarker
            if ($before -ne $rotationMarker -and $before -ne "none") {
                if ($initialized -and (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
                    $oldChunk = Read-LogBytes $backupPath $position
                    if (-not $oldChunk.Truncated) {
                        $oldLines = Split-CompleteLogLines $pending $oldChunk.Bytes
                        foreach ($line in $oldLines.Lines) { Write-Output $line }
                    }
                }
                $position = [long]0
                $pending = [byte[]]::new(0)
                $rotationMarker = $before
            }
            $chunk = Read-LogBytes $logPath $position
            $after = Get-LogRotationMarker
            if ($after -ne $before -and $after -ne "none") {
                continue
            }
            if ($chunk.Truncated) {
                $position = [long]0
                $pending = [byte[]]::new(0)
                $chunk = Read-LogBytes $logPath $position
            }
            $position = $chunk.Position
            $parsed = Split-CompleteLogLines $pending $chunk.Bytes
            $pending = $parsed.Pending
            $lines = $parsed.Lines
            if (-not $initialized) {
                $tailCount = if ($Follow) { 100 } else { 200 }
                $start = [Math]::Max(0, $lines.Count - $tailCount)
                for ($i = $start; $i -lt $lines.Count; $i++) { Write-Output $lines[$i] }
                $initialized = $true
            } else {
                foreach ($line in $lines) { Write-Output $line }
            }
            if (-not $Follow) { return }
        } catch [IO.IOException] {
            if (-not $Follow) { throw }
            # Rotation may replace the active file between opening and reading it.
        }
        Start-Sleep -Milliseconds 500
    }
}

function Remove-IfPresent([string]$Path, [string]$Label) {
    if (Test-Path -LiteralPath $Path) {
        Write-Host "Removing $Label`: $Path"
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Invoke-Clean {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "Close SteamVR normally before clean. The helper will not stop or start it."
    }
    Invoke-Unregister
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1) {
        throw "SteamVR started during clean; package files were not removed. Close it and retry."
    }
    Write-Step "Removing reproducible development artifacts"
    Remove-IfPresent $script:BuildDirectory "CMake build tree"
    Remove-IfPresent $script:PackageDirectory "runtime package"
    Remove-IfPresent $script:PackageStagingDirectory "staged runtime package"
    Remove-IfPresent $script:PackagePreviousDirectory "package switch backup"
    Remove-IfPresent $script:PackageIntegrityCatalogPath "package integrity catalog"
    Remove-IfPresent "$($script:PackageIntegrityCatalogPath).tmp" "package integrity catalog temporary file"
    if (-not $KeepUserData) { Remove-IfPresent $script:UserDataDirectory "settings and diagnostic logs" }
    else { Write-Host "Keeping user data because -KeepUserData was specified: $script:UserDataDirectory" }
    Write-Host "Clean complete. SteamVR was not started or stopped by this command." -ForegroundColor Green
}

function Invoke-Update {
    Invoke-Package
    $confirmedManifest = Get-ReceiptValue $script:RegistrationReceiptPath
    if ($confirmedManifest -ne $script:ManifestPath -or
        (Test-Path -LiteralPath $script:PendingRegistrationReceiptPath -PathType Leaf)) {
        throw "Package updated, but registration needs attention. Run 'register'; update did not start SteamVR or the add-on."
    }
    Write-Host "Package updated. Registration and auto-launch settings were left unchanged; update did not start SteamVR or the add-on." -ForegroundColor Green
}

function Show-Help {
    @"
SteamVR Gesture Exit Windows development helper

Usage:
  .\scripts\windows\dev.cmd <action> [-Configuration Debug|Release]

Actions:
  configure   Configure an x64 Visual Studio 2022 CMake build.
  build       Compile the selected configuration.
  package     Build and install the runtime package; SteamVR must be stopped.
  register    Register the manifest; the SteamVR window may remain closed.
  update      Replace the package while SteamVR is stopped; start neither program.
  status      Show toolchain, package, registration, and process status.
  logs        Show the last 200 log lines; add -Follow to tail continuously.
  stop-orphans  When SteamVR is stopped, stop leftover add-on processes.
  unregister  Remove this application's registration; offline also removes vrappconfig.
  clean       With SteamVR stopped, remove owned OpenVR config and artifacts.
"@ | Write-Host
}

switch ($Action) {
    "configure"  { Invoke-Configure }
    "build"      { Invoke-Build }
    "package"    { Invoke-Package }
    "register"   { Invoke-Register }
    "update"     { Invoke-Update }
    "status"     { Invoke-Status }
    "logs"       { Invoke-Logs }
    "stop-orphans" { Invoke-Stop-Orphans }
    "unregister" { Invoke-Unregister }
    "clean"      { Invoke-Clean }
    default       { Show-Help }
}
