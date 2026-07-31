# AGENTS.md

## Scope

This repository contains `libppuc`, the host-side C++ library that configures
and communicates with PPUC IO Boards.

- Treat `src/`, `README.md`, `CMakeLists.txt`, and `platforms/` as the project.
- This repo is the counterpart to `../io-boards` and the layer below `../ppuc`.
- `libppuc` is **not** the protocol source of truth. The wire format lives in
  `../io-boards/src/PPUCProtocolV2.h`, whose headers are staged into
  `third-party/include/io-boards/` by `platforms/*/*/external.sh`, pinned by
  `IO_BOARDS_SHA` in `platforms/config.sh`.

## Project Overview

`libppuc` is a shared library that:

- loads and validates machine configuration from YAML
- configures attached IO boards over RS485
- derives dense `v2` bitmap mappings from sparse logical device numbers
- runs a background RS485 loop that pushes output snapshots and collects switch
  updates
- exposes a logical-number API so callers never deal with bitmaps

Main files:

- `src/PPUC.cpp` (~1.9k lines): YAML load + schema validation, board and device
  registration, config frame generation, mapping derivation, virtual-board
  handling, public API.
- `src/RS485Comm.cpp` (~2k lines): serial transport via `libserialport`, `v2`
  frame encode/decode, runtime output loop, token-ring switch polling, epoch
  session resync, timing constants.
- `src/RS485Comm.h`: transport state, runtime bitmaps, queueing, tuning macros.
- `src/PPUC.h`, `src/PPUC_structs.h`: the public API consumed by `../ppuc`.
  `PPUC.h` also carries the version macros parsed by CMake.

Host serial baud must come from the shared constant `ppuc::v2::kBaudRate`, not
from an independent host-only value.

## Build And Validation

