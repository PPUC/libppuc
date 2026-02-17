#include "RS485Comm.h"

#include "io-boards/PPUCTimings.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/serial.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#if defined(__linux__)
namespace {
bool ShouldEnableHardwareRs485(const char* device) {
  const char* force = getenv("PPUC_RS485_HW");
  if (force && strcmp(force, "1") == 0) {
    return true;
  }
  if (!device) {
    return false;
  }

  // Raspberry Pi UART commonly used with RS485 overlay.
  return strcmp(device, "/dev/ttyAMA0") == 0 ||
         strcmp(device, "/dev/serial0") == 0;
}

void TryEnableHardwareRs485(const char* device, bool debug) {
  if (!ShouldEnableHardwareRs485(device)) {
    return;
  }

  const int fd = open(device, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    if (debug) {
      printf("RS485 HW mode: could not open %s for TIOCSRS485\n", device);
    }
    return;
  }

  struct serial_rs485 rs485;
  memset(&rs485, 0, sizeof(rs485));
  rs485.flags |= SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
  rs485.flags &= ~SER_RS485_RTS_AFTER_SEND;
  rs485.delay_rts_before_send = 0;
  rs485.delay_rts_after_send = 0;

  if (ioctl(fd, TIOCSRS485, &rs485) < 0) {
    if (debug) {
      printf("RS485 HW mode: TIOCSRS485 failed on %s\n", device);
    }
  } else if (debug) {
    printf("RS485 HW mode enabled on %s (RTS as DE)\n", device);
  }

  close(fd);
}
}  // namespace
#endif

RS485Comm::RS485Comm() {
  m_pThread = NULL;
  m_pSerialPort = NULL;
  m_pSerialPortConfig = NULL;
  m_runtimeConfig = ppuc::v2::RuntimeConfig();
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

    while (m_pSerialPort != NULL) {
      uint8_t eventsSent = 0;
      while (eventsSent++ < RS485_COMM_MAX_EVENTS_TO_SEND) {
        Event* event = nullptr;
        m_eventQueueMutex.lock();
        if (!m_events.empty()) {
          event = m_events.front();
          m_events.pop();
        }
        m_eventQueueMutex.unlock();
        if (!event) {
          break;
        }
        SendEvent(event);
        delete event;
      }

      if (!m_runtimeEnabled) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      uint8_t nextBoard = ppuc::v2::kNoBoard;
      if (m_switchBoardCounter > 0) {
        nextBoard = m_switchBoards[0];
      }

      SendOutputStateFrame(nextBoard);
      if (nextBoard != ppuc::v2::kNoBoard) {
        ReceiveSwitchStateChain(nextBoard);
      }
    }

    LogMessage("RS485Comm run thread finished");
  });
}

void RS485Comm::QueueEvent(Event* event) {
  if (!event) {
    return;
  }

  switch (event->sourceId) {
    case EVENT_SOURCE_SOLENOID: {
      auto it = m_coilNumberToIndex.find(event->eventId);
      if (it != m_coilNumberToIndex.end() && it->second < ppuc::v2::kMaxCoilBits) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        ppuc::v2::SetBitmapBit(m_coilBitmap, it->second, event->value != 0);
      }
      delete event;
      return;
    }

    case EVENT_SOURCE_LIGHT:
    case EVENT_SOURCE_GI: {
      auto it = m_lampNumberToIndex.find(event->eventId);
      if (it != m_lampNumberToIndex.end() && it->second < ppuc::v2::kMaxLampBits) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        ppuc::v2::SetBitmapBit(m_lampBitmap, it->second, event->value != 0);
      }
      delete event;
      return;
    }

    case EVENT_RUN:
      m_runtimeEnabled = event->value != 0;
      break;
  }

  std::lock_guard<std::mutex> lock(m_eventQueueMutex);
  m_events.push(event);
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
#if defined(__linux__)
  TryEnableHardwareRs485(pDevice, m_debug);
