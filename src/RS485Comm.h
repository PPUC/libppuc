#pragma once

#include <thread>
#include <queue>
#include <string>
#include <inttypes.h>
#include <mutex>
#include <stdio.h>
#include <stdarg.h>
#include "PPUC.h"
#include "io-boards/Event.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !((defined(__APPLE__) && ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_TV) && TARGET_OS_TV))) || defined(__ANDROID__))
#include "libserialport.h"
#endif

#if _MSC_VER
#define CALLBACK __stdcall
#else
#define CALLBACK
#endif

#define RS485_COMM_BAUD_RATE 115200
#define RS485_COMM_SERIAL_READ_TIMEOUT 8
#define RS485_COMM_SERIAL_WRITE_TIMEOUT 8

#define RS485_COMM_MAX_BOARDS 16

#if _MSC_VER
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#elif defined(__APPLE__)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#else
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#endif

#define RS485_COMM_QUEUE_SIZE_MAX 128

typedef void(CALLBACK *PPUC_LogMessageCallback)(const char *format, va_list args, const void *userData);

class RS485Comm
{
public:
   RS485Comm();
   ~RS485Comm();

   void SetLogMessageCallback(PPUC_LogMessageCallback callback, const void *userData);

   bool Connect(const char *device);
   void Disconnect();

   void Run();

   void QueueEvent(Event *event);
   bool SendConfigEvent(ConfigEvent *configEvent);

   void RegisterSwitchBoard(uint8_t number);
   PPUCSwitchState *GetNextSwitchState();

   void SetDebug(bool debug);

private:
   void LogMessage(const char *format, ...);

   bool SendEvent(Event *event);
   Event *receiveEvent();
   void PollEvents(int board);

   PPUC_LogMessageCallback m_logMessageCallback = nullptr;
   const void *m_logMessageUserData = nullptr;

   uint8_t m_switchBoards[RS485_COMM_MAX_BOARDS];
   uint8_t m_switchBoardCounter = 0;
   bool m_activeBoards[RS485_COMM_MAX_BOARDS] = { false };

   bool m_debug = false;

   // Event message buffers, we need two independent for events and config events because of threading.
   uint8_t m_msg[7];
   uint8_t m_cmsg[12];

   struct sp_port *m_pSerialPort;
   struct sp_port_config *m_pSerialPortConfig;
   std::thread *m_pThread;
   std::queue<Event *> m_events;
   std::queue<PPUCSwitchState *> m_switches;
   std::mutex m_eventQueueMutex;
   std::mutex m_switchesQueueMutex;
};