# Agent Implementation Guardrails

This file exists to prevent predictable failure modes during later coding. The MVP is a small SteamVR exit add-on; the code should be direct, production-oriented, and easy for a future agent to understand within limited context.

## Primary Risks

Before coding or reviewing, also read `08-prior-failure-regression-guards.md`. It captures specific P0/P1/P2 failures from a prior attempt and should be treated as a blocking checklist, not historical trivia.

### Risk 1: Over-Abstract C++ Architecture

Failure mode:

- Too many files for simple behavior.
- Thirty or forty files appear before the add-on can run a single full exit flow.
- Framework-style managers, factories, registries, and interfaces before there is a concrete need.
- Thin one-method classes that hide the real flow.
- Important behavior spread across many small files so a future agent cannot reconstruct the feature in context.

Prevention:

- Start with a compact module set that can plausibly stay under 10 production source/header files for the first MVP pass.
- Combine simple responsibilities when that keeps the main flow readable. For example, the OpenVR runtime loop and overlay rendering can begin in one cohesive file if splitting would only create indirection.
- Prefer one `.h` and one `.cpp` per cohesive module.
- Keep related state and behavior together unless there is a real platform boundary.
- Do not split every UI state, event type, or log event into separate files.
- Do not introduce dependency injection frameworks, service locators, plugin systems, message buses, or generic resource managers for MVP.
- Use plain structs/enums/functions where they make the behavior clearer.
- Add a new abstraction only when it removes duplicated production code or isolates a hard external boundary.

Context-size rule:

- A future agent should be able to understand one feature by reading no more than 3 to 5 project files plus the relevant OpenVR reference file.
- If a change requires reading many disconnected local files to understand a simple gesture-to-exit path, the architecture is too fragmented.
- The full MVP control path should be traceable in roughly 5 to 8 local source files. If it needs dozens, the implementation is overbuilt.

### Risk 2: Building a Shell and Deferring Real Integration

Failure mode:

- Build system and empty app exist, but OpenVR overlays, OpenVR input, Windows process control, or UI are still fake.
- Dashboard exists only as a placeholder.
- Exit Control or HUD has no real OpenVR overlay path.
- Current VR Game and Exit Target mapping are described but not implemented.
- Windows code is hidden behind interfaces with no production implementation.

Prevention:

- Every runtime slice must use real OpenVR APIs checked against `openvr/`.
- Every Windows-facing production path must have real Windows implementation when introduced.
- UI slices must create/render actual OpenVR overlays, even if the visual design is simple.
- Process-control slices must implement graceful close, identity revalidation, Exit Grace Period, and force close together.
- If current host limitations prevent live Windows SteamVR execution, still write complete production code and mark only runtime validation as pending.
- Do not treat "build/package foundation" as a shippable milestone. The useful milestone is a thin end-to-end path that the user can copy to Windows and try in SteamVR.

Definition of done for integration work:

- Builds into the add-on package.
- Calls real OpenVR or Windows APIs for the production path.
- Has diagnostics/log entries for unavailable states.
- Fails closed without expanding process scope.
- Leaves clear Windows SteamVR validation steps for the user.

### Risk 3: Overestimating Scope and Choosing to Wait

Failure mode:

- Agent decides SteamVR, Windows, OpenVR, UI, or process control is too broad and postpones it.
- Agent produces more planning documents instead of code.
- Agent implements only internal state and says external integration can come later.

Prevention:

- Treat the MVP as directly buildable from the current repo plus `openvr/`.
- Use `openvr/headers`, `openvr/docs`, and `openvr/samples` to resolve unknown APIs.
- Implement the real production path first, then simplify if compile/runtime feedback requires it.
- Prefer a minimal working visual/UI implementation over a polished but absent one.
- Prefer conservative safe no-op behavior over incomplete integration.
- If exact runtime behavior must be verified on Windows, write the code and expose diagnostics so the user can test and report the bug.

### Risk 4: Over-Testing SteamVR and Windows Integration

Failure mode:

- Agent tries to unit-test every C++ class, OpenVR adapter, overlay renderer, and Windows API edge.
- Test harness becomes larger than the feature.
- Production code is reshaped around mocks only to satisfy coverage goals.
- Coding stalls because SteamVR UI, controller input, or Windows process behavior is difficult to automate on the current host.

Prevention:

- Automated tests should focus on core logic that does not depend on SteamVR:
  - state-machine transitions,
  - locked Exit Target and countdown duration behavior,
  - gesture classification math over synthetic pose samples,
  - settings validation/recovery,
  - language fallback selection when represented as pure inputs,
  - small diagnostic event formatting helpers if useful.
- Do not chase broad line coverage for OpenVR runtime, overlays, rendering, audio, or Windows process APIs.
- Do not add mock-heavy test layers for SteamVR just to make coverage look complete.
- For OpenVR/UI/Windows integration, the expected verification is:
  - compile/build on Windows,
  - package inspection,
  - diagnostics/log visibility,
  - manual Windows SteamVR validation using `docs/test-plan/mvp.md`.
- Add focused adapter tests only when they are cheap and do not distort production code.

Testing definition of done:

- Core state and pure logic have useful automated tests.
- The runtime builds into the real add-on package.
- Integration behavior has clear diagnostics and manual Windows VR validation steps.
- Lack of full automated coverage for SteamVR/OpenVR/UI/Windows process behavior is acceptable for MVP.

### Risk 5: Logic-Only MVP With Missing User Experience

Failure mode:

- Agent implements state transitions and process control but skips Dashboard Panel, Exit Control UI, HUD Overlay, countdown visuals, or countdown sound.
- Agent treats UI as polish and says it can be added later.
- Countdown exists only as an internal timer or log line.
- Confirmation exists only as a core event, not as a real opposite-hand overlay interaction.