#endif

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

  auto sendAndDelete = [this](Event* event) {
    SendEvent(event);
    delete event;
  };

  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  // Wait before continuing.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Reset boards before sending a fresh configuration. This handles
  // restart-without-power-cycle scenarios.
  SendResetFrame();
  std::this_thread::sleep_for(
      std::chrono::milliseconds(WAIT_FOR_IO_BOARD_RESET));

  for (int i = 0; i < RS485_COMM_MAX_BOARDS; i++) {
    // Let the boards synchronize themselves to the RS485 bus.
    sendAndDelete(new Event(EVENT_NULL));
    SendConfigEvent(new ConfigEvent(i));
    sendAndDelete(new Event(EVENT_NULL));
  }

  for (int i = 0; i < RS485_COMM_MAX_BOARDS; i++) {
    // Let the boards synchronize themselves again to the RS485 bus.
    sendAndDelete(new Event(EVENT_NULL));
    SendConfigEvent(new ConfigEvent(i));
    sendAndDelete(new Event(EVENT_NULL));
  }

  // Wait before continuing.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  sendAndDelete(new Event(EVENT_PING));
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
  if (m_switchBoardCounter < RS485_COMM_MAX_BOARDS && number < RS485_COMM_MAX_BOARDS) {
    m_switchBoards[m_switchBoardCounter] = number;
    m_switchBoardCounter++;
  }
}

void RS485Comm::SetRuntimeConfig(const ppuc::v2::RuntimeConfig& config) {
  if (ppuc::v2::IsValidRuntimeConfig(config)) {
    m_runtimeConfig = config;
  }
}

void RS485Comm::SetMappings(const std::vector<uint16_t>& coils,
                            const std::vector<uint16_t>& lamps,
                            const std::vector<uint16_t>& switches) {
  m_coilIndexToNumber = coils;
  m_lampIndexToNumber = lamps;
  m_switchIndexToNumber = switches;

  m_coilNumberToIndex.clear();
  m_lampNumberToIndex.clear();
  m_switchNumberToIndex.clear();
  for (uint16_t i = 0; i < m_coilIndexToNumber.size(); ++i) {
    m_coilNumberToIndex[m_coilIndexToNumber[i]] = i;
  }
  for (uint16_t i = 0; i < m_lampIndexToNumber.size(); ++i) {
    m_lampNumberToIndex[m_lampIndexToNumber[i]] = i;
  }
  for (uint16_t i = 0; i < m_switchIndexToNumber.size(); ++i) {
    m_switchNumberToIndex[m_switchIndexToNumber[i]] = i;
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

  if (m_pSerialPort == NULL || !event) {
    delete event;
    return false;
  }

  uint8_t buffer[ppuc::v2::kConfigFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameConfig,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = event->boardId;
  buffer[5] = event->topic;
  buffer[6] = event->index;
  buffer[7] = event->key;
  buffer[8] = static_cast<uint8_t>((event->value >> 24) & 0xff);
  buffer[9] = static_cast<uint8_t>((event->value >> 16) & 0xff);
  buffer[10] = static_cast<uint8_t>((event->value >> 8) & 0xff);
  buffer[11] = static_cast<uint8_t>(event->value & 0xff);
  const uint16_t crc = ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes +
                                                        ppuc::v2::kConfigPayloadBytes);
  buffer[12] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[13] = static_cast<uint8_t>(crc & 0xff);

  delete event;

  if (sp_blocking_write(m_pSerialPort, buffer, sizeof(buffer),
                        RS485_COMM_SERIAL_WRITE_TIMEOUT) > 0) {
    if (m_debug) {
      printf("Sent V2 ConfigFrame board=%u topic=%u index=%u key=%u seq=%u\n",
             buffer[4], buffer[5], buffer[6], buffer[7], buffer[3]);
    }
    return true;
  }

  return false;
}

bool RS485Comm::SendSetupFrame() {
  if (m_pSerialPort == NULL || !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig)) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kSetupFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameSetup,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = static_cast<uint8_t>((m_runtimeConfig.coilBits >> 8) & 0xff);
  buffer[5] = static_cast<uint8_t>(m_runtimeConfig.coilBits & 0xff);
  buffer[6] = static_cast<uint8_t>((m_runtimeConfig.lampBits >> 8) & 0xff);
  buffer[7] = static_cast<uint8_t>(m_runtimeConfig.lampBits & 0xff);
  buffer[8] = static_cast<uint8_t>((m_runtimeConfig.switchBits >> 8) & 0xff);
  buffer[9] = static_cast<uint8_t>(m_runtimeConfig.switchBits & 0xff);
  const uint16_t crc = ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes +
                                                        ppuc::v2::kSetupPayloadBytes);
  buffer[10] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[11] = static_cast<uint8_t>(crc & 0xff);

  if (sp_blocking_write(m_pSerialPort, buffer, sizeof(buffer),
                        RS485_COMM_SERIAL_WRITE_TIMEOUT)) {
    if (m_debug) {
      printf("Sent V2 SetupFrame coil=%u lamp=%u switch=%u seq=%u\n",
             m_runtimeConfig.coilBits, m_runtimeConfig.lampBits,
             m_runtimeConfig.switchBits, buffer[3]);
    }
    return true;
  }

  return false;
}

