# SteamVR Gesture Exit（SteamVR手势退出）

This context defines the user-facing domain language for a SteamVR add-on that lets a player intentionally leave the current VR game through an in-world farewell gesture.

## Language

**Current VR Game**:
The SteamVR foreground VR application that is occupying the VR scene and is meaningful for the player to leave. SteamVR itself, SteamVR Home, Dashboard overlays, room setup, no-application states, non-game VR utilities, and this add-on are not Current VR Games.
_Avoid_: current app, foreground window, launched game

**Game Exit**:
The intentional application-level leaving of the Current VR Game. It ends only the game application itself, not SteamVR, SteamVR Home, SteamVR runtime processes, or an in-game state such as leaving a world, returning to a lobby, or disconnecting from a session. The user is informed that an unresponsive game may be force closed.
_Avoid_: leave world, leave instance, quit room, exit SteamVR

**Exit Target**:
The reliable main-process target selected from the Current VR Game at the moment the user confirms a Game Exit. The Exit Target remains fixed through the Exit Countdown; if it exits or no longer matches the confirmed game, the Game Exit flow ends without affecting a different application. Target identity remains an internal safety boundary and does not add copy to the compact Exit Control.
_Avoid_: current process, latest app, foreground app

**Farewell Gesture**:
The intentional gesture used only to request the Exit Control. It is made by moving the Configured Hand controller through three committed side-to-side reversals with enough repeated lateral travel and a recognisable human rhythm. Recognition combines head-relative trajectory smoothing, lateral path dominance, stroke efficiency, endpoint stability, extent and timing consistency, plausible speed, and the absence of active controller input. Compact, quick motion at chest, armrest, or lap height is valid. Generic controller poses do not establish anatomical palm direction, so recognition does not depend on a guessed palm orientation or a standing posture.
_Avoid_: wave, bye-bye, gesture trigger

**Activation Zone**:
The head-relative area where a Farewell Gesture can be recognized. It stays within plausible arm reach and is not substantially behind the user, but has no chest or height gate: seated players may begin from an armrest, in front of the body, or over their lap.
_Avoid_: world position, height threshold, tracking area

**Configured Hand**:
The hand selected by the user for detecting the Farewell Gesture.
_Avoid_: dominant hand, active hand, right hand

**Gesture Sensitivity**:
The user-selected amount of clean Farewell Gesture evidence required before showing the Exit Control. Low, Normal, and High adjust accepted stroke extent, lateral dominance, path efficiency, rhythm consistency, endpoint stability, and aggregate confidence; High is the default. Every level still requires three committed reversals and preserves the same tracking, active-input, plausible-speed, arm-reach, head-relative, and behind-head safety boundaries. Sensitivity never imposes a hand-height, standing-posture, overhead, forward-reach, or palm-orientation requirement.
_Avoid_: pose difficulty, safety level, gesture mode

**Opposite Hand Pointer**:
The pointing ray and trigger input from the hand that is not the Configured Hand. It is the only intended way to hover and activate the Exit Control. While the ray intersects the Exit Control, the add-on renders a live guide line from the controller pose to the pointer point on the control; both disappear as soon as the intersection ends. The add-on renders this pointer itself and must not activate SteamVR's system-wide overlay laser or capture scene input focus.
_Avoid_: gaze pointer, same-hand input, controller shortcut

**Supported Controller Setup**:
A SteamVR controller setup that provides stable poses for both hands and an Opposite Hand Pointer. MVP support is defined by these capabilities rather than by a specific controller brand or hand-tracking device.
_Avoid_: hand tracking, single-controller mode, controller brand support

**Exit Control**:
The compact, low-distraction, rounded in-world control that appears in the Opposite-side View Zone after a Farewell Gesture is detected while a Current VR Game and Opposite Hand Pointer are available. Its square layout, flat controls, visually central exit question, restrained upper-corner brand signature, and fixed button group minimise view obstruction and layout shift. Its position and eye-facing angle are captured in world space when it first appears, so head movement does not make the click target move. A top progress bar visualises the auto-dismiss timeout; pointing anywhere at the control pauses that timeout and refills the bar, while leaving restarts the full timeout. The same single control can ask the user to exit or cancel, ask for a second confirmation, or replace the question with the Game Exit countdown centred in the space above the button group. The countdown may carry an extremely faint circularly clipped brand watermark behind the number, but the number and progress ring retain full legibility and contrast.
_Avoid_: exit button, quit button, wrist button

