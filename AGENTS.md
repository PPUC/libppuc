# AGENTS.md

## Scope

This repository contains `libppuc`, the host-side library that communicates with PPUC IO Boards.

- Treat `src/`, `README.md`, `CMakeLists.txt`, and `platforms/` as the project.
- This repo is the counterpart to `../io-boards`.
- When working on protocol behavior, inspect both repos because `libppuc` includes shared protocol headers from `io-boards`.

## Project Overview

`libppuc` is a C++ host library that:

- loads machine configuration from YAML in `PPUC`
- configures attached IO boards over RS485
- translates high-level lamp/coil/switch usage into dense `v2` bitmaps
- runs a background RS485 loop that pushes output snapshots and receives switch updates

Main files:

- `src/PPUC.cpp`: YAML-driven configuration, mapping derivation, library API.
- `src/RS485Comm.h`: transport state, runtime bitmaps, queueing, protocol API.
- `src/RS485Comm.cpp`: RS485 serial transport and `v2` frame handling.
- `src/PPUC_structs.h`: public structs exposed to callers.

## Build And Validation

Build instructions in `README.md` are platform-specific and use CMake.

- Linux/macOS/Windows dependency setup happens through `platforms/*/external.sh`.
- Prefer validating protocol changes with a normal CMake build for the active platform.
- This repo uses `libserialport` for serial I/O.
- On Linux, hardware RS485 mode may be enabled automatically for `/dev/ttyAMA0` or `/dev/serial0`, or forced with `PPUC_RS485_HW=1`.

## Working Rules

- Do not treat `libppuc` as the protocol source of truth by itself. The shared wire format lives in `../io-boards/src/PPUCProtocolV2.h`.
- Keep host and firmware assumptions aligned when changing:
  - frame sizes
  - CRC rules
  - field endianness
  - token-ring switch polling
  - runtime bitmap sizing and mappings
- Preserve the current compatibility model: host APIs still expose legacy event-like concepts while RS485 transport is bitmap-based on `v2`.
- Internal legacy `Event` objects still exist in `libppuc`, but legacy RS485 event packets are no longer part of the wire protocol.

## V2 Host Protocol

On branch `v2`, `libppuc` actively drives the new protocol implemented by the IO-board firmware.

### Shared Wire Format

The host includes the board repo headers directly:

- `io-boards/PPUCProtocolV2.h`
- `io-boards/Event.h`
- `io-boards/PPUCTimings.h`

Core transport details:

- UART baud: `115200`
- Planned follow-up: move the `v2` transport baseline to `250000` after the
  current reset/restart issues are resolved.
- sync byte: `0xA5`
- 5-byte header: `sync`, `typeAndFlags`, `nextBoard`, `sequence`, `epoch`
- CRC: CCITT-16 over header + payload

`libppuc` writes all multibyte values in big-endian byte order:

- `SetupFrame`: 16-bit counts
- `MappingFrame`: 16-bit `index` and `number`
- `ConfigFrame`: 32-bit `value`

That matches the current firmware parser in `io-boards`.

`OutputStateFrame` now carries:

- dense coil bitmap
- dense lamp bitmap
- packed GI brightness for 5 fixed GI strings

### Host Startup Sequence

The effective `v2` startup flow in `PPUC::Connect()` is:

1. Open serial port through `RS485Comm::Connect()`.
2. Flush the port and send `RestartFrame`.
3. Wait briefly, then flush stale adapter bytes.
4. Parse YAML config and emit `ConfigFrame`s for platform, switches, switch matrix, PWM outputs, LEDs, effects, and trigger rules.
5. Require addressed boards to acknowledge config with `ConfigAck`.
6. Register switch-capable boards and send `CONFIG_TOPIC_SWITCH_CHAIN` to define token handoff only across present switch boards.
7. Derive dense coil/lamp/switch mappings from configured logical numbers.
8. Send `SetupFrame`.
9. Wait 100 ms.
10. Send all `MappingFrame`s.
11. Wait 1 s.
12. Queue initial GI.
13. Start `RS485Comm::Run()` background loop.

