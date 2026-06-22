# Coding Task Map

This file converts the MVP architecture into an implementation sequence and coverage checklist.

Before coding any phase, read `07-agent-implementation-guardrails.md`. The phases below are not permission to build abstract layers first. Each phase should produce real production code for the external boundary it touches, using `openvr/` and Windows APIs directly where applicable. Automated tests should stay focused on core state and pure logic; OpenVR/UI/Windows integration is primarily verified by Windows build, diagnostics, and manual SteamVR testing.

Also read `08-prior-failure-regression-guards.md` before claiming a phase is complete. It lists concrete failed shapes that must not recur.

Important: these phases are a review map, not stopping points. A first implementation that completes only Phase 1 or Phase 2 is not useful. The first serious coding pass should build a thin but complete vertical MVP:

```text
SteamVR add-on starts
  -> Dashboard opens
  -> Current VR Game / Exit Target diagnostics update
  -> Farewell Gesture opens Exit Control
  -> Opposite Hand Pointer hover/click confirms through real UI
  -> HUD counts down with visible progress and optional sound
  -> Windows Game Exit path runs
  -> Last Exit / Diagnostic Log updates
```

The first pass should prefer a small number of direct files over a broad architecture. Target fewer than 10 production source/header files unless a real external boundary makes another file necessary.

## Phase 1: Build and Package Foundation

Phase 1 must not be delivered alone as the MVP. It is only acceptable when immediately followed by the vertical runtime path below.

Deliverables:

- C++20 build system.
- Runtime executable target.
- Core library target.
- Windows adapter library target.
- Unit test target.
- SteamVR Add-on Package directory layout.
- SteamVR manifest templates.
- Real OpenVR-linked runtime startup and shutdown path.
- Initial Dashboard Panel, Exit Control, and HUD resources wired to real OpenVR overlay creation.
- Windows settings/log/process-control interfaces with production Windows implementations where they are first used.

Acceptance:

- `build` creates a Windows runtime package artifact.
- Core unit-test target can run without SteamVR where supported.
- Package includes manifest and resource paths expected by SteamVR.
- Runtime code links against OpenVR from `openvr/` and follows API usage verified from local OpenVR headers/docs/samples.
- No production path is backed only by a fake SteamVR adapter, fake overlay adapter, fake input adapter, or fake Windows process controller.
- Source shape remains compact; no empty managers, placeholder adapters, or broad framework layers.
- Runtime does not initialize OpenVR and exit; it owns a persistent loop and dispatches real events/side effects.
- The first pass must include real user-visible Dashboard, Exit Control, HUD countdown, and failure/exiting states. A logic-only pass is not accepted.

## Phase 2: Core Models and State Machine

Deliverables:

- Domain types for Current VR Game, Exit Target, settings, tracking status, pointer status, Last Exit.
- Core state machine from `03-exit-flow-state-machine.md`.
- Side-effect command interface.
- Unit tests for core transitions.

Acceptance:

- Gesture never directly confirms or exits.
- Exit Target and duration lock on Confirm.
- Confirm rejects target mismatch instead of locking an arbitrary reliable target.
- Countdown ignores new gestures and settings changes.
- Tracking loss behavior differs before and during countdown.
- Force close failure stops with failed status.

## Phase 3: Settings, Language, and Diagnostic Log

Deliverables:

- Settings schema.
- Per-Windows-user settings store.
- Immediate save on Dashboard changes.
- Corruption/version recovery to defaults.
- Language resolver and string tables for English/Chinese.
- Structured English Diagnostic Log.

Acceptance:

- Missing/corrupted settings recover without blocking startup.
- Dashboard can show recovery status.
- Settings changes emit state-machine events.
- Diagnostic Log covers lifecycle, detection, state, settings, and exit results.
- Logs exclude continuous pose and sensitive content.

## Phase 4: OpenVR Main Loop and Dashboard Panel

Deliverables:

- OpenVR initialization/shutdown.
- Main loop event pump.
- Dashboard Panel creation/rendering.
- Dashboard settings controls.
- Dashboard first-use help.
- Dashboard diagnostics.
- Initialization failure reporting through diagnostics/logs.
- API choices cross-checked against `openvr/headers`, `openvr/docs`, and `openvr/samples`.

Acceptance:

