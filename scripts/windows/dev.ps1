[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("help", "configure", "build", "package", "register", "update", "status", "launch", "logs", "clean", "unregister")]
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
$script:PackageDirectory = Join-Path $script:ProjectRoot "out\package\steamvr-bail-out"
$script:PackageStagingDirectory = "$($script:PackageDirectory).staging"
$script:PackagePreviousDirectory = "$($script:PackageDirectory).previous"
$script:ExecutablePath = Join-Path $script:PackageDirectory "bin\win64\steamvr_bail_out.exe"
$script:CliPath = Join-Path $script:PackageDirectory "bin\win64\steamvr_bail_out_cli.exe"
$script:ManifestPath = Join-Path $script:PackageDirectory "manifest\steamvr_bail_out.vrmanifest"
$script:PackageIntegrityFileName = ".package-integrity.json"
$script:RequiredPackageFiles = @(
    "bin/win64/steamvr_bail_out.exe",
    "bin/win64/steamvr_bail_out_cli.exe",
    "bin/win64/openvr_api.dll",
    "manifest/steamvr_bail_out.vrmanifest"
)
$script:AppKey = "com.steamvr-bail-out.overlay"
$script:UserDataDirectory = Join-Path $env:LOCALAPPDATA "SteamVRBailOut"
$script:LogDirectory = Join-Path $script:UserDataDirectory "logs"
$script:RegistrationReceiptPath = Join-Path $script:UserDataDirectory "registered-manifest.txt"
$script:PendingRegistrationReceiptPath = Join-Path $script:UserDataDirectory "registering-manifest.txt"
$script:StartedSteamVrServer = $null
$script:StartedSteamVrStartTime = $null
$script:SteamVrStartAttempted = $false

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

function Get-PackageFilePath([string]$PackageRoot, [string]$RelativePath) {
    return Join-Path $PackageRoot ($RelativePath -replace '/', '\')
}

function Write-PackageIntegrity([string]$PackageRoot) {
    $files = @()
    foreach ($relativePath in $script:RequiredPackageFiles) {
        $filePath = Get-PackageFilePath $PackageRoot $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Cannot complete the package because a required file is missing: $filePath"
        }
        $files += [ordered]@{
            path = $relativePath
            sha256 = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $integrity = [ordered]@{
        schema_version = 1
        package_id = [Guid]::NewGuid().ToString("D")
        configuration = $Configuration
        created_utc = [DateTime]::UtcNow.ToString("o")
        files = $files
    }
    $integrityPath = Join-Path $PackageRoot $script:PackageIntegrityFileName
    $integrity | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $integrityPath -Encoding UTF8
}

function Require-Package([string]$PackageRoot = $script:PackageDirectory) {
    $integrityPath = Join-Path $PackageRoot $script:PackageIntegrityFileName
    if (-not (Test-Path -LiteralPath $integrityPath -PathType Leaf)) {
        throw "Package integrity record is missing: $integrityPath. Run 'package' first."
    }
    try { $integrity = Get-Content -LiteralPath $integrityPath -Raw | ConvertFrom-Json }
    catch { throw "Package integrity record is not valid JSON: $integrityPath. $($_.Exception.Message)" }
    if ($integrity.schema_version -ne 1) {
        throw "Package integrity record has an unsupported schema: $integrityPath"
    }
    $packageId = [Guid]::Empty
    if (-not [Guid]::TryParse([string]$integrity.package_id, [ref]$packageId)) {
        throw "Package integrity record has an invalid package id: $integrityPath"
    }
    if (@("Debug", "Release") -notcontains [string]$integrity.configuration) {
        throw "Package integrity record has an invalid build configuration: $integrityPath"
    }
    $recordedFiles = @($integrity.files)
    if ($recordedFiles.Count -ne $script:RequiredPackageFiles.Count) {
        throw "Package integrity record does not describe the complete required file set: $integrityPath"
    }
    foreach ($relativePath in $script:RequiredPackageFiles) {
        $matchingRecords = @($recordedFiles | Where-Object { [string]$_.path -ceq $relativePath })
        if ($matchingRecords.Count -ne 1) {
            throw "Package integrity record does not uniquely identify '$relativePath': $integrityPath"
        }
        $expectedHash = [string]$matchingRecords[0].sha256
        if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Package integrity record has an invalid SHA-256 for '$relativePath': $integrityPath"
        }
        $filePath = Get-PackageFilePath $PackageRoot $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Required package file is missing: $filePath. Run 'package' first."
        }
        $actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash
        if ($actualHash -ine $expectedHash) {
            throw "Package file does not match build '$packageId': $filePath. Run 'package' again."
        }
    }

    $executablePath = Get-PackageFilePath $PackageRoot "bin/win64/steamvr_bail_out.exe"
    $manifestPath = Get-PackageFilePath $PackageRoot "manifest/steamvr_bail_out.vrmanifest"
    try { $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json }
    catch { throw "Application manifest is not valid JSON: $manifestPath. $($_.Exception.Message)" }
    $application = @($manifest.applications) | Where-Object { $_.app_key -eq $script:AppKey } | Select-Object -First 1
    if (-not $application) { throw "Application manifest does not contain the expected app key '$script:AppKey'." }
    $expectedBinary = [IO.Path]::GetFullPath((Join-Path (Split-Path $manifestPath -Parent) ([string]$application.binary_path_windows)))
    if ($expectedBinary -ne [IO.Path]::GetFullPath($executablePath)) {
        throw "Manifest binary path resolves to '$expectedBinary', expected '$executablePath'."
    }
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
    Remove-Item -LiteralPath "$($script:RegistrationReceiptPath).tmp" -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$($script:PendingRegistrationReceiptPath).tmp" -Force -ErrorAction SilentlyContinue
}

