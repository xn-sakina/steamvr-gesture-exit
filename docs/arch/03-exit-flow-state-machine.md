# Exit Flow State Machine

The state machine is the core behavioral contract. OpenVR, overlays, settings, and Windows process control should feed events into it and execute its side-effect commands.

## State Data

Shared context available to the state machine:

- Current settings snapshot.
- Current VR Game status.
- Exit Target availability.
- Configured Hand tracking status.
- Opposite Hand Pointer availability.
- Current time.
- Last Exit status.

Locked data captured at confirmation:

- Confirmed Exit Target identity.
- Confirmed Exit Target display name.
- Confirmed Exit Countdown duration.
- Confirmation timestamp.

The locked data must not be replaced by later Current VR Game changes.

## States

### No Active Flow

No in-game Exit Control or HUD is visible.

Allowed entry reasons:

- Startup.
- Idle timeout.
- Dismiss.
- Cancel.
- Prerequisite loss before confirmation.
- Target gone.
- Exit success.
- Exit failure after showing result.

Rendered output:

- Exit Control hidden.
- HUD Overlay hidden.

### Idle Visible

Farewell Gesture has requested Exit Control, and all prerequisites are available.

Rendered output:

- Compact Exit Control near Controller Wrist Zone.
- Optional target hint if space allows.
- Dismiss affordance.
- Hover feedback when Opposite Hand Pointer hovers.
- HUD hidden.

State data:

- Idle timeout deadline.
- Current target preview, not locked.

### Confirming

User clicked the idle Exit Control with Opposite Hand Pointer and must explicitly confirm or dismiss.

Rendered output:

- Exit Target display name when available.
- Force-close notice.
- Large separated Confirm and Dismiss targets.
- Hover feedback for both targets.
- HUD hidden.

State data:

- Current target preview, not locked until Confirm event.

### Counting Down

User confirmed Game Exit. Exit Target and duration are locked.

Rendered output:

- HUD Overlay visible in right-upper view with seconds and smooth progress.
- Exit Control shows Cancel Exit near Controller Wrist Zone when tracking/pointer are available.
- HUD is non-interactive.

State data:

- Locked Exit Target identity.
- Locked duration.
- Countdown start and end timestamps.
- Cancellation availability flag based on current wrist/pointer availability.

### Exiting

Countdown reached zero. Cancellation is no longer available. The add-on is attempting graceful close, waiting Exit Grace Period, or force closing the confirmed Exit Target.

Rendered output:

- HUD Overlay shows exiting state.
- Exit Control hidden.

State data:

- Locked Exit Target identity.
- Exit attempt stage.
- Exit Grace Period deadline.

### Failure Notice

Exit execution failed after confirmation. The add-on is showing a short HUD error before returning to No Active Flow.

Rendered output:

- HUD Overlay shows a short localized failure message.
- Exit Control hidden.

State data:

- Failure notice deadline.
- Last stable error code for Dashboard diagnostics and Diagnostic Log.

## Events

External events:

- `FarewellGestureRecognized`.
- `IdleTimeoutElapsed`.
- `ExitControlDismissClicked`.
- `ExitControlPrimaryClicked`.
- `ConfirmClicked`.
- `CancelExitClicked`.
- `ConfiguredHandTrackingLost`.
- `ConfiguredHandTrackingRestored`.
- `OppositeHandPointerLost`.
- `OppositeHandPointerRestored`.
- `CurrentVrGameChanged`.
- `ExitTargetAvailabilityChanged`.
- `SettingsChanged`.
- `CountdownTick`.
- `CountdownElapsed`.
- `ConfirmedTargetGoneOrInvalid`.
- `GracefulCloseRequested`.
- `GracefulCloseSucceededOrTargetExited`.
- `GracePeriodElapsed`.
- `ForceCloseSucceeded`.
- `ForceCloseFailed`.
- `HudErrorElapsed`.

Derived prerequisite:

```text
CanShowExitControl =
  Current VR Game is available
  and Exit Target is available
  and Configured Hand is tracked
  and Opposite Hand Pointer is available
```

## Transitions

