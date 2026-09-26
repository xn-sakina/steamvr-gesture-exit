# SteamVR Gesture Exit MVP PRD

Issue label: `ready-for-agent`

## Problem Statement

SteamVR players sometimes want to leave the Current VR Game without breaking presence immediately. In social VR games such as VRChat, a player may want a few seconds to perform a deliberate farewell or exit pose before the application closes. Existing exit paths require menus, desktop interaction, SteamVR Dashboard navigation, or game-specific UI, all of which interrupt the moment and can be awkward inside VR.

The feature must make Game Exit easy and theatrical while staying conservative. The add-on must not treat SteamVR, SteamVR Home, Dashboard overlays, non-game VR tools, or unknown processes as exit targets. If the safe Exit Target cannot be identified, the add-on should be unavailable rather than risk closing the wrong process.

## Solution

Build a Windows Runtime SteamVR Add-on Package that loads with SteamVR and provides a Dashboard Panel plus in-game overlays. While a Current VR Game is running, the player performs a Farewell Gesture with the Configured Hand. If the add-on can also identify a reliable Exit Target and the Opposite Hand Pointer is available, a compact Exit Control appears in the Opposite-side View Zone and locks to that world-space position.

The Farewell Gesture only requests the Exit Control. The user must explicitly interact with the Exit Control using the Opposite Hand Pointer, then confirm the Game Exit. After confirmation, the same Exit Control changes in place to show the Exit Countdown and Cancel action. When the countdown reaches zero, the control closes without another success or exiting panel, and the add-on requests a graceful close of the fixed Exit Target main process. If the target remains alive after the fixed Exit Grace Period, the add-on force closes only that same main process.

The Dashboard Panel provides User Settings, first-use help, and lightweight diagnostics. Diagnostic Log output supports development and Windows-side testing without recording continuous motion data, game content, chat, audio, or broad process dumps.

## User Stories

