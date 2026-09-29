[CmdletBinding()]
param(
    [string]$IsccPath,
    [switch]$SkipRuntimeBuild,
    [switch]$RequireSigning,
    [string]$SignToolPath,
    [string]$CertificateThumbprint = [Environment]::GetEnvironmentVariable(
        "WINDOWS_CODESIGN_CERTIFICATE_THUMBPRINT"
    ),
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-SignTool([string]$RequestedPath) {
    if ($RequestedPath) {
        $resolved = [IO.Path]::GetFullPath($RequestedPath)
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "SignTool.exe was not found: $resolved"
        }
        return $resolved
    }

    $command = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $programFilesX86 = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
    if ($programFilesX86) {
        $kitsBin = Join-Path $programFilesX86 "Windows Kits\10\bin"
        if (Test-Path -LiteralPath $kitsBin -PathType Container) {
            $candidate = Get-ChildItem -Path (Join-Path $kitsBin "*\x64\signtool.exe") -File |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($candidate) { return $candidate.FullName }
        }
    }

    throw "SignTool.exe was not found. Install the Windows SDK or pass -SignToolPath."
}

function Get-CodeSigningCertificate([string]$Thumbprint) {
    $normalized = ($Thumbprint -replace '\s', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{40}$') {
        throw "CertificateThumbprint must be a 40-character SHA-1 certificate thumbprint."
    }

    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$normalized" -ErrorAction SilentlyContinue
    if (-not $certificate -or -not $certificate.HasPrivateKey) {
        throw "The code-signing certificate '$normalized' with a private key was not found in Cert:\CurrentUser\My."
    }
    $codeSigningUsage = @($certificate.EnhancedKeyUsageList | Where-Object {
        $_.ObjectId.Value -eq "1.3.6.1.5.5.7.3.3"
    })
    if ($codeSigningUsage.Count -eq 0) {
        throw "The certificate '$normalized' is not valid for code signing."
    }
    $now = Get-Date
    if ($now -lt $certificate.NotBefore -or $now -gt $certificate.NotAfter) {
        throw "The code-signing certificate '$normalized' is not currently valid."
    }
    return $certificate
}

function Assert-AuthenticodeSignature([string]$Path, [string]$ExpectedThumbprint) {
    $signature = Get-AuthenticodeSignature -FilePath $Path
    if ([string]$signature.Status -ne "Valid" -or -not $signature.SignerCertificate) {
        throw "Authenticode verification failed for '$Path': $($signature.Status) $($signature.StatusMessage)"
    }
    if ($signature.SignerCertificate.Thumbprint -ine $ExpectedThumbprint) {
        throw "'$Path' was signed by an unexpected certificate '$($signature.SignerCertificate.Thumbprint)'."
    }
}

function Set-BinarySignature(
    [string]$Path,
    [string]$ResolvedSignTool,
    [string]$Thumbprint,
    [string]$TimestampUrl
) {
    & $ResolvedSignTool sign /sha1 $Thumbprint /s My /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed for '$Path' with exit code $LASTEXITCODE." }

    & $ResolvedSignTool verify /pa /all /tw $Path
    if ($LASTEXITCODE -ne 0) { throw "SignTool verification failed for '$Path' with exit code $LASTEXITCODE." }
    Assert-AuthenticodeSignature -Path $Path -ExpectedThumbprint $Thumbprint
}