If the first startup pass misses config acknowledgments after `RestartFrame`,
the host now logs that condition and performs one whole-startup retry after a
hard `ResetFrame` before failing the connection.

Normal shutdown flow:

1. Stop the runtime thread.
2. Send `RestartFrame` as a best-effort output-off/session-clear request.
3. Drain/flush the serial adapter.
4. Close the port.

Startup is now `v2`-only on the wire. Presence detection comes from `ConfigAck`, not legacy probe packets.

### Runtime Mapping Model

`PPUC::Connect()` builds dense `v2` indexes from configured logical numbers.

- Coils, lamps, and switches are collected into sorted unique sets.
- Those sets become dense index-to-number mappings.
- `RuntimeConfig` bit counts are set to the mapping sizes, clamped to `kMax*Bits`.
- `RS485Comm` stores both directions:
  - index -> logical number vectors
  - logical number -> index hash maps

Practical consequence:

- Host callers still set lamps/coils by logical number.
- `QueueEvent()` converts those numbers into bitmap bits.
- If a number is not present in the derived mapping, the bitmap update is ignored.

Design direction:

- Dense wire-level mappings do not need to preserve the highest logical number.
- Higher logical coil numbers can and should be mapped into a compact dense coil bitmap.
- For this project, 64 coil slots are considered sufficient even if logical coil numbers are larger or sparse.

### Runtime Output Loop

`RS485Comm::Run()` is the steady-state transport loop.

- It converts lamp, GI, and solenoid events into local coil/lamp bitmap changes instead of sending them as discrete RS485 packets.
- It sends one `OutputStateFrame` each iteration containing the full coil/lamp snapshot.
- `header.nextBoard` is set to the first registered switch board, or `kNoBoard` if no switch boards exist.
- If a switch board chain exists, the host immediately reads the chained switch responses.

This means the host is snapshot-driven on `v2`. Output state is not incremental.

### GI Runtime State

General Illumination is transmitted separately from lamps in `v2` runtime traffic.

- There are 5 fixed GI strings.
- Each string carries a packed 4-bit value.
- Valid values are `0..8`, where `0` means off.
- `QueueEvent(EVENT_SOURCE_GI, string, brightness)` updates dedicated GI state, not the lamp bitmap.

This matters because one addressable LED string may mix lamps, GI, and flashers, and those domains must stay independent in runtime transport.

### Switch Polling And Token Ring

Switch-capable boards are registered through `RegisterSwitchBoard()` when YAML board entries have `pollEvents: true`.

The host then:

- sends `CONFIG_TOPIC_SWITCH_CHAIN` so each board knows which board follows it
- starts polling by placing the first switch board into `OutputStateFrame.header.nextBoard`
- calls `ReceiveSwitchStateChain(firstBoard)` after each output frame

`ReceiveSwitchStateChain()` expects:

1. `SwitchStateFrame` or `SwitchNoChangeFrame` from the selected board
2. `header.nextBoard` in that reply tells the host which board should answer next
3. the chain ends at `kNoBoard`

Important current behavior:

- The host does not verify sender identity beyond the chained `nextBoard` value.
- It accepts either `SwitchStateFrame` or `SwitchNoChangeFrame`.
- CRC failure aborts the receive chain for that cycle.
- Switch changes are edge-diffed against `m_switchBitmap` and exposed as queued `PPUCSwitchState` objects.

### Legacy/V2 Boundary

`v2` is now the only host-side RS485 wire protocol.

- Internal legacy-style `Event` objects still exist in `libppuc`.
- `v2` frames are used for:
  - soft restart
  - hard reset
  - runtime setup
  - mappings
  - config transport
  - config acknowledgment
  - output snapshots
  - switch-state return traffic
  - host-driven virtual switch updates

When modifying transport behavior, prefer extending `v2` rather than reviving legacy RS485 packets.

Control-frame semantics to preserve:

- `RestartFrame`:
  - normal host startup/shutdown path
  - boards must clear config/runtime state and turn outputs off
  - boards must stay alive on UART for immediate reconfiguration
- `ResetFrame`:
  - hard-reboot recovery path
  - keep available for wedged-board recovery and debugging

## Known Risks

