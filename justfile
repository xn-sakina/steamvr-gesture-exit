version := `tr -d '\r\n' < VERSION`

# Show or run the existing Windows development workflow.
dev action="help" configuration="Debug":
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/windows/dev.ps1 {{action}} -Configuration {{configuration}}

test:
    cmake --preset host
    cmake --build --preset host --parallel
    ctest --test-dir out/build/host --output-on-failure

build-dev:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/windows/dev.ps1 package -Configuration Debug

build-release:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/windows/dev.ps1 package -Configuration Release

build-installer:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File installer/build.ps1

# Bump patch by default; accepts minor, major, or an explicit X.Y.Z.
up-version part="patch":
    bash scripts/up-version.sh {{part}}

show-version:
    @echo {{version}}