void RS485Comm::ReceiveSwitchStateChain(uint8_t firstBoard) {
  uint8_t expected = firstBoard;
  uint8_t next = ppuc::v2::kNoBoard;
  bool hadState = false;
  uint8_t hops = 0;

  while (expected != ppuc::v2::kNoBoard &&
         hops++ < RS485_COMM_MAX_BOARDS) {
    if (!ReceiveSwitchStateFrame(expected, &next, &hadState)) {
      break;
    }
    expected = next;
  }
}

bool RS485Comm::SendResetFrame() {
  if (m_pSerialPort == NULL) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kResetFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameReset,
                                            ppuc::v2::kFlagNone);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  const uint16_t crc =
      ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes);
  buffer[4] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[5] = static_cast<uint8_t>(crc & 0xff);

  if (sp_blocking_write(m_pSerialPort, buffer, sizeof(buffer),
                        RS485_COMM_SERIAL_WRITE_TIMEOUT)) {
    if (m_debug) {
      printf("Sent V2 ResetFrame seq=%u\n", buffer[3]);
    }
    return true;
  }

  return false;
}

bool RS485Comm::SendMappingFrame(uint8_t domain, uint16_t index,
                                 uint16_t number) {
  if (m_pSerialPort == NULL) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kMappingFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameMapping,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = domain;
  buffer[5] = 0;
  buffer[6] = static_cast<uint8_t>((index >> 8) & 0xff);
  buffer[7] = static_cast<uint8_t>(index & 0xff);
  buffer[8] = static_cast<uint8_t>((number >> 8) & 0xff);
  buffer[9] = static_cast<uint8_t>(number & 0xff);
  const uint16_t crc = ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes +
                                                        ppuc::v2::kMappingPayloadBytes);
  buffer[10] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[11] = static_cast<uint8_t>(crc & 0xff);

  return sp_blocking_write(m_pSerialPort, buffer, sizeof(buffer),
                           RS485_COMM_SERIAL_WRITE_TIMEOUT) > 0;
}

bool RS485Comm::SendMappingFrames() {
  for (uint16_t i = 0; i < m_coilIndexToNumber.size(); ++i) {
    if (!SendMappingFrame(ppuc::v2::kDomainCoil, i, m_coilIndexToNumber[i])) {
      return false;
    }
  }
  for (uint16_t i = 0; i < m_lampIndexToNumber.size(); ++i) {
    if (!SendMappingFrame(ppuc::v2::kDomainLamp, i, m_lampIndexToNumber[i])) {
      return false;
    }
  }
  for (uint16_t i = 0; i < m_switchIndexToNumber.size(); ++i) {
    if (!SendMappingFrame(ppuc::v2::kDomainSwitch, i,
                          m_switchIndexToNumber[i])) {
      return false;
    }
  }
  return true;
}

bool RS485Comm::SendOutputStateFrame(uint8_t nextBoard) {
  if (m_pSerialPort == NULL || !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig) ||
      !ppuc::v2::IsValidBoard(nextBoard)) {
    return false;
  }

  const size_t coilBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.coilBits);
  const size_t lampBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.lampBits);
  const size_t payloadBytes = coilBytes + lampBytes;
  const size_t frameBytes = ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;
  uint8_t buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxCoilBytes +
                 ppuc::v2::kMaxLampBytes + ppuc::v2::kCrcBytes];

  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameOutputState,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = nextBoard;
  buffer[3] = m_sequence++;

  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    memcpy(&buffer[4], m_coilBitmap, coilBytes);
    memcpy(&buffer[4 + coilBytes], m_lampBitmap, lampBytes);
  }

  const uint16_t crc =
      ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes + payloadBytes);
  buffer[4 + payloadBytes] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[5 + payloadBytes] = static_cast<uint8_t>(crc & 0xff);

  return sp_blocking_write(m_pSerialPort, buffer, frameBytes,
                           RS485_COMM_SERIAL_WRITE_TIMEOUT) > 0;
}

