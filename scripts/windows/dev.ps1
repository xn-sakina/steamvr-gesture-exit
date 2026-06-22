param(
  [ValidateSet("configure", "build", "test", "package", "register", "unregister", "start", "stop", "rebuild", "status", "logs")]
  [string]$Action = "rebuild",
  [string]$BuildDir = "",
  [string]$Config = "Debug",
  [string]$OpenVrRoot = "",
  [string]$SteamVrRoot = "",
  [string]$Generator = "Visual Studio 17 2022",
  [string]$Platform = "x64",
  [switch]$Run,
  [switch]$Register,
  [switch]$NoTest
)

$ErrorActionPreference = "Stop"

function RepoRoot {
  return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$Root = RepoRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
  $BuildDir = Join-Path $Root ".build\windows"
}
if ([string]::IsNullOrWhiteSpace($OpenVrRoot)) {
  $OpenVrRoot = Join-Path $Root "openvr"
}

function Require-File([string]$Path, [string]$Name) {
  if (!(Test-Path -LiteralPath $Path)) {
    throw "$Name not found: $Path"
  }
}

function Resolve-VrPathReg {
  $candidates = @()
  if (![string]::IsNullOrWhiteSpace($SteamVrRoot)) {
    $candidates += (Join-Path $SteamVrRoot "bin\win64\vrpathreg.exe")
    $candidates += (Join-Path $SteamVrRoot "bin\win32\vrpathreg.exe")
  }
  if ($env:STEAMVR_ROOT) {
    $candidates += (Join-Path $env:STEAMVR_ROOT "bin\win64\vrpathreg.exe")
    $candidates += (Join-Path $env:STEAMVR_ROOT "bin\win32\vrpathreg.exe")
  }
  $candidates += "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
  $candidates += "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win32\vrpathreg.exe"

  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw "vrpathreg.exe not found. Pass -SteamVrRoot or set STEAMVR_ROOT."
}

function PackageManifest {
  return (Join-Path $BuildDir "package\steamvr-bail-out.vrmanifest")
}

function PackageExe {
  return (Join-Path $BuildDir "package\bin\win64\steamvr-bail-out.exe")
}

function Configure {
  Require-File (Join-Path $OpenVrRoot "headers\openvr.h") "OpenVR header"
  Require-File (Join-Path $OpenVrRoot "lib\win64\openvr_api.lib") "OpenVR import library"
  Require-File (Join-Path $OpenVrRoot "bin\win64\openvr_api.dll") "OpenVR runtime DLL"
  cmake -S $Root -B $BuildDir -G $Generator -A $Platform -DOPENVR_ROOT="$OpenVrRoot" -DSBO_BUILD_RUNTIME=ON
}

function Build {
  Configure
  cmake --build $BuildDir --config $Config --target steamvr-bail-out
}

function Test-Core {
  Configure
  cmake --build $BuildDir --config $Config --target steamvr_bail_out_tests
  ctest --test-dir $BuildDir -C $Config --output-on-failure
}

function Package {
  Configure
  cmake --build $BuildDir --config $Config --target package
  Require-File (PackageManifest) "Packaged SteamVR manifest"
  Require-File (PackageExe) "Packaged runtime executable"
}

function Register-Manifest {
  Package
  $vrpathreg = Resolve-VrPathReg
  & $vrpathreg addappmanifest (PackageManifest)
  & $vrpathreg showappmanifest
}

function Unregister-Manifest {
  $manifest = PackageManifest
  $vrpathreg = Resolve-VrPathReg
  & $vrpathreg removeappmanifest $manifest
  & $vrpathreg showappmanifest
}

function Stop-Runtime {
  Get-Process -Name "steamvr-bail-out" -ErrorAction SilentlyContinue | Stop-Process -Force
}

function Start-Runtime {
  Require-File (PackageExe) "Packaged runtime executable"
  Start-Process -FilePath (PackageExe) -WorkingDirectory (Split-Path (PackageExe) -Parent)
}

function Show-Status {
  Write-Host "Repo: $Root"
  Write-Host "BuildDir: $BuildDir"
  Write-Host "Config: $Config"
  Write-Host "OpenVrRoot: $OpenVrRoot"
  Write-Host "Manifest: $(PackageManifest)"
  Write-Host "Runtime: $(PackageExe)"
  if (Get-Process -Name "steamvr-bail-out" -ErrorAction SilentlyContinue) {
    Write-Host "Runtime process: running"
  } else {
    Write-Host "Runtime process: stopped"
  }
}

function Show-Logs {
  $log = Join-Path $env:APPDATA "SteamVR Bail Out\diagnostic.log"
  Require-File $log "Diagnostic log"
  Get-Content -LiteralPath $log -Tail 120
}

switch ($Action) {
  "configure" { Configure }
  "build" { Build }
  "test" { Test-Core }
  "package" { Package }
  "register" { Register-Manifest }
  "unregister" { Unregister-Manifest }
  "start" { Start-Runtime }
  "stop" { Stop-Runtime }
  "status" { Show-Status }
  "logs" { Show-Logs }
  "rebuild" {
    Stop-Runtime
    Package
    if (!$NoTest) { Test-Core }
    if ($Register) { Register-Manifest }
    if ($Run) { Start-Runtime }
  }
}
