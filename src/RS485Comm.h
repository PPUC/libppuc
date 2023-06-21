#pragma once

#include <thread>
#include <queue>
#include <string>
#include <inttypes.h>
#include <mutex>
#include <stdio.h>
#include <stdarg.h>
#include "SerialPort.h"
#include "Event.h"
#include "PPUC.h"

#if defined(_WIN32) || defined(_WIN64)
#define CALLBACK __stdcall
#else
#define CALLBACK
#endif

#define RS485_COMM_BAUD_RATE 115200
#define RS485_COMM_SERIAL_READ_TIMEOUT 8
#define RS485_COMM_SERIAL_WRITE_TIMEOUT 8

#define RS485_COMM_MAX_BOARDS 16

#if defined(_WIN32) || defined(_WIN64)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#elif defined(__APPLE__)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#elif defined(__ANDROID__)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#else
// defined (__linux__)
#define RS485_COMM_MAX_SERIAL_WRITE_AT_ONCE 256
#endif

#define RS485_COMM_QUEUE_SIZE_MAX 128

#ifdef __ANDROID__
typedef void *(*RS485_AndroidGetJNIEnvFunc)();
#endif

typedef void(CALLBACK *PPUC_LogMessageCallback)(const char *format, va_list args, const void *userData);

class RS485Comm
{
public:
   RS485Comm();
   ~RS485Comm();

#ifdef __ANDROID__
   void SetAndroidGetJNIEnvFunc(RS485_AndroidGetJNIEnvFunc func);
#endif

   void SetLogMessageCallback(PPUC_LogMessageCallback callback, const void *userData);

   bool Connect(const char *device);
   void Disconnect();

   void Run();

   void QueueEvent(Event *event);
   bool SendConfigEvent(ConfigEvent *configEvent);

   void RegisterSwitchBoard(uint8_t number);
   PPUCSwitchState *GetNextSwitchState();

private:
   void LogMessage(const char *format, ...);

   bool SendEvent(Event *event);
   Event *receiveEvent();
   void PollEvents(int board);

   PPUC_LogMessageCallback m_logMessageCallback = nullptr;
   const void *m_logMessageUserData = nullptr;

   uint8_t m_switchBoards[RS485_COMM_MAX_BOARDS];
   uint8_t m_switchBoardCounter = 0;

   // Event message buffers, we need two independent for events and config events because of threading.
   uint8_t m_msg[6] = {255};
   uint8_t m_cmsg[11] = {255};

   SerialPort m_serialPort;
   std::thread *m_pThread;
   std::queue<Event *> m_events;
   std::queue<PPUCSwitchState *> m_switches;
   std::mutex m_eventQueueMutex;
   std::mutex m_switchesQueueMutex;
};