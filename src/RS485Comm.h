#pragma once

#include <inttypes.h>
#include <stdarg.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "io-boards/PPUCProtocolV2.h"
#include "PPUC_structs.h"
#include "io-boards/Event.h"
#include "libserialport.h"

#if _MSC_VER
#define CALLBACK __stdcall
#else
#define CALLBACK
#endif

#define RS485_COMM_BAUD_RATE 115200
#define RS485_COMM_SERIAL_READ_TIMEOUT 5
#define RS485_COMM_SERIAL_WRITE_TIMEOUT 20

#define RS485_COMM_MAX_BOARDS 8

#if _MSC_VER
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#elif defined(__APPLE__)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#else
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#endif

#define RS485_COMM_QUEUE_SIZE_MAX 128
#define RS485_COMM_SWITCH_POLL_INTERVAL_MS 3
#define RS485_COMM_OUTPUT_FRAME_INTERVAL_MS 4
#define RS485_COMM_SWITCH_REPLY_MISS_THRESHOLD 10
#define RS485_COMM_RESYNC_COOLDOWN_MS 5000
#define RS485_COMM_CONFIG_ACK_TIMEOUT_US 50000
#define RS485_COMM_CONFIG_ACK_RETRIES 3

struct VirtualSwitchBoardState {
  uint8_t board = ppuc::v2::kNoBoard;
  std::vector<uint16_t> switchNumbers;
  std::vector<uint8_t> switchStates;
  bool dirty = false;
};

class RS485Comm {
 public:
  RS485Comm();
  ~RS485Comm();

  void SetLogMessageCallback(PPUC_LogMessageCallback callback,
                             const void* userData);

  bool Connect(const char* device);
  void Disconnect();

  void Run();

  void QueueEvent(Event* event);
  bool SendConfigEvent(ConfigEvent* configEvent);
  void SetRuntimeConfig(const ppuc::v2::RuntimeConfig& config);
  bool SendSetupFrame();
  bool SendResetFrame();
  void SetMappings(const std::vector<uint16_t>& coils,
                   const std::vector<uint16_t>& lamps,
                   const std::vector<uint16_t>& switches);
  bool SendMappingFrames();
  void SetConfiguredBoards(const std::vector<uint8_t>& boards);
  void SetSwitchNumbersByBoard(
      const std::unordered_map<uint8_t, std::vector<uint16_t>>& switchesByBoard);
  void SetSkippedBoards(const std::set<uint8_t>& boards);
  void FinalizeConfiguredBoardPresence();
  bool IsBoardPresent(uint8_t board) const;
  bool IsBoardVirtualized(uint8_t board) const;
  void SetActiveSwitchBoards(const std::vector<uint8_t>& boards);

  void RegisterSwitchBoard(uint8_t number);
  PPUCSwitchState* GetNextSwitchState();
  bool SetVirtualSwitchState(uint16_t number, uint8_t state);
  bool IsSwitchVirtualized(uint16_t number) const;

  void SetDebug(bool debug);
  void SetDebugErrors(bool debugErrors);

 private:
  void LogMessage(const char* format, ...);

  bool ResyncSession();
  bool SendOutputStateFrame(uint8_t nextBoard);
  bool ReceiveConfigAck(uint8_t boardId, uint8_t topic, uint8_t index,
                        uint8_t key);
  bool ReceiveSwitchStateFrame(uint8_t expectedBoard, uint8_t* outNextBoard,
                               bool* outHadState);
  bool SendVirtualSwitchReply(uint8_t board, uint8_t nextBoard,
                              bool* outHadState);
  uint8_t GetLogicalNextSwitchBoard(uint8_t board) const;
  void ReceiveSwitchStateChain(uint8_t firstBoard);
  void ApplySwitchBitmapDiff(const uint8_t* bitmap, size_t bytes);
  void EnsureConfiguredBoardPresenceKnown();
  bool SendMappingFrame(uint8_t domain, uint16_t index, uint16_t number);
  bool WriteBytes(const char* context, const uint8_t* buffer, size_t size);
  void DebugPrintf(const char* format, ...);
  void ErrorPrintf(const char* format, ...);

  PPUC_LogMessageCallback m_logMessageCallback = nullptr;
  const void* m_logMessageUserData = nullptr;

  uint8_t m_switchBoards[RS485_COMM_MAX_BOARDS];
  uint8_t m_switchBoardCounter = 0;  // Number of registered switch boards.
  uint8_t m_switchBoardIndex = 0;
  std::vector<uint8_t> m_configuredBoards;
  std::set<uint8_t> m_presentBoards;
  std::set<uint8_t> m_skippedBoards;
  std::unordered_map<uint8_t, std::vector<uint16_t>> m_switchNumbersByBoard;
  std::unordered_map<uint8_t, VirtualSwitchBoardState> m_virtualSwitchBoards;
  std::unordered_map<uint16_t, uint8_t> m_virtualSwitchOwnerByNumber;

  bool m_debug = false;
  bool m_debugErrors = false;
  bool m_runtimeEnabled = true;
  uint8_t m_sequence = 0;
  uint8_t m_epoch = 1;
  uint8_t m_lastOutputSequenceSent = 0;
  bool m_needSessionResync = false;
  uint8_t m_switchReplyMisses = 0;
  ppuc::v2::RuntimeConfig m_runtimeConfig;
  std::vector<uint16_t> m_coilIndexToNumber;
  std::vector<uint16_t> m_lampIndexToNumber;
  std::vector<uint16_t> m_switchIndexToNumber;
  std::unordered_map<uint16_t, uint16_t> m_coilNumberToIndex;
  std::unordered_map<uint16_t, uint16_t> m_lampNumberToIndex;
  std::unordered_map<uint16_t, uint16_t> m_switchNumberToIndex;

  uint8_t m_coilBitmap[ppuc::v2::kMaxCoilBytes] = {0};
  uint8_t m_lampBitmap[ppuc::v2::kMaxLampBytes] = {0};
  uint8_t m_giLevels[ppuc::v2::kGiStrings] = {0};
  uint8_t m_switchBitmap[ppuc::v2::kMaxSwitchBytes] = {0};

  struct sp_port* m_pSerialPort;
  struct sp_port_config* m_pSerialPortConfig;
  std::thread* m_pThread;
  std::queue<PPUCSwitchState*> m_switches;
  std::mutex m_switchesQueueMutex;
  std::mutex m_stateMutex;
  std::atomic<bool> m_stopRequested{false};
  std::chrono::steady_clock::time_point m_nextOutputFrameAt;
  std::chrono::steady_clock::time_point m_nextSwitchPollAt;
  std::chrono::steady_clock::time_point m_nextAllowedResyncAt;
  bool m_boardPresenceFinalized = false;
};
