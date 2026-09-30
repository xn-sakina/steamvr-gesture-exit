#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$IsccPath,
    [switch]$SkipRuntimeBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FileSha256([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($Path)
        try {
            return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
        } finally {
            $stream.Dispose()
        }
    } finally {
        $algorithm.Dispose()
    }
}

function New-ZipArchive([string]$SourceDirectory, [string]$DestinationPath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $SourceDirectory,
        $DestinationPath,
        [IO.Compression.CompressionLevel]::Optimal,
        $false
    )
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$version = (Get-Content -LiteralPath (Join-Path $projectRoot "VERSION") -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain X.Y.Z."
}

$packageVersion = (Get-Content -LiteralPath (Join-Path $projectRoot "package.json") -Raw | ConvertFrom-Json).version
if ($packageVersion -ne $version) {
    throw "VERSION ($version) and package.json ($packageVersion) do not match."
}

$innoVersionPath = Join-Path $PSScriptRoot "INNO_SETUP_VERSION"
$requiredInnoVersion = (Get-Content -LiteralPath $innoVersionPath -Raw).Trim()
if ($requiredInnoVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "INNO_SETUP_VERSION must contain X.Y.Z."
}

if (-not $SkipRuntimeBuild) {
    & (Join-Path $projectRoot "scripts\windows\dev.ps1") package -Configuration Release
}

$runtimeRoot = Join-Path $projectRoot "out\package\steamvr-gesture-exit"
$buildRoot = Join-Path $projectRoot "out\build\windows-x64\Release"
$payloadRoot = Join-Path $projectRoot "out\installer\payload"
$outputRoot = Join-Path $projectRoot "out\installer"
$archivePath = Join-Path $outputRoot "payload.zip"
$transactionScript = Join-Path $PSScriptRoot "transaction.ps1"

$runtimeFiles = @(
    "bin\win64\steamvr_gesture_exit.exe",
    "bin\win64\openvr_api.dll",
    "manifest\steamvr_gesture_exit.vrmanifest",
    "assets\brand\steamvr-gesture-exit.png"
)
foreach ($relative in $runtimeFiles) {
    $source = Join-Path $runtimeRoot $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing release file: $source" }
}

$helperCli = Join-Path $buildRoot "steamvr_gesture_exit_cli.exe"
$helperOpenVr = Join-Path $buildRoot "openvr_api.dll"
$coordinatorRunner = Join-Path $buildRoot "steamvr_gesture_exit_coordinator_runner.exe"
foreach ($source in @($helperCli, $helperOpenVr, $coordinatorRunner)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing installer helper: $source" }
}

Remove-Item -LiteralPath $payloadRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
foreach ($relative in $runtimeFiles) {
    $destination = Join-Path $payloadRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $runtimeRoot $relative) -Destination $destination -Force
}
New-Item -ItemType Directory -Path (Join-Path $payloadRoot "installer") -Force | Out-Null
Copy-Item -LiteralPath $helperCli -Destination (Join-Path $payloadRoot "installer\steamvr_gesture_exit_cli.exe") -Force
Copy-Item -LiteralPath $helperOpenVr -Destination (Join-Path $payloadRoot "installer\openvr_api.dll") -Force
Copy-Item -LiteralPath $coordinatorRunner -Destination (Join-Path $payloadRoot "installer\coordinator_runner.exe") -Force
Copy-Item -LiteralPath $transactionScript -Destination (Join-Path $payloadRoot "installer\transaction.ps1") -Force
$projectLicense = Join-Path $projectRoot "LICENSE"
if (-not (Test-Path -LiteralPath $projectLicense -PathType Leaf)) { throw "Missing project license: $projectLicense" }
Copy-Item -LiteralPath $projectLicense -Destination (Join-Path $payloadRoot "LICENSE") -Force
$openVrLicense = Join-Path $projectRoot "openvr\LICENSE"
if (-not (Test-Path -LiteralPath $openVrLicense -PathType Leaf)) { throw "Missing OpenVR license: $openVrLicense" }
@(
    "OpenVR SDK / openvr_api.dll"
    ""
    (Get-Content -LiteralPath $openVrLicense -Raw)
) -join "`r`n" | Set-Content -LiteralPath (Join-Path $payloadRoot "THIRD_PARTY_NOTICES.txt") -Encoding UTF8