- SteamVR can recognize/manage the add-on package.
- Dashboard Panel opens.
- Settings are readable and changeable.
- Help explains gesture, opposite-hand confirmation, countdown, and force-close risk.
- Diagnostics show Current VR Game, Exit Target, hand/pointer prerequisites, and Last Exit.
- Production runtime uses real OpenVR events, overlays, and input APIs rather than local mock behavior.
- Dashboard must not be a placeholder; it must expose MVP settings, help, and diagnostics in a usable form.

## Phase 5: Current VR Game and Exit Target

Deliverables:

- Current VR Game resolver.
- SteamVR/Home/Dashboard/non-game filtering.
- Exit Target resolver for reliable Windows main process mapping.
- Target identity revalidation.
- Dashboard unavailable reason codes.
- Windows main-process mapping implementation, not a deferred stub.

Acceptance:

- VRChat is detected as Current VR Game and maps to an Exit Target.
- SteamVR Home, Dashboard, no game, and non-game tools do not show Exit Control.
- Ambiguous/unavailable target keeps Exit Control hidden.
- Target identity can be revalidated during countdown and before force close.
- Mapping failures fail closed with diagnostics, but the mapping code itself is implemented.
- Process identity revalidation uses PID plus creation/start-time guard and executable path or SteamVR app identity when available.

## Phase 6: Farewell Gesture Recognition

Deliverables:

- Pose sample buffer for Configured Hand.
- Activation Zone check.
- Outward palm approximation.
- Non-downward orientation check.
- Direction reversal count.
- Amplitude/jitter rejection.
- 1.2-second default recognition window.
- Gesture success event logging.

Acceptance:

- Valid Farewell Gesture opens Idle Visible when prerequisites pass.
- Low movement, behind-body movement, downward hand, or missing prerequisites do not open Exit Control.
- Gesture success logs are discrete; pose streams are not logged.

## Phase 7: Exit Control Overlay

Deliverables:

- In-game Exit Control overlay creation.
- Idle rendering.
- Confirming rendering.
- Countdown Cancel rendering.
- Controller Wrist Zone placement from Configured Hand pose.
- Smoothing.
- Opposite Hand Pointer hover/click hit testing.
- Native hover feedback.
- Manual Dismiss.
- Idle timeout.

Acceptance:

- Exit Control appears near Configured Hand wrist area.
- Control follows the Configured Hand.
- Opposite Hand Pointer hover changes visual state without clicking.
- Click enters Confirming.
- Confirm and Dismiss are separated large hit targets.
- Tracking/pointer loss before confirmation closes control.
- Normal game input is not captured when not hovering/clicking the control.

## Phase 8: HUD Overlay and Countdown Sound

Deliverables:

- Non-interactive HUD Overlay.
- Right-upper view placement.
- Countdown seconds display.
- Smooth progress display.
- Exiting state.
- Short failure state.
- Optional countdown sound.

Acceptance:

- HUD appears after Confirm and stays in right-upper view.
- HUD does not accept clicks.
- Cancel remains on wrist, not HUD.
- Sound plays only when enabled.
- Countdown proceeds if sound fails.
- Countdown must not be internal-only; seconds/progress must be visible.
- Exiting and short failure states must be visible in HUD.

## Phase 9: Windows Game Exit Execution

Deliverables:

- Graceful close implementation.
- Fixed 5-second Exit Grace Period.
- Force close same confirmed main process.
- Target-gone handling.
- Failure HUD and Last Exit updates.
- Complete Windows API implementation ready for Windows SteamVR validation.

Acceptance:

- Countdown zero closes only confirmed target.
- SteamVR and the add-on remain running.
- If target exits during countdown, no process is closed.
- If target changes identity, no process is closed.
- Force close failure logs and reports failure without retrying or broadening scope.
- macOS or non-Windows development limitations may block live execution, but must not be used as a reason to leave the Windows implementation incomplete.
- Verification priority is Windows build plus manual controlled-process or game validation. Add automated process-control tests only if they are cheap and do not require large mock infrastructure.
- Countdown elapsed is wired to real Game Exit execution; a state transition to Exiting alone is not accepted.
- Exit failure produces a visible HUD failure notice before returning to hidden state.

## Phase 10: Windows VR Validation

Deliverables:

