#pragma once

#define PPUC_VERSION_MAJOR 0  // X Digits
#define PPUC_VERSION_MINOR 3  // Max 2 Digits
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
#else
#define PPUCAPI __attribute__((visibility("default")))
#endif

#include "PPUC_structs.h"
#include "yaml-cpp/yaml.h"

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

  uint8_t GetCoinDoorClosedSwitch() { return m_coinDoorClosedSwitch; };
  uint8_t GetGameOnSolenoid() { return m_gameOnSolenoid; };

  void CoilTest(uint8_t number);
  void LampTest(uint8_t number);
  void FlasherTest(uint8_t number);
  void GITest(uint8_t number);
  void SwitchTest();

  std::vector<PPUCCoil> GetCoils();
  std::vector<PPUCLamp> GetLamps();
  std::vector<PPUCSwitch> GetSwitches();

 private:
  YAML::Node m_ppucConfig;
  RS485Comm* m_pRS485Comm;
  uint8_t ResolveLedType(std::string type);
  std::vector<PPUCCoil> m_coils;
  std::vector<PPUCLamp> m_lamps;
  std::vector<PPUCSwitch> m_switches;

  bool m_debug = false;
  char* m_rom;
  char* m_serial;
  uint8_t m_platform;
  uint8_t m_coinDoorClosedSwitch;
  uint8_t m_gameOnSolenoid;

  void SendTriggerConfigBlock(const YAML::Node& items, uint32_t type,
                              uint8_t board, uint32_t port);
  void SendLedConfigBlock(const YAML::Node& items, uint32_t type, uint8_t board,
                          uint32_t port);
};
