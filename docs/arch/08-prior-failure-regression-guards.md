# Prior Failure Regression Guards

This file records concrete failure modes seen in a previous coding attempt. Later agents should use it as a review checklist before claiming the MVP is implemented.

## Pattern Behind The Failure

The failed implementation looked organized but was not vertically complete:

- Many files and abstractions existed.
- Unit tests focused on internals.
- Runtime code still exited immediately.
- OpenVR, Windows, UI, and Game Exit wiring were incomplete or weak.
- The code shape made a small SteamVR plugin look like a large framework before it could run the core flow.

That shape is unacceptable for this MVP. A small, direct implementation with real SteamVR/OpenVR and Windows paths is better than a large abstract implementation whose critical path is disconnected.

The first acceptable coding pass should be a thin playable MVP, not a shell. If the implementation creates dozens of local production files before Dashboard, Exit Control, HUD, and Game Exit execution are wired, reject it and collapse the design.

## P0 Regression Guards

### Runtime Must Not Be A Stub

Reject the implementation if the runtime:

- Initializes OpenVR, logs once, and exits.
- Prints or logs that wiring comes later.
- Has no persistent SteamVR event loop.
- Has no Dashboard Panel overlay creation.
- Has no in-game Exit Control overlay creation.
- Has no HUD Overlay creation.
- Does not sample tracked poses.
- Does not process OpenVR overlay pointer events.
- Does not update Current VR Game and Exit Target availability.
- Does not feed gesture, pointer, tracking, and target events into the state machine.

Minimum acceptable runtime:

- Starts OpenVR.
- Creates Dashboard, Exit Control, and HUD overlays.
- Runs until shutdown.
- Polls OpenVR events and poses.
- Updates overlay transforms/render state.
- Dispatches state-machine side effects.
- Keeps the Add-on Session alive after Game Exit.

### Exiting Must Execute Game Exit

Reject the implementation if `Counting Down -> Exiting` only changes enum state.

Countdown zero must trigger the production Game Exit path:

1. Hide cancellation.
2. Show HUD exiting state.
3. Revalidate locked Exit Target identity.
4. Request graceful close.
5. Wait fixed 5-second Exit Grace Period.
6. Revalidate same identity.
7. Force close only the same main process if still alive.
8. Report success/failure/target gone.

There must be explicit code wiring state transition or side-effect command to the Windows Game Exit executor.

### Confirmation Must Lock The Displayed Target

Reject the implementation if `ConfirmClicked` can accept and lock an arbitrary reliable target supplied by the caller.

Confirmation must:

- Compare the target being locked with the candidate shown in Confirming.
- Revalidate that the candidate is still the same Exit Target.
- Reject confirmation if the displayed candidate changed, disappeared, or no longer matches.
- Lock only that confirmed identity and display name.

The user must never see VRChat in confirmation while the code locks another process.

### Process Identity Must Resist PID Reuse

Reject the implementation if target validation checks only whether a PID is alive.

`ExitTargetIdentity` revalidation must use all stable identity fields available:

- PID.
- Process creation time or equivalent start-time guard.
- Executable path when available.
- SteamVR application key or mapped app identity when available.

Before graceful close and before force close, revalidate that the process is still the confirmed Exit Target. If identity cannot be revalidated, fail closed and do not close anything.

## P1 Regression Guards

### Failure HUD Must Be Visible

Reject the implementation if failure handling stores an error message while the render model hides HUD for `No Active Flow`.

On exit failure:

- Show a short HUD failure state.
- Keep it visible for a short duration.
- Update Last Exit to failed.
- Log the error code.
- Only then clear to normal hidden state.

The state model may use a dedicated `FailureNotice` state or an explicit HUD notice timer. Do not rely on `No Active Flow` to render failure UI unless its render model explicitly supports temporary HUD notices.

### Current VR Game Classification Must Not Be Substring Guessing

Reject the implementation if game classification mainly uses broad name substrings such as `tool`, `utility`, `overlay`, or `steamvr`.

Current VR Game handling must be conservative and SteamVR-aware:

- Prefer SteamVR application identity, application type, manifest metadata, process mapping, and known runtime exclusions.
- Maintain explicit deny rules for SteamVR, SteamVR Home, Dashboard overlays, room setup, runtime processes, this add-on, and known non-game utilities.
- Treat unknown or ambiguous candidates as unavailable rather than accepted.
- Provide Dashboard diagnostics for unavailable/filtered reasons.

Substring checks may be used only as narrow, explicit deny-list helpers after better SteamVR metadata is considered. They must not be the primary accept/reject mechanism.

## P2 Regression Guards

### Language Auto Must Follow PRD

Reject the implementation if `LanguageMode::Auto` simply falls back to English.

Auto must resolve in this order:

1. SteamVR or Steam runtime language.
2. OS UI language.
3. English.

Manual English and Chinese override Auto. Diagnostic Log remains English.

## Additional Risks To Avoid

### Green Tests Are Not Proof Of MVP

Core tests passing does not mean the add-on works. The MVP is not complete unless the user-visible production path is wired:

```text
OpenVR runtime loop
  -> Current VR Game + Exit Target availability
  -> Farewell Gesture sampling
  -> Exit Control overlay with hover/confirm/dismiss
  -> Opposite Hand Pointer confirmation through real UI
  -> HUD Countdown with visible seconds/progress and optional sound
  -> Windows Game Exit execution
  -> Last Exit + Diagnostic Log
```

### Logic-Only Is Not MVP

Reject the implementation if it focuses on core logic while skipping the user-facing interaction. Required first-pass user experience:

- Dashboard Panel with MVP settings/help/diagnostics.
- Exit Control overlay near wrist.
- Hover feedback.
- Confirm and Dismiss UI.
- Cancel Exit during countdown.
- HUD countdown with seconds and smooth progress.
- Exiting state.
- Short failure HUD state.
- Countdown Sound setting and production sound path.

These are not optional polish. They are the feature the user will test in VR.

### Avoid Disconnected Side Effects

Side-effect commands are only useful if something executes them. For every state-machine command, verify the runtime adapter or service actually observes and performs it.

Examples:

- `RequestGracefulClose` must reach Windows process control.
- `ShowHud` must reach OpenVR HUD rendering.
- `ShowExitControl` must create/show the overlay, not just update an in-memory model.
- `SetLastExit` must appear in Dashboard diagnostics.

### Keep Review Local

Do not spread the critical path across dozens of files. A reviewer should be able to trace:

1. Gesture recognized.
2. Exit Control shown.
3. Confirm clicked.
4. Countdown finishes.
5. Windows process close happens.

If that path requires reading a large framework, the implementation is already drifting.

For the first pass, fewer than 10 production source/header files is the expected shape. More files are allowed only when they are backed by real code and a concrete OpenVR or Windows boundary.

## Review Checklist Before Claiming MVP Coding Is Done

- [ ] Runtime has a persistent OpenVR event loop.
- [ ] Dashboard Panel overlay is created and rendered.
- [ ] Exit Control overlay is created, shown, follows wrist, and handles Opposite Hand Pointer hover/click.
- [ ] HUD Overlay is created, non-interactive, and shows countdown/exiting/failure.
- [ ] Countdown is visibly rendered with seconds and progress, not only logged or timed internally.
- [ ] Countdown Sound setting and production sound path exist.
- [ ] Farewell Gesture is sampled from real OpenVR poses.
- [ ] Current VR Game detection is SteamVR-aware and conservative.
- [ ] Exit Target mapping is implemented and diagnostics explain unavailable states.
- [ ] Confirm locks only the displayed and revalidated Exit Target.
- [ ] Countdown elapsed triggers real Game Exit execution.
- [ ] Graceful close, Exit Grace Period, and force close are wired.
- [ ] PID reuse is guarded by process identity revalidation.
- [ ] Failure HUD is visible before clearing flow.
- [ ] Language Auto follows SteamVR/Steam, OS UI, English fallback.
- [ ] Core tests cover state and pure logic without bloating the implementation.
- [ ] Windows build and manual SteamVR validation remain the integration acceptance path.
- [ ] First pass is a thin playable MVP, not a shell or partial phase.
- [ ] Production source/header count stays under the first-pass budget, or every extra file has a concrete real-boundary reason.
- [ ] UI/audio/countdown feedback are not deferred as polish.