function Get-PackagedAddOnProcesses {
    $expectedPath = [IO.Path]::GetFullPath($script:ExecutablePath)
    foreach ($process in @(Get-Process -Name "steamvr_bail_out" -ErrorAction SilentlyContinue)) {
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
        $eventName = "Local\SteamVRBailOut.Stop.$($process.Id).$creationToken"
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
}

function Start-SteamVrIfNeeded {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue) { return }
    Write-Step "Starting SteamVR"
    Start-Process "steam://rungameid/250820"
    $script:SteamVrStartAttempted = $true
    $deadline = (Get-Date).AddSeconds(60)
    do {
        Start-Sleep -Milliseconds 500
        $server = Get-Process -Name "vrserver" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($server) {
            $script:StartedSteamVrServer = $server
            $script:StartedSteamVrStartTime = $server.StartTime.ToFileTimeUtc()
            return
        }
    } while ((Get-Date) -lt $deadline)
    throw "SteamVR did not become ready within 60 seconds. Start SteamVR manually and retry."
}

function Wait-SteamVrApplicationsReady {
    $deadline = (Get-Date).AddSeconds(60)
    $lastOutput = ""
    Write-Step "Waiting for the SteamVR applications interface"
    do {
        $probeOutput = & $script:CliPath "--manifest-status" $script:AppKey 2>&1
        $probeExitCode = $LASTEXITCODE
        if ($probeExitCode -eq 0 -or $probeExitCode -eq 2) { return }
        $lastOutput = ($probeOutput | Out-String).Trim()
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    if ($lastOutput) {
        throw "SteamVR applications interface did not become ready within 60 seconds: $lastOutput"
    }
    throw "SteamVR applications interface did not become ready within 60 seconds."
}

function Stop-SteamVrStartedForClean {
    if (-not $script:StartedSteamVrServer) {
        if ($script:SteamVrStartAttempted -and
            (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue)) {
            throw "SteamVR was launched but its server identity could not be verified; close it normally and retry clean."
        }
        return
    }
    $started = $script:StartedSteamVrServer
    $server = Get-Process -Id $started.Id -ErrorAction SilentlyContinue
    if (-not $server) {
        if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue) {
            throw "SteamVR restarted during clean; the new session was left running."
        }
        return
    }
    if ($server.ProcessName -ne "vrserver" -or
        $server.StartTime.ToFileTimeUtc() -ne $script:StartedSteamVrStartTime) {
        throw "The SteamVR server changed while cleaning; close the new session manually."
    }
    Write-Step "Closing SteamVR started for clean"
    Start-Process -FilePath "vrmonitor://quit"
    $deadline = (Get-Date).AddSeconds(30)
    do {
        if (-not (Get-Process -Id $started.Id -ErrorAction SilentlyContinue)) {
            if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue) {
                throw "SteamVR restarted during clean; the new session was left running."
            }
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "SteamVR did not exit within 30 seconds after the quit request; close it normally and retry clean."
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
    Repair-PackageSwap
    Require-Package
    [void](Find-SteamVr)
    Start-SteamVrIfNeeded
    Wait-SteamVrApplicationsReady
    $pendingManifest = Get-ReceiptValue $script:PendingRegistrationReceiptPath
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
    Write-Step "Registering the SteamVR application manifest and enabling auto-launch"
    Invoke-Checked $script:CliPath @("--register-manifest", $script:ManifestPath)
    Invoke-Checked $script:CliPath @("--manifest-status", $script:AppKey)
    Set-ReceiptValue $script:RegistrationReceiptPath $script:ManifestPath
    Remove-Item -LiteralPath $script:PendingRegistrationReceiptPath -Force
    Write-Host "Registered: $script:ManifestPath" -ForegroundColor Green
}

function Invoke-Unregister {
    Repair-PackageSwap
    Require-Package
    Start-SteamVrIfNeeded
    Wait-SteamVrApplicationsReady
    Stop-AddOn
    & $script:CliPath "--manifest-status" $script:AppKey
    $manifestState = $LASTEXITCODE
    if ($manifestState -eq 2) {
        Remove-RegistrationReceipts
        Write-Host "Manifest is already unregistered." -ForegroundColor Green
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
    Write-Step "Disabling auto-launch and unregistering the application manifest"
    Invoke-Checked $script:CliPath @("--unregister-manifest", $registeredManifest)
    & $script:CliPath "--manifest-status" $script:AppKey
    if ($LASTEXITCODE -ne 2) {
        throw "Manifest removal could not be verified (status exit code $LASTEXITCODE); no build or user data was removed."
    }
    Remove-RegistrationReceipts
    Write-Host "Unregistered: $registeredManifest" -ForegroundColor Green
}

function Invoke-Launch {
    Repair-PackageSwap
    Require-Package
    Start-SteamVrIfNeeded
    if (Get-PackagedAddOnProcesses) {
        Write-Host "SteamVR Bail Out is already running."
        return
    }
    Write-Step "Launching SteamVR Bail Out"
    Start-Process -FilePath $script:ExecutablePath -WorkingDirectory $script:PackageDirectory
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
    try { Write-Host "CMake:             $(Find-CMake)" } catch { Write-Host "CMake:             not found" -ForegroundColor Yellow }
    try { Write-Host "SteamVR:           $(Find-SteamVr)" } catch { Write-Host "SteamVR:           not found" -ForegroundColor Yellow }
    Write-Host "Build directory:   $script:BuildDirectory ($(if (Test-Path -LiteralPath $script:BuildDirectory) { 'present' } else { 'absent' }))"
    Write-Host "Package directory: $script:PackageDirectory ($(if (Test-Path -LiteralPath $script:PackageDirectory) { 'present' } else { 'absent' }))"
    Write-Host "Package integrity: $(if ($packageReady) { 'verified' } else { 'not verified' })$(if ($packageProblem) { " ($packageProblem)" })"
    Write-Host "Package recovery:  $(if ((Test-Path -LiteralPath $script:PackageStagingDirectory) -or (Test-Path -LiteralPath $script:PackagePreviousDirectory)) { 'pending; run package, launch, register, unregister, or clean' } else { 'none' })"
    Write-Host "Manifest:          $script:ManifestPath ($(if (Test-Path -LiteralPath $script:ManifestPath) { 'present' } else { 'absent' }))"
    Write-Host "SteamVR process:   $(if (Get-Process -Name 'vrserver' -ErrorAction SilentlyContinue) { 'running' } else { 'stopped' })"
    Write-Host "Add-on process:    $(if (Get-PackagedAddOnProcesses) { 'running' } else { 'stopped' })"
    Write-Host "User data:         $script:UserDataDirectory ($(if (Test-Path -LiteralPath $script:UserDataDirectory) { 'present' } else { 'absent' }))"
    if (Test-Path -LiteralPath $script:RegistrationReceiptPath -PathType Leaf) {
        Write-Host "Registration path: $((Get-Content -LiteralPath $script:RegistrationReceiptPath -Raw).Trim())"
    }
    if (Test-Path -LiteralPath $script:PendingRegistrationReceiptPath -PathType Leaf) {
        Write-Host "Pending registration: $((Get-Content -LiteralPath $script:PendingRegistrationReceiptPath -Raw).Trim())" -ForegroundColor Yellow
    }
    if ($packageReady -and (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue)) {
        & $script:CliPath "--manifest-status" $script:AppKey
        switch ($LASTEXITCODE) {
            0 { Write-Host "Manifest state:    registered" -ForegroundColor Green }
            2 { Write-Host "Manifest state:    not registered" -ForegroundColor Yellow }
            default { Write-Host "Manifest state:    unavailable (exit code $LASTEXITCODE)" -ForegroundColor Yellow }
        }
    } else {
        Write-Host "Manifest state:    unavailable (SteamVR/package is not running/present)" -ForegroundColor Yellow
    }
    $latestLog = Get-ChildItem -LiteralPath $script:LogDirectory -Filter "*.log" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latestLog) { Write-Host "Latest log:        $($latestLog.FullName)" }
}

function Invoke-Logs {
    $latestLog = Get-ChildItem -LiteralPath $script:LogDirectory -Filter "*.log" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latestLog) {
        Write-Host "No diagnostic log exists under $script:LogDirectory. Launch the add-on first."
        return
    }
    Write-Step "Showing $($latestLog.FullName)"
    if (-not $Follow) {
        Get-Content -LiteralPath $latestLog.FullName -Tail 200
        return
    }
    Get-Content -LiteralPath $latestLog.FullName -Tail 100
    $position = $latestLog.Length
    $created = $latestLog.CreationTimeUtc
    while ($true) {
        Start-Sleep -Milliseconds 500
        $file = Get-Item -LiteralPath $latestLog.FullName -ErrorAction SilentlyContinue
        if (-not $file) { continue }
        if ($file.CreationTimeUtc -ne $created -or $file.Length -lt $position) {
            $position = 0
            $created = $file.CreationTimeUtc
        }
        if ($file.Length -eq $position) { continue }
        $stream = $null
        try {
            $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
            $stream = [IO.FileStream]::new($file.FullName, [IO.FileMode]::Open,
                [IO.FileAccess]::Read, $sharing)
            [void]$stream.Seek($position, [IO.SeekOrigin]::Begin)
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
            try {
                while ($null -ne ($line = $reader.ReadLine())) { Write-Output $line }
                $position = $stream.Position
            } finally {
                $reader.Dispose()
            }
        } catch [IO.IOException] {
            # A rotation may replace the file between Get-Item and opening it.
        } finally {
            if ($stream) { $stream.Dispose() }
        }
    }
}

