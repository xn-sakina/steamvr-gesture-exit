# ADR 0002: Prioritize Safe Exit Boundaries

- Status: Accepted
- Date: 2026-08-31

## Context

An overlay that closes processes can cause data loss or disrupt the VR runtime if it guesses incorrectly. SteamVR exposes several application categories and transitions, and a Windows foreground process is not sufficient identity evidence.

## Decision

The add-on acts only when a SteamVR Current VR Game maps to one verified Windows main process. It locks PID, process creation time, executable path, and SteamVR application key at confirmation and revalidates that identity before graceful and force close. It never substitutes a newly foregrounded application.

SteamVR, Steam, SteamVR Home, Dashboard overlays, room setup, runtime components, the add-on, unrelated utilities, children outside the verified identity, and unknown processes are excluded. Graceful close uses `WM_CLOSE`, never simulated input. Force close is allowed only for the same identity after the fixed five-second Exit Grace Period.

Missing identity, tracking, or pointer evidence produces a Safe No-op.

## Consequences

- Some games remain unavailable rather than being guessed.
- Failure to exit is preferable to expanding process scope.
- Diagnostic Log and Dashboard Panel diagnostics explain bounded availability reasons without broad process enumeration or sensitive game data.
- Changes to target mapping or exit scope require explicit review against this ADR.
