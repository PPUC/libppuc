#pragma once

#define PPUC_VERSION_MAJOR 0  // X Digits
#define PPUC_VERSION_MINOR 2  // Max 2 Digits
#define PPUC_VERSION_PATCH 0  // Max 2 Digits

#define _PPUC_STR(x) #x
#define PPUC_STR(x) _PPUC_STR(x)

#define PPUC_VERSION           \
  PPUC_STR(PPUC_VERSION_MAJOR) \
  "." PPUC_STR(PPUC_VERSION_MINOR) "." PPUC_STR(PPUC_VERSION_PATCH)
#define PPUC_MINOR_VERSION \
  PPUC_STR(PPUC_VERSION_MAJOR) "." PPUC_STR(PPUC_VERSION_MINOR)

#ifdef _MSC_VER
#define PPUCAPI __declspec(dllexport)
#define CALLBACK __stdcall
#else
#define PPUCAPI __attribute__((visibility("default")))
#define CALLBACK
#endif

#include "yaml-cpp/yaml.h"

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

class RS485Comm;

class PPUCAPI PPUC {
 public:
  PPUC();
  ~PPUC();

  void SetLogMessageCallback(PPUC_LogMessageCallback callback,
                             const void* userData);

  void LoadConfiguration(const char* configFile);
  void SetDebug(bool debug);
  bool GetDebug();
  void SetRom(const char* rom);
  const char* GetRom();
  void SetSerial(const char* serial);
  const char* GetSerial();
  bool Connect();
  void Disconnect();
  void StartUpdates();
  void StopUpdates();

  void SetSolenoidState(int number, int state);
  void SetLampState(int number, int state);
  PPUCSwitchState* GetNextSwitchState();

 private:
  YAML::Node m_ppucConfig;
  RS485Comm* m_pRS485Comm;
  uint8_t ResolveLedType(std::string type);

  bool m_debug = false;
  char* m_rom;
  char* m_serial;
  uint8_t m_platform;

  void SendLedConfigBlock(const YAML::Node& items, uint32_t type, uint8_t board,
                          uint32_t port);
};