**Opposite-side View Zone**:
The side of the user's view at the moment the control appears corresponding to the Opposite Hand Pointer: right side after a left-hand Farewell Gesture and left side after a right-hand Farewell Gesture. Placement uses the full HMD forward, right, and up axes, including pitch and roll, so the control remains in the intended view when looking down, reclining, or lying on either side; its top aligns with the player's view. It selects the initial world-space position without attaching the Exit Control to the HMD or either moving controller.
_Avoid_: real wrist, hand bone, watch position, controller-attached panel

**Exit Countdown**:
The short, visible delay between confirming a Game Exit and carrying it out. A large integer second value and a circular progress ring that shifts from blue toward red replace the central brand mark inside the existing Exit Control, with a button-group-width Cancel action as its only action. Fractions above zero display as at least one second. New Farewell Gestures or temporary hand-tracking loss do not change an active Exit Countdown.
_Avoid_: delay, timer, grace period

**Exit Grace Period**:
The short wait after the add-on asks the Exit Target to close gracefully and before it may force close the same game process. It is separate from the Exit Countdown and does not create another user-facing panel after the countdown closes.
_Avoid_: countdown, delay, timeout

**HUD Overlay**:
A non-interactive status display reserved for actionable failure messages after cancellation has closed. Successful execution and the Exit Grace Period remain silent; the Exit Countdown does not create a separate HUD Overlay.
_Avoid_: world overlay, clickable HUD, floating world panel

**Countdown Sound**:
An optional, low-distraction audio cue that supports the Exit Countdown. It is not required for a Game Exit to proceed.
_Avoid_: alarm, alert, warning sound

**User Settings**:
The small set of choices exposed to the user for adapting the add-on without exposing individual recognition thresholds. MVP User Settings cover Configured Hand, Gesture Sensitivity, Exit Countdown duration, Exit Control timeout, Countdown Sound, and Language.
_Avoid_: advanced settings, debug settings, tuning panel

**Dashboard Panel**:
The SteamVR Dashboard surface for User Settings and lightweight status diagnostics. It is not part of the in-game exit flow.
_Avoid_: settings page, debug console, overlay menu

**Diagnostic Log**:
A local record of low-sensitivity add-on events used to diagnose setup, recognition, and Game Exit outcomes. It does not record continuous tracking data, game content, chat, audio, or broad process dumps.
_Avoid_: telemetry, motion recording, debug dump

**Windows Runtime**:
The supported runtime environment for the add-on. macOS may be used for development tasks, but user-facing SteamVR execution and Game Exit behavior are defined for Windows.
_Avoid_: cross-platform runtime, macOS support, platform-neutral plugin

**SteamVR Add-on Package**:
The distributable form recognized and managed by SteamVR so the add-on can load with SteamVR and provide both the Dashboard Panel and in-game overlays. MVP installation may be manual or script-assisted rather than a full installer.
_Avoid_: standalone app, normal executable, installer

**Add-on Session**:
The add-on's running lifetime within SteamVR. It is independent of any single Current VR Game and continues after a Game Exit so the add-on can serve later games. SteamVR backgrounding, standby, HMD removal, and temporary or indefinite controller disconnection disable unavailable interactions but do not end the Add-on Session; the session ends when SteamVR shuts down or the add-on is explicitly disabled or stopped.
_Avoid_: game session, app lifetime, exit session

**Language**:
The user-facing language used by the add-on. Auto follows the SteamVR or Steam runtime language when available, then the operating system UI language, and finally English.
_Avoid_: game language, locale setting, translation mode

**Safe No-op**:
The deliberate choice to do nothing when a Current VR Game, Exit Target, tracked Configured Hand, or Opposite Hand Pointer cannot be proven safe and available. It is the correct product result, not a degraded guess.
_Avoid_: best effort, fallback target, foreground guess

## Reserved State Names

`No Active Flow`, `Idle Visible`, `Confirming`, `Counting Down`, and `Exiting` are product state names and should be used consistently in code, tests, logs, and documentation.

## Scope Boundary

The runtime supports Windows x64 with SteamVR. macOS is an editing and static-validation environment only. The add-on exits only a confirmed Exit Target main process; it must never broaden the target to SteamVR, Steam, SteamVR Home, Dashboard overlays, runtime processes, unrelated utilities, children, or unknown processes.
