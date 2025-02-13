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
  uint8_t number;
  std::string description;

  PPUCSwitch(uint8_t n, const std::string& d)
  : number(n), description(d) {}
};

struct PPUCCoil {
  uint8_t type;
  uint8_t number;
  std::string description;

  PPUCCoil(uint8_t t, uint8_t n,const std::string& d)
  : type(t), number(n), description(d) {}
};

struct PPUCLamp {
  uint8_t type;
  uint8_t number;
  std::string description;

  PPUCLamp(uint8_t t, uint8_t n, const std::string& d)
  : type(t), number(n), description(d) {}
};
