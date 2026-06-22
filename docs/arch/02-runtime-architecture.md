# Runtime Architecture

## Package Shape

The MVP is a Windows SteamVR Add-on Package implemented in C++20. It should load with SteamVR, provide a Dashboard Panel, render in-game overlays, and continue running across multiple Current VR Games during the same Add-on Session.

Implementation must use the checked-in OpenVR source tree under `openvr/` as the direct API reference. When an OpenVR behavior, event, overlay flag, input path, manifest field, or sample pattern is unclear, inspect `openvr/headers`, `openvr/src`, `openvr/docs`, and `openvr/samples` before coding. Do not replace SteamVR/OpenVR integration with project-local mocks or invented abstractions.

Expected package contents:

- Runtime executable or add-on binary.
- SteamVR manifest templates.
- Dashboard Panel assets.
- Exit Control overlay assets.
- HUD Overlay assets.
- Localization resources for English and Chinese user-facing text.
- Default settings/schema metadata.

## Build Architecture

Initial build requirements:

- Compile as C++20.
- Treat Windows runtime code as the primary target.
- Use OpenVR headers, libraries, samples, and docs from `openvr/`.
- Build production OpenVR integration from the first runtime slice; do not start with a SteamVR shell that postpones overlay, event, input, or application-resolution compatibility.
- Include Windows runtime implementation in coding tasks that touch external behavior; do not leave process control, per-user storage, or OS language integration as later stubs.
- Keep Windows-only APIs behind platform interfaces so state-machine and settings tests can run without SteamVR.
- Provide build targets for:
  - runtime package build,
  - unit tests,
  - static or compile checks,
  - clean.

Recommended high-level target split:

- `steamvr_bail_out_app`: runtime entry point and OpenVR integration.
- `steamvr_bail_out_core`: state machine, settings model, gesture classifier, diagnostics model.
- `steamvr_bail_out_win`: Windows process control and per-user storage.
- `steamvr_bail_out_tests`: non-runtime unit tests.

Keep this split compact. It is a target boundary, not permission to create many tiny files or empty framework layers. The implementation should stay small enough that a future agent can trace Farewell Gesture -> Exit Control -> Confirm -> Countdown -> Windows Game Exit by reading a handful of cohesive files.

## Module Boundaries

### Add-on Application

Responsibilities:

- Own process lifetime.
- Initialize and shut down OpenVR.
- Start and stop services.
- Run the main loop.
- Convert OpenVR and Windows observations into core events.
- Execute side effects requested by the state machine.
- Fail toward self-disablement if initialization is incomplete.

Does not:

- Decide state transitions directly.
- Guess Exit Targets.
- Store settings ad hoc outside the settings module.

### OpenVR Runtime Adapter

Responsibilities:

- Initialize OpenVR runtime access.
- Create and manage Dashboard Panel overlay.
- Create and manage Exit Control overlay.
- Create and manage HUD Overlay.
- Poll OpenVR events.
- Poll tracked device poses.
- Read controller roles or action/pointer availability.
- Resolve SteamVR foreground application identity when available.
- Report Dashboard overlay visibility and input focus context.
- Follow real OpenVR API contracts from `openvr/headers/openvr.h`, `openvr/docs`, and relevant `openvr/samples` code.
- Avoid mock-only event, input, or overlay paths in production code.

Outputs:

- Tracked pose snapshots.
- Pointer availability and pointer events.
- Overlay lifecycle events.
- Current VR Game candidate changes.
- Dashboard interaction events.

### Current VR Game Resolver

Responsibilities:

- Convert OpenVR foreground application/application key data into Current VR Game status.
- Exclude SteamVR, SteamVR Home, Dashboard overlays, room setup, no-application states, non-game VR utilities, and this add-on.
- Provide a stable display name when available.
- Report unavailable/filtered reasons for Dashboard diagnostics.

Outputs:

- `NoCurrentVrGame`.
- `CurrentVrGameAvailable`.
- `CurrentVrGameFiltered(reason)`.
- `CurrentVrGameChanged(previous, next)`.

### Exit Target Resolver

Responsibilities:

- Map Current VR Game to a reliable Windows main process.
- Reject unavailable or ambiguous mappings.
- Provide display name and stable process identity.
- Revalidate confirmed target identity later.
- Never broaden scope to child processes or nearby tools unless a future ADR explicitly changes behavior.

Outputs:

- `ExitTargetAvailable`.
- `ExitTargetUnavailable(reason)`.
- `ExitTargetIdentity`.

### Gesture Recognizer

Responsibilities:

- Consume Configured Hand pose samples.
- Apply Activation Zone, outward palm, non-downward orientation, direction reversal, and amplitude rules.
- Emit only discrete Farewell Gesture success events.
- Avoid logging continuous motion samples.
- Ignore gestures when core state says they are not actionable.