```shell
platforms/macos/arm64/external.sh
cmake -DPLATFORM=macos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

- `external.sh` stages the `io-boards` headers, builds `libserialport`
  (sigrok) and `yaml-cpp`, and copies artifacts into `third-party/`.
- Supported platform/arch combinations live under `platforms/`: `macos/{arm64,
  x64}`, `linux/{x64,aarch64}`, `win/{x64,x86}`, `win-mingw/x64`.
- CI is `.github/workflows/libppuc.yml`.
- There are **no automated tests**. CI only proves that the library compiles.
  Unit tests for YAML validation and mapping derivation are the cheapest
  stabilization win here and would not touch the timing-sensitive transport.
- On Linux, hardware RS485 mode is enabled automatically for `/dev/ttyAMA0` and
  `/dev/serial0`, or forced with `PPUC_RS485_HW=1`.
- Format with `.clang-format` before committing.
- A change here only reaches a `../ppuc` build after `LIBPPUC_SHA` in
  `../ppuc/platforms/config.sh` is bumped, or when building with
  `LIBPPUC_SOURCE_DIR=../libppuc`.

## Working Rules

- Keep host and firmware assumptions aligned when changing frame sizes, CRC
  rules, field endianness, token-ring switch polling, or runtime bitmap sizing.
  Validate `libppuc` and `../io-boards` together.
- Preserve the compatibility model: the public API exposes legacy event-like
  concepts (`SetSolenoidState`, `SetLampState`, …) while the wire transport is
  bitmap-based `v2`. Internal legacy `Event` objects still exist; legacy RS485
  event packets do not.
- Prefer extending `v2` over reviving legacy RS485 packets.

## Game YAML

`src/PPUC.cpp` owns both parsing and schema validation. Top-level sections:
`ppucVersion`, `rom`, `serialPort`, `platform`, `debug`, `boards`,
`dipSwitches`, `switches`, `switchMatrix`, `switchGroups`, `pwmOutput`,
`ledStripes`, `mechs`. Per-device `effects` blocks carry effect and trigger
configuration.

Optional metadata parsed and exposed here:

- `button: true` on switches → `PPUCSwitch::button`, so applications can treat
  cabinet/flipper controls differently from playfield/ball switches.
- `ballSearch: true` on PWM outputs/coils → `PPUCCoil::ballSearch`. `libppuc`
  does **not** fire ball-search coils itself; `../ppuc` decides.
- `pollEvents: true` on boards → registers the board as switch-capable.
- `debounce` + `debounceMode` → `CONFIG_TOPIC_DEBOUNCE_TIME` /
  `CONFIG_TOPIC_MODE`.
- `switchGroups` → named groups exposed to the Lua rules engine. The group
  `buttons` is built in from `button: true` switches and cannot be overridden.

**Schema validation must be kept in sync with every config feature.** Whenever a
YAML section, field, accepted type, or optional key is added anywhere in the
stack, extend the validation pass so malformed files fail early with the
section path and YAML line/column — and check that `../config-tool` exports the
same shape.

## V2 Host Protocol

### Shared wire format

- UART baud `115200` (`ppuc::v2::kBaudRate`); planned move to `250000`.
- Sync `0xA5`, 5-byte header (`sync`, `typeAndFlags`, `nextBoard`, `sequence`,
  `epoch`), CCITT-16 CRC over header + payload.
- All multibyte values are written big-endian: `SetupFrame` 16-bit counts,
  `MappingFrame` 16-bit `index`/`number`, `ConfigFrame` 32-bit `value`. That
  matches the current firmware parser.
- `OutputStateFrame` carries a dense coil bitmap, a dense lamp bitmap, and
  packed GI brightness for 5 fixed GI strings.

### Startup sequence (`PPUC::Connect()`)

1. Open the serial port through `RS485Comm::Connect()`.
2. Flush and send `RestartFrame`.
3. Wait briefly, then flush stale adapter bytes.
4. Parse YAML and emit `ConfigFrame`s for platform, switches, switch matrix,
   PWM outputs, LEDs, effects, and trigger rules.
5. Require addressed boards to acknowledge config with `ConfigAck`.
6. Register switch-capable boards and send `CONFIG_TOPIC_SWITCH_CHAIN` to define
   token handoff.
7. Derive dense coil/lamp/switch mappings from configured logical numbers.
8. Send `SetupFrame`, wait 100 ms.
9. Send all `MappingFrame`s, wait 1 s.
10. For non-WPC platforms, force GI string 1 to full brightness — older systems
    such as System 6 do not provide useful GI updates through PinMAME. The same
    reassertion happens in `StartUpdates()`.
11. Start the `RS485Comm::Run()` background loop.

If config acknowledgments are missing after `RestartFrame`, the host logs the
condition and fails the connection. A hard `ResetFrame` retry is not automatic;
it only happens when the caller forces hard reset
(`SetForceHardReset()` / `ppuc-pinmame --hard-reset`).

Shutdown: stop the runtime thread, send `RestartFrame` as a best-effort
output-off/session-clear, drain and flush the adapter, close the port.

### Runtime mapping model

- Coils, lamps, and switches are collected into sorted unique sets, which become
  dense index↔number mappings.
- `RuntimeConfig` bit counts are the mapping sizes, clamped to `kMax*Bits`
  (`kMaxCoilBits = 64`, `kMaxLampBits = 256`, `kMaxSwitchBits = 256`).
- `RS485Comm` keeps both directions: index→number vectors and number→index hash
  maps.
- Callers set lamps/coils by logical number; `QueueEvent()` converts them into
  bitmap bits. Numbers absent from the derived mapping are **silently ignored** —
  a known sharp edge.
- Dense wire mappings do not need to preserve the highest logical number. 64
  coil slots are considered sufficient even when logical coil numbers are larger
  or sparse.

### Runtime output loop

`RS485Comm::Run()` is the steady-state loop.

- Lamp, GI, and solenoid events become local bitmap changes, not discrete
  packets.
- One `OutputStateFrame` per iteration carries the complete snapshot. Output
  state is **not** incremental.
- Default cadence is 4 ms (`RS485_COMM_DEFAULT_OUTPUT_FRAME_INTERVAL_MS`),
  configurable through `SetOutputFrameIntervalMs()`.
- `header.nextBoard` is the first registered switch board, or `kNoBoard`.
- After each output frame the host immediately reads the chained switch replies.
- `SwitchRefreshFrame` is sent through the same chain when the
  application-configured idle timer expires.
- Host-injected runtime events reach board-local effects as `kFrameTrigger`.

### GI runtime state

GI is transmitted separately from lamps: 5 fixed strings, packed 4-bit values,
valid `0..8` with `0` = off. `QueueEvent(EVENT_SOURCE_GI, string, brightness)`
updates dedicated GI state, not the lamp bitmap. This matters because one
addressable LED string may mix lamps, GI, and flashers, and those domains must
stay independent in transport.

### Switch polling and token ring

`ReceiveSwitchStateChain()` expects a `SwitchStateFrame` or
`SwitchNoChangeFrame` from the selected board; `header.nextBoard` in that reply
selects the next responder; the chain ends at `kNoBoard`.

- Sender identity is not verified beyond the chained `nextBoard` value, because
  frames carry no sender ID.
- CRC failure aborts the receive chain for that cycle.
- Switch changes are edge-diffed against `m_switchBitmap` and exposed as queued
  `PPUCSwitchState` objects.
- Replies carry status flags (`kStatusInSync`, `kStatusNeedsSetup`,
  `kStatusMappingIncomplete`, `kStatusSequenceGap`, `kStatusParserResynced`,
  `kStatusSwitchOverflow`) that feed the resync decision.
- Session resync bumps `m_epoch` and re-sends setup/mapping instead of resetting
  boards.

### Control-frame semantics to preserve

- `RestartFrame`: normal startup/shutdown path. Boards clear config/runtime
  state, turn outputs off, and stay alive on UART for immediate
  reconfiguration.
- `ResetFrame`: hard-reboot recovery path. Keep it for wedged boards and
  debugging.

## Transport Timing

Host switch-chain timing affects overall runtime quality, not only switch
diagnostics. Confirmed on real hardware: increasing the switch-reply receive
window, increasing the serial read timeout, flushing stale input after a missed
chain, and delaying session resync until several consecutive misses made lamp
attract-mode animation visibly correct again. Aggressive timeout/resync behavior
degrades normal output animation even when lamps still look generally active.

Current tuning points in `RS485Comm.h` / `RS485Comm.cpp`:

- `RS485_COMM_SERIAL_READ_TIMEOUT` (5), `RS485_COMM_SERIAL_WRITE_TIMEOUT` (20)
- `RS485_COMM_SWITCH_REPLY_MISS_THRESHOLD` (3)
- `RS485_COMM_SWITCH_POLL_STARTUP_HOLD_MS` (250)
- `RS485_COMM_CONFIG_ACK_TIMEOUT_US` (50000),
  `RS485_COMM_CONFIG_ACK_RETRIES` (3),
  `RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD` (10)
- `RS485_COMM_DEFAULT_OUTPUT_FRAME_INTERVAL_MS` (4)
- `RS485_COMM_EFFECT_EVENT_SPACING_US` (1000)
- the switch-reply receive window in `ReceiveSwitchStateFrame()`
- stale-input flush after a missed chain, short-write retry behavior

Best known-good evidence: Time Warp attract mode ran `1h40m4s` with no
communication errors. Treat these as **per-cabinet transport tuning
parameters**, not protocol constants — games with more switch boards will need
retuning. Do not casually retune them while working on unrelated features.

Protocol hardening must not cost throughput or latency: a full output snapshot
plus the complete switch chain runs every 4 ms. Sequence/loss detection,
heartbeats, and error reporting should ride in existing header/status fields or
idle time rather than adding runtime frames or round trips. Measure before and
after on hardware.

## Virtual Boards

- Board presence is determined by firmware-backed `ConfigAck` responses during
  startup config transmission; every `ConfigFrame` must be acknowledged by the
  addressed board.
- Configured boards that do not acknowledge become host-side virtual boards.
  Unacknowledged config frames are printed as startup errors with
  board/topic/index/key details.
- All switches owned by a virtual board are initialized to `open`, and host-side
  switch injection is limited to those switches.
- Runtime switch polling uses the full logical switch-board order including
  missing boards. Real boards hand off to the next logical board even if that
  board is missing; the host then emits a normal `SwitchState` or
  `SwitchNoChange` frame on the bus on behalf of the virtual board.
- `SetSkippedBoardsCsv()` lets `../ppuc` force boards virtual immediately
  (`--skip-boards`) without waiting for presence detection.
- This is the basis for bench setups where the cabinet board is absent, using
  one unmodified game config.

## Known Risks

- The runtime loop always restarts token polling from the first registered
  switch board on every output frame. Changing fairness or latency requires
  coordinated firmware changes.
- `sequence` is transmitted but not validated for loss, duplicate detection, or
  synchronization.
- `QueueEvent()` silently ignores lamp/coil numbers absent from the derived
  mappings.
- GI uses fixed runtime slots rather than the dynamic mapping tables used for
  coils, lamps, and switches.
- Reset/restart robustness remains the top unresolved transport problem;
  soft restart is the preferred path.

## Debugging Checklist

1. Re-test board restart/reset behavior first.
2. Verify mapping sizes equal the number of unique configured logical numbers,
   not raw port counts.
3. Check that `pollEvents: true` boards also receive
   `CONFIG_TOPIC_SWITCH_CHAIN`.
4. Run a single-board switch test first, then multi-board token chaining.
5. On Linux, test with and without `PPUC_RS485_HW=1` if direction control looks
   wrong.
6. When multi-board chaining mostly works but animation or switch latency looks
   wrong, tune timing before changing protocol semantics.
