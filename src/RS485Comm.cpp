#include "RS485Comm.h"

#include "io-boards/PPUCTimings.h"

RS485Comm::RS485Comm() {
  m_pThread = NULL;
  m_pSerialPort = NULL;
  m_pSerialPortConfig = NULL;
}

RS485Comm::~RS485Comm() {
  Disconnect();

  if (m_pThread) {
    m_pThread->join();

    delete m_pThread;
  }
}

void RS485Comm::SetLogMessageCallback(PPUC_LogMessageCallback callback,
                                      const void* userData) {
  m_logMessageCallback = callback;
  m_logMessageUserData = userData;
}

void RS485Comm::LogMessage(const char* format, ...) {
  if (!m_logMessageCallback) {
    return;
  }

  va_list args;
  va_start(args, format);
  (*(m_logMessageCallback))(format, args, m_logMessageUserData);
  va_end(args);
}

void RS485Comm::SetDebug(bool debug) { m_debug = debug; }

void RS485Comm::Run() {
  m_pThread = new std::thread([this]() {
    LogMessage("RS485Comm run thread starting");

    int switchBoardCount = 0;
    while (m_pSerialPort != NULL) {
      m_eventQueueMutex.lock();
      uint8_t eventsSent = m_events.empty() ? RS485_COMM_MAX_EVENTS_TO_SEND : 0;
      m_eventQueueMutex.unlock();
      while (eventsSent++ < RS485_COMM_MAX_EVENTS_TO_SEND) {
        m_eventQueueMutex.lock();
        if (!m_events.empty()) {
          Event* event = m_events.front();
          m_events.pop();
          m_eventQueueMutex.unlock();
          SendEvent(event);
        } else {
          m_eventQueueMutex.unlock();
          break;
        }
      }

      if (m_activeBoards[m_switchBoards[switchBoardCount]]) {
        PollEvents(m_switchBoards[switchBoardCount]);
      }

      if (++switchBoardCount > m_switchBoardCounter) {
        switchBoardCount = 0;
      }

      // std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    LogMessage("RS485Comm run thread finished");
  });
}

void RS485Comm::QueueEvent(Event* event) {
  m_eventQueueMutex.lock();
  m_events.push(event);
  m_eventQueueMutex.unlock();
}

void RS485Comm::Disconnect() {
  if (m_pSerialPort == NULL) {
    return;
  }

  sp_set_config(m_pSerialPort, m_pSerialPortConfig);
  sp_free_config(m_pSerialPortConfig);
  m_pSerialPortConfig = NULL;

  sp_close(m_pSerialPort);
  sp_free_port(m_pSerialPort);
  m_pSerialPort = NULL;
}

bool RS485Comm::Connect(const char* pDevice) {
  enum sp_return result = sp_get_port_by_name(pDevice, &m_pSerialPort);
  if (result != SP_OK) {
    return false;
  }

  result = sp_open(m_pSerialPort, SP_MODE_READ_WRITE);
  if (result != SP_OK) {
    sp_free_port(m_pSerialPort);
    m_pSerialPort = NULL;
    return false;
  }

  sp_new_config(&m_pSerialPortConfig);
  sp_get_config(m_pSerialPort, m_pSerialPortConfig);
  sp_set_baudrate(m_pSerialPort, RS485_COMM_BAUD_RATE);
  sp_set_bits(m_pSerialPort, 8);
  sp_set_parity(m_pSerialPort, SP_PARITY_NONE);
  sp_set_stopbits(m_pSerialPort, 1);
  sp_set_xon_xoff(m_pSerialPort, SP_XONXOFF_DISABLED);

  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  // Wait before continuing.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  for (int i = 0; i < RS485_COMM_MAX_BOARDS; i++) {
    // Let the boards synchronize themselves to the RS485 bus.
    SendEvent(new Event(EVENT_NULL));
    SendConfigEvent(new ConfigEvent(i));
    SendEvent(new Event(EVENT_NULL));
  }

  // End previous game. The reset timer of the boards is configured to 3 seconds
  // to reset all devices.
  SendEvent(new Event(EVENT_RESET));
  // Wait before continuing.
  // The EffectControllers get a grace period atfer the reset event to turn off
  // all effect devices before the reset happens After the reset, each IO boards
  // waits a bit for a USB debugger connection before turning on.
  std::this_thread::sleep_for(
      std::chrono::milliseconds(WAIT_FOR_IO_BOARD_RESET));

  for (int i = 0; i < RS485_COMM_MAX_BOARDS; i++) {
    // Let the boards synchronize themselves again to the RS485 bus.
    SendEvent(new Event(EVENT_NULL));
    SendConfigEvent(new ConfigEvent(i));
    SendEvent(new Event(EVENT_NULL));
  }

  // Wait before continuing.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  SendEvent(new Event(EVENT_PING));
  // Wait before continuing.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  for (int i = 0; i < RS485_COMM_MAX_BOARDS; i++) {
    if (m_debug) {
      printf("Probe i/o board %d\n", i);
    }
    PollEvents(i);
  }

  return true;
}

