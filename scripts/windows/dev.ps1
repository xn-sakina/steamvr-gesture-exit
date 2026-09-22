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
$script:ExecutablePath = Join-Path $script:PackageDirectory "bin\win64\steamvr_bail_out.exe"
$script:CliPath = Join-Path $script:PackageDirectory "bin\win64\steamvr_bail_out_cli.exe"
$script:ManifestPath = Join-Path $script:PackageDirectory "manifest\steamvr_bail_out.vrmanifest"
$script:AppKey = "com.steamvr-bail-out.overlay"
$script:UserDataDirectory = Join-Path $env:LOCALAPPDATA "SteamVRBailOut"
$script:LogDirectory = Join-Path $script:UserDataDirectory "logs"
$script:RegistrationReceiptPath = Join-Path $script:UserDataDirectory "registered-manifest.txt"

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

function Require-Package {
    if (-not (Test-Path -LiteralPath $script:ExecutablePath -PathType Leaf)) {
        throw "Runtime executable is missing: $script:ExecutablePath. Run 'package' first."
    }
    if (-not (Test-Path -LiteralPath $script:CliPath -PathType Leaf)) {
        throw "Command-line helper is missing: $script:CliPath. Run 'package' first."
    }
    if (-not (Test-Path -LiteralPath $script:ManifestPath -PathType Leaf)) {
        throw "Application manifest is missing: $script:ManifestPath. Run 'package' first."
    }
    try { $manifest = Get-Content -LiteralPath $script:ManifestPath -Raw | ConvertFrom-Json }
    catch { throw "Application manifest is not valid JSON: $script:ManifestPath. $($_.Exception.Message)" }
    $application = @($manifest.applications) | Where-Object { $_.app_key -eq $script:AppKey } | Select-Object -First 1
    if (-not $application) { throw "Application manifest does not contain the expected app key '$script:AppKey'." }
    $expectedBinary = [IO.Path]::GetFullPath((Join-Path (Split-Path $script:ManifestPath -Parent) ([string]$application.binary_path_windows)))
    if ($expectedBinary -ne [IO.Path]::GetFullPath($script:ExecutablePath)) {
        throw "Manifest binary path resolves to '$expectedBinary', expected '$script:ExecutablePath'."
    }
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

function Start-SteamVrIfNeeded {
    if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue) { return }
    Write-Step "Starting SteamVR"
    Start-Process "steam://rungameid/250820"
    $deadline = (Get-Date).AddSeconds(60)
    do {
        Start-Sleep -Milliseconds 500
        if (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue) { return }
    } while ((Get-Date) -lt $deadline)
    throw "SteamVR did not become ready within 60 seconds. Start SteamVR manually and retry."
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
    Stop-AddOn
    Invoke-Build
    Write-Step "Installing the relocatable runtime package"
    Invoke-Checked (Find-CMake) @(
        "--install", $script:BuildDirectory, "--config", $Configuration,
        "--prefix", $script:PackageDirectory
    )
    Require-Package
    Write-Host "Package ready: $script:PackageDirectory" -ForegroundColor Green
}

function Invoke-Register {
    Require-Package
    [void](Find-SteamVr)
    Start-SteamVrIfNeeded
    if (Test-Path -LiteralPath $script:RegistrationReceiptPath -PathType Leaf) {
        $previousManifest = (Get-Content -LiteralPath $script:RegistrationReceiptPath -Raw).Trim()
        if ($previousManifest -and $previousManifest -ne $script:ManifestPath) {
            Write-Step "Removing the manifest registration from the previous package location"
            Invoke-Checked $script:CliPath @("--unregister-manifest", $previousManifest)
        }
    }
    Write-Step "Registering the SteamVR application manifest and enabling auto-launch"
    Invoke-Checked $script:CliPath @("--register-manifest", $script:ManifestPath)
    Invoke-Checked $script:CliPath @("--manifest-status", $script:AppKey)
    New-Item -ItemType Directory -Path $script:UserDataDirectory -Force | Out-Null
    Set-Content -LiteralPath $script:RegistrationReceiptPath -Value $script:ManifestPath -Encoding UTF8
    Write-Host "Registered: $script:ManifestPath" -ForegroundColor Green
}