1. As a VR player, I want to wave goodbye to reveal an exit control, so that leaving a VR game feels intentional instead of abrupt.
2. As a VR player, I want a short countdown before the game exits, so that I can perform a farewell or exit pose.
3. As a VR player, I want the default countdown and Exit Control display time to be 3 seconds, so that the exit flow stays quick by default.
4. As a VR player, I want optional countdown durations of 1, 3, 5, 10, and 15 seconds, so that I can choose a pace that fits how I play.
5. As a VR player, I want the Exit Control to initially appear on the side of the hand that will click it, so that standing, seated, armrest, and lap postures all work.
6. As a VR player, I want the Exit Control to stay fixed after it appears and angle toward my eyes, so that natural head movement does not make the click target wobble.
7. As a VR player, I want the exit control to be compact when idle, so that accidental activation does not block my gameplay.
8. As a VR player, I want visible pointer/button feedback and subtle hover/click sounds, so that I know where I am pointing and whether activation succeeded.
9. As a VR player, I want a second confirmation before exit begins, so that a gesture alone cannot close my game.
10. As a VR player, I want Confirm and Dismiss to be separated large targets, so that I do not accidentally confirm while trying to dismiss.
11. As a VR player, I want the confirmation copy reduced to the single question “Exit game?”, so that I can decide without reading a dense panel.
12. As a VR player, I want the Exit Control to retain subtle product branding without competing with the decision, so that it feels intentional but unobtrusive.
13. As a VR player, I want to cancel during the Exit Countdown from the Exit Control, so that I can recover from a mistaken confirmation.
14. As a VR player, I want the existing Exit Control to change into the countdown view, so that confirmation does not spawn another panel elsewhere.
15. As a VR player, I want Cancel Exit to remain on that countdown view, so that the same pointer interaction can stop the exit.
16. As a VR player, I want the countdown to show both seconds and smooth progress, so that I can time my farewell action accurately.
17. As a VR player, I want a subtle countdown sound, so that I can pace my exit without needing to stare at the control.
18. As a VR player, I want to disable countdown sound, so that the countdown stays quiet when I prefer visual timing feedback only.
19. As a VR player, I want the exit control to disappear automatically while unattended, pause that timeout while I point at it, and restart the full timeout after I point away, so that it never vanishes during an intended interaction.
20. As a VR player, I want to dismiss the exit control manually, so that I can clear it without waiting for timeout.
21. As a VR player, I want repeated goodbye gestures to refresh only the idle control timeout, so that natural movement does not restart or alter a confirmed exit.
22. As a VR player, I want new gestures to be ignored during countdown, so that the countdown remains predictable.
23. As a VR player, I want hand-tracking loss before confirmation to close the control, so that an incomplete interaction does not linger.
24. As a VR player, I want temporary hand-tracking loss during countdown not to cancel exit, so that a big farewell movement does not break the flow.
25. As a VR player, I want the add-on to stay running after the game exits, so that it works again when I launch another VR game.
26. As a VR player, I want SteamVR to remain running after Game Exit, so that I naturally return to SteamVR state.
27. As a VR player, I want the add-on to avoid showing controls in SteamVR Home, so that SteamVR itself is not treated like a game.
28. As a VR player, I want no exit control when no game is running, so that normal SteamVR use is not affected.
29. As a VR player, I want no exit control for Dashboard overlays, so that configuration or SteamVR menus are not exit targets.
30. As a VR player, I want no exit control when the Exit Target is unavailable, so that the add-on does not guess at what to close.
31. As a VR player, I want no exit control when the opposite hand pointer is unavailable, so that I cannot enter a flow I cannot confirm or cancel.
32. As a VR player, I want the add-on to close only the confirmed game main process, so that SteamVR and unrelated tools remain safe.
33. As a VR player, I want a short error message if exit fails, so that I know to close the game manually.
34. As a VR player, I want failures before interaction to stay silent in-game, so that normal social gestures do not produce warning popups.
35. As a VR player, I want a Dashboard Panel for settings, so that I can adjust the small number of meaningful choices.
36. As a VR player, I want settings to save immediately, so that I do not need to find an extra save button in VR.
37. As a left-handed player, I want to choose the Configured Hand, so that the gesture and opposite-side Exit Control match my preference.
38. As a multilingual player, I want Auto language selection, so that UI follows SteamVR or system language when possible.
39. As a multilingual player, I want manual English and Chinese choices, so that I can override Auto.
40. As a first-time user, I want brief Dashboard help, so that I understand the gesture, opposite-hand confirmation, and force-close risk.
41. As a tester, I want Dashboard diagnostics for Current VR Game and Exit Target availability, so that I can tell why the control does or does not appear.
42. As a tester, I want diagnostics for Configured Hand tracking and Opposite Hand Pointer availability, so that I can debug controller prerequisites.
43. As a tester, I want Last Exit status, so that I can verify success, cancel, and failure outcomes.
44. As a developer, I want a Diagnostic Log with low-sensitivity events, so that Windows-side failures are diagnosable.
45. As a developer, I want logs to avoid continuous tracking data and game content, so that diagnostics do not become a privacy risk.
46. As a developer, I want settings corruption to fall back to defaults, so that a damaged settings file does not break SteamVR startup.
47. As a developer, I want initialization failures to be reported through Dashboard diagnostics or logs, so that system popups do not interrupt VR.
48. As a developer, I want the add-on to fail toward self-disablement, so that SteamVR and the Current VR Game remain stable.
49. As a Windows tester, I want VRChat to be the reference game, so that the main social VR flow is validated against a real target.
50. As a Windows tester, I want at least two additional SteamVR titles smoke-tested, so that the add-on does not accidentally depend only on VRChat behavior.
51. As a future maintainer, I want safe exit boundaries recorded as product behavior, so that later changes do not expand process scope casually.
52. As a future maintainer, I want Windows-only runtime scope recorded, so that macOS development support is not mistaken for macOS runtime support.

## Implementation Decisions

