# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An archival patch for Rec Room build **19/07/25** (19 July 2025) that repoints the client from the
dead official backend to a mirror (`recflare`): API host rewritten to `ns.recflare.net`, Photon
redirected to `photon.recflare.net`, plus a self-contained Referee (anti-cheat/anti-debug) bypass.
Ships as an injected DLL and a standalone injector.

## Build

Release|x64 is the only working configuration — see the caveat below.

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  2025Patch.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
```

Both projects write to the solution-level `x64/Release/`: `2025Patch.dll` and `Injector.exe`.
Ship the two side by side; the injector defaults to `2025Patch.dll` next to itself (or takes a path
as argv[1]).

There is no test suite, linter, or CI. Verification is empirical: run the game, read
`2025patch.log`.

**Debug|x64 does not link.** The `ml64.exe` CustomBuild step for `RetSpoof.asm` in
`2025Patch/2025Patch.vcxproj` is conditioned on `Release|x64` only, so a Debug build has no
`_spoofer_stub` to link against. Add a matching Debug condition before trying to build Debug.

## Architecture

### Boot sequence

`Injector/injector.cpp` waits for `Recroom_Release.exe`, then waits until **both**
`GameAssembly.dll` and `Referee.dll` are in its module list (the patch resolves against both at
attach time and would crash if injected earlier), settles 2s, then `CreateRemoteThread` +
`LoadLibraryW`. It refuses to double-inject — a second attach installs every hook twice and crashes.

`DllMain` (`2025Patch/src/main.cpp`) then calls `RR::Patches::Resolve()` followed by
`RR::Patches::Patch()`, both in `2025Patch/src/RR/Patching/Patches.h`.

### Addresses are hardcoded RVAs, against two different bases

`2025Patch/src/RR/Methods.h` is the address table. **Which module a value is relative to depends on
its namespace:** `RR::Methods::Referee::*` are RVAs into `Referee.dll`; everything else is an RVA
into `GameAssembly.dll` (`GA`, `globals.h`). Patches.h reflects this — `Referee + Check1` vs
`GA + SendRequest`.

Nothing is pattern-scanned except the return-spoof gadget, so **any other game build invalidates
every offset in Methods.h**. Structure field offsets (`RR::Offsets::*`) come from the runtime dump
of the same build and are equally build-locked. Rec Room's il2cpp names are obfuscated
(`CBACIMLIBPF`, `HPJKKCCECLH`); Methods.h comments record what each one actually is — keep that
mapping updated when adding an address, it is the only way back to the managed symbol.

`Resolve()` pattern-scans `FF 23` (`jmp [rbx]`) for the spoof gadget and resolves the three
`il2cpp_*` exports via `GetProcAddress`.

### Three separate network paths, hooked three different ways

This split is the single most load-bearing thing to know, and it is not visible from any one file:

1. **BestHTTP** (`HTTPRequest.SendRequest`) — hooked and *rewritten*: `ns.rec.net` →
   `ns.recflare.net`, by constructing a fresh `System.Uri` and storing it back into the request.
2. **Photon** — does not use HTTP at all; it resolves `*.photonengine`/`exitgames`/
   `photonindustries` and connects over raw sockets. Redirected at the winsock layer by hooking
   `getaddrinfo` / `GetAddrInfoW` in `ws2_32`.
3. **System.Net.Http** (`RR::Methods::HttpClient::Request9`/`Request10`) — RecNet's *other* HTTP
   client, which is what room-save and asset blobs actually go through. The SendRequest hook is
   blind to it. It is currently **logged only, never rewritten**; those requests reach the mirror
   only because their DNS goes through the winsock hooks.

So "add a URL rewrite" is not one edit — decide which stack the request travels first.

### Calling back into il2cpp

Managed methods are invoked through `spoof_call` (`Utils/deps/spoofcall/`), which hides the DLL's
return address from Referee's stack walks. Be sparing: a spoofed call on a path where a managed
exception can unwind through the faked return address yields `STATUS_INVALID_DISPOSITION`
(0xC0000026) and kills the process. `SendRequest_H` carries a comment marking exactly which
redundant spoofed call caused that crash — do not reintroduce one just to log something already in
hand.

`ReadIl2CppString` (`globals.h`) deliberately ignores `Il2cppString::Length`: on this il2cpp layout
offset 0 is the klass pointer, not a length. It converts the null-terminated `wchar_t` buffer at
wchar offset `0xA` under a fixed cap, returns a `new[]` buffer or `nullptr`, and **callers must
`delete[]` it and null-check** (`std::string s = nullptr` is UB and was a live crash).

### Hook conventions

Every hook is written to never take the game down: wrap the interesting work in `__try/__except`
(or `try/catch` where C++ exceptions are possible) and **always** forward to the original, unchanged,
on any failure. Hooks that force a constant return (`VerifyImageSig_H`, `IsTransportEncrypted_H`)
call the original once and log the true value before overriding it — that line is how you find out
whether the hook is masking a different fault. Keep that pattern for new forcing hooks.

### Diagnostics

`PatchLog` (`Utils/Inc/Includes.h`) writes timestamped, per-line-flushed output to `2025patch.log`
**next to the game exe** — it survives a crash and correlates with Unity's `Player.log` by
timestamp. The AllocConsole path is off by default (`kEnableConsole` in `main.cpp`): the console
steals foreground focus and Unity throttles hard when unfocused, which measurably wrecked room-load
times. `std::cout` calls therefore go nowhere; every one of them is mirrored by a `PatchLog` line,
and new diagnostics should use `PatchLog`.

## Configuration knobs

All compile-time constants, no config file:

- `UsePhotonCloud` (Patches.h) — `false` targets the self-hosted server and enables the DNS
  redirect; `true` skips the redirect and injects the `CloudAppId*` GUIDs into `AppSettings` just
  before connect, for A/B testing against real Photon Cloud.
- `PhotonHost`, `CloudAppId*`, `CloudFixedRegion` (Patches.h).
- `kEnableConsole` (main.cpp).

## Investigation notes in comments

Long comment blocks in `Methods.h` and `Patches.h` record *why* a hook exists and what was measured,
including dead ends kept deliberately (e.g. `EnetPeer.IsTransportEncrypted` is defined but **not**
hooked, because the original measured `0` and datagram encryption was ruled out). Treat these as the
project's findings log — read the relevant block before changing or re-adding a hook, and record new
measurements the same way rather than deleting the old reasoning.