function Invoke-Unregister {
    if (-not (Test-Path -LiteralPath $script:ExecutablePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $script:CliPath -PathType Leaf)) {
        throw "The packaged runtime or command-line helper is missing, so clean cannot prove that the manifest was unregistered. Run 'package', then 'unregister', then 'clean'. No files were removed."
    }
    Start-SteamVrIfNeeded
    Stop-AddOn
    & $script:CliPath "--manifest-status" $script:AppKey
    $manifestState = $LASTEXITCODE
    if ($manifestState -eq 2) {
        Remove-Item -LiteralPath $script:RegistrationReceiptPath -Force -ErrorAction SilentlyContinue
        Write-Host "Manifest is already unregistered." -ForegroundColor Green
        return
    }
    if ($manifestState -ne 0) {
        throw "Unable to query manifest state (exit code $manifestState); no files were removed."
    }
    $registeredManifest = $script:ManifestPath
    if (Test-Path -LiteralPath $script:RegistrationReceiptPath -PathType Leaf) {
        $receiptValue = (Get-Content -LiteralPath $script:RegistrationReceiptPath -Raw).Trim()
        if ($receiptValue) { $registeredManifest = $receiptValue }
    }
    Write-Step "Disabling auto-launch and unregistering the application manifest"
    Invoke-Checked $script:CliPath @("--unregister-manifest", $registeredManifest)
    & $script:CliPath "--manifest-status" $script:AppKey
    if ($LASTEXITCODE -ne 2) {
        throw "Manifest removal could not be verified (status exit code $LASTEXITCODE); no build or user data was removed."
    }
    Remove-Item -LiteralPath $script:RegistrationReceiptPath -Force -ErrorAction SilentlyContinue
    Write-Host "Unregistered: $registeredManifest" -ForegroundColor Green
}

function Invoke-Launch {
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
    try { Write-Host "CMake:             $(Find-CMake)" } catch { Write-Host "CMake:             not found" -ForegroundColor Yellow }
    try { Write-Host "SteamVR:           $(Find-SteamVr)" } catch { Write-Host "SteamVR:           not found" -ForegroundColor Yellow }
    Write-Host "Build directory:   $script:BuildDirectory ($(if (Test-Path -LiteralPath $script:BuildDirectory) { 'present' } else { 'absent' }))"
    Write-Host "Package directory: $script:PackageDirectory ($(if (Test-Path -LiteralPath $script:PackageDirectory) { 'present' } else { 'absent' }))"
    Write-Host "Manifest:          $script:ManifestPath ($(if (Test-Path -LiteralPath $script:ManifestPath) { 'present' } else { 'absent' }))"
    Write-Host "SteamVR process:   $(if (Get-Process -Name 'vrserver' -ErrorAction SilentlyContinue) { 'running' } else { 'stopped' })"
    Write-Host "Add-on process:    $(if (Get-PackagedAddOnProcesses) { 'running' } else { 'stopped' })"
    Write-Host "User data:         $script:UserDataDirectory ($(if (Test-Path -LiteralPath $script:UserDataDirectory) { 'present' } else { 'absent' }))"
    if (Test-Path -LiteralPath $script:RegistrationReceiptPath -PathType Leaf) {
        Write-Host "Registration path: $((Get-Content -LiteralPath $script:RegistrationReceiptPath -Raw).Trim())"
    }
    if ((Test-Path -LiteralPath $script:CliPath -PathType Leaf) -and (Get-Process -Name "vrserver" -ErrorAction SilentlyContinue)) {
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
    if ($Follow) { Get-Content -LiteralPath $latestLog.FullName -Tail 100 -Wait }
    else { Get-Content -LiteralPath $latestLog.FullName -Tail 200 }
}

function Remove-IfPresent([string]$Path, [string]$Label) {
    if (Test-Path -LiteralPath $Path) {
        Write-Host "Removing $Label`: $Path"
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Invoke-Clean {
    Stop-AddOn
    Write-Step "Unregistering before deleting the package"
    Invoke-Unregister
    Write-Step "Removing reproducible development artifacts"
    Remove-IfPresent $script:BuildDirectory "CMake build tree"
    Remove-IfPresent $script:PackageDirectory "runtime package"
    if (-not $KeepUserData) { Remove-IfPresent $script:UserDataDirectory "settings and diagnostic logs" }
    else { Write-Host "Keeping user data because -KeepUserData was specified: $script:UserDataDirectory" }
    Write-Host "Clean complete. SteamVR, Steam, games, and unrelated OpenVR registrations were not modified." -ForegroundColor Green
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
  update      Stop, rebuild, repackage, refresh registration, and relaunch.
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
    "update"     { Stop-AddOn; Invoke-Package; Invoke-Register; Invoke-Launch }
    "status"     { Invoke-Status }
    "launch"     { Invoke-Launch }
    "logs"       { Invoke-Logs }
    "unregister" { Invoke-Unregister }
    "clean"      { Invoke-Clean }
    default       { Show-Help }
}
