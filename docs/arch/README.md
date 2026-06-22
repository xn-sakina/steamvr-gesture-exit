# MVP Architecture Index

This directory turns `docs/prd/mvp.md` into implementation-facing architecture and task scope. The PRD, `CONTEXT.md`, and ADRs remain the source of truth for product behavior; these files organize that behavior into coding surfaces so implementation work can be planned without dropping features.

## Canonical Inputs

- `CONTEXT.md`: vocabulary and product terms.
- `docs/prd/mvp.md`: MVP product scope and decisions.
- `docs/adr/0001-target-windows-runtime.md`: Windows-only runtime.
- `docs/adr/0002-prioritize-safe-exit-boundaries.md`: safe Exit Target boundaries.
- `docs/test-plan/mvp.md`: manual runtime validation checklist.
- `openvr/`: local OpenVR source, headers, samples, and docs to use directly when choosing SteamVR/OpenVR APIs.

## Documents

- `01-mvp-feature-scope.md`: full MVP feature inventory, safety invariants, and out-of-scope guardrails.
- `02-runtime-architecture.md`: C++20 package architecture, module boundaries, and runtime data flow.
- `03-exit-flow-state-machine.md`: core flow states, events, locked data, and transition side effects.
- `04-openvr-overlays-and-input.md`: SteamVR/OpenVR main loop, Dashboard Panel, Exit Control, HUD Overlay, pointer input, wrist following, and visual/audio feedback.
- `05-windows-settings-diagnostics.md`: Windows process control, Current VR Game to Exit Target mapping, settings persistence, language, and Diagnostic Log.
- `06-coding-task-map.md`: implementation sequence, test seams, and coverage checklist.
- `07-agent-implementation-guardrails.md`: coding-agent guardrails for keeping the implementation direct, compact, integrated, and testable.
- `08-prior-failure-regression-guards.md`: concrete regression guards from a previous failed implementation attempt.

## MVP Architectural Principles

1. Fail closed. If Current VR Game, Exit Target, Configured Hand tracking, or Opposite Hand Pointer availability is not reliable enough, do not show Exit Control.
2. Gesture is not consent. Farewell Gesture only requests Exit Control; Game Exit requires explicit opposite-hand confirmation.
3. Lock target at confirmation. Countdown completion must only act on the confirmed Exit Target main process.
4. Preserve SteamVR. Never close SteamVR, Steam, SteamVR Home, Dashboard overlays, runtime processes, unrelated tools, or unknown processes.
5. Keep Dashboard Panel separate. Settings, help, and diagnostics live in Dashboard; in-game overlays only support the exit flow.
6. Keep privacy boundaries. Diagnostic Log records low-sensitivity events and short errors, never continuous pose samples, game content, chat, audio, or broad process dumps.
7. Implement against real OpenVR. Coding work must use the local `openvr/` source, headers, samples, and documentation directly; do not invent mock SteamVR behavior when the API can be verified from this repository.
8. No empty scaffolding phase. Do not deliver a SteamVR shell and defer compatibility, overlay behavior, input handling, or process-control behavior to later. Each coding slice should wire the production path end to end for its feature.
9. Do not defer Windows integration. Windows process control, per-user settings, language fallback, package layout, and runtime behavior are part of the coding work, even when final validation happens later on a Windows SteamVR machine.
10. Keep the C++ shape small. Prefer a few cohesive modules over many tiny framework-style files. Add abstraction only when it directly protects a real boundary: OpenVR, Windows process/settings APIs, rendering, or the core state machine.
11. Prefer vertical completion. A coding task should end with a user-testable behavior or a complete production path, not just an abstract layer that assumes another agent will connect it later.
12. Guard against known failed shapes. Before accepting coding work, check `08-prior-failure-regression-guards.md`; a build that still has no real runtime loop, no exit execution wiring, weak process identity, substring app classification, hidden failure HUD, or fake Auto language is not an MVP implementation.
13. First coding pass means playable thin MVP. Do not stop after a package shell, library split, or partial phase. The first serious coding agent should aim to produce a Windows-buildable add-on path that can be installed, opened in SteamVR, show overlays, confirm, count down, and attempt Game Exit.
14. Keep file count intentionally low. Default target is fewer than 10 production source/header files for the first MVP pass, excluding vendored `openvr/`, build files, assets, and tests. Exceeding that requires a concrete reason tied to a real OpenVR or Windows boundary.
15. User-visible flow is not polish. Dashboard Panel, wrist Exit Control, hover feedback, Confirm/Dismiss UI, HUD countdown seconds/progress, Cancel Exit, exiting/failure HUD states, and Countdown Sound are first-pass MVP behavior. Core logic without these surfaces is incomplete.
