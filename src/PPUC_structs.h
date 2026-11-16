#pragma once

#ifdef _MSC_VER
#define CALLBACK __stdcall
#else
#define CALLBACK
#endif

#include <inttypes.h>
#include <string>
#include <vector>

typedef void(CALLBACK* PPUC_LogMessageCallback)(const char* format,
                                                va_list args,
                                                const void* userData);

struct PPUCSwitchState {
  int number;
  int state;

  PPUCSwitchState(int n, int s) {
    number = n;
    state = s;
  }
};

struct PPUCSwitch {
  uint8_t board;
  uint8_t port;
  uint8_t number;
  bool button;
  std::string description;

  PPUCSwitch(uint8_t b, uint8_t p, uint8_t n, const std::string& d,
             bool btn = false)
  : board(b), port(p), number(n), button(btn), description(d) {}
};

struct PPUCCoil {
  uint8_t board;
  uint8_t port;
  uint8_t type;
  uint8_t number;
  bool ballSearch;
  std::string description;

  PPUCCoil(uint8_t b, uint8_t p, uint8_t t, uint8_t n, const std::string& d,
           bool bs = false)
  : board(b), port(p), type(t), number(n), ballSearch(bs), description(d) {}
};

struct PPUCLamp {
  uint8_t board;
  uint8_t port;
  uint8_t type;
  uint8_t number;
  std::string description;
  uint32_t color;

  PPUCLamp(uint8_t b, uint8_t p, uint8_t t, uint8_t n, const std::string& d, uint32_t c)
  : board(b), port(p), type(t), number(n), description(d), color(c) {}
};

// A running tally of how often the bus needed the mechanisms that protect it.
//
// The host carries a number of timeouts, retries and windows - the 40 ms
// switch-reply window, the miss threshold that triggers a session resync, the
// config-ack retries - and no way to tell which of them ever actually fire.
// Some are genuine protocol design; others are compensations tuned by trial
// against a bus that was misbehaving for reasons now being fixed. Without
// counts there is no way to separate the two, and removing a load-bearing
// timeout is how a recoverable glitch becomes a dead table.
//
// Deliberately always collected, never behind a debug flag: enabling debug
// output changes the timing, so a counter that only runs in debug mode
// measures a different machine than the one people play.
//
// Raw counts only, no derived rates - there is nothing here to get wrong, and
// whoever reads them can divide.
// Progress during a firmware transfer, so a long operation is not silent.
typedef void (*PPUC_FirmwareProgressCallback)(uint8_t board, size_t sentBytes,
                                              size_t totalBytes,
                                              void* userData);

// Outcome of a firmware update attempt.
struct PPUCFirmwareUpdateResult {
  bool ok = false;
  uint8_t board = 0;
  uint8_t status = 0;       // AdminUpdateStatus from the board, when it spoke
  size_t bytesSent = 0;
  std::string error;        // why it stopped, when !ok
};

// What a board reports about itself when asked, before any session exists.
// One board's own view of the transport, read out of band. The host can see
// that a board did not answer but not why; these say whether it saw the frame.
struct PPUCBoardStats {
  uint8_t board = 0;
  bool responded = false;
  uint32_t rxFrames = 0;
  uint32_t rxCrcFail = 0;
  uint32_t rawBytes = 0;
  uint32_t txFrames = 0;
  // Times the token named this board, whether or not it managed to answer.
  uint32_t selected = 0;
  // Version queries this board saw arrive, answered or not.
  uint32_t versionQueries = 0;
  // Version reports this board actually put on the wire.
  uint32_t versionReplies = 0;
  // High-power gate: bit0 powerOn, bit1 coinDoorClosed,
  // bit2 gameOnSolenoid configured, bit3 coinDoorSwitch configured.
  // bit0 powerOn, bit1 coinDoorClosed, bit3 coinDoorSwitch set;
  // bits 8-15 the configured gameOnSolenoid, bits 16-23 the coinDoorSwitch.
  // Bits 24-31 carry resetCause below and are cleared here.
  uint32_t highPowerGate = 0;
  // Why the board last started, a ppuc::v2::ResetCause; 0 from firmware
  // before 0.5.2. PPUC::ResetCauseName() says it in words.
  uint8_t resetCause = 0;
};

