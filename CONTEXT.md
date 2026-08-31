# SteamVR Bail Out

This context defines the user-facing domain language for a SteamVR add-on that lets a player intentionally leave the current VR game through an in-world farewell gesture.

## Language

**Current VR Game**:
The SteamVR foreground VR application that is occupying the VR scene and is meaningful for the player to leave. SteamVR itself, SteamVR Home, Dashboard overlays, room setup, no-application states, non-game VR utilities, and this add-on are not Current VR Games.
_Avoid_: current app, foreground window, launched game

**Game Exit**:
The intentional application-level leaving of the Current VR Game. It ends only the game application itself, not SteamVR, SteamVR Home, SteamVR runtime processes, or an in-game state such as leaving a world, returning to a lobby, or disconnecting from a session. The user is informed that an unresponsive game may be force closed.
_Avoid_: leave world, leave instance, quit room, exit SteamVR

**Exit Target**:
The reliable main-process target selected from the Current VR Game at the moment the user confirms a Game Exit. The Exit Target remains fixed through the Exit Countdown; if it exits or no longer matches the confirmed game, the Game Exit flow ends without affecting a different application. User-facing confirmation and countdown text should identify the Exit Target when a reliable display name is available.
_Avoid_: current process, latest app, foreground app

**Farewell Gesture**:
The intentional gesture used only to request the Exit Control. It is made with the Configured Hand inside the Activation Zone, with the palm broadly facing away from the headset or user, while the hand performs a short side-to-side wave motion with at least two direction reversals.
_Avoid_: wave, bye-bye, gesture trigger

**Activation Zone**:
The body-relative area where a Farewell Gesture can be recognized. It is roughly in front of or to the side-front of the user and high enough to resemble an intentional farewell, rather than a low or behind-the-body hand movement.
_Avoid_: world position, height threshold, tracking area

**Configured Hand**:
The hand selected by the user for detecting the Farewell Gesture and carrying the Exit Control.
_Avoid_: dominant hand, active hand, right hand

**Opposite Hand Pointer**:
The pointing input from the hand that is not the Configured Hand. It is the only intended way to hover and activate the Exit Control.
_Avoid_: gaze pointer, same-hand input, controller shortcut

**Supported Controller Setup**:
A SteamVR controller setup that provides stable poses for both hands and an Opposite Hand Pointer. MVP support is defined by these capabilities rather than by a specific controller brand or hand-tracking device.
_Avoid_: hand tracking, single-controller mode, controller brand support

**Exit Control**:
The compact, low-distraction in-world control that follows the Configured Hand near the Controller Wrist Zone after a Farewell Gesture is detected while a Current VR Game and Opposite Hand Pointer are available. It can be idle, ask the user to confirm or dismiss a Game Exit, or offer cancellation while a Game Exit is counting down.
_Avoid_: exit button, quit button, wrist button

**Controller Wrist Zone**:
The approximate wrist or watch-like area inferred from the Configured Hand controller pose. It is a practical placement target for the Exit Control, not a requirement for precise anatomical wrist tracking.
_Avoid_: real wrist, hand bone, watch position

**Exit Countdown**:
The short, visible delay between confirming a Game Exit and carrying it out. It communicates remaining time without being directly interactive; cancellation remains part of the Exit Control, and new Farewell Gestures or temporary hand-tracking loss do not change an active Exit Countdown.
_Avoid_: delay, timer, grace period

**Exit Grace Period**:
The short wait after the add-on asks the Exit Target to close gracefully and before it may force close the same game process. It is separate from the Exit Countdown and is communicated as an exiting state rather than a cancellable countdown.
_Avoid_: countdown, delay, timeout

**HUD Overlay**:
A non-interactive status display that stays in the user's view rather than in the game world's space. The Exit Countdown uses a HUD Overlay.
_Avoid_: world overlay, clickable HUD, floating world panel

**Countdown Sound**:
An optional, low-distraction audio cue that supports the Exit Countdown. It is not required for a Game Exit to proceed.
_Avoid_: alarm, alert, warning sound

**User Settings**:
The small set of choices exposed to the user for adapting the add-on without tuning recognition internals. MVP User Settings cover Configured Hand, Exit Countdown duration, Exit Control timeout, Countdown Sound, and Language.
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
The add-on's running lifetime within SteamVR. It is independent of any single Current VR Game and continues after a Game Exit so the add-on can serve later games.
_Avoid_: game session, app lifetime, exit session

**Language**:
The user-facing language used by the add-on. Auto follows the SteamVR or Steam runtime language when available, then the operating system UI language, and finally English.
_Avoid_: game language, locale setting, translation mode