Inputs:

- Configured Hand pose.
- Headset pose or body-relative frame.
- User setting: Configured Hand.
- Current prerequisites from the state controller.

### Core Flow Controller

Responsibilities:

- Own state machine.
- Track locked Exit Target and locked countdown duration.
- Enforce transition rules.
- Produce render model state for overlays.
- Produce side-effect commands such as start countdown, close target, clear UI, or update Last Exit.

Does not:

- Call OpenVR directly.
- Call Windows process APIs directly.
- Persist settings directly.

### Overlay View Models and Renderer

Responsibilities:

- Convert core flow state and Dashboard diagnostics into renderable view models.
- Render Dashboard Panel.
- Render Exit Control idle, confirming, cancel, hover, disabled, and error states.
- Render HUD countdown progress, seconds, exiting, and failure states.
- Keep HUD non-interactive.
- Keep Exit Control compact and low-distraction.

### Settings Store

Responsibilities:

- Load per-Windows-user settings.
- Validate schema version and values.
- Recover missing/corrupted/version-incompatible settings to defaults.
- Save immediately after each setting change.
- Publish settings change events to the flow controller and renderers.
- Surface recovery status to Dashboard diagnostics.

### Language Service

Responsibilities:

- Resolve Auto language using SteamVR or Steam runtime language, OS UI language, then English.
- Apply manual English or Chinese override.
- Provide localized user-facing strings.
- Keep Diagnostic Log in English.

### Diagnostic Log

Responsibilities:

- Write structured English log entries.
- Include low-sensitivity lifecycle, setup, detection, state, settings, and exit-result events.
- Exclude continuous poses, game content, chat, audio, and broad process dumps.
- Provide current/last status summary for Dashboard Panel.

### Windows Process Controller

Responsibilities:

- Check whether Exit Target is alive.
- Revalidate target identity.
- Request graceful close without focus-dependent input.
- Wait fixed 5-second Exit Grace Period.
- Force close same target if still alive.
- Report success, target gone, graceful failure, force success, force failure.

## Runtime Data Flow

```text
OpenVR events/poses/apps
  -> OpenVR Runtime Adapter
  -> Current VR Game Resolver
  -> Exit Target Resolver
  -> Core Flow Controller
  -> Overlay View Models
  -> OpenVR Overlay Renderer

Tracked poses
  -> Gesture Recognizer
  -> Core Flow Controller

Exit Control pointer events
  -> OpenVR Runtime Adapter
  -> Core Flow Controller

Dashboard events
  -> Settings Store
  -> Core Flow Controller / Dashboard diagnostics

Core side effects
  -> Windows Process Controller
  -> Diagnostic Log / Last Exit
```

## Initialization Flow

1. Start add-on runtime process.
2. Initialize Diagnostic Log.
3. Load settings; recover to defaults if invalid.
4. Resolve language.
5. Initialize OpenVR.
6. Create Dashboard Panel overlay.
7. Create in-game Exit Control overlay in hidden state.
8. Create HUD Overlay in hidden non-interactive state.
9. Start main loop.
10. If any non-critical in-game feature fails, keep Dashboard diagnostics available when possible.
11. If OpenVR initialization fails, log and exit without system popups.

## Main Loop Responsibilities

Each tick should:

- Poll OpenVR events.
- Poll tracked poses.
- Update Current VR Game and Exit Target availability.
- Update pointer and hand prerequisite status.
- Feed gesture recognition.
- Feed state-machine events.
- Advance countdown and exit-grace timers.
- Render changed overlay view models.
- Flush Diagnostic Log entries.

The loop must avoid blocking on process control. Graceful close and Exit Grace Period should be modeled as asynchronous or staged work so overlay state remains responsive until cancellation closes at countdown zero.

The runtime is not acceptable if it initializes OpenVR and exits after logging. It must keep a persistent event loop, create the Dashboard Panel, Exit Control, and HUD overlays, poll OpenVR events/poses, update Current VR Game and Exit Target availability, feed the state machine, and execute side-effect commands.

## Failure Posture

- Missing game: no in-game warning, Dashboard diagnostic only.
- Filtered game or SteamVR state: no Exit Control, Dashboard diagnostic only.
- Missing Exit Target: no Exit Control, Dashboard diagnostic only.
- Missing pointer or hand tracking before confirmation: close control silently.
- Target gone during countdown: clear flow, update status, no other process action.
- Exit failure after confirmation: short HUD error, Last Exit failed, Diagnostic Log entry.
- Settings corruption: recover to defaults, log, Dashboard status.
- Initialization failure: diagnostics/logs, no intrusive system popups.