$records = [Collections.Generic.List[object]]::new()
[long]$payloadInstallSize = 0
[long]$payloadLargestFileSize = 0
$payloadPrefix = $payloadRoot.TrimEnd('\') + '\'
foreach ($file in (Get-ChildItem -LiteralPath $payloadRoot -File -Recurse | Sort-Object FullName)) {
    if (-not $file.FullName.StartsWith($payloadPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Payload file escaped the staging root: $($file.FullName)"
    }
    $relative = $file.FullName.Substring($payloadPrefix.Length).Replace('\', '/')
    [long]$fileSize = $file.Length
    $records.Add([pscustomobject][ordered]@{
        path = $relative
        size = $fileSize
        sha256 = Get-FileSha256 $file.FullName
    })
    $payloadInstallSize = [long]($payloadInstallSize + $fileSize)
    if ($fileSize -gt $payloadLargestFileSize) { $payloadLargestFileSize = $fileSize }
}
if ($records.Count -eq 0) { throw "The installer payload is empty." }
$manifest = [ordered]@{ schema = 1; version = $version; files = @($records) }
$payloadManifestPath = Join-Path $payloadRoot "payload.json"
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $payloadManifestPath -Encoding UTF8
$payloadExpandedSize = [long]($payloadInstallSize + (Get-Item -LiteralPath $payloadManifestPath).Length)

Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
New-ZipArchive $payloadRoot $archivePath
$payloadArchiveSize = [long](Get-Item -LiteralPath $archivePath).Length

if (-not $IsccPath) {
    $command = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
}
if (-not $IsccPath) {
    $isccCandidates = [Collections.Generic.List[string]]::new()
    if ($env:ProgramFiles) { $isccCandidates.Add((Join-Path $env:ProgramFiles "Inno Setup 7\ISCC.exe")) }
    if ($env:LOCALAPPDATA) { $isccCandidates.Add((Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 7\ISCC.exe")) }
    if (${env:ProgramFiles(x86)}) {
        $isccCandidates.Add((Join-Path ${env:ProgramFiles(x86)} "Inno Setup 7\ISCC.exe"))
    }
    foreach ($candidate in $isccCandidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $IsccPath = $candidate
            break
        }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw "ISCC.exe was not found. Install Inno Setup $requiredInnoVersion or pass -IsccPath."
}

$isccVersionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($IsccPath)
$actualInnoVersion = "$($isccVersionInfo.FileMajorPart).$($isccVersionInfo.FileMinorPart).$($isccVersionInfo.FileBuildPart)"
if ($actualInnoVersion -ne $requiredInnoVersion) {
    throw "ISCC.exe version $actualInnoVersion does not match required Inno Setup version $requiredInnoVersion."
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$isccArguments = @(
    "/DAppVersion=$version",
    "/DPayloadArchive=$archivePath",
    "/DOutputDir=$outputRoot",
    "/DTransactionScript=$transactionScript",
    "/DPayloadManifest=$payloadManifestPath",
    "/DCoordinatorRunner=$(Join-Path $payloadRoot 'installer\coordinator_runner.exe')",
    "/DPayloadInstallSize=$payloadInstallSize",
    "/DPayloadExpandedSize=$payloadExpandedSize",
    "/DPayloadArchiveSize=$payloadArchiveSize",
    "/DPayloadLargestFileSize=$payloadLargestFileSize"
)
$isccArguments += (Join-Path $PSScriptRoot "steamvr-gesture-exit.iss")
& $IsccPath @isccArguments
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed with exit code $LASTEXITCODE." }

$installer = Join-Path $outputRoot "steamvr-gesture-exit-v$version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Installer output was not created: $installer" }
Write-Host "Installer ready: $installer" -ForegroundColor Green