function Set-ScriptSignature(
    [string]$Path,
    [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
    [string]$TimestampUrl
) {
    $signature = Set-AuthenticodeSignature -FilePath $Path -Certificate $Certificate `
        -HashAlgorithm SHA256 -TimestampServer $TimestampUrl
    if ([string]$signature.Status -ne "Valid") {
        throw "PowerShell script signing failed for '$Path': $($signature.Status) $($signature.StatusMessage)"
    }
    Assert-AuthenticodeSignature -Path $Path -ExpectedThumbprint $Certificate.Thumbprint
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

if (-not $SkipRuntimeBuild) {
    & (Join-Path $projectRoot "scripts\windows\dev.ps1") package -Configuration Release
}

$runtimeRoot = Join-Path $projectRoot "out\package\steamvr-gesture-exit"
$buildRoot = Join-Path $projectRoot "out\build\windows-x64\Release"
$payloadRoot = Join-Path $projectRoot "out\installer\payload"
$outputRoot = Join-Path $projectRoot "out\installer"
$archivePath = Join-Path $outputRoot "payload.zip"
$stagingRoot = Join-Path $outputRoot "staging"
$transactionScript = Join-Path $stagingRoot "transaction.ps1"

$signingEnabled = -not [string]::IsNullOrWhiteSpace($CertificateThumbprint)
if ($RequireSigning -and -not $signingEnabled) {
    throw "Release signing is required, but no certificate thumbprint was provided."
}
$certificate = $null
if ($signingEnabled) {
    $CertificateThumbprint = ($CertificateThumbprint -replace '\s', '').ToUpperInvariant()
    $certificate = Get-CodeSigningCertificate $CertificateThumbprint
    $SignToolPath = Resolve-SignTool $SignToolPath
    if ([string]::IsNullOrWhiteSpace($TimestampServer)) {
        throw "TimestampServer is required for signed builds."
    }
}

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
foreach ($source in @($helperCli, $helperOpenVr)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing installer helper: $source" }
}

Remove-Item -LiteralPath $payloadRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
foreach ($relative in $runtimeFiles) {
    $destination = Join-Path $payloadRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $runtimeRoot $relative) -Destination $destination -Force
}
New-Item -ItemType Directory -Path (Join-Path $payloadRoot "installer") -Force | Out-Null
Copy-Item -LiteralPath $helperCli -Destination (Join-Path $payloadRoot "installer\steamvr_gesture_exit_cli.exe") -Force
Copy-Item -LiteralPath $helperOpenVr -Destination (Join-Path $payloadRoot "installer\openvr_api.dll") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "transaction.ps1") -Destination $transactionScript -Force
if ($signingEnabled) {
    Set-ScriptSignature -Path $transactionScript -Certificate $certificate -TimestampUrl $TimestampServer
}
$payloadTransactionScript = Join-Path $payloadRoot "installer\transaction.ps1"
Copy-Item -LiteralPath $transactionScript -Destination $payloadTransactionScript -Force
if ($signingEnabled) {
    Assert-AuthenticodeSignature -Path $payloadTransactionScript -ExpectedThumbprint $CertificateThumbprint
}
$openVrLicense = Join-Path $projectRoot "openvr\LICENSE"
if (-not (Test-Path -LiteralPath $openVrLicense -PathType Leaf)) { throw "Missing OpenVR license: $openVrLicense" }
@(
    "OpenVR SDK / openvr_api.dll"
    ""
    (Get-Content -LiteralPath $openVrLicense -Raw)
) -join "`r`n" | Set-Content -LiteralPath (Join-Path $payloadRoot "THIRD_PARTY_NOTICES.txt") -Encoding UTF8

if ($signingEnabled) {
    $binaryFiles = @(
        (Join-Path $payloadRoot "bin\win64\steamvr_gesture_exit.exe"),
        (Join-Path $payloadRoot "bin\win64\openvr_api.dll"),
        (Join-Path $payloadRoot "installer\steamvr_gesture_exit_cli.exe"),
        (Join-Path $payloadRoot "installer\openvr_api.dll")
    )
    foreach ($path in $binaryFiles) {
        Set-BinarySignature -Path $path -ResolvedSignTool $SignToolPath `
            -Thumbprint $CertificateThumbprint -TimestampUrl $TimestampServer
    }
}

$records = [Collections.Generic.List[object]]::new()
$payloadPrefix = $payloadRoot.TrimEnd('\') + '\'
foreach ($file in (Get-ChildItem -LiteralPath $payloadRoot -File -Recurse | Sort-Object FullName)) {
    if (-not $file.FullName.StartsWith($payloadPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Payload file escaped the staging root: $($file.FullName)"
    }
    $relative = $file.FullName.Substring($payloadPrefix.Length).Replace('\', '/')
    $records.Add([ordered]@{
        path = $relative
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    })
}
$manifest = [ordered]@{ schema = 1; version = $version; files = $records }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $payloadRoot "payload.json") -Encoding UTF8

Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $payloadRoot "*") -DestinationPath $archivePath -CompressionLevel Optimal

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
    throw "ISCC.exe was not found. Install Inno Setup 7 or pass -IsccPath."
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$isccArguments = @(
    "/DAppVersion=$version",
    "/DPayloadArchive=$archivePath",
    "/DOutputDir=$outputRoot",
    "/DTransactionScript=$transactionScript"
)
if ($signingEnabled) {
    $signToolName = "SteamVrGestureExitAuthenticode"
    $signToolCommand = '$q' + $SignToolPath + '$q sign /sha1 ' + $CertificateThumbprint +
        ' /s My /fd SHA256 /tr $q' + $TimestampServer + '$q /td SHA256 $f'
    $isccArguments += "/DSignToolName=$signToolName"
    $isccArguments += "--signtool=$signToolName=$signToolCommand"
}
$isccArguments += (Join-Path $PSScriptRoot "steamvr-gesture-exit.iss")
& $IsccPath @isccArguments
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed with exit code $LASTEXITCODE." }

$installer = Join-Path $outputRoot "steamvr-gesture-exit-v$version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Installer output was not created: $installer" }
if ($signingEnabled) {
    & $SignToolPath verify /pa /all /tw $installer
    if ($LASTEXITCODE -ne 0) {
        throw "Installer signature verification failed with exit code $LASTEXITCODE."
    }
    Assert-AuthenticodeSignature -Path $installer -ExpectedThumbprint $CertificateThumbprint
}
Write-Host "Installer ready: $installer" -ForegroundColor Green
