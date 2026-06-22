# OpenVR, Overlays, and Input

This file defines the SteamVR/OpenVR runtime work needed for Dashboard Panel, in-game Exit Control, HUD Overlay, pointer input, wrist following, and native feedback.

## OpenVR Runtime Main Loop

The runtime loop must integrate these tasks:

- Initialize OpenVR for the add-on.
- Register/create Dashboard Panel overlay.
- Register/create Exit Control overlay.
- Register/create HUD Overlay.
- Poll OpenVR events.
- Poll overlay events.
- Poll tracked poses for headset, Configured Hand, and Opposite Hand.
- Poll pointer/action availability for Opposite Hand Pointer.
- Detect Current VR Game changes.
- Dispatch pointer, gesture, tracking, target, and settings events into the core state machine.
- Render overlay content when view models change.
- Update overlay transforms every tick while visible.
- Keep the Add-on Session alive after a Game Exit.

## Current VR Game Handling in OpenVR

The OpenVR adapter should provide a SteamVR application candidate to the Current VR Game Resolver. The resolver owns filtering.

Required filtering outcomes:

- No running foreground VR application: no Current VR Game.
- SteamVR Home: filtered.
- SteamVR Dashboard or Dashboard overlays: filtered.
- Room setup or runtime utilities: filtered.
- This add-on: filtered.
- Non-game VR utility: filtered or unavailable based on reliable classification.
- Game candidate with reliable process mapping: Current VR Game available.

Dashboard diagnostics must expose the current reason when Exit Control cannot appear.

Current VR Game classification must not be built primarily on broad name substring guesses. Use SteamVR application identity, application type/metadata, manifest information, mapped process identity, and explicit runtime exclusions where available. Unknown or ambiguous candidates should be unavailable with diagnostics, not accepted as games.

## Dashboard Panel

Dashboard Panel is a SteamVR Dashboard overlay, not part of the in-game exit flow.

### Dashboard Features

- User Settings:
  - Configured Hand.
  - Exit Countdown duration: 1, 3, 5, 10, 15 seconds.
  - Exit Control timeout.
  - Countdown Sound enabled/disabled.
  - Language: Auto, English, Chinese.
- First-use help:
  - Farewell Gesture overview.
  - Opposite-hand confirmation requirement.
  - Exit Countdown explanation.
  - Force-close risk explanation.
  - Safety note that SteamVR is not closed.
- Diagnostics:
  - Current VR Game status and display name when available.
  - Exit Target availability and reason if unavailable.
  - Configured Hand tracking status.
  - Opposite Hand Pointer availability.
  - Last Exit status.
  - Settings recovery/reset status.
  - Initialization failure status when relevant.

### Dashboard Rendering

Implementation may start with a simple native-rendered panel or a texture-backed UI, but it must support:

- Immediate settings updates.
- Clear readable text in VR.
- Localized user-facing strings.
- No system popup dependency for normal operation.
- A diagnostics view that can be checked while troubleshooting in SteamVR.

## Exit Control Overlay

Exit Control is an in-game overlay attached near the Controller Wrist Zone.

### Creation

- Create at startup or lazily on first use.
- Keep hidden until Idle Visible.
- Enable input only when visible and interactive.
- Associate interaction with Opposite Hand Pointer only.
- Keep dimensions compact in idle state and larger in Confirming state.

### Placement

Use Configured Hand controller pose to infer Controller Wrist Zone:

- Derive a stable local offset from controller pose rather than anatomical bones.
- Position near watch-like wrist area.
- Orient toward headset/user enough to remain readable.
- Smooth transform updates to avoid jitter.
- Continue following Configured Hand in Idle Visible and Confirming.
- During Counting Down, show Cancel Exit on wrist only when hand tracking and pointer are available.

### States to Render

- Idle:
  - compact visual control,
  - target hint if available,
  - dismiss affordance,
  - hover feedback.
- Confirming:
  - target name,
  - force-close notice,
  - Confirm target,
  - Dismiss target,
  - separated large hit areas,
  - hover feedback per target.
- Countdown cancel:
  - Cancel Exit target,
  - disabled/hidden state if tracking or pointer unavailable.