```text
No Active Flow
  FarewellGestureRecognized + CanShowExitControl
    -> Idle Visible
       side effects: show Exit Control, set idle timeout, log gesture success/state transition

No Active Flow
  FarewellGestureRecognized + !CanShowExitControl
    -> No Active Flow
       side effects: no in-game warning; optional diagnostic reason update

Idle Visible
  FarewellGestureRecognized + CanShowExitControl
    -> Idle Visible
       side effects: refresh idle timeout only

Idle Visible
  ExitControlPrimaryClicked
    -> Confirming
       side effects: render confirmation view

Idle Visible
  IdleTimeoutElapsed | ExitControlDismissClicked
    -> No Active Flow
       side effects: hide Exit Control

Idle Visible
  ConfiguredHandTrackingLost | OppositeHandPointerLost | CurrentVrGameChanged | ExitTargetAvailabilityChanged(unavailable)
    -> No Active Flow
       side effects: hide Exit Control, no Last Exit success

Idle Visible
  SettingsChanged(ConfiguredHand)
    -> No Active Flow
       side effects: hide Exit Control

Confirming
  ConfirmClicked + Exit Target still available
    -> Counting Down
       side effects: revalidate displayed candidate, lock same Exit Target, lock duration, show HUD, show Cancel Exit, update Last Exit pending

Confirming
  ConfirmClicked + Exit Target unavailable or candidate mismatch
    -> No Active Flow
       side effects: hide Exit Control, diagnostic target unavailable or changed

Confirming
  ExitControlDismissClicked
    -> No Active Flow
       side effects: hide Exit Control

Confirming
  ConfiguredHandTrackingLost | OppositeHandPointerLost | CurrentVrGameChanged | ExitTargetAvailabilityChanged(unavailable)
    -> No Active Flow
       side effects: hide Exit Control

Confirming
  SettingsChanged(ConfiguredHand)
    -> No Active Flow
       side effects: hide Exit Control

Counting Down
  FarewellGestureRecognized
    -> Counting Down
       side effects: ignore gesture

Counting Down
  CancelExitClicked
    -> No Active Flow
       side effects: hide overlays, Last Exit canceled, log canceled

Counting Down
  ConfirmedTargetGoneOrInvalid
    -> No Active Flow
       side effects: hide overlays, clear locked target, log target gone

Counting Down
  ConfiguredHandTrackingLost | OppositeHandPointerLost
    -> Counting Down
       side effects: keep HUD countdown, hide or disable wrist Cancel Exit

Counting Down
  ConfiguredHandTrackingRestored | OppositeHandPointerRestored
    -> Counting Down
       side effects: restore Cancel Exit when both are available

Counting Down
  SettingsChanged(any)
    -> Counting Down
       side effects: active locked target and duration unchanged

Counting Down
  CurrentVrGameChanged | ExitTargetAvailabilityChanged(any candidate other than confirmed target)
    -> Counting Down
       side effects: keep locked target and duration unchanged; do not switch to a different target

Counting Down
  CountdownElapsed
    -> Exiting
       side effects: hide Cancel Exit, render HUD exiting, request graceful close

Exiting
  GracefulCloseSucceededOrTargetExited
    -> No Active Flow
       side effects: hide HUD, Last Exit success, log success

Exiting
  GracePeriodElapsed + target still alive and identity valid
    -> Exiting
       side effects: force close same target

Exiting
  GracePeriodElapsed + target gone or invalid
    -> No Active Flow
       side effects: hide HUD, Last Exit success or target-gone, log result

Exiting
  ForceCloseSucceeded
    -> No Active Flow
       side effects: hide HUD, Last Exit success, log forced success

Exiting
  ForceCloseFailed
    -> Failure Notice
       side effects: render short failure, Last Exit failed, log error code

Failure Notice
  HudErrorElapsed
    -> No Active Flow
       side effects: hide HUD
```

## Side-Effect Commands

The state machine may request these commands; adapters execute them:

- `ShowExitControl(viewModel)`.
- `UpdateExitControl(viewModel)`.
- `HideExitControl`.
- `ShowHud(viewModel)`.
- `UpdateHud(viewModel)`.
- `HideHud`.
- `StartIdleTimer(duration)`.
- `StartCountdown(duration)`.
- `LockExitTarget(identity, displayName)`.
- `RequestGracefulClose(identity)`.
- `StartExitGracePeriod(5s)`.
- `RequestForceClose(identity)`.
- `ShowFailureNotice(message, duration)`.
- `SetLastExit(status)`.
- `WriteDiagnosticLog(event)`.

## Test Coverage Targets

Core unit tests should cover:

- Gesture ignored when prerequisites fail.
- Gesture opens Idle Visible when prerequisites pass.
- Repeated gestures refresh idle timeout only.
- Idle timeout and Dismiss return to No Active Flow.
- Tracking/pointer loss before confirmation closes control.
- Confirm locks only the displayed and revalidated Exit Target plus countdown duration.
- Confirm rejects candidate mismatch.
- Settings changes before confirmation close control when Configured Hand changes.
- Settings changes during countdown do not mutate locked target or duration.
- New gestures during countdown are ignored.
- Tracking/pointer loss during countdown does not cancel.
- Cancel during countdown works.
- Target gone during countdown clears without process action.
- Countdown zero enters Exiting and removes cancellation.
- Countdown zero emits a Game Exit side-effect command; changing state alone is not enough.
- Graceful success reports success.
- Force close success reports success.
- Force close failure reports failure and keeps a visible failure HUD notice before stopping.
