#pragma once

#ifdef _MSC_VER
#define CALLBACK __stdcall
#else
#define CALLBACK
#endif

#include <inttypes.h>
#include <string>

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
