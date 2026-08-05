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

  // Board configuration, which happens at startup and after a resync.
  uint32_t configAckRetries = 0;   // config frames that needed repeating
  uint32_t configAckTimeouts = 0;  // config frames never acknowledged

  // Transport faults, counted wherever they are reported.
  uint32_t serialWriteFailures = 0;  // the port rejected or truncated a write
  uint32_t frameCrcErrors = 0;       // a frame arrived corrupt
};
