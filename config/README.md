# Supported VR games

`supported_vr_games.json` is the single source of truth for the reviewed SteamVR game allowlist.
CMake validates it and generates the C++ key table at configure time. The installed application does
not trust a user-editable runtime allowlist because changing exit scope is a safety-sensitive change.

## Contribution requirements

An entry must satisfy every requirement below:

1. It is available on Steam and its official Steam page identifies it as VR-only or VR-supported.
2. It supports tracked controllers, because the product requires a controller farewell gesture.
3. It has an online multiplayer mode where another live player is visibly co-present as an avatar
   or body and that representation follows the player's head, hand, controller, or body motion.
   The player must be able to see a gesture such as a wave from that person. Leaderboards, ghosts,
   replays, abstract pieces, cars, and multiplayer modes that only expose scores do not qualify.
4. It is established enough to have at least 1,000 Steam user reviews when reviewed. This is a
   simple, reproducible popularity threshold rather than a subjective recommendation.
5. `steam_app_id`, `app_key` and `store_url` refer to the same official Steam application.

Add the entry to `games`, keep the list ordered by product relevance, and summarize the evidence in
the evidence fields. `store_url` is the authoritative App ID source. `embodied_presence_url` should
link directly to first-party documentation or an official developer post demonstrating visible
player embodiment; do not infer it from an `Online Multiplayer` store tag alone. CMake rejects
duplicate IDs/keys, mismatched app keys, invalid or missing evidence URLs, missing evidence, and a
missing VRChat entry.

Beat Saber is intentionally included: official multiplayer material places other players' avatars
beside the player, and official release notes refer to avatar hand movement in the multiplayer
result screen. Its social affordances are limited, but a visible farewell wave is technically
possible. The Lab remains excluded because it does not provide this live social co-presence.

Changing this file still requires review against `docs/adr/0002-prioritize-safe-exit-boundaries.md`
and Windows/SteamVR smoke testing before a release is considered validated.
