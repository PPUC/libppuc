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

void RS485Comm::Run()
{
   m_pThread = new std::thread([this]()
                               {
                                  LogMessage("RS485Comm run thread starting");

                                  bool sleep = false;

                                  while (m_serialPort.IsOpen())
                                  {
                                     m_eventQueueMutex.lock();

                                     if (m_events.empty())
                                     {
                                        m_eventQueueMutex.unlock();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                        continue;
                                     }

                                     Event *event = m_events.front();
                                     m_events.pop();
                                     m_eventQueueMutex.unlock();

                                    SendEvent(event);

                                    // @todo millis or one board per run
                                    for (int i = 0; i < m_switchBoardCounter; i++)
                                    {
                                       PollEvents(i);
                                    }
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
      return false;

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
      //       = (UINT8) 255;
      m_msg[1] = event->sourceId;
      m_msg[2] = event->boardId;
      m_msg[3] = event->topic;
      m_msg[4] = event->key;
      m_msg[5] = event->index;
      m_msg[6] = event->value >> 24;
      m_msg[7] = (event->value >> 16) & 0xff;
      m_msg[8] = (event->value >> 8) & 0xff;
      m_msg[9] = event->value & 0xff;
      //       = (UINT8) 255;

      delete event;

      return m_serialPort.WriteBytes(m_msg, 11);
   }

   return false;
}

bool RS485Comm::SendEvent(Event *event)
{
   if (m_serialPort.IsOpen())
   {
      //       = (UINT8) 255;
      m_msg[1] = event->sourceId;
      m_msg[2] = event->eventId >> 8;
      m_msg[3] = event->eventId & 0xff;
      m_msg[4] = event->value;
      //       = (UINT8) 255;

      return m_serialPort.WriteBytes(m_msg, 6);
   }

   return false;
}

Event *RS485Comm::receiveEvent()
{
   if (m_serialPort.IsOpen())
   {
      std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

      // Set a timeout of 1ms when waiting for an I/O board event.
      while ((std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - start))
                 .count() < 1000)
      {
         if (m_serialPort.Available() >= 6)
         {
            uint_fast8_t poll[6] = {0};
            if (m_serialPort.ReadBytes(poll, 6))
            {
               if (poll[0] == 255 && poll[5] == 255)
               {
                  return new Event(poll[1], (((UINT16)poll[2]) << 8) + poll[3], poll[4]);
               }
            }
            return nullptr;
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
         case EVENT_NULL:
            null_event = true;
            delete event_recv;
            break;

         case EVENT_SOURCE_SWITCH:
            m_switchesQueueMutex.lock();
            m_switches.push(new PPUCSwitchState(event_recv->sourceId, event_recv->value));
            m_switchesQueueMutex.unlock();
            delete event_recv;
            break;

         default:
            // @todo handle events like error reports, broken coils, ...
            delete event_recv;
            break;
         }
      }
   }
}
