#pragma once

#include <inttypes.h>
#include <stdarg.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
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

#define RS485_COMM_BAUD_RATE 250000
#define RS485_COMM_SERIAL_READ_TIMEOUT 2
#define RS485_COMM_SERIAL_WRITE_TIMEOUT 4

#define RS485_COMM_MAX_BOARDS 8

#if _MSC_VER
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#elif defined(__APPLE__)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#else
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#endif

#define RS485_COMM_QUEUE_SIZE_MAX 128
#define RS485_COMM_MAX_EVENTS_TO_SEND 32

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

  void RegisterSwitchBoard(uint8_t number);
  PPUCSwitchState* GetNextSwitchState();

  void SetDebug(bool debug);

 private:
  void LogMessage(const char* format, ...);

  bool SendEvent(Event* event);
  Event* receiveEvent();
  void PollEvents(int board);
  bool SendOutputStateFrame(uint8_t nextBoard);
  bool ReceiveSwitchStateFrame(uint8_t expectedBoard);
  void ApplySwitchBitmapDiff(const uint8_t* bitmap, size_t bytes);
  bool SendMappingFrame(uint8_t domain, uint16_t index, uint16_t number);

  PPUC_LogMessageCallback m_logMessageCallback = nullptr;
  const void* m_logMessageUserData = nullptr;

  uint8_t m_switchBoards[RS485_COMM_MAX_BOARDS];
  uint8_t m_switchBoardCounter = 0;  // Number of registered switch boards.
  uint8_t m_switchBoardIndex = 0;
  bool m_activeBoards[RS485_COMM_MAX_BOARDS] = {false};

  bool m_debug = false;
  bool m_runtimeEnabled = true;
  uint8_t m_sequence = 0;
  ppuc::v2::RuntimeConfig m_runtimeConfig;
  std::vector<uint16_t> m_coilIndexToNumber;
  std::vector<uint16_t> m_lampIndexToNumber;
  std::vector<uint16_t> m_switchIndexToNumber;
  std::unordered_map<uint16_t, uint16_t> m_coilNumberToIndex;
  std::unordered_map<uint16_t, uint16_t> m_lampNumberToIndex;
  std::unordered_map<uint16_t, uint16_t> m_switchNumberToIndex;

  uint8_t m_coilBitmap[ppuc::v2::kMaxCoilBytes] = {0};
  uint8_t m_lampBitmap[ppuc::v2::kMaxLampBytes] = {0};
  uint8_t m_switchBitmap[ppuc::v2::kMaxSwitchBytes] = {0};

  // Event message buffers, we need two independent for events and config events
  // because of threading.
  uint8_t m_msg[7];
  uint8_t m_cmsg[12];

  struct sp_port* m_pSerialPort;
  struct sp_port_config* m_pSerialPortConfig;
  std::thread* m_pThread;
  std::queue<Event*> m_events;
  std::queue<PPUCSwitchState*> m_switches;
  std::mutex m_eventQueueMutex;
  std::mutex m_switchesQueueMutex;
  std::mutex m_stateMutex;
};