struct PPUCBoardVersion {
  uint8_t board = 0;
  bool responded = false;
  uint8_t firmwareMajor = 0;
  uint8_t firmwareMinor = 0;
  uint8_t firmwarePatch = 0;
  uint8_t adminProtocolMajor = 0;
  uint8_t adminProtocolMinor = 0;
  uint8_t capabilities = 0;
  // Which board this is. Firmware is board-specific, so an image may only be
  // sent to a board whose type it was built for.
  uint8_t boardType = 0;

  // Which build, as opposed to which version. Zero means the board did not
  // record one - a local build - and must not be treated as a build that
  // differs from every other.
  uint32_t buildId = 0;

  std::string FirmwareVersion() const {
    return std::to_string(firmwareMajor) + "." + std::to_string(firmwareMinor) +
           "." + std::to_string(firmwarePatch);
  }

  // Ordering for "is the file newer than the board", so the comparison lives
  // in one place rather than being open-coded wherever it is needed.
  uint32_t FirmwareOrdinal() const {
    return (static_cast<uint32_t>(firmwareMajor) << 16) |
           (static_cast<uint32_t>(firmwareMinor) << 8) |
           static_cast<uint32_t>(firmwarePatch);
  }
};

struct PPUCBusHealth {
  // Switch reply chains: one per poll cycle round the configured boards.
  uint32_t switchReplyChains = 0;       // attempted
  uint32_t switchReplyChainsClean = 0;  // completed intact
  uint32_t switchReplyMisses = 0;       // did not complete (lifetime, not the
                                        // consecutive streak used internally)
  uint32_t sessionResyncs = 0;          // miss streak reached the threshold
  // Every session resync carried out, whatever asked for it: the miss streak
  // above, or a board reporting another epoch or an incomplete mapping. Each
  // one is about 0.4 s in which no lamp or coil is updated, so this is the
  // number that says how a game felt. sessionResyncs alone hid hundreds.
  uint32_t sessionResyncsStarted = 0;
  // Frames a board missed that needed no resync: the next snapshot put the
  // outputs right.
  uint32_t framesLostTolerated = 0;
  // Switch replies left over from an earlier chain and dropped, rather than
  // taken for the board being polled. Each one says a chain was cut short
  // while boards behind the failing one were still answering.
  uint32_t staleSwitchReplies = 0;
  // Times a board stopped answering the switch token for long enough to be
  // passed over, and was from then on asked once a second. Non-zero says which
  // problem it was: a board without power or a wire, not a busy bus.
  uint32_t switchBoardSuspensions = 0;

  // A board reported that it needs setup while the game was running, which
  // means it restarted and lost its configuration. Distinct from a resync: a
  // resync resends setup and mapping frames, but not the per-device config -
  // power, debounce, maxPulseTime, stop switches - which only Connect() sends.
  // A board that rejoins on setup alone is driving coils without its
  // protection, so this is not something to recover from silently.
  uint32_t boardsLostConfiguration = 0;
  // The board the last of those was, by its place in the switch chain. Its
  // stats say why it restarted (PPUCBoardStats::resetCause). 0 for none.
  uint8_t lastBoardLostConfiguration = 0;

  // Board configuration, which happens at startup and after a resync.
  uint32_t configAckRetries = 0;   // config frames that needed repeating
  uint32_t configAckTimeouts = 0;  // config frames never acknowledged

  // Transport faults, counted wherever they are reported.
  uint32_t serialWriteFailures = 0;  // the port rejected or truncated a write
  uint32_t frameCrcErrors = 0;       // a frame arrived corrupt
};
