# Rec Room 2025 Patch

Fork of https://github.com/Carpetsoft/Rec-Room-2025-Patch

- Disabled image signing, no need for sigs
- Added logging to investigate a Photon issue (probably remove later)
- Added custom .DLL injector

## Configuration

`2025patch.ini` in this repo is a ready-to-use example carrying the built-in defaults, and it
ships in the release zip. Drop it next to `Recroom_Release.exe` (the same folder the patch writes
`2025patch.log` to) and edit as needed -- it is read once at injection.

If the file is absent the patch writes exactly this content itself on first run, so the copy in
the release is only there to make the knobs discoverable without launching first. An existing
file is never overwritten; delete a key (or the whole file) to fall back to the default.

| key | default | effect |
| --- | --- | --- |
| `ApiHost` | `ns.recflare.net` | replaces `ns.rec.net` in the game's API request URIs |
| `PhotonHost` | `photon.recflare.net` | DNS target for `*.photonengine` / `exitgames` / `photonindustries` |
| `UsePhotonCloud` | `false` | `false` = self-hosted; `true` = real Photon Cloud (`CloudAppId*` used instead) |
| `PhotonPort` | `0` | self-hosted only; port of the initial Photon connect, `0` = leave it alone |
| `EnableConsole` | `false` | debug console window; costs load time (focus theft -> Unity throttling) |
| `BlockDeadHosts` | `true` | fail third-party telemetry/analytics lookups instantly |
| `EnableTracing` | `false` | verbose diagnostic hooks; chatty, only for investigating |
| `CloudAppIdRealtime` / `CloudAppIdVoice` / `CloudAppIdChat` | dashboard GUIDs | Cloud only; empty keeps the server-supplied id |
| `CloudFixedRegion` | *(empty)* | Cloud only, e.g. `us`; empty = pick via name server |

Whatever ends up in effect is logged as `[Config] ...` in `2025patch.log`.
