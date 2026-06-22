# Windows, Settings, and Diagnostics

This file defines Windows integration, process-control behavior, settings persistence, language, and Diagnostic Log requirements.

## Windows Runtime Boundary

The MVP runtime is Windows only. Windows-specific implementation is required for:

- Per-user settings storage.
- Current VR Game to Exit Target process mapping.
- Graceful process close.
- Fixed Exit Grace Period.
- Force close of confirmed main process.
- OS UI language fallback.

The rest of the code should keep Windows APIs behind interfaces so core behavior can be tested without SteamVR. Those interfaces must have real Windows implementations when first used by production flow code; do not merge no-op process control, fake per-user storage, fake language fallback, or incomplete Windows behavior.

## Current VR Game to Exit Target Mapping

### Inputs

- OpenVR foreground VR application identity.
- SteamVR application key or metadata when available.
- Process identity or launch information available through SteamVR/OpenVR or Windows APIs.

### Required Output

An `ExitTargetIdentity` should include enough stable data to revalidate the same process later:

- Process id.
- Creation time or equivalent identity guard.
- Executable path or application key when available.
- Display name for user-facing confirmation when reliable.

PID alone is not a valid identity. PID reuse must not allow graceful close or force close to target a different process later.

### Mapping Rules

- Accept only a reliable main process for the Current VR Game.
- Reject missing process ids.
- Reject ambiguous candidates.
- Reject SteamVR, Steam, SteamVR Home, Dashboard overlays, room setup, runtime processes, non-game utilities, this add-on, and unknown processes.
- Do not infer from window title alone.
- Do not scan broad process lists to guess.
- Do not target child processes unless they are explicitly and reliably the game main process.

### Diagnostics

Dashboard should show:

- Current VR Game display name or unavailable reason.
- Exit Target available/unavailable.
- Short reason code for unavailable target.

Diagnostic Log should record changes and reason codes, not full process dumps.

## Process Control

### Confirmation

At Confirm click:

1. Revalidate that the current Exit Target is still available.
2. Verify that the revalidated target is the same candidate displayed in Confirming.
3. Lock `ExitTargetIdentity`.
4. Lock display name.
5. Lock Exit Countdown duration.
6. Start Counting Down.

If the candidate changed, disappeared, or no longer matches, do not lock another target. Close the flow or return to an unavailable diagnostic state.

### Countdown Completion

At countdown zero:

1. Revalidate locked target identity.
2. If target is gone or identity no longer matches, clear flow without closing anything.
3. If target is valid, request graceful close.
4. Start fixed 5-second Exit Grace Period.
5. If target exits during grace period, mark success.
6. If target remains alive after grace period, force close only the same target.
7. If force close succeeds, mark success.
8. If force close fails, show short HUD error, mark failed, log error.

### Graceful Close Requirements

- Use a Windows process close mechanism that targets the process directly.
- Do not use keyboard simulation.
- Do not use Alt+F4.
- Do not depend on foreground window focus.
- Keep SteamVR and add-on alive.
- Implement this path with real Windows APIs during coding. If final behavior cannot be exercised on the current development host, leave the code complete and record that Windows SteamVR validation is pending.

### Force Close Requirements

- Revalidate same locked identity immediately before force close.
- Revalidation must compare PID plus creation time/start-time guard and executable path or SteamVR application identity when available.
- Force close only that main process.
- Do not close Steam, SteamVR, runtime processes, child process trees, launchers, or unrelated tools.
- Do not retry indefinitely.
- Implement this path with real Windows APIs during coding, not a placeholder or mock production implementation.

## Settings Persistence

### Settings Schema

MVP settings:

- `configuredHand`: left or right.
- `exitCountdownDurationSeconds`: 1, 3, 5, 10, or 15.
- `exitControlTimeoutSeconds`: bounded MVP timeout value.
- `countdownSoundEnabled`: boolean.
- `language`: auto, en, zh.
- `schemaVersion`: integer.

