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

#include <set>
#include <unordered_map>

class RS485Comm;

// Board type names, forwarded from the protocol definition.
//
// Exposed here because ppuc.cpp cannot include the protocol header: it has a
// global variable called `ppuc`, which cannot coexist with a namespace of the
// same name. The mapping still has exactly one definition - these forward to
// ppuc::v2::BoardTypeName / BoardTypeFromName.
PPUCAPI const char* PPUCBoardTypeName(uint8_t type);
PPUCAPI uint8_t PPUCBoardTypeFromName(const char* name);

struct PPUCCoilGiMapping {
  uint16_t coil = 0;
  uint8_t gi = 0;
  uint8_t onBrightness = 8;
  uint8_t offBrightness = 0;
};


class PPUCAPI PPUC {
 public:
  PPUC();
  ~PPUC();

  void SetLogMessageCallback(PPUC_LogMessageCallback callback,
                             const void* userData);

  void LoadConfiguration(const char* configFile);
  void SetDebug(bool debug);
  void SetDebugErrors(bool debugErrors);
  void SetSkippedBoardsCsv(const char* skippedBoardsCsv);
  void SetSwitchReplyDelayUs(uint32_t delayUs);
  void SetSwitchRefreshIdleMs(uint32_t idleMs);
  void SetOutputFrameIntervalMs(uint32_t intervalMs);
  void SetCoilHoldFrames(uint8_t holdFrames);
  void SetDisableFastFlipForTests(bool disableFastFlipForTests);
  void SetForceHardReset(bool forceHardReset);
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
  void SetGIState(int string, int brightness);
  void SetSwitchState(int number, int state);
  void TriggerEvent(uint8_t source, int number, int value);
  bool IsSwitchVirtualized(int number);
  bool IsBoardVirtualized(uint8_t board);
  PPUCSwitchState* GetNextSwitchState();
  uint32_t GetCleanSwitchReplyChainCount();

  // Bus recovery counters since startup. See PPUCBusHealth.
  PPUCBusHealth GetBusHealth();

  // The most recent unexpected conditions, oldest first, held in RAM because
  // the target has no filesystem to log to. Retrieve over ssh rather than
  // hoping someone saw them scroll past.
  std::vector<std::string> GetRecentAnomalies();

  // Asks every configured board what firmware it is running.
  //
  // Boards are polled one at a time: administration happens outside the switch
  // chain, so nothing arbitrates who replies to a broadcast. Entries for
  // boards that did not answer are returned with responded == false rather
  // than omitted, so a missing board is visible instead of silently absent.
  std::vector<PPUCBoardVersion> QueryBoardVersions();

  // Sends a firmware image to one board. Call StopUpdates() first: the runtime
  // loop must not be transmitting into the middle of a transfer.
  PPUCFirmwareUpdateResult UpdateBoardFirmware(
      uint8_t board, uint8_t imageBoardType, const uint8_t* image,
      size_t imageBytes, PPUC_FirmwareProgressCallback progress = nullptr,
      void* progressUserData = nullptr);

  uint8_t GetCoinDoorClosedSwitch() { return m_coinDoorClosedSwitch; };
  uint8_t GetGameOnSolenoid() { return m_gameOnSolenoid; };
  uint8_t GetPlatform() { return m_platform; };

  std::vector<PPUCCoil> GetCoils();
  std::vector<PPUCLamp> GetLamps();
  std::vector<PPUCSwitch> GetSwitches();
  std::unordered_map<std::string, std::vector<uint16_t>> GetSwitchGroups();
  const std::vector<PPUCCoilGiMapping>& GetCoilGiMappings() const;

 private:
  YAML::Node m_ppucConfig;
  RS485Comm* m_pRS485Comm;
  uint8_t ResolveLedType(const std::string& type);
  uint32_t ResolveSwitchDebounceMode(const YAML::Node& node);
  std::vector<PPUCCoil> m_coils;
  std::vector<PPUCLamp> m_lamps;
  std::vector<PPUCSwitch> m_switches;
  std::vector<PPUCCoilGiMapping> m_coilGiMappings;
  std::unordered_map<std::string, std::vector<uint16_t>> m_switchGroups;

  bool m_debug = false;
  char* m_rom;
  char* m_serial;
  uint8_t m_platform;
  uint8_t m_coinDoorClosedSwitch;
  uint8_t m_gameOnSolenoid;
  uint32_t m_switchReplyDelayUs = 0;
  uint32_t m_switchRefreshIdleMs = 0;
  uint8_t m_coilHoldFrames = 3;
  bool m_disableFastFlipForTests = false;
  bool m_forceHardReset = false;
  std::set<uint8_t> m_skippedBoards;

  void SendLedConfigBlock(const YAML::Node& items, uint32_t type, uint8_t board,
                          uint32_t port);
  bool AbortConfigurationEarly() const;
};
