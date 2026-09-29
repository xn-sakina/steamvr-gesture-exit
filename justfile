set windows-shell := ["cmd.exe", "/C"]

version := trim(read("VERSION"))

dev action="help" configuration="Debug":
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/windows/dev.ps1 {{ action }} -Configuration {{ configuration }}

test:
    cmake --preset host
    cmake --build --preset host --parallel
    ctest --preset host

format:
    cmake --preset host
    cmake --build --preset host --target format

build-dev:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/windows/dev.ps1 package -Configuration Debug

build-release:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/windows/dev.ps1 package -Configuration Release

build-installer:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File installer/build.ps1

up-version part="patch":
    bash scripts/up-version.sh {{ part }}

show-version:
    @echo {{ version }}