function Remove-IfPresent([string]$Path, [string]$Label) {
    if (Test-Path -LiteralPath $Path) {
        Write-Host "Removing $Label`: $Path"
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Invoke-Clean {
    Repair-PackageSwap
    Stop-AddOn
    $script:StartedSteamVrServer = $null
    $script:StartedSteamVrStartTime = $null
    $script:SteamVrStartAttempted = $false
    $completePackage = $false
    try { Require-Package; $completePackage = $true } catch { }
    $hasRegistrationReceipt = (
        (Test-Path -LiteralPath $script:RegistrationReceiptPath -PathType Leaf) -or
        (Test-Path -LiteralPath $script:PendingRegistrationReceiptPath -PathType Leaf)
    )
    if ($completePackage -or $hasRegistrationReceipt) {
        Write-Step "Unregistering before deleting the package"
        try {
            Invoke-Unregister
        } finally {
            Stop-SteamVrStartedForClean
        }
    } else {
        Write-Host "No complete package or registration receipt exists; skipping SteamVR unregistration."
    }
    Write-Step "Removing reproducible development artifacts"
    Remove-IfPresent $script:BuildDirectory "CMake build tree"
    Remove-IfPresent $script:PackageDirectory "runtime package"
    Remove-IfPresent $script:PackageStagingDirectory "staged runtime package"
    Remove-IfPresent $script:PackagePreviousDirectory "package switch backup"
    if (-not $KeepUserData) { Remove-IfPresent $script:UserDataDirectory "settings and diagnostic logs" }
    else { Write-Host "Keeping user data because -KeepUserData was specified: $script:UserDataDirectory" }
    Write-Host "Clean complete. SteamVR was returned to its original running state." -ForegroundColor Green
}

function Show-Help {
    @"
SteamVR Bail Out Windows development helper

Usage:
  .\scripts\windows\dev.cmd <action> [-Configuration Debug|Release]

Actions:
  configure   Configure an x64 Visual Studio 2022 CMake build.
  build       Compile the selected configuration.
  package     Build and install the relocatable runtime package.
  register    Register the packaged SteamVR application manifest and auto-launch.
  update      Build, stage, stop, switch packages, refresh registration, and relaunch.
  status      Show toolchain, package, registration, and process status.
  launch      Start SteamVR if needed and launch the packaged add-on.
  logs        Show the last 200 log lines; add -Follow to tail continuously.
  unregister  Disable auto-launch and remove only this application's manifest.
  clean       Unregister and remove build/package/user data; -KeepUserData preserves settings/logs.
"@ | Write-Host
}

switch ($Action) {
    "configure"  { Invoke-Configure }
    "build"      { Invoke-Build }
    "package"    { Invoke-Package }
    "register"   { Invoke-Register }
    "update"     { Invoke-Package; Invoke-Register; Invoke-Launch }
    "status"     { Invoke-Status }
    "launch"     { Invoke-Launch }
    "logs"       { Invoke-Logs }
    "unregister" { Invoke-Unregister }
    "clean"      { Invoke-Clean }
    default       { Show-Help }
}