Defaults:

- Configured Hand: right.
- Exit Countdown duration: 5 seconds.
- Exit Control timeout: 8 seconds.
- Countdown Sound: enabled.
- Language: Auto.

These defaults are part of the MVP settings recovery contract. If settings are missing, corrupted, or version-incompatible, recovery should produce the same values.

### Persistence Rules

- Store per Windows user.
- Load during add-on startup.
- Save immediately after each Dashboard setting change.
- Validate all enum and numeric values.
- Do not require Apply or Save.
- If saving fails, keep current in-memory setting for the session, show Dashboard status, and log the failure.

### Corruption and Version Recovery

Recover to defaults when:

- Settings file is missing.
- Settings file is malformed.
- Required fields are missing.
- Field values are invalid.
- Schema version is incompatible.

Recovery side effects:

- Continue startup with defaults.
- Write Diagnostic Log entry.
- Show short Dashboard reset/recovery status.
- Save recovered defaults when safe.

## Settings Change Effects

- Configured Hand change:
  - Idle Visible or Confirming: close current control.
  - Counting Down: active countdown and locked target are not affected.
  - Future gestures use new hand.
- Countdown duration change:
  - No Active Flow/Idle/Confirming: next confirmation uses new duration.
  - Counting Down/Exiting: active locked duration is unchanged.
- Exit Control timeout change:
  - Applies to next Idle Visible entry or refreshed idle timeout.
- Countdown Sound change:
  - Applies immediately where feasible, but must not affect Game Exit execution.
- Language change:
  - Re-render Dashboard and overlays with selected language where visible.

## Language Service

Resolution order for Auto:

1. SteamVR or Steam runtime language.
2. OS UI language.
3. English.

Manual choices:

- English.
- Chinese.

Rules:

- User-facing strings are localized.
- Diagnostic Log remains English.
- Short error codes remain stable across languages.
- `Auto` must not be implemented as English-only fallback. If SteamVR/Steam language cannot be read on the current host, keep the production code path and fall back to OS UI language, then English.

## Diagnostic Log

### Required Events

- Add-on start.
- Settings load result.
- Settings recovery.
- OpenVR initialization result.
- Overlay creation result.
- Current VR Game detection changes.
- Exit Target availability changes.
- Supported Controller Setup availability changes.
- Farewell Gesture success.
- State transitions.
- User Settings changes.
- Countdown start/cancel/elapsed.
- Graceful close request/result.
- Force close request/result.
- Exit success/cancel/failure.
- Initialization failures.

### Event Shape

Each entry should include:

- Timestamp.
- Event name.
- Session/build identifier if available.
- Short stable reason or error code.
- Low-sensitivity fields such as app key, sanitized display name, pid only when needed for debugging.

### Privacy Exclusions

Do not log:

- Continuous pose samples.
- Raw hand/controller motion streams.
- Game content.
- Chat.
- Audio.
- Screenshots.
- Broad process lists.
- Window title scraping dumps.
- Steam account identifiers unless absolutely required by SteamVR APIs and explicitly reviewed later.

## Last Exit Status

Dashboard must expose a concise Last Exit status:

- None.
- Pending.
- Canceled.
- Success graceful.
- Success forced.
- Target gone before execution.
- Failed graceful/force close.
- Failed unavailable target.

Last Exit should update on meaningful completion points and survive Dashboard open/close during the same Add-on Session. Persisting Last Exit across SteamVR restarts is optional unless implementation chooses to include it in diagnostics.

## Windows/SteamVR Integration Checklist

- Add-on appears as SteamVR-managed add-on.
- Starts with SteamVR.
- Dashboard Panel opens from SteamVR Dashboard.
- In-game overlays appear over Current VR Game.
- Game Exit closes only confirmed game process.
- SteamVR remains running after Game Exit.
- Add-on remains running after Game Exit.
- Settings are per Windows user.
- Logs are locally available for Windows-side testing.
