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
#define RS485_COMM_OUTPUT_QUEUE_SIZE_MAX 256
#define RS485_COMM_MAX_EVENTS_TO_SEND 32
#define RS485_COMM_OUTPUT_FRAME_INTERVAL_MS 4
#define RS485_COMM_EFFECT_EVENT_SPACING_US 1000
#define RS485_COMM_SWITCH_REPLY_MISS_THRESHOLD 3
#define RS485_COMM_SWITCH_POLL_STARTUP_HOLD_MS 250
#define RS485_COMM_CONFIG_ACK_TIMEOUT_US 50000
#define RS485_COMM_CONFIG_ACK_RETRIES 3
#define RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD 10

struct VirtualSwitchBoardState {
  uint8_t board = ppuc::v2::kNoBoard;
  std::vector<uint16_t> switchNumbers;
  std::vector<uint8_t> switchStates;
  bool dirty = false;
};

struct QueuedOutputSnapshot {
  uint8_t coilBitmap[ppuc::v2::kMaxCoilBytes] = {0};
  uint8_t lampBitmap[ppuc::v2::kMaxLampBytes] = {0};
  uint8_t giLevels[ppuc::v2::kGiStrings] = {0};
};

class RS485Comm {
 public:
  RS485Comm();
  ~RS485Comm();

  void SetLogMessageCallback(PPUC_LogMessageCallback callback,
                             const void* userData);

  bool Connect(const char* device);
  void Disconnect();
  bool RestartBoards();
  bool ResetBoards();

  void Run();

  void QueueEvent(Event* event);
  bool SendConfigEvent(ConfigEvent* configEvent);
  void SetRuntimeConfig(const ppuc::v2::RuntimeConfig& config);
  bool SendSetupFrame();
  bool SendResetFrame();
  bool SendRestartFrame();
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
  bool HadConfigurationFailure() const;
  bool ShouldAbortConfigurationEarly() const;
  std::vector<uint8_t> GetMissingConfiguredBoards() const;

  void RegisterSwitchBoard(uint8_t number);
  PPUCSwitchState* GetNextSwitchState();
  uint32_t GetCleanSwitchReplyChainCount() const;
  bool IsBoardActive(uint8_t number) const;
  bool SetVirtualSwitchState(uint16_t number, uint8_t state);
  bool IsSwitchVirtualized(uint16_t number) const;

  void SetDebug(bool debug);
  void SetDebugErrors(bool debugErrors);
  void SetSwitchReplyDelayUs(uint32_t delayUs);

 private:
  void LogMessage(const char* format, ...);

  bool SendEvent(Event* event);
  Event* receiveEvent();
  void PollEvents(int board);
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
  void ApplySwitchBitmapDiff(uint8_t board, const uint8_t* bitmap, size_t bytes);
  void RebuildSwitchOwnershipMasks();
  void EnsureConfiguredBoardPresenceKnown();
  bool SendMappingFrame(uint8_t domain, uint16_t index, uint16_t number);
  bool SendOutputStateFrameFromBuffers(uint8_t nextBoard, const uint8_t* coils,
                                       const uint8_t* lamps,
                                       const uint8_t* giLevels);
  bool WriteBytes(const char* context, const uint8_t* buffer, size_t size);
  void ClearQueuedEvents();
  void ClearQueuedOutputSnapshots();
  void ClearOutputState();
  void QueueOutputSnapshotLocked();
  bool SendOutputsOffFrame();
  void DebugPrintf(const char* format, ...);
  void ErrorPrintf(const char* format, ...);
  int64_t SwitchReplyWindowUs() const;
  uint32_t SwitchReadTimeoutMs() const;

  PPUC_LogMessageCallback m_logMessageCallback = nullptr;
  const void* m_logMessageUserData = nullptr;

  uint8_t m_switchBoards[RS485_COMM_MAX_BOARDS];
  uint8_t m_switchBoardCounter = 0;  // Number of registered switch boards.
  uint8_t m_switchBoardIndex = 0;
  std::vector<uint8_t> m_configuredBoards;
  std::set<uint8_t> m_presentBoards;
  std::set<uint8_t> m_skippedBoards;
  std::unordered_map<uint8_t, std::vector<uint16_t>> m_switchNumbersByBoard;
  uint8_t m_switchOwnershipMaskByBoard[RS485_COMM_MAX_BOARDS]
                                      [ppuc::v2::kMaxSwitchBytes] = {{0}};
  std::unordered_map<uint8_t, VirtualSwitchBoardState> m_virtualSwitchBoards;
  std::unordered_map<uint16_t, uint8_t> m_virtualSwitchOwnerByNumber;
  bool m_activeBoards[RS485_COMM_MAX_BOARDS] = {false};

  bool m_debug = false;
  bool m_debugErrors = false;
  bool m_runtimeEnabled = true;
  uint8_t m_sequence = 0;
  uint8_t m_epoch = 1;
  uint8_t m_lastOutputSequenceSent = 0;
  bool m_needSessionResync = false;
  uint8_t m_switchReplyMisses = 0;
  uint32_t m_switchReplyDelayUs = 0;
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

  // Event message buffers, we need two independent for events and config events
  // because of threading.
  uint8_t m_msg[7];
  uint8_t m_cmsg[12];

  struct sp_port* m_pSerialPort;
  struct sp_port_config* m_pSerialPortConfig;
  std::thread* m_pThread;
  std::queue<Event*> m_events;
  std::queue<QueuedOutputSnapshot> m_outputSnapshots;
  std::queue<PPUCSwitchState*> m_switches;
  std::mutex m_eventQueueMutex;
  std::mutex m_outputQueueMutex;
  std::mutex m_switchesQueueMutex;
  std::mutex m_stateMutex;
  std::atomic<bool> m_stopRequested{false};
  std::atomic<uint32_t> m_cleanSwitchReplyChainCount{0};
  bool m_configFailed = false;
  bool m_configEarlyAbortLogged = false;
  uint8_t m_initialConfigAckMissStreak = 0;
  uint8_t m_initialConfigAckMissesByBoard[RS485_COMM_MAX_BOARDS] = {0};
  uint8_t m_configEarlyAbortBoard = ppuc::v2::kNoBoard;
  std::set<uint8_t> m_configAckFailedBoards;
  std::chrono::steady_clock::time_point m_nextSwitchPollAt;
  bool m_boardPresenceFinalized = false;
};