void RS485Comm::RegisterSwitchBoard(uint8_t number) {
  if (m_switchBoardCounter < RS485_COMM_MAX_BOARDS) {
    m_switchBoards[m_switchBoardCounter++] = number;
  }
}

PPUCSwitchState* RS485Comm::GetNextSwitchState() {
  PPUCSwitchState* switchState = nullptr;

  m_switchesQueueMutex.lock();

  if (!m_switches.empty()) {
    switchState = m_switches.front();
    m_switches.pop();
  }

  m_switchesQueueMutex.unlock();

  return switchState;
}

bool RS485Comm::SendConfigEvent(ConfigEvent* event) {
  // Wait a bit to not exceed the output buffer in case of large configurations.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  if (m_pSerialPort != NULL) {
    m_cmsg[0] = 0b11111111;
    m_cmsg[1] = event->sourceId;
    m_cmsg[2] = event->boardId;
    m_cmsg[3] = event->topic;
    m_cmsg[4] = event->index;
    m_cmsg[5] = event->key;
    m_cmsg[6] = event->value >> 24;
    m_cmsg[7] = (event->value >> 16) & 0xff;
    m_cmsg[8] = (event->value >> 8) & 0xff;
    m_cmsg[9] = event->value & 0xff;
    m_cmsg[10] = 0b10101010;
    m_cmsg[11] = 0b01010101;

    delete event;

    if (sp_blocking_write(m_pSerialPort, m_cmsg, 12,
                          RS485_COMM_SERIAL_WRITE_TIMEOUT)) {
      if (m_debug) {
        // @todo user logger
        printf(
            "Sent ConfigEvent %02X %d %d %d %d %d %02x%02x%02x%02x %02X %02X\n",
            m_cmsg[0], m_cmsg[1], m_cmsg[2], m_cmsg[3], m_cmsg[4], m_cmsg[5],
            m_cmsg[6], m_cmsg[7], m_cmsg[8], m_cmsg[9], m_cmsg[10], m_cmsg[11]);
      }
      return true;
    } else {
      if (m_debug) {
        // @todo user logger
        printf(
            "Error when sending ConfigEvent %02X %d %d %d %d %d "
            "%02x%02x%02x%02x %02X %02X\n",
            m_cmsg[0], m_cmsg[1], m_cmsg[2], m_cmsg[3], m_cmsg[4], m_cmsg[5],
            m_cmsg[6], m_cmsg[7], m_cmsg[8], m_cmsg[9], m_cmsg[10], m_cmsg[11]);
      }
    }
  }

  return false;
}

bool RS485Comm::SendEvent(Event* event) {
  if (m_pSerialPort != NULL) {
    m_msg[0] = (uint8_t)255;
    m_msg[1] = event->sourceId;
    m_msg[2] = event->eventId >> 8;
    m_msg[3] = event->eventId & 0xff;
    m_msg[4] = event->value;
    m_msg[5] = 0b10101010;
    m_msg[6] = 0b01010101;

    if (sp_blocking_write(m_pSerialPort, m_msg, 7,
                          RS485_COMM_SERIAL_WRITE_TIMEOUT)) {
      if (m_debug) {
        // @todo user logger
        printf("Sent Event %d %d %d\n", event->sourceId, event->eventId,
               event->value);
      }
      return true;
    }
  }

  return false;
}

