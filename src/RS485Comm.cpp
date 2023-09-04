#include "RS485Comm.h"

RS485Comm::RS485Comm()
{
   m_pThread = NULL;
}

RS485Comm::~RS485Comm()
{
   if (m_pThread)
   {
      m_pThread->join();

      delete m_pThread;
   }
}

#ifdef __ANDROID__
void RS485Comm::SetAndroidGetJNIEnvFunc(RS485_AndroidGetJNIEnvFunc func)
{
   m_serialPort.SetAndroidGetJNIEnvFunc(func);
}
#endif

void RS485Comm::SetLogMessageCallback(PPUC_LogMessageCallback callback, const void *userData)
{
   m_logMessageCallback = callback;
   m_logMessageUserData = userData;
}

void RS485Comm::LogMessage(const char *format, ...)
{
   if (!m_logMessageCallback)
      return;

   va_list args;
   va_start(args, format);
   (*(m_logMessageCallback))(format, args, m_logMessageUserData);
   va_end(args);
}

void RS485Comm::SetDebug(bool debug)
{
   m_debug = debug;
}

void RS485Comm::Run()
{
   m_pThread = new std::thread([this]()
                               {
      LogMessage("RS485Comm run thread starting");

      int switchBoardCount = 0;
      while (m_serialPort.IsOpen())
      {
         m_eventQueueMutex.lock();

         if (!m_events.empty())
         {
            Event *event = m_events.front();
            m_events.pop();
            m_eventQueueMutex.unlock();

            SendEvent(event);
         }
         else
         {
            m_eventQueueMutex.unlock();
         }

         if (m_activeBoards[m_switchBoards[switchBoardCount]]) {
            PollEvents(m_switchBoards[switchBoardCount]);
         }

         if (++switchBoardCount > m_switchBoardCounter)
         {
            switchBoardCount = 0;
         }

         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      LogMessage("RS485Comm run thread finished"); });
}

void RS485Comm::QueueEvent(Event *event)
{
   m_eventQueueMutex.lock();
   m_events.push(event);
   m_eventQueueMutex.unlock();
}

void RS485Comm::Disconnect()
{
   if (!m_serialPort.IsOpen())
      return;

   m_serialPort.Close();
}

bool RS485Comm::Connect(const char *pDevice)
{
   m_serialPort.SetReadTimeout(RS485_COMM_SERIAL_READ_TIMEOUT);
   m_serialPort.SetWriteTimeout(RS485_COMM_SERIAL_WRITE_TIMEOUT);

   if (m_serialPort.Open(pDevice, RS485_COMM_BAUD_RATE, 8, 1, 0) != 1)
   {
      return false;
   }

   // Wait before continuing.
   std::this_thread::sleep_for(std::chrono::milliseconds(200));
   SendEvent(new Event(EVENT_RESET));
   // Wait before continuing.
   std::this_thread::sleep_for(std::chrono::milliseconds(1000));
   SendEvent(new Event(EVENT_PING));
   // Wait before continuing.
   std::this_thread::sleep_for(std::chrono::milliseconds(200));
   for (int i = 0; i < RS485_COMM_MAX_BOARDS; i++)
   {
      if (m_debug)
      {
         printf("Probe i/o board %d\n", i);
      }
      PollEvents(i);
   }

   return true;
}

void RS485Comm::RegisterSwitchBoard(uint8_t number)
{
   if (m_switchBoardCounter < RS485_COMM_MAX_BOARDS)
   {
      m_switchBoards[m_switchBoardCounter++] = number;
   }
}

PPUCSwitchState *RS485Comm::GetNextSwitchState()
{
   PPUCSwitchState *switchState = nullptr;

   m_switchesQueueMutex.lock();

   if (!m_switches.empty())
   {
      switchState = m_switches.front();
      m_switches.pop();
   }

   m_switchesQueueMutex.unlock();

   return switchState;
}

bool RS485Comm::SendConfigEvent(ConfigEvent *event)
{
   if (m_serialPort.IsOpen())
   {
      m_cmsg[0] = (uint8_t)255;
      m_cmsg[1] = event->sourceId;
      m_cmsg[2] = event->boardId;
      m_cmsg[3] = event->topic;
      m_cmsg[4] = event->index;
      m_cmsg[5] = event->key;
      m_cmsg[6] = event->value >> 24;
      m_cmsg[7] = (event->value >> 16) & 0xff;
      m_cmsg[8] = (event->value >> 8) & 0xff;
      m_cmsg[9] = event->value & 0xff;
      m_cmsg[10] = (uint8_t)255;

      delete event;

      if (1 == m_serialPort.WriteBytes(m_cmsg, 11))
      {
         if (m_debug)
         {
            // @todo user logger
            printf("Sent ConfigEvent %d %d %d %d %d %d %02x%02x%02x%02x %d\n", m_cmsg[0], m_cmsg[1], m_cmsg[2], m_cmsg[3], m_cmsg[4], m_cmsg[5], m_cmsg[6], m_cmsg[7], m_cmsg[8], m_cmsg[9], m_cmsg[10]);
         }
         return true;
      }
   }

   return false;
}

bool RS485Comm::SendEvent(Event *event)
{
   if (m_serialPort.IsOpen())
   {
      m_msg[0] = (uint8_t)255;
      m_msg[1] = event->sourceId;
      m_msg[2] = event->eventId >> 8;
      m_msg[3] = event->eventId & 0xff;
      m_msg[4] = event->value;
      m_msg[5] = (uint8_t)255;

      if (1 == m_serialPort.WriteBytes(m_msg, 6))
      {
         if (m_debug)
         {
            // @todo user logger
            printf("Sent Event %d %d %02x%02x %d %d\n", m_msg[0], m_msg[1], m_msg[2], m_msg[3], m_msg[4], m_msg[5]);
         }
         return true;
      }
   }

   return false;
}

Event *RS485Comm::receiveEvent()
{
   if (m_serialPort.IsOpen())
   {
      std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

      // Set a timeout of 5ms when waiting for an I/O board event.
      // The RS485 converter on the board itself requires 1ms to toggle send/receive mode.
      while ((std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - start))
                 .count() < 5000)
      {
         // printf("Available %d\n", m_serialPort.Available());
         if (m_serialPort.Available() >= 6)
         {
            uint8_t startByte;
            m_serialPort.ReadChar(&startByte);

            if (startByte == 255)
            {
               uint8_t sourceId;
               m_serialPort.ReadChar(&sourceId);
               if (sourceId != 0)
               {
                  uint8_t eventIdHigh;
                  uint8_t eventIdLow;
                  m_serialPort.ReadChar(&eventIdHigh);
                  m_serialPort.ReadChar(&eventIdLow);
                  uint16_t eventId = (((uint16_t)eventIdHigh) << 8) + eventIdLow;
                  if (eventId != 0)
                  {
                     uint8_t value;
                     m_serialPort.ReadChar(&value);
                     m_serialPort.ReadChar(&startByte);
                     if (startByte == 255)
                     {
                        return new Event(sourceId, eventId, value);
                     }
                  }
               }
            }
         }
      }
   }

   return nullptr;
}

void RS485Comm::PollEvents(int board)
{
   Event *event = new Event(EVENT_POLL_EVENTS, 1, board);
   if (SendEvent(event))
   {
      bool null_event = false;
      Event *event_recv;
      while (!null_event && (event_recv = receiveEvent()))
      {
         switch (event_recv->sourceId)
         {
         case EVENT_PONG:
            m_activeBoards[(int)event_recv->value] = true;
            if (m_debug)
            {
               // @todo user logger
               printf("Found i/o board %d\n", (int)event_recv->value);
            }
            break;

         case EVENT_NULL:
            null_event = true;
            break;

         case EVENT_SOURCE_SWITCH:
            m_switchesQueueMutex.lock();
            m_switches.push(new PPUCSwitchState(event_recv->sourceId, event_recv->value));
            m_switchesQueueMutex.unlock();
            break;

         default:
            // @todo handle events like error reports, broken coils, ...
            break;
         }

         delete event_recv;
      }
   }
}
