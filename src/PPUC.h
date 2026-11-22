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

#include <functional>
#include <set>
#include <string>
#include <unordered_map>

class RS485Comm;


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
  // A switch number the host asserts while the machine is tilted, so boards can
  // inhibit their fast-flip outputs locally. Must be set before Connect(); it is
  // sent as a config topic during session setup. Zero disables the feature.
  void SetTiltSwitch(uint8_t number) { m_tiltSwitch = number; }
  uint8_t GetTiltSwitch() const { return m_tiltSwitch; }
  void SetSwitchReplyDelayUs(uint32_t delayUs);
  // Polls a board answers "no change" before it re-reads its switches and
  // restarts its switch reader on its own. 0 turns that off; boards default to
  // 100. Sent with the switch chain configuration.
  void SetSwitchIdleRefreshPolls(uint32_t polls);
  // What the host currently believes a switch is, rather than what it last
  // changed to. Needed wherever resting state matters - a closed coin door,
  // balls already in the trough - because a machine that is not moving reports
  // nothing at all.
  bool IsSwitchClosed(int number);

  // Asks the boards for a full switch state on the next poll cycle instead of
  // waiting for the periodic one.
  void RequestSwitchRefresh();

  void SetSwitchRefreshIdleMs(uint32_t idleMs);
  void SetOutputFrameIntervalMs(uint32_t intervalMs);
  void SetCoilHoldFrames(uint8_t holdFrames);
  void SetDisableFastFlipForTests(bool disableFastFlipForTests);
  void SetForceHardReset(bool forceHardReset);
  bool GetDebug();
  // The ppuc::v2::BoardType the configuration gives a board, or 0 when the
  // configuration does not list it. Compare with what the board itself reports
  // in QueryBoardVersions(): a pin map is board-specific, so a configuration
  // written for one type must not be sent to another.
  uint8_t GetConfiguredBoardType(uint8_t board);
  void SetRom(const char* rom);
  const char* GetRom();
  void SetSerial(const char* serial);
  const char* GetSerial();
  // Runs once the boards have been reset and have booted, before any of them
  // is configured. Returning false stops Connect(), which then returns false
  // with WasStoppedBeforeConfiguration() set.
  //
  // This is where firmware is checked and updated. Doing it after
  // configuration meant a board that could not be configured could not be
  // updated either - so firmware too old or too broken to accept the
  // configuration was the one firmware the update could never replace. The
  // admin protocol needs no session, and nothing else is on the bus yet.
  void SetBeforeConfigurationHook(std::function<bool()> hook);
  bool WasStoppedBeforeConfiguration() const { return m_stoppedBeforeConfiguration; }

  bool Connect();
  void Disconnect();
  void StartUpdates();
  void StopUpdates();

  void SetSolenoidState(int number, int state);
  void SetLampState(int number, int state);
  // Motors whose speed the ROM sets by pulsing them. The host measures it and
  // passes it on with SetCoilPowerScale.
  std::vector<uint16_t> GetRomSpeedCoils() const;
  // Scales a running motor's power, 0-255 of what it is configured with.
  void SetCoilPowerScale(int number, uint8_t scale);
  // Board-controlled flippers (`boardFlippers: true`): the flipper windings
  // follow their buttons on the board alone, between SetFlippersEnabled(true)
  // at game start and SetFlippersEnabled(false) at game over. The ROM's flipper
  // pulses and the game-on solenoid do not reach them.
  // The version and the commit this library was built from, for the log.
  static const char* GetLibraryVersion();
  static const char* GetLibraryCommit();
  bool UsesBoardFlippers() const { return !m_boardFlipperCoils.empty(); }
  const std::vector<uint16_t>& GetBoardFlipperCoils() const {
    return m_boardFlipperCoils;
  }
  bool IsBoardFlipperCoil(uint16_t number) const;
  void SetFlippersEnabled(bool enabled);
  void SetGIState(int string, int brightness);

  // GI color programs, for custom games whose modes have colors. The boards
  // render the program on the WS2812 LEDs of the string and keep scaling it
  // with SetGIState(); it is sent once per change (one 25-byte frame), and
  // again after a session resync. Needs io-boards firmware 0.5.9 on every
  // board, and a GI string wired as one LED segment.
  //
  // colors are 0xRRGGBB, highest priority first; past four only the first
  // four are used. effect is a PPUCGiEffect. periodMs is the time per color
  // for cycle and fade. These mean the configured colors again (the
  // default): PPUC_GI_EFFECT_DEFAULT, an empty color list, or "static" with a
  // color that some LED of the string is configured with - which is what a
  // game says when no colored mode runs. One color is static whatever the
  // effect, and so is a period of 0.
  //
  // False when the string or effect is out of range, or the boards' firmware
  // does not know the command (logged once); the GI then keeps its colors.
  bool SetGiColorProgram(int string, const std::vector<uint32_t>& colors,
                         uint8_t effect, uint16_t periodMs);
  // What SetGiColorProgram() makes of its arguments. False if out of range.
  bool ResolveGiColorProgram(int string, const std::vector<uint32_t>& colors,
                             uint8_t effect, uint16_t periodMs,
                             PPUCGiProgram& out) const;
  // "default", "static", "cycle", "fade"; -1 for anything else.
  static int GiEffectFromName(const std::string& name);
  // The colors the GI LEDs of a string are configured with, 0xRRGGBB.
  const std::set<uint32_t>& GetConfiguredGiColors(int string) const;
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
  // Asks every configured, non-skipped board for its firmware version.
  //
  // waitForBoardsMs keeps asking the boards that have not answered until they
  // all have or the time is up. Needed when the boards have only just been
  // reset: they take a moment longer than the reset wait to come back, and a
  // single round then finds none of them - which reads exactly like boards
  // without the admin protocol, and silently prevents every update.
  std::vector<PPUCBoardVersion> QueryBoardVersions(uint32_t waitForBoardsMs = 0);

  // Each board's own transport counters, for diagnosing a board that stopped
  // answering. Safe to call at any time; it is an out-of-band admin exchange.
  // settleBus: wait out the line and flush it first, which is right after
  // configuration. Pass false while the poll loop runs: the flush is not under
  // the port lock and could take a switch reply the chain is still reading.
  std::vector<PPUCBoardStats> QueryBoardStats(bool settleBus = true);
  // Every configured board's own counters, see PPUCBoardDiagnostics. A board
  // with firmware before 0.5.7 comes back with responded false.
  std::vector<PPUCBoardDiagnostics> QueryBoardDiagnostics(bool settleBus = true);
  // A PPUCBoardDiagnostics::values index in words, for the log.
  static const char* DiagnosticsFieldName(uint8_t field);
  // PPUCBoardStats::resetCause in words, for the log.
  static const char* ResetCauseName(uint8_t resetCause);

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
  std::vector<uint16_t> m_boardFlipperCoils;
  std::set<uint32_t> m_configuredGiColors[5];
  bool m_giProgramsRefusedLogged = false;
  std::unordered_map<std::string, std::vector<uint16_t>> m_switchGroups;

  bool m_debug = false;
  std::string m_rom;
  std::string m_serial;
  uint8_t m_platform;
  uint8_t m_coinDoorClosedSwitch;
  // Logs why each board last started, read before startup restarts them.
  void LogBoardLastStarts();
  uint8_t m_gameOnSolenoid;
  uint8_t m_tiltSwitch = 0;
  uint32_t m_switchReplyDelayUs = 0;
  uint32_t m_switchIdleRefreshPolls = 100;
  uint32_t m_switchRefreshIdleMs = 0;
  uint8_t m_coilHoldFrames = 3;
  bool m_disableFastFlipForTests = false;
  bool m_forceHardReset = false;
  std::function<bool()> m_beforeConfigurationHook;
  bool m_stoppedBeforeConfiguration = false;
  std::set<uint8_t> m_skippedBoards;

  void SendLedConfigBlock(const YAML::Node& items, uint32_t type, uint8_t board,
                          uint32_t port);
  bool AbortConfigurationEarly() const;
};