- Build a SteamVR Add-on Package rather than a standalone executable. The package must be recognizable and manageable by SteamVR, with manual or script-assisted installation acceptable for MVP.
- Target Windows as the only runtime. macOS remains useful for editing, asset work, static checks, and non-runtime tests. This follows the accepted Windows Runtime ADR.
- Treat VRChat as the reference game for MVP behavior, while keeping the SteamVR concepts generic enough for additional SteamVR titles.
- Define Current VR Game as the SteamVR foreground VR application meaningful for the player to leave. SteamVR itself, SteamVR Home, Dashboard overlays, room setup, no-application states, non-game VR utilities, and the add-on are not Current VR Games.
- Bind Exit Target at the moment of confirmation. Countdown completion must not re-query a different foreground app and exit that instead.
- If a reliable Exit Target main process cannot be mapped from the Current VR Game, do not show Exit Control.
- Limit Game Exit to the reliable Exit Target main process. Never close SteamVR, Steam, SteamVR Home, runtime processes, unrelated tools, child processes that are not clearly in scope, or unknown processes. This follows the accepted safe-boundaries ADR.
- Use a graceful close request against the Exit Target main process first. Do not use keyboard simulation, Alt+F4, or focus-dependent input.
- Use a fixed 5-second Exit Grace Period after graceful close before force closing the same target main process.
- Keep Exit Grace Period internal and separate from the user-visible Exit Countdown.
- Keep the Add-on Session running after Game Exit and clear only Exit Target related state.
- Model Farewell Gesture as a request for Exit Control only. It must never directly confirm, cancel, or execute Game Exit.
- Detect Farewell Gesture from the Configured Hand controller's tracked position in the Activation Zone. Require three committed direction reversals and aggregate evidence from repeated lateral travel, path efficiency, vertical/depth suppression, endpoint stability, stroke extent and rhythm, and plausible speed. Evaluate controller motion relative to the HMD so ordinary body movement is not mistaken for a gesture. Accept compact, quick waves without inferring palm direction from controller axes.
- Do not gate Farewell Gesture on a chest-height assumption. Armrest and lap-height starts are valid when they remain within arm reach and not substantially behind the HMD.
- Use a 1.8-second rolling recognition window. Allow 75–650 ms per full stroke and at least 300 ms across the final three full strokes, with a 7.5 cm full-stroke floor and at least 23.5 cm combined lateral travel. Smooth short tracking shimmer, reject abrupt tracking jumps, and suppress recognition while a controller button or legacy OpenVR axis is active. Keep gesture thresholds internal for MVP.
- Require a Supported Controller Setup with both hand poses and Opposite Hand Pointer. Do not support naked hand tracking, data gloves, or single-controller exit flows in MVP.
- Place Exit Control in the Opposite-side View Zone at the moment of recognition: right after a left-hand Farewell Gesture and left after a right-hand Farewell Gesture.
- Capture one absolute standing-space transform when Exit Control first appears. Aim it modestly inward/upward toward the current eye position, then keep that single overlay fixed through Idle Visible, Confirming, and Counting Down; content changes and repeated gestures must not reposition or duplicate it.
- Make the Opposite Hand Pointer the only hover and activation input for Exit Control. Render its point and button hover feedback on the control itself, and play subtle sounds when the pointer enters a button and when a button activates. Do not implement gaze click, same-hand input, keyboard shortcuts, or controller shortcut cancellation.
- Keep the Exit Control overlay non-interactive to SteamVR and perform ray hit-testing and trigger-edge handling locally. Merely pointing at or clicking the control must not capture scene input focus or change the Current VR Game's AFK/activity state.
- Use a three-state control flow before execution: Idle Visible, Confirming, Counting Down, then Exiting.
- In Idle Visible, repeated Farewell Gestures refresh only the timeout. In Confirming and Counting Down, new Farewell Gestures are ignored. Idle Visible and Confirming both auto-dismiss when unattended; pointer hover anywhere on the control pauses the timeout and refills its top progress bar, and pointer exit restarts the full timeout.
- In Idle Visible and Confirming, loss of Configured Hand tracking or Opposite Hand Pointer availability closes the control and returns to no active flow.
- In Counting Down, temporary hand-tracking loss does not cancel the active countdown. If tracking returns, Cancel Exit remains available from the same world-anchored Exit Control.
- In Counting Down, disconnection of the HMD or all controllers cancels the flow before a game-close request. Temporary pose loss while devices remain connected does not cancel it.
- During Counting Down, replace the question with a large integer-second value, a circular progress ring that transitions from blue toward red, and Cancel in its fixed button position. Display one second for every positive fraction below one second. Do not create a second countdown panel or HUD.
- At countdown zero, cancellation ends and the Exit Control closes. Request graceful close immediately and keep the Exit Grace Period silent; do not show an exiting or success HUD.
- If the target exits or no longer matches the confirmed Exit Target before execution, clear the flow without affecting another application.
- If force close fails, show a short HUD error, update Last Exit to failed, record the Diagnostic Log entry, and do not retry indefinitely or expand process scope.
- Keep Dashboard Panel as the surface for User Settings, first-use help, and lightweight diagnostics. It is not part of the in-game exit flow.
- Expose only Configured Hand, Exit Countdown duration, Exit Control timeout, Countdown Sound, and Language as MVP User Settings.
- Save User Settings immediately per Windows user. Do not require Apply or Save.
- If settings are missing, corrupted, or version-incompatible, recover to defaults, log the recovery, and show a short Dashboard status.
- Language Auto should prefer SteamVR or Steam runtime language, then OS UI language, then English. Manual English or Chinese overrides Auto.
- Keep Diagnostic Log entries in English structured form and do not localize them.
- Do not add haptics, theme customization, advanced gesture sensitivity, pose mode settings, Steam account sync, installer work, auto-update, or signed release to MVP.

Prototype-derived state model:

```text
No Active Flow
  Farewell Gesture with prerequisites met -> Idle Visible

Idle Visible
  timeout -> No Active Flow
  pointer enters control -> pause timeout and refill progress
  pointer leaves control -> restart full timeout
  dismiss -> No Active Flow
  tracking/pointer lost -> No Active Flow
  repeated Farewell Gesture -> refresh timeout
  click with Opposite Hand Pointer -> Confirming

Confirming
  timeout -> No Active Flow
  pointer enters control -> pause timeout and refill progress
  pointer leaves control -> restart full timeout
  dismiss -> No Active Flow
  tracking/pointer lost -> No Active Flow
  Confirm -> Counting Down with locked Exit Target and duration

Counting Down
  Cancel Exit -> No Active Flow
  target exits or target identity invalid -> No Active Flow
  countdown reaches zero -> Exiting

Exiting
  graceful close succeeds -> No Active Flow with success
  grace period expires and force close succeeds -> No Active Flow with success
  force close fails -> No Active Flow with failure
```

## Testing Decisions

- Test behavior at the highest seams available. The primary acceptance seam is a full Windows SteamVR runtime flow using VRChat: gesture recognition, overlay display, confirmation, countdown, graceful close, fallback force close, SteamVR survival, and add-on persistence.
- Use compatibility smoke tests with at least two additional SteamVR titles to verify that Current VR Game and Exit Target handling are not VRChat-only.
- Use negative scenario tests as first-class acceptance criteria. A safe no-op is often the correct behavior.
- Test user-visible behavior rather than implementation details. Good tests assert whether controls appear, whether exit is allowed, whether only the Exit Target closes, whether SteamVR remains stable, and whether diagnostics explain unavailable states.
- Test the gesture recognition seam through external VR behavior where possible: valid Farewell Gesture should show Exit Control; low/behind-body movement, no game, missing pointer, and unavailable Exit Target should not.
- Test the state machine seam independently if an internal state reducer/controller is introduced. These tests should cover transitions, locked target and duration, repeated gestures, tracking loss, cancel, target exit during countdown, and failure handling.
- Test the process-control seam behind an abstraction. Use controlled test processes to verify graceful close, fixed Exit Grace Period, force close, identity revalidation, and force-close failure without needing a real game for every case.
- Test the Dashboard Panel seam for settings persistence, damaged settings recovery, diagnostics, first-use help, and language switching.
- Test the overlay input seam for input priority: visible Exit Control must not steal normal game input unless explicitly hovered and clicked with Opposite Hand Pointer.
- Test Diagnostic Log behavior for useful event coverage and privacy boundaries. Logs should include add-on lifecycle, detection changes, gesture success events, state transitions, settings changes, exit results, and short error codes, but not continuous pose samples, game content, chat, audio, or broad process dumps.
- There is no existing codebase test prior art yet. The repo currently contains planning documents and package metadata, so new tests should be introduced at the highest stable seams as implementation modules appear.
- The existing `docs/test-plan/mvp.md` should remain the manual Windows VR validation checklist.

## Out of Scope

- Exiting a VRChat world, room, instance, lobby, or any game-internal state without closing the application.
- Closing SteamVR, Steam, SteamVR Home, Dashboard overlays, room setup, runtime processes, or unrelated tools.
- Guessing an Exit Target from window title, broad process lists, or focus state when a reliable target cannot be established.
- macOS runtime support.
- Full installer, MSI/NSIS packaging, code signing, auto-update, or store distribution.
- Per-game integrations, special VRChat APIs, or game-specific world/session handling.
- Naked hand tracking, data gloves, single-controller operation, or game-specific hand skeleton data.
- Gaze input, same-hand input, keyboard shortcuts, Alt+F4, or simulated keyboard/mouse input.
- Haptics, theme customization, color customization, gesture sensitivity UI, pose mode settings, controller-brand-specific UI, or advanced tuning panels.
- Steam account synchronization for settings.
- Localizing Diagnostic Log output.
- Making overlays visible to other players, game cameras, or recording tools as a product promise.

## Further Notes

- The original rough plan remains in `prompts/0-prototype.prompt.md` and should be treated as historical input, not the canonical current PRD.
- The project glossary in `CONTEXT.md` is the canonical vocabulary for this PRD.
- ADR 0001 records the Windows-only runtime decision.
- ADR 0002 records that safe exit boundaries take priority over guaranteeing every exit attempt succeeds.
- The project issue tracker is not currently discoverable from the local workspace or connected GitHub repositories. This PRD is ready to publish as an issue with label `ready-for-agent` once the target tracker is provided.
