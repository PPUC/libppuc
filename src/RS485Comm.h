#pragma once

#include "PPUC_structs.h"

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

static constexpr uint32_t RS485_COMM_BAUD_RATE = ppuc::v2::kBaudRate;
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
static constexpr uint32_t RS485_COMM_DEFAULT_OUTPUT_FRAME_INTERVAL_MS = 4;
#define RS485_COMM_EFFECT_EVENT_SPACING_US 1000
#define RS485_COMM_SWITCH_REPLY_MISS_THRESHOLD 3
#define RS485_COMM_SWITCH_POLL_STARTUP_HOLD_MS 250
// A board carrying only slow switches - start button, coin door, tilt - is
// polled on every Nth chain instead of every chain. At the default 4 ms output
// cadence that is roughly every 32 ms, far below what a hand can produce, and
// it shortens every other cycle by that board's reply time.
#define RS485_COMM_SLOW_SWITCH_POLL_DIVIDER 8
#define RS485_COMM_CONFIG_ACK_TIMEOUT_US 50000
#define RS485_COMM_CONFIG_ACK_RETRIES 3
// A version query is a single frame answered by a single board, with nothing
// arbitrating the wire. One lost reply used to mean "no firmware version", so
// a board that is present, configured and answering everything else still got
// reported as unknown. Retried like a config frame, for the same reason.
#define RS485_COMM_VERSION_QUERY_ATTEMPTS 3
// How long to spend assembling one admin frame once a sync byte is seen,
// bounded independently of the query timeout so a false sync costs a slice
// rather than the whole attempt.
#define RS485_COMM_ADMIN_FRAME_ASSEMBLY_MS 50
// How long to let the bus fall quiet before the first admin query of a batch.
#define RS485_COMM_ADMIN_SETTLE_MS 60
// Must cover a first-time LittleFS format on the board, not just a reply.
#define RS485_COMM_UPDATE_BEGIN_TIMEOUT_MS 30000
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

  // Classes of unexpected condition, each rate limited independently so one
  // noisy fault cannot bury the others.
  enum class Anomaly : uint8_t {
    SerialWrite,       // the port rejected or truncated a write
    FrameCrc,          // a frame arrived corrupt
    ConfigAck,         // a config frame was unacknowledged, late or unexpected
    SwitchChainMiss,   // a switch reply chain did not complete
    EpochMismatch,     // a board is answering for a previous session
    BoardStatus,       // a board reported a status flag worth knowing about
    QueueOverflow,     // host-side output snapshots were dropped
    SessionResync,     // the host restarted the session
    UnmappedDevice,    // an output was addressed that no board claims
    Count
  };

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
  bool SendSwitchRefreshFrame(uint8_t nextBoard);
  void SetMappings(const std::vector<uint16_t>& coils,
                   const std::vector<uint16_t>& lamps,
                   const std::vector<uint16_t>& switches);
  bool SendMappingFrames();
  void SetConfiguredBoards(const std::vector<uint8_t>& boards);
  void SetSwitchNumbersByBoard(
      const std::unordered_map<uint8_t, std::vector<uint16_t>>& switchesByBoard);
  void SetSkippedBoards(const std::set<uint8_t>& boards);
  void AddSkippedBoard(uint8_t board);
  void SetButtonSwitchNumbers(const std::set<uint16_t>& numbers);
  void FinalizeConfiguredBoardPresence();
  bool IsBoardPresent(uint8_t board) const;
  bool IsBoardVirtualized(uint8_t board) const;
  // `boards` is the token-ring order. The first `slowPrefixCount` entries are
  // boards with no latency-critical switches; the poll loop skips that prefix
  // on most cycles. See SetSlowSwitchPollDivider().
  void SetActiveSwitchBoards(const std::vector<uint8_t>& boards,
                             uint8_t slowPrefixCount = 0);
  void SetSlowSwitchPollDivider(uint8_t divider);

  // Where in the token ring this cycle starts: 0 for the whole chain, or
  // `slowCount` to skip the slow prefix. Pure, and public, so the rule can be
  // tested without a serial port - it decides how often a start button is
  // looked at, which is not something to leave unverified.
  static uint8_t SwitchChainEntryIndex(uint8_t slowCount, uint8_t boardCount,
                                       uint8_t divider, uint32_t cycle);
  bool HadConfigurationFailure() const;
  bool ShouldAbortConfigurationEarly() const;
  std::vector<uint8_t> GetMissingConfiguredBoards() const;

  void RegisterSwitchBoard(uint8_t number);
  PPUCSwitchState* GetNextSwitchState();
  uint32_t GetCleanSwitchReplyChainCount() const;
  PPUCBusHealth GetBusHealth() const;

  // Asks one board what it is running. Polls a single board rather than
  // broadcasting: administration happens outside the switch chain, so nothing
  // arbitrates who replies.
  // Waits out any traffic still in flight from configuration, so the first
  // admin query does not spend its window on a stale byte. Call once before a
  // batch of queries, not per query.
  void SettleBusBeforeAdmin();

  PPUCBoardVersion QueryBoardVersion(uint8_t board, uint32_t timeoutMs = 250);

  // Reads one board's transport counters. Diagnostics: run it after a test to
  // see whether a board that never answered had seen the frame at all.
  PPUCBoardStats QueryBoardStats(uint8_t board, uint32_t timeoutMs = 250);

  // Sends a firmware image to one board and asks it to install.
  //
  // Synchronous and chunk-at-a-time: every chunk is acknowledged before the
  // next is sent. Slower than streaming, but a board that falls behind stops
  // the transfer instead of silently losing the middle of its own firmware.
  PPUCFirmwareUpdateResult UpdateBoardFirmware(
      uint8_t board, uint8_t imageBoardType, const uint8_t* image,
      size_t imageBytes, PPUC_FirmwareProgressCallback progress,
      void* progressUserData);

 private:
  // Waits for one admin reply with the given command. Returns false on
  // timeout or if the board reports a different command.
  bool AwaitAdminReply(uint8_t board, uint8_t expectedCommand, uint8_t* status,
                       uint32_t* offset, uint32_t timeoutMs);

 public:
  std::vector<std::string> GetRecentAnomalies() const;
  bool IsBoardActive(uint8_t number) const;
  bool SetVirtualSwitchState(uint16_t number, uint8_t state);
  bool IsSwitchVirtualized(uint16_t number) const;

  void SetDebug(bool debug);
  void SetDebugErrors(bool debugErrors);
  void SetSwitchReplyDelayUs(uint32_t delayUs);
  void SetSwitchRefreshIdleMs(uint32_t idleMs);
  void SetOutputFrameIntervalMs(uint32_t intervalMs);
  void SetCoilHoldFrames(uint8_t holdFrames);

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
  // Reports an output number the configuration does not map, once per number.
  enum class DeviceDomain : uint8_t { Coil, Lamp, GiString };
  void ReportUnmappedDevice(DeviceDomain domain, uint16_t number);
  void ApplySwitchBitmapDiff(uint8_t board, const uint8_t* bitmap, size_t bytes);
  void RebuildSwitchOwnershipMasks();
  void EnsureConfiguredBoardPresenceKnown();
  bool SendMappingFrame(uint8_t domain, uint16_t index, uint16_t number);
  bool SendOutputStateFrameFromBuffers(uint8_t nextBoard, const uint8_t* coils,
                                       const uint8_t* lamps,
                                       const uint8_t* giLevels);
  void NoteSwitchActivity(uint16_t switchNumber);
  void ApplyCoilHoldover(uint8_t* coils, const uint8_t* holdFrames) const;
  void ConsumeCoilHoldoverLocked(const uint8_t* holdFrames);
  bool WriteBytes(const char* context, const uint8_t* buffer, size_t size);
  void ClearQueuedEvents();
  void ClearQueuedOutputSnapshots();
  void ClearOutputState();
  void QueueOutputSnapshotLocked();
  bool SendOutputsOffFrame();
  void DebugPrintf(const char* format, ...);
  int64_t SwitchReplyWindowUs() const;
  uint32_t SwitchReadTimeoutMs() const;

  PPUC_LogMessageCallback m_logMessageCallback = nullptr;
  const void* m_logMessageUserData = nullptr;

  // Numbers already reported as unmapped, so a lamp the ROM drives every frame
  // is named once rather than every frame. Capped: the set exists to diagnose a
  // handful of mistyped numbers, not to absorb a runaway source.
  static constexpr size_t kMaxReportedUnmappedDevices = 32;
  std::set<uint32_t> m_reportedUnmappedDevices;
  bool m_unmappedDeviceListFull = false;
  std::mutex m_unmappedDeviceMutex;

  uint8_t m_switchBoards[RS485_COMM_MAX_BOARDS];
  uint8_t m_switchBoardCounter = 0;  // Number of registered switch boards.
  // How many leading entries of m_switchBoards carry only slow switches, and
  // how often the chain is nevertheless started at the front to include them.
  uint8_t m_slowSwitchBoardCount = 0;
  uint8_t m_slowSwitchPollDivider = RS485_COMM_SLOW_SWITCH_POLL_DIVIDER;
  uint32_t m_switchPollCycle = 0;
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
  uint8_t m_coilHoldFrameCount = 3;
  uint8_t m_sequence = 0;
  uint8_t m_epoch = 1;
  uint8_t m_lastOutputSequenceSent = 0;
  bool m_needSessionResync = false;
  uint8_t m_switchReplyMisses = 0;
  uint32_t m_switchReplyDelayUs = 0;
  uint32_t m_switchRefreshIdleMs = 0;
  uint32_t m_outputFrameIntervalMs = RS485_COMM_DEFAULT_OUTPUT_FRAME_INTERVAL_MS;
  ppuc::v2::RuntimeConfig m_runtimeConfig;
  std::vector<uint16_t> m_coilIndexToNumber;
  std::vector<uint16_t> m_lampIndexToNumber;
  std::vector<uint16_t> m_switchIndexToNumber;
  std::unordered_map<uint16_t, uint16_t> m_coilNumberToIndex;
  std::unordered_map<uint16_t, uint16_t> m_lampNumberToIndex;
  std::unordered_map<uint16_t, uint16_t> m_switchNumberToIndex;
  std::set<uint16_t> m_buttonSwitchNumbers;

  uint8_t m_coilBitmap[ppuc::v2::kMaxCoilBytes] = {0};
  uint8_t m_coilHoldFrames[ppuc::v2::kMaxCoilBits] = {0};
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
  // Guards the serial port. The poll thread holds it for one pass; admin
  // exchanges hold it for their duration. Recursive because a firmware update
  // re-reads the board version while already holding it.
  std::recursive_mutex m_portMutex;
  std::atomic<uint32_t> m_boardsLostConfigurationCount { 0 };
  std::mutex m_outputQueueMutex;
  std::mutex m_switchesQueueMutex;
  std::mutex m_stateMutex;
  std::atomic<bool> m_stopRequested{false};
  // Reports an unexpected condition. Always emitted, never behind a debug
  // flag: a fault that only shows up when tracing is enabled is a fault
  // nobody sees in the field, and enabling tracing changes the timing being
  // diagnosed. Rate limited per kind - see the implementation.
  void ReportAnomaly(Anomaly kind, const char* format, ...);
  // Bypasses the per-kind rate limiter, for callers that deduplicate more
  // precisely themselves.
  void ReportAnomalyOnce(Anomaly kind, const char* format, ...);
  void ReportAnomalyV(Anomaly kind, bool rateLimit, const char* format,
                      va_list args);

  struct AnomalyState {
    std::atomic<uint32_t> total{0};        // lifetime occurrences
    uint32_t suppressed = 0;               // since the last line recorded
    std::chrono::steady_clock::time_point lastRecord{};
    bool everRecorded = false;
  };

  // The most recent anomalies, kept in RAM.
  //
  // ppuc-pinmame runs on a Raspberry Pi with a read-only root and no console -
  // an attached monitor is showing the game, not a terminal - so there is
  // nowhere to write a log and nobody to read stdout. Printing by default
  // would be work done on the bus thread for an audience of nobody.
  //
  // Recording costs a memcpy into a fixed ring, needs no filesystem, and means
  // an intermittent fault during an event leaves something to find afterwards
  // over ssh instead of having to be reproduced.
  static constexpr size_t kAnomalyLogSize = 32;
  struct AnomalyLogEntry {
    char text[176] = {0};
    int64_t wallMs = 0;
  };
  AnomalyState m_anomalies[static_cast<size_t>(Anomaly::Count)];
  AnomalyLogEntry m_anomalyLog[kAnomalyLogSize];
  size_t m_anomalyLogHead = 0;   // next slot to write
  size_t m_anomalyLogCount = 0;  // entries held, saturates at kAnomalyLogSize
  // Config frames are written from the startup thread while the bus thread is
  // writing too, so both can report. Guards the non-atomic members above.
  std::mutex m_anomalyMutex;

  std::atomic<uint32_t> m_cleanSwitchReplyChainCount{0};
  // Lifetime tallies behind PPUCBusHealth. Separate from the consecutive
  // streaks above, which reset on every success and so cannot show an
  // intermittent fault.
  std::atomic<uint32_t> m_switchReplyChainCount{0};
  std::atomic<uint32_t> m_switchReplyMissCount{0};
  std::atomic<uint32_t> m_sessionResyncCount{0};
  std::atomic<uint32_t> m_configAckRetryCount{0};
  std::atomic<uint32_t> m_configAckTimeoutCount{0};
  bool m_configFailed = false;
  bool m_configEarlyAbortLogged = false;
  uint8_t m_initialConfigAckMissStreak = 0;
  uint8_t m_initialConfigAckMissesByBoard[RS485_COMM_MAX_BOARDS] = {0};
  uint8_t m_configEarlyAbortBoard = ppuc::v2::kNoBoard;
  std::set<uint8_t> m_configAckFailedBoards;
  std::chrono::steady_clock::time_point m_nextSwitchPollAt;
  std::chrono::steady_clock::time_point m_nextSwitchRefreshAt;
  bool m_boardPresenceFinalized = false;
};
