# ADR 0001: Target Windows Runtime

- Status: Accepted
- Date: 2026-08-31

## Context

SteamVR Gesture Exit depends on SteamVR overlay behavior and Windows process identity and termination APIs. Development happens primarily on macOS, while acceptance testing happens on a Windows SteamVR machine. Pretending to support multiple runtimes would increase ambiguity in the highest-risk process-control code.

## Decision

The shipped runtime is Windows x64 only and uses CMake with a Visual Studio 2022-compatible MSVC toolchain. macOS supports source editing, asset work, static checks, portable tests, and optional Windows cross-compilation, but not user-facing runtime execution. Runtime acceptance requires a Windows SteamVR system and a Supported Controller Setup.

The repository provides a PowerShell workflow for configuring, building, packaging, registering, updating, inspecting, and cleanly removing the development package.

## Consequences

- Windows-specific process APIs do not require a macOS runtime equivalent.
- Runtime failures must be diagnosable with bounded Diagnostic Log events and Dashboard Panel diagnostics.
- A successful macOS test or cross-build is not SteamVR runtime acceptance.
- Supporting another runtime requires a new ADR and a complete safety review.
