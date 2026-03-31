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
- Preserve the current compatibility model: host APIs still expose legacy event-like concepts while transport is bitmap-based on `v2`.
- `RS485Comm` still contains legacy event support for bootstrap/probing paths. Avoid deleting that unless both repos are migrated together.

## V2 Host Protocol

On branch `v2`, `libppuc` actively drives the new protocol implemented by the IO-board firmware.

### Shared Wire Format

The host includes the board repo headers directly:

- `io-boards/PPUCProtocolV2.h`
- `io-boards/Event.h`
- `io-boards/PPUCTimings.h`

Core transport details:

- UART baud: `250000`
- sync byte: `0xA5`
- 4-byte header: `sync`, `typeAndFlags`, `nextBoard`, `sequence`
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
2. Flush the port and send `ResetFrame`.
3. Wait for board reset grace period.
4. Send legacy bootstrap traffic:
   - `EVENT_NULL`
   - empty `ConfigEvent(board)`
   - `EVENT_NULL`
   - repeated twice for all possible boards
5. Send legacy `EVENT_PING` and poll each board with legacy `EVENT_POLL_EVENTS`.
6. Parse YAML config and emit `ConfigFrame`s for platform, switches, switch matrix, PWM outputs, LEDs, effects, and trigger rules.
7. Register switch-capable boards and send `CONFIG_TOPIC_SWITCH_CHAIN` to define token handoff.
8. Derive dense coil/lamp/switch mappings from configured logical numbers.
9. Send `SetupFrame`.
10. Wait 20 ms.
11. Send all `MappingFrame`s.
12. Wait 1 s.
13. Queue initial GI and `EVENT_READ_SWITCHES`.
14. Start `RS485Comm::Run()` background loop.

This mixed startup is intentional right now. Discovery/probing still uses legacy event packets, while steady-state output and switch transport use `v2`.

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

- It drains up to `RS485_COMM_MAX_EVENTS_TO_SEND` queued legacy events per iteration.
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

`v2` is not a full clean break yet.

- Legacy events are still used for:
  - reset/bootstrap synchronization
  - board probing
  - some control signals like `EVENT_RUN`
- `v2` frames are used for:
  - runtime setup
  - mappings
  - config transport
  - output snapshots
  - switch-state return traffic

When modifying this area, keep both paths coherent unless the migration is explicitly being completed on both repos.

## Known Risks

- The runtime loop always restarts token polling from the first registered switch board on every output frame. That is the current design; if fairness or latency changes are needed, coordinate them with firmware.
- `sequence` is transmitted but not validated for loss, duplicate detection, or synchronization.
- `ReceiveSwitchStateFrame()` checks the reply chain by `nextBoard`, not by an explicit sender ID, because frames do not carry one.
- The startup path still depends on legacy packets before `v2` steady-state begins.
- `QueueEvent()` silently ignores lamp/coil numbers that are absent from the derived mappings.
- GI uses fixed runtime slots rather than the dynamic mapping tables used for coils, lamps, and switches.

## Next Bring-Up Focus

When debugging `v2` end-to-end, start with:

1. Confirm `SetupFrame` is received before the firmware attempts DMA cutover.
2. Verify mapping sizes equal the number of unique configured logical numbers, not raw port counts.
3. Check that `pollEvents: true` boards also receive `CONFIG_TOPIC_SWITCH_CHAIN`.
4. Run a single-board switch test first, then multi-board token chaining.
5. If Linux RS485 direction control looks wrong, test with and without `PPUC_RS485_HW=1`.