Event* RS485Comm::receiveEvent() {
  if (m_pSerialPort != NULL) {
    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    // Set a timeout of 8ms when waiting for an I/O board event.
    // The RS485 converter on the board itself requires 1ms to toggle
    // send/receive mode.
    while ((std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start))
               .count() < 8000) {
      // printf("Available %d\n", m_serialPort.Available());
      if ((int)sp_input_waiting(m_pSerialPort) >= 6) {
        uint8_t startByte;
        sp_blocking_read(m_pSerialPort, &startByte, 1,
                         RS485_COMM_SERIAL_READ_TIMEOUT);
        if (startByte == 255) {
          uint8_t sourceId;
          sp_blocking_read(m_pSerialPort, &sourceId, 1,
                           RS485_COMM_SERIAL_READ_TIMEOUT);
          if (sourceId != 0) {
            uint8_t eventIdHigh;
            uint8_t eventIdLow;
            sp_blocking_read(m_pSerialPort, &eventIdHigh, 1,
                             RS485_COMM_SERIAL_READ_TIMEOUT);
            sp_blocking_read(m_pSerialPort, &eventIdLow, 1,
                             RS485_COMM_SERIAL_READ_TIMEOUT);
            uint16_t eventId = (((uint16_t)eventIdHigh) << 8) + eventIdLow;
            if (eventId != 0) {
              uint8_t value;
              sp_blocking_read(m_pSerialPort, &value, 1,
                               RS485_COMM_SERIAL_READ_TIMEOUT);

              uint8_t stopByte;
              sp_blocking_read(m_pSerialPort, &stopByte, 1,
                               RS485_COMM_SERIAL_READ_TIMEOUT);
              if (stopByte == 0b10101010) {
                sp_blocking_read(m_pSerialPort, &stopByte, 1,
                                 RS485_COMM_SERIAL_READ_TIMEOUT);
                if (stopByte == 0b01010101) {
                  if (m_debug) {
                    // @todo use logger
                    printf("Received Event %d %d %d\n", sourceId, eventId,
                           value);
                  }
                  return new Event(sourceId, eventId, value);
                } else if (m_debug) {
                  // @todo use logger
                  printf("Received wrong second stop byte %d\n", stopByte);
                }
              } else if (m_debug) {
                // @todo use logger
                printf("Received wrong first stop byte %d\n", stopByte);
              }
            } else if (m_debug) {
              // @todo use logger
              printf("Received illegal event id %d\n", eventId);
            }
          } else if (m_debug) {
            // @todo use logger
            printf("Received illegal source id %d\n", sourceId);
          }

          // Something went wrong after the start byte, try to get back in sync.
          while (sp_input_waiting(m_pSerialPort) > 0) {
            if (m_debug) {
              // @todo use logger
              printf("Error: Lost sync, %d bytes remaining\n",
                     sp_input_waiting(m_pSerialPort));
            }
            uint8_t stopByte;
            sp_blocking_read(m_pSerialPort, &stopByte, 1,
                             RS485_COMM_SERIAL_READ_TIMEOUT);
            if (stopByte == 0b10101010) {
              sp_blocking_read(m_pSerialPort, &stopByte, 1,
                               RS485_COMM_SERIAL_READ_TIMEOUT);
              if (stopByte == 0b01010101) {
                // Now we should be back in sync.
                break;
              }
            }
          }
        }
      }
    }
    if (m_debug) {
      // @todo use logger
      printf("Timeout when waiting for events from i/o boards\n");
    }
  } else if (m_debug) {
    // @todo use logger
    printf("RS485 Error\n");
  }

  return nullptr;
}

void RS485Comm::PollEvents(int board) {
  if (m_debug) {
    // @todo use logger
    printf("Polling board %d ...\n", board);
  }

  Event* event = new Event(EVENT_POLL_EVENTS, 1, board);
  if (SendEvent(event)) {
    // Wait until the i/o board switched to RS485 send mode.
    std::this_thread::sleep_for(
        std::chrono::microseconds(RS485_MODE_SWITCH_DELAY));

    bool null_event = false;
    Event* event_recv;
    while (!null_event && (event_recv = receiveEvent())) {
      switch (event_recv->sourceId) {
        case EVENT_PONG:
          if ((int)event_recv->value < RS485_COMM_MAX_BOARDS) {
            m_activeBoards[(int)event_recv->value] = true;
            if (m_debug) {
              // @todo user logger
              printf("Found i/o board %d\n", (int)event_recv->value);
            }
          }
          break;

        case EVENT_NULL:
          null_event = true;
          break;

        case EVENT_SOURCE_SWITCH:
          m_switchesQueueMutex.lock();
          m_switches.push(
              new PPUCSwitchState(event_recv->eventId, event_recv->value));
          m_switchesQueueMutex.unlock();
          break;

        default:
          // @todo handle events like error reports, broken coils, ...
          break;
      }

      delete event_recv;
    }

    // Wait until the i/o board switched back to RS485 receive mode.
    std::this_thread::sleep_for(
        std::chrono::microseconds(RS485_MODE_SWITCH_DELAY));
  }
}