void RS485Comm::ApplySwitchBitmapDiff(const uint8_t* bitmap, size_t bytes) {
  for (uint16_t n = 0; n < m_runtimeConfig.switchBits; ++n) {
    const bool oldState = ppuc::v2::GetBitmapBit(m_switchBitmap, n);
    const bool newState = ppuc::v2::GetBitmapBit(bitmap, n);
    if (oldState != newState) {
      int switchNumber = n;
      if (n < m_switchIndexToNumber.size()) {
        switchNumber = m_switchIndexToNumber[n];
      }
      std::lock_guard<std::mutex> lock(m_switchesQueueMutex);
      m_switches.push(new PPUCSwitchState(switchNumber, newState ? 1 : 0));
    }
  }

  memcpy(m_switchBitmap, bitmap, bytes);
}

bool RS485Comm::ReceiveSwitchStateFrame(uint8_t expectedBoard,
                                        uint8_t* outNextBoard,
                                        bool* outHadState) {
  if (m_pSerialPort == NULL || !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig)) {
    return false;
  }

  const size_t switchBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.switchBits);
  uint8_t header[ppuc::v2::kHeaderBytes];
  uint8_t buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxSwitchBytes +
                 ppuc::v2::kCrcBytes];

  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  while ((std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start))
             .count() < 8000) {
    if ((int)sp_input_waiting(m_pSerialPort) <= 0) {
      continue;
    }

    sp_blocking_read(m_pSerialPort, &header[0], 1, RS485_COMM_SERIAL_READ_TIMEOUT);
    if (header[0] != ppuc::v2::kSyncByte) {
      continue;
    }

    sp_blocking_read(m_pSerialPort, &header[1], ppuc::v2::kHeaderBytes - 1,
                     RS485_COMM_SERIAL_READ_TIMEOUT);
    const ppuc::v2::FrameType frameType =
        ppuc::v2::ExtractType(header[1]);
    size_t payloadBytes = 0;
    if (frameType == ppuc::v2::kFrameSwitchState) {
      payloadBytes = switchBytes;
      if (outHadState) {
        *outHadState = true;
      }
    } else if (frameType == ppuc::v2::kFrameSwitchNoChange) {
      payloadBytes = 0;
      if (outHadState) {
        *outHadState = false;
      }
    } else {
      continue;
    }

    memcpy(buffer, header, ppuc::v2::kHeaderBytes);
    sp_blocking_read(m_pSerialPort, &buffer[ppuc::v2::kHeaderBytes],
                     payloadBytes + ppuc::v2::kCrcBytes,
                     RS485_COMM_SERIAL_READ_TIMEOUT);

    if (header[2] != ppuc::v2::kNoBoard && header[2] != expectedBoard &&
        m_debug) {
      printf("Warning: V2 switch frame nextBoard=%u expected=%u\n", header[2],
             expectedBoard);
    }
    if (outNextBoard) {
      *outNextBoard = header[2];
    }

    const size_t frameBytes =
        ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;
    const uint16_t receivedCrc =
        (static_cast<uint16_t>(buffer[frameBytes - 2]) << 8) |
        static_cast<uint16_t>(buffer[frameBytes - 1]);
    const uint16_t calculatedCrc =
        ppuc::v2::Crc16Ccitt(buffer, frameBytes - ppuc::v2::kCrcBytes);
    if (receivedCrc != calculatedCrc) {
      if (m_debug) {
        printf("Invalid V2 switch frame CRC: got=%04X expected=%04X\n",
               receivedCrc, calculatedCrc);
      }
      return false;
    }

    if (frameType == ppuc::v2::kFrameSwitchState) {
      ApplySwitchBitmapDiff(&buffer[ppuc::v2::kHeaderBytes], switchBytes);
    }
    return true;
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
        // @todo use logger
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
    delete event;
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
  } else {
    delete event;
  }
}