- The runtime loop always restarts token polling from the first registered switch board on every output frame. That is the current design; if fairness or latency changes are needed, coordinate them with firmware.
- `sequence` is transmitted but not validated for loss, duplicate detection, or synchronization.
- `ReceiveSwitchStateFrame()` checks the reply chain by `nextBoard`, not by an explicit sender ID, because frames do not carry one.
- Startup and steady-state transport are both `v2`-only on the wire.
- `QueueEvent()` silently ignores lamp/coil numbers that are absent from the derived mappings.
- GI uses fixed runtime slots rather than the dynamic mapping tables used for coils, lamps, and switches.

## Confirmed Timing Behavior

- The current host-side switch-chain timing matters for overall runtime quality, not only for switch test.
- Confirmed on real hardware: increasing the switch-reply receive window, increasing the serial read timeout, flushing stale input after a missed chain, and delaying session resync until several consecutive misses made the lamp attract-mode animation visibly correct again.
- This means aggressive switch timeout/resync behavior can degrade normal output animation even when lamps still appear generally active.
- Keep these timing values as part of the current known-good baseline unless testing proves a better set.
- For future games with more switch boards, expect these values to need retuning. Treat them as transport tuning parameters, not protocol constants.
- Latest real-machine result: Time Warp attract mode ran for `1h40m4s` with no communication error messages.
- That run is the current best evidence that the present `libppuc` `v2` timing, config-ack startup, and output/switch polling balance are stable enough to treat as the new baseline.
- Do not casually retune transport timing away from this point while working on unrelated features such as coil test or virtual-board behavior.

Current transport-tuning points to remember in `RS485Comm`:

- `RS485_COMM_SERIAL_READ_TIMEOUT`
- switch-reply receive window in `ReceiveSwitchStateFrame()`
- stale-input flush after a missed switch-reply chain
- miss-count threshold before `m_needSessionResync = true`
- output-frame pacing in the runtime loop
- serial write timeout / short-write retry behavior

## Current Runtime Status

- Long-run runtime behavior on the good boards is substantially better than in
  the earlier bring-up phase.
- Host timing still matters, but firmware-side switch reply latency was also a
  real contributor. Moving switch-token forwarding earlier on the boards
  reduced the required experimental switch-reply delay substantially in
  practice.
- The remaining primary transport issue is restart/reset robustness rather than
  the earlier "runtime only works with very large switch delay" symptom.

## Virtual Board Implementation Notes

- First implementation slice is host-side only.
- `libppuc` now tracks configured boards and switch ownership by board from YAML.
- Board presence is now determined by explicit firmware-backed `ConfigAck` responses during startup config transmission.
- Every `ConfigFrame` must be acknowledged by the addressed board.
- Missing configured boards become host-side virtual boards if they do not acknowledge config during startup.
- Config frames that are not acknowledged are printed as startup errors with board/topic/index/key details.
- All switches owned by a virtual board are initialized to `open`.
- Host-side switch injection is now limited to switches owned by virtualized boards.
- This is the intended basis for bench setups where the cabinet board is absent.
- Runtime switch polling now uses the full logical switch-board order, including missing boards.
- Real boards are configured to hand off to the next logical board, even if that board is missing.
- When the chain reaches a missing board token, `libppuc` now emits a normal `SwitchState` or `SwitchNoChange` frame on the RS485 bus on behalf of that virtual board and then hands over to the next logical board.
- `libppuc` also supports a forced-virtual board set from `ppuc`, so test runs can skip specific configured boards immediately without waiting for presence detection.

## Next Bring-Up Focus

When debugging `v2` end-to-end, start with:

1. Re-test board restart/reset behavior first; that is the top unresolved
   transport problem.
2. Verify mapping sizes equal the number of unique configured logical numbers,
   not raw port counts.
3. Check that `pollEvents: true` boards also receive
   `CONFIG_TOPIC_SWITCH_CHAIN`.
4. Run a single-board switch test first, then multi-board token chaining.
5. If Linux RS485 direction control looks wrong, test with and without
   `PPUC_RS485_HW=1`.
6. When multi-board switch chaining mostly works but animation or switch
   latency looks wrong, tune timing before changing protocol semantics.