- Manual test run against VRChat.
- Smoke tests against at least two additional SteamVR titles.
- Negative scenario checklist execution.
- Diagnostic Log review.

Acceptance:

- `docs/test-plan/mvp.md` primary VRChat flow passes.
- Cancel flow passes.
- Negative scenarios fail closed.
- Compatibility smoke titles either exit safely or refuse with diagnostics.

## Full PRD Coverage Checklist

Build and packaging:

- [ ] C++20 build.
- [ ] SteamVR Add-on Package.
- [ ] SteamVR manifest templates.
- [ ] Windows-only runtime boundary.
- [ ] OpenVR API usage verified against checked-in `openvr/` source/docs/samples.
- [ ] No mock-only SteamVR or Windows production path.
- [ ] Compact source layout without unnecessary abstraction or file explosion.
- [ ] Each completed coding slice is vertically wired to a production behavior.

Core behavior:

- [ ] Core state machine.
- [ ] Farewell Gesture recognition.
- [ ] Supported Controller Setup prerequisite.
- [ ] Safe no-op when prerequisites fail.
- [ ] Exit Target locked at confirmation.
- [ ] Exit Countdown separate from Exit Grace Period.
- [ ] Add-on Session persists after Game Exit.

OpenVR and overlays:

- [ ] OpenVR runtime main loop.
- [ ] OpenVR event handling.
- [ ] Current VR Game filtering.
- [ ] Current VR Game to Exit Target mapping feed.
- [ ] Dashboard Panel creation/rendering.
- [ ] Exit Control creation/rendering.
- [ ] HUD Overlay creation/rendering.
- [ ] OpenVR overlay mouse/laser event handling.
- [ ] Opposite Hand Pointer-only hover/click.
- [ ] Controller Wrist Zone following with smoothing.
- [ ] Right-upper HUD positioning.
- [ ] Native hover feedback.
- [ ] Countdown Sound.

Dashboard:

- [ ] User Settings.
- [ ] First-use help.
- [ ] Diagnostics.
- [ ] Immediate save.
- [ ] Settings recovery status.
- [ ] Last Exit status.
- [ ] Language selection.

Windows integration:

- [ ] Per-user settings persistence.
- [ ] Corrupted settings recovery.
- [ ] Windows process identity mapping.
- [ ] Graceful close.
- [ ] Fixed 5-second Exit Grace Period.
- [ ] Same-process force close.
- [ ] Failure reporting.
- [ ] SteamVR survival after Game Exit.

Diagnostics and privacy:

- [ ] Diagnostic Log lifecycle events.
- [ ] Detection changes.
- [ ] Gesture success events only.
- [ ] State transitions.
- [ ] Settings changes.
- [ ] Exit results and error codes.
- [ ] No continuous tracking data.
- [ ] No game content/chat/audio/process dumps.

Testing scope:

- [ ] Core state-machine transitions covered by automated tests.
- [ ] Gesture classification pure logic covered by focused tests.
- [ ] Settings validation/recovery covered by focused tests.
- [ ] No broad mock-heavy coverage requirement for OpenVR overlays, UI rendering, audio, or Windows process APIs.
- [ ] Windows build and `docs/test-plan/mvp.md` manual SteamVR validation remain the integration acceptance path.

Prior failure regression guards:

- [ ] Runtime is not a stub and has persistent OpenVR loop.
- [ ] Exiting state is wired to Game Exit execution.
- [ ] Confirm locks only the displayed/revalidated Exit Target.
- [ ] PID reuse is guarded by stronger process identity checks.
- [ ] Exit failure HUD is visible.
- [ ] Current VR Game detection is not broad substring guessing.
- [ ] Language Auto follows SteamVR/Steam, OS UI, English fallback.
- [ ] Dashboard, Exit Control, HUD countdown, and Countdown Sound are implemented as real user-facing paths.
- [ ] Core logic is not treated as complete without UI/audio/countdown feedback.

Explicit exclusions:

- [ ] No SteamVR/Steam/runtime process closing.
- [ ] No game-internal VRChat world/session exit.
- [ ] No macOS runtime.
- [ ] No gaze/same-hand/keyboard/Alt+F4/simulated input.
- [ ] No haptics/theme/sensitivity/pose tuning UI.
- [ ] No installer/signing/auto-update requirement.
