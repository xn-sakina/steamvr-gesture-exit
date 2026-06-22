# MVP Feature Scope

This file is the implementation checklist for all MVP behavior in `docs/prd/mvp.md`.

## Required Runtime Scope

- Runtime target: Windows only.
- Language/toolchain target: C++20.
- SteamVR form factor: SteamVR Add-on Package, not a standalone user-launched app.
- MVP install path: manual or script-assisted installation is acceptable.
- Development hosts: macOS may be used for editing, asset work, static checks, and non-runtime tests, but not runtime acceptance.
- Reference game: VRChat.
- Compatibility coverage: at least two additional SteamVR titles smoke-tested.

## Direct Coding Constraints

- Use the checked-in `openvr/` source tree as the direct reference for OpenVR APIs, overlay behavior, event handling, input handling, samples, manifests, and docs.
- If an OpenVR API choice is unclear, inspect `openvr/headers`, `openvr/src`, `openvr/docs`, and `openvr/samples` before implementing.
- Do not create production SteamVR behavior from local mocks when the real API can be checked from `openvr/`.
- Do not first build a SteamVR shell and defer overlay compatibility, main loop behavior, input handling, Current VR Game handling, or Exit Target mapping.
- Do not defer Windows-related work. Per-user settings, Windows process identity, graceful close, force close, OS language fallback, logging paths, and package/runtime integration must be implemented directly during coding.
- It is acceptable for final runtime validation to happen later on a Windows SteamVR machine; it is not acceptable for the code to leave Windows or SteamVR production paths incomplete because the current development host cannot run them.

## Must-Hold Safety Invariants

- Current VR Game excludes SteamVR, SteamVR Home, Dashboard overlays, room setup, no-application states, non-game VR utilities, and this add-on.
- Exit Control is unavailable unless a Current VR Game and reliable Exit Target are both known.
- Exit Control is unavailable unless both hands are tracked and the Opposite Hand Pointer is available.
- Exit Target is bound at confirmation time and remains fixed through Exit Countdown and Exit Grace Period.
- Game Exit is limited to the confirmed Exit Target main process.
- If the confirmed target exits or no longer matches before execution, clear the flow and do not affect any other process.
- Graceful close is attempted before force close.
- Fixed Exit Grace Period is 5 seconds and is internal; it is not the user-visible Exit Countdown.
- If force close fails, report failure, log it, and stop. Do not retry indefinitely and do not expand process scope.

## User-Visible Feature Inventory

### Farewell Gesture

- Detect gesture from Configured Hand only.
- Gesture must occur in Activation Zone.
- Palm must broadly face away from headset or user.
- Hand orientation must be non-downward.
- Motion must include at least two direction reversals.
- Motion amplitude must reject jitter.
- Recognition window defaults to 1.2 seconds.
- Thresholds stay internal for MVP.
- Gesture only requests Exit Control.
- Gesture never confirms, cancels, or executes Game Exit.
- Repeated gestures refresh only Idle Visible timeout.
- Gestures are ignored during Confirming and Counting Down.

### Supported Controller Setup

- Require stable poses for both hands.
- Require Opposite Hand Pointer.
- No naked hand tracking, data gloves, or single-controller exit flows.
- No controller-brand-specific product behavior.

### Exit Control

- Appears only after Farewell Gesture when prerequisites are met.
- Appears near Controller Wrist Zone inferred from Configured Hand controller pose.
- Follows Configured Hand with smoothing.
- Remains compact and low-distraction while idle.
- Supports hover feedback with Opposite Hand Pointer.
- Supports click with Opposite Hand Pointer.
- Provides manual Dismiss.
- Times out automatically while idle.
- Closes if Configured Hand tracking or Opposite Hand Pointer availability is lost before confirmation.
- During countdown, exposes Cancel Exit on the wrist when tracking is available.
- Does not use gaze click, same-hand input, keyboard shortcuts, controller shortcuts, or Alt+F4.

### Confirmation

- Confirmation is required before countdown starts.
- Confirm and Dismiss are separated large targets.
- Confirmation names the Exit Target when a reliable display name is available.
- Confirmation communicates that the game may be force closed if unresponsive.
- Confirmation binds Exit Target and Exit Countdown duration.

### Exit Countdown

- Default user-visible duration: 5 seconds.
- Supported durations: 1, 3, 5, 10, 15 seconds.
- Shows seconds remaining.
- Shows smooth progress.
- New gestures are ignored.
- Temporary hand-tracking loss does not cancel countdown.
- Cancellation remains available through Exit Control only.
- At zero, cancellation ends and HUD Overlay changes to exiting state.

### HUD Overlay

- Non-interactive.
- Stays in the right-upper view while counting down.
- Does not steal pointer input or block gameplay interactions.
- Shows Exit Countdown seconds and progress.
- Shows `Exiting...` or equivalent during Exit Grace Period.
- Shows a short error message if exit fails.

### Countdown Sound