Prevention:

- Dashboard Panel, Exit Control, HUD Overlay, Exit Countdown visuals, Cancel Exit, failure HUD, and Countdown Sound are MVP behavior, not polish.
- A minimal visual implementation is acceptable; an absent UI is not.
- The first playable pass must expose the user-facing flow in VR:
  - Dashboard settings/help/diagnostics,
  - wrist Exit Control,
  - hover feedback,
  - Confirm/Dismiss targets,
  - non-interactive right-upper HUD countdown,
  - visible exiting/failure states,
  - optional countdown sound controlled by setting.
- If an audio device or SteamVR runtime behavior cannot be validated on the current host, still implement the production sound path and setting; leave only Windows SteamVR validation pending.

Logic-only work is insufficient even if all core tests pass. The feature is a VR exit interaction; without user-visible overlays and countdown feedback, the core MVP is missing.

## Recommended First Coding Shape

Keep the initial implementation small enough to remain maintainable:

```text
src/
  main.cpp
  runtime.h/.cpp          # OpenVR loop, overlay creation, event dispatch
  flow.h/.cpp             # state machine, view state, small domain structs
  gesture.h/.cpp          # pose-window gesture recognition
  windows_platform.h/.cpp # settings, language, process identity/close
  diagnostics.h/.cpp      # structured log, Last Exit summary
tests/
  state_machine_tests.cpp
  gesture_tests.cpp
  settings_tests.cpp
```

This is a starting shape, not a mandate to create files before they carry real code. Do not add empty files to match this tree. Splitting `runtime` into separate OpenVR and renderer files is fine only when the file is becoming hard to read because real code exists.

First-pass file budget:

- Aim for 5 to 8 production source/header file pairs or fewer.
- Stay below 10 production source/header files by default.
- Tests, build files, assets, generated package files, and vendored `openvr/` do not count.
- If the agent creates more than 10 production source/header files, it must explain why each extra file is needed for a real boundary or readability problem.

## Vertical Slice Rule

Prefer vertical implementation slices. For the first serious coding pass, do not stop until the thin full path exists:

- SteamVR add-on starts and keeps running.
- Dashboard opens with settings/diagnostics.
- Current VR Game and Exit Target diagnostics update.
- Farewell Gesture from real OpenVR poses can request Exit Control.
- Exit Control is a real overlay with pointer hover/click.
- Exit Control has visible hover feedback plus Confirm/Dismiss UI.
- Confirm locks the displayed target.
- HUD counts down with seconds and smooth progress.
- Cancel Exit is visible on the wrist during countdown when tracking/pointer are available.
- Countdown Sound setting exists and drives the production sound path.
- HUD shows exiting and short failure states.
- Windows Game Exit path executes.
- Last Exit and Diagnostic Log update.

After the thin full path exists, improve slices:

- Dashboard settings slice: render Dashboard setting, update model, save to Windows user storage, log change.
- Gesture slice: read OpenVR poses, classify Farewell Gesture, open real Exit Control overlay.
- Confirmation slice: handle real OpenVR pointer events, show confirmation, lock Exit Target.
- Countdown slice: show real HUD, play optional sound, expose Cancel Exit.
- Exit slice: revalidate target, graceful close, wait Exit Grace Period, force close same process if needed, update Last Exit.

Avoid horizontal-only slices:

- "Create all interfaces."
- "Set up all managers."
- "Stub all platform adapters."
- "Add placeholder UI."
- "Implement core only; integrate later."
- "Create a complete architecture file tree before the add-on can run."
- "Split this simple MVP into dozens of files so it looks enterprise-grade."

## File and Abstraction Budget

Hard default limits for the first MVP coding pass:

- Keep production source/header files under 10 unless real compiled code demands more.
- Do not create more files than needed to run the full path.
- Do not split code just to mirror architecture document headings.
- Do not create empty or near-empty `.h/.cpp` pairs.
- Do not add manager/factory/service classes before direct functions or structs become unclear.

Soft limits after the first full path works:

- Keep production source files under roughly 15 unless real compiled code demands more.
- Keep most feature work in existing cohesive modules.
- Avoid adding a new class when a function or struct is enough.
- Avoid templates/generics unless required by standard library use or clear type safety.
- Avoid async frameworks; use simple staged timers/state where possible.
- Keep tests focused on behavior, not private implementation details.
- Do not increase file count or abstraction solely to enable mocks for OpenVR or Windows APIs.

When exceeding these limits, the coding agent should explain why the added shape is necessary for a real OpenVR or Windows boundary.

## Required Agent Behavior Before Coding

Before implementing a feature:

1. Read the matching `docs/arch/*` section.
2. Inspect relevant OpenVR source/docs/samples under `openvr/`.
3. Identify the real OpenVR or Windows APIs to call.
4. Implement the production path, not just the interface.
5. Add the smallest useful core tests or diagnostics.
6. Report what was implemented and what needs Windows SteamVR validation.

## Anti-Patterns to Reject

- "First build the SteamVR skeleton, then wire OpenVR later."
- "Leave Windows process control as a mock until a Windows machine is available."
- "Dashboard placeholder is enough for now."
- "Exit Target mapping can be a TODO."
- "Overlay rendering can be simulated by logs."
- "State machine complete means MVP coding is complete."
- "UI, countdown visuals, and sound are polish; implement them later."
- "The countdown timer can be internal only for now."
- "Confirmation can be represented by a test event instead of real overlay input."
- "Add a framework now to make future features easier."
- "Create thirty or forty C++ files for a small SteamVR exit plugin before it runs end to end."
- "Every OpenVR, UI, and Windows API path needs unit-test coverage before the user can try it."
- "Production code should be refactored around mocks to improve coverage numbers."

For this MVP, direct production behavior beats speculative architecture.
