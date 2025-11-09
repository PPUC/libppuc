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
  std::string description;

  PPUCSwitch(uint8_t b, uint8_t p, uint8_t n, const std::string& d)
  : board(b), port(p), number(n), description(d) {}
};

struct PPUCCoil {
  uint8_t board;
  uint8_t port;
  uint8_t type;
  uint8_t number;
  std::string description;

  PPUCCoil(uint8_t b, uint8_t p, uint8_t t, uint8_t n,const std::string& d)
  : board(b), port(p), type(t), number(n), description(d) {}
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