- Optional user setting.
- Low-distraction countdown cue.
- Disabled means no countdown sound.
- Game Exit proceeds even if audio output is unavailable.
- No haptics in MVP.

### Dashboard Panel

- SteamVR Dashboard surface, separate from the in-game flow.
- Shows User Settings.
- Shows first-use help.
- Shows lightweight diagnostics.
- Shows Current VR Game availability.
- Shows Exit Target availability.
- Shows Configured Hand tracking status.
- Shows Opposite Hand Pointer availability.
- Shows Last Exit status.
- Shows short settings reset/recovery status after corrupted settings recovery.
- Reports initialization failures through diagnostics or logs rather than system popups.

### User Settings

- Configured Hand.
- Exit Countdown duration.
- Exit Control timeout.
- Countdown Sound.
- Language.
- Default Configured Hand is right.
- Default Exit Countdown duration is 5 seconds.
- Default Exit Control timeout is 8 seconds.
- Countdown Sound is enabled by default.
- Default Language is Auto.
- Settings save immediately per Windows user.
- No Apply or Save button is required for settings changes.
- Settings corruption, missing file, or version incompatibility recovers to defaults.
- Current Idle Visible or Confirming control closes when Configured Hand changes.
- Active countdown target and duration do not change when settings change after confirmation.

### Language

- Auto language preference order: SteamVR or Steam runtime language, OS UI language, English.
- Manual choices: English and Chinese.
- User-facing UI follows selected language.
- Diagnostic Log remains English structured form and is not localized.

### Diagnostic Log

- Records add-on lifecycle.
- Records initialization result.
- Records Current VR Game detection changes.
- Records Farewell Gesture success events only, not per-frame pose data.
- Records state transitions.
- Records settings changes.
- Records Game Exit result and short error codes.
- Records settings recovery.
- Does not record continuous tracking data, game content, chat, audio, or broad process dumps.

## Internal Feature Inventory

### C++20 Build

- Add a top-level C++20 build system for the add-on runtime.
- Link against OpenVR headers/library already present under `openvr/`.
- Separate platform-specific Windows code behind explicit interfaces.
- Keep non-runtime static/unit tests runnable from development hosts where possible.
- Produce the runtime binary and SteamVR Add-on Package layout.

### SteamVR Add-on Package

- Provide manifest templates for SteamVR to recognize and manage the add-on.
- Package Dashboard Panel resources.
- Package in-game overlay resources.
- Package localization resources.
- Package default settings/schema version metadata if needed.

### Core State Machine

- Implement a deterministic reducer/controller for:
  - No Active Flow.
  - Idle Visible.
  - Confirming.
  - Counting Down.
  - Exiting.
- Treat external systems as event producers and side-effect executors.
- Unit-test transitions independently from OpenVR and Windows process APIs.

### OpenVR Runtime Main Loop

- Initialize OpenVR as a SteamVR add-on process.
- Create Dashboard Panel overlay.
- Create in-game Exit Control overlay.
- Create in-game HUD Overlay.
- Poll and dispatch OpenVR events.
- Poll tracked device poses for Configured Hand and Opposite Hand.
- Resolve Current VR Game and Current VR Game changes.
- Maintain Current VR Game to Exit Target mapping status.
- Feed gesture recognition and pointer input into the state machine.
- Render overlay state every frame or on invalidation.
- Shut down OpenVR resources cleanly.

### Overlay Input

- Handle OpenVR overlay mouse/laser pointer events for Exit Control.
- Use Opposite Hand Pointer only.
- Track hover enter/leave.
- Track click/press/release semantics.
- Provide native hover feedback.
- Avoid stealing normal game input when the pointer is not over Exit Control.
- HUD Overlay is non-interactive.

### Windows Process Control

- Map Current VR Game to reliable main process.
- Validate target identity before graceful close and before force close.
- Send graceful close request without focus-dependent input.
- Wait fixed 5-second Exit Grace Period.
- Force close only the same confirmed main process when still alive.
- Surface success, canceled, failed, and target-gone statuses.

## Explicitly Out of Scope

- Exiting a VRChat world, room, instance, lobby, or game-internal state.
- Closing SteamVR, Steam, SteamVR Home, Dashboard overlays, room setup, runtime processes, or unrelated tools.
- Guessing Exit Target from window title, broad process lists, or focus state.
- macOS runtime support.
- Full installer, MSI/NSIS packaging, code signing, auto-update, or store distribution.
- Per-game integrations or VRChat-specific APIs.
- Naked hand tracking, data gloves, single-controller operation, or game-specific hand skeleton data.
- Gaze input, same-hand input, keyboard shortcuts, Alt+F4, simulated keyboard/mouse input.
- Haptics.
- Theme/color customization.
- Gesture sensitivity UI.
- Pose mode settings.
- Controller-brand-specific UI.
- Steam account sync.
- Localized Diagnostic Log output.
- Promise that overlays are visible to other players, game cameras, or recording tools.