- Hidden:
  - no input capture.

### Native Hover Feedback

Hover feedback should be implemented as a native overlay state change:

- Track pointer enter/leave over hit regions.
- Update render model to highlight hovered region.
- Do not trigger click on hover.
- Preserve low-distraction visual style.
- Do not add haptics in MVP.

## HUD Overlay

HUD Overlay is a non-interactive in-game overlay used for countdown and exit status.

### Creation

- Create at startup or lazily.
- Set to non-interactive.
- Keep hidden outside Counting Down, Exiting, and short failure display.

### Placement

- Stay in the user's right-upper view.
- Use a head-relative or dashboard-equivalent transform appropriate for OpenVR overlays.
- Update transform so it remains visible while the user moves.
- Avoid world-space placement that can be left behind.

### States to Render

- Countdown:
  - remaining seconds,
  - smooth progress,
  - optional target name if space allows,
  - no click targets.
- Exiting:
  - exiting status,
  - cancellation no longer available.
- Failure:
  - short error message,
  - no retry target,
  - message clears after a short duration.

## Overlay Input Events

Exit Control must handle OpenVR overlay pointer/mouse/laser events. The exact event names depend on OpenVR APIs used, but the adapter must normalize them into core input events:

- Pointer hover enter region.
- Pointer hover leave region.
- Pointer press down.
- Pointer release/click.
- Pointer canceled/lost.

Rules:

- Only Opposite Hand Pointer can hover or click.
- Same-hand pointer input is ignored.
- Gaze input is ignored.
- Keyboard/mouse desktop simulation is not part of MVP.
- Normal game input should not be stolen unless the pointer is explicitly over the Exit Control.
- HUD Overlay consumes no pointer events.

## Farewell Gesture Input

The OpenVR adapter provides pose samples to the Gesture Recognizer:

- Headset pose or user-relative frame.
- Configured Hand pose.
- Controller orientation sufficient to infer palm direction approximation.
- Timestamped samples over the 1.2-second default window.

Coordinate and orientation contract:

- Interpret Activation Zone in a headset-relative frame so the gesture is based on the player's current view/body orientation rather than world origin.
- Approximate palm direction from the Configured Hand controller's local axes and document the controller-axis mapping used by the implementation.
- Treat outward palm as broadly facing away from the headset/user within internal MVP thresholds.
- Treat non-downward orientation as a separate guard from direction-reversal motion, so a low waving movement with the controller pointed down does not qualify.
- Keep exact gesture thresholds internal for MVP, but keep the frame, controller-axis mapping, direction-reversal count, and amplitude/jitter rejection visible in code comments or diagnostics-level developer notes.

Gesture recognizer emits only `FarewellGestureRecognized` events. It should not expose raw pose streams to Diagnostic Log.

## Countdown Sound

The audio component is driven by state-machine view state:

- Play subtle countdown ticks only when Countdown Sound is enabled.
- Stop immediately on cancel, target-gone clear, or exit completion.
- Continue Game Exit if sound playback fails.
- Do not play sound for idle timeout, dismiss, unavailable prerequisites, or failure before interaction.

## Rendering Implementation Notes

The first implementation should prefer simple, deterministic render paths:

- Small texture-backed UI surfaces are acceptable.
- Text rendering must handle English and Chinese user-facing strings.
- View models should be plain data generated by core/render-model code.
- Overlay renderer should be replaceable without changing state-machine behavior.
- All overlay resource handles must be cleaned up on shutdown.

## Acceptance Checklist

- Dashboard Panel can be opened in SteamVR.
- Dashboard shows settings, help, diagnostics, and Last Exit.
- Exit Control appears only when prerequisites pass.
- Exit Control follows Configured Hand wrist area.
- Hover feedback appears only under Opposite Hand Pointer.
- Confirm and Dismiss are large and separated.
- HUD appears in right-upper view during countdown.
- HUD is non-interactive.
- Cancel remains on wrist during countdown when tracking is available.
- Temporary tracking loss during countdown keeps HUD running.
- Normal game input is not captured when not interacting with Exit Control.
