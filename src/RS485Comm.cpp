#include "RS485Comm.h"

#include <string>

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

#if defined(__APPLE__)
namespace {
std::string NormalizeSerialDevice(const char* device, bool debug) {
  if (!device) {
    return {};
  }

  const std::string name(device);
  const std::string ttyPrefix = "/dev/tty.";
  if (name.rfind(ttyPrefix, 0) == 0) {
    const std::string normalized = "/dev/cu." + name.substr(ttyPrefix.size());
    if (debug) {
      printf("macOS serial: using callout device %s instead of %s\n",
             normalized.c_str(), name.c_str());
    }
    return normalized;
  }

  return name;
}
}  // namespace
#endif

RS485Comm::RS485Comm() {
  m_pThread = NULL;
  m_pSerialPort = NULL;
  m_pSerialPortConfig = NULL;
  m_runtimeConfig = ppuc::v2::RuntimeConfig();
  m_nextOutputFrameAt = std::chrono::steady_clock::now();
  m_nextSwitchPollAt = std::chrono::steady_clock::now();
  m_nextAllowedResyncAt = std::chrono::steady_clock::now();
}

RS485Comm::~RS485Comm() {
  Disconnect();

  if (m_pThread) {
    delete m_pThread;
    m_pThread = NULL;
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

void RS485Comm::SetDebugErrors(bool debugErrors) {
  m_debugErrors = debugErrors;
}

void RS485Comm::DebugPrintf(const char* format, ...) {
  if (!m_debug) {
    return;
  }

  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  printf("%lld DEBUG: %s", static_cast<long long>(now), buffer);
  if (buffer[0] == '\0' || buffer[strlen(buffer) - 1] != '\n') {
    printf("\n");
  }
}

void RS485Comm::ErrorPrintf(const char* format, ...) {
  if (!(m_debug || m_debugErrors)) {
    return;
  }

  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  printf("%lld ERROR: %s", static_cast<long long>(now), buffer);
  if (buffer[0] == '\0' || buffer[strlen(buffer) - 1] != '\n') {
    printf("\n");
  }
}

bool RS485Comm::WriteBytes(const char* context, const uint8_t* buffer,
                           size_t size) {
  if (m_pSerialPort == NULL) {
    return false;
  }

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    const int written = sp_blocking_write(m_pSerialPort, buffer, size,
                                          RS485_COMM_SERIAL_WRITE_TIMEOUT);
    if (written == static_cast<int>(size)) {
      return true;
    }

    if (attempt == 0 && written >= 0) {
      // USB-RS485 adapters occasionally fail to accept a short write burst in
      // time even though the link recovers immediately afterward. Give the
      // host driver one brief retry before treating it as a transport error.
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }

    if (written < 0) {
      char* errorMessage = sp_last_error_message();
      if (errorMessage) {
        ErrorPrintf("Serial write failed for %s: %s", context, errorMessage);
        sp_free_error_message(errorMessage);
      } else {
        ErrorPrintf("Serial write failed for %s: libserialport error %d",
                    context, written);
      }
    } else {
      ErrorPrintf("Serial write incomplete for %s: wrote %d of %zu bytes",
                  context, written, size);
    }

    return false;
  }

  return false;
}

void RS485Comm::Run() {
  m_stopRequested = false;
  m_nextOutputFrameAt = std::chrono::steady_clock::now();
  m_nextSwitchPollAt = std::chrono::steady_clock::now();
  m_nextAllowedResyncAt = std::chrono::steady_clock::now();
  m_pThread = new std::thread([this]() {
    LogMessage("RS485Comm run thread starting");

    while (!m_stopRequested) {
      if (!m_runtimeEnabled) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      if (m_needSessionResync &&
          std::chrono::steady_clock::now() >= m_nextAllowedResyncAt) {
        if (!ResyncSession()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
      }

      auto now = std::chrono::steady_clock::now();
      if (now < m_nextOutputFrameAt) {
        std::this_thread::sleep_until(m_nextOutputFrameAt);
        now = std::chrono::steady_clock::now();
      }
      m_nextOutputFrameAt =
          now + std::chrono::milliseconds(RS485_COMM_OUTPUT_FRAME_INTERVAL_MS);

      uint8_t nextBoard = ppuc::v2::kNoBoard;
      if (m_switchBoardCounter > 0 && now >= m_nextSwitchPollAt) {
        nextBoard = m_switchBoards[0];
        m_nextSwitchPollAt =
            now + std::chrono::milliseconds(RS485_COMM_SWITCH_POLL_INTERVAL_MS);
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
      if (it != m_coilNumberToIndex.end() &&
          it->second < ppuc::v2::kMaxCoilBits) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        ppuc::v2::SetBitmapBit(m_coilBitmap, it->second, event->value != 0);
      }
      delete event;
      return;
    }

    case EVENT_SOURCE_LIGHT:
    case EVENT_SOURCE_GI: {
      if (event->sourceId == EVENT_SOURCE_GI) {
        if (event->eventId >= 1 && event->eventId <= ppuc::v2::kGiStrings) {
          std::lock_guard<std::mutex> lock(m_stateMutex);
          m_giLevels[event->eventId - 1] = ppuc::v2::ClampGiLevel(event->value);
        }
        delete event;
        return;
      }

      auto it = m_lampNumberToIndex.find(event->eventId);
      if (it != m_lampNumberToIndex.end() &&
          it->second < ppuc::v2::kMaxLampBits) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        ppuc::v2::SetBitmapBit(m_lampBitmap, it->second, event->value != 0);
      }
      delete event;
      return;
    }

    case EVENT_RUN:
      m_runtimeEnabled = event->value != 0;
      delete event;
      return;

    // @todo: Nobody should be sending switch events back to us anymore
    case EVENT_READ_SWITCHES:
      delete event;
      return;
  }

  delete event;
}

void RS485Comm::Disconnect() {
  m_stopRequested = true;

  if (m_pThread && m_pThread->joinable()) {
    m_pThread->join();
    delete m_pThread;
    m_pThread = NULL;
  }

  if (m_pSerialPort == NULL) {
    return;
  }

  // Some USB-RS485 adapters/drivers keep modem-control/flow-control state
  // across close/open cycles. Drain and explicitly deassert those lines before
  // closing so the next run starts from a neutral adapter state.
  sp_drain(m_pSerialPort);
  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  sp_set_flowcontrol(m_pSerialPort, SP_FLOWCONTROL_NONE);
  sp_set_rts(m_pSerialPort, SP_RTS_OFF);
  sp_set_dtr(m_pSerialPort, SP_DTR_OFF);

  if (m_pSerialPortConfig != NULL) {
    sp_set_config(m_pSerialPort, m_pSerialPortConfig);
    sp_free_config(m_pSerialPortConfig);
    m_pSerialPortConfig = NULL;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  sp_close(m_pSerialPort);
  sp_free_port(m_pSerialPort);
  m_pSerialPort = NULL;
}

bool RS485Comm::Connect(const char* pDevice) {
#if defined(__linux__)
  TryEnableHardwareRs485(pDevice, m_debug);
#endif

  const char* device = pDevice;
#if defined(__APPLE__)
  const std::string normalizedDevice = NormalizeSerialDevice(pDevice, m_debug);
  if (!normalizedDevice.empty()) {
    device = normalizedDevice.c_str();
  }
#endif

  m_stopRequested = false;

  if (m_debug) {
    printf("Opening serial device %s at %d baud\n", device,
           RS485_COMM_BAUD_RATE);
  }

  enum sp_return result = sp_get_port_by_name(device, &m_pSerialPort);
  if (result != SP_OK) {
    if (m_debug) {
      printf("sp_get_port_by_name failed for %s: %d\n", device, result);
    }
    return false;
  }

  result = sp_open(m_pSerialPort, SP_MODE_READ_WRITE);
  if (result != SP_OK) {
    if (m_debug) {
      printf("sp_open failed for %s: %d\n", device, result);
    }
    sp_free_port(m_pSerialPort);
    m_pSerialPort = NULL;
    return false;
  }

  sp_new_config(&m_pSerialPortConfig);
  sp_get_config(m_pSerialPort, m_pSerialPortConfig);
  if (sp_set_config_baudrate(m_pSerialPortConfig, RS485_COMM_BAUD_RATE) !=
      SP_OK) {
    if (m_debug) {
      printf("sp_set_baudrate failed\n");
    }
    sp_free_config(m_pSerialPortConfig);
    m_pSerialPortConfig = NULL;
    sp_close(m_pSerialPort);
    sp_free_port(m_pSerialPort);
    m_pSerialPort = NULL;
    return false;
  }
  if (sp_set_config_bits(m_pSerialPortConfig, 8) != SP_OK ||
      sp_set_config_parity(m_pSerialPortConfig, SP_PARITY_NONE) != SP_OK ||
      sp_set_config_stopbits(m_pSerialPortConfig, 1) != SP_OK ||
      sp_set_config_xon_xoff(m_pSerialPortConfig, SP_XONXOFF_DISABLED) !=
          SP_OK ||
      sp_set_config_flowcontrol(m_pSerialPortConfig, SP_FLOWCONTROL_NONE) !=
          SP_OK) {
    if (m_debug) {
      printf("sp_set_* serial config failed\n");
    }
    sp_free_config(m_pSerialPortConfig);
    m_pSerialPortConfig = NULL;
    sp_close(m_pSerialPort);
    sp_free_port(m_pSerialPort);
    m_pSerialPort = NULL;
    return false;
  }
  if (sp_set_config(m_pSerialPort, m_pSerialPortConfig) != SP_OK) {
    if (m_debug) {
      printf("sp_set_config failed\n");
    }
    sp_free_config(m_pSerialPortConfig);
    m_pSerialPortConfig = NULL;
    sp_close(m_pSerialPort);
    sp_free_port(m_pSerialPort);
    m_pSerialPort = NULL;
    return false;
  }
  // Apply a fully passive host-side serial state. This avoids adapters getting
  // wedged by inherited flow-control or modem-control settings between runs.
  sp_set_flowcontrol(m_pSerialPort, SP_FLOWCONTROL_NONE);
  sp_set_rts(m_pSerialPort, SP_RTS_OFF);
  sp_set_dtr(m_pSerialPort, SP_DTR_OFF);

  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  // Wait before continuing.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  m_needSessionResync = false;
  m_epoch = 1;
  m_sequence = 0;
  m_lastOutputSequenceSent = 0;

  // Reset boards before sending a fresh configuration. This handles
  // restart-without-power-cycle scenarios.
  SendResetFrame();
  std::this_thread::sleep_for(
      std::chrono::milliseconds(WAIT_FOR_IO_BOARD_RESET));
  // Boards can finish rebooting slightly before the USB-RS485 adapter and host
  // driver have fully drained stale bytes. Start configuration from a clean
  // RX/TX state after the reset window closes.
  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  return true;
}

void RS485Comm::RegisterSwitchBoard(uint8_t number) {
  if (m_switchBoardCounter < RS485_COMM_MAX_BOARDS &&
      number < RS485_COMM_MAX_BOARDS) {
    m_switchBoards[m_switchBoardCounter] = number;
    m_switchBoardCounter++;
  }
}

void RS485Comm::SetConfiguredBoards(const std::vector<uint8_t>& boards) {
  m_configuredBoards = boards;
}

void RS485Comm::SetSwitchNumbersByBoard(
    const std::unordered_map<uint8_t, std::vector<uint16_t>>& switchesByBoard) {
  m_switchNumbersByBoard = switchesByBoard;
}

void RS485Comm::SetSkippedBoards(const std::set<uint8_t>& boards) {
  m_skippedBoards = boards;
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

bool RS485Comm::SetVirtualSwitchState(uint16_t number, uint8_t state) {
  EnsureConfiguredBoardPresenceKnown();

  const auto owner = m_virtualSwitchOwnerByNumber.find(number);
  if (owner == m_virtualSwitchOwnerByNumber.end()) {
    return false;
  }

  auto boardIt = m_virtualSwitchBoards.find(owner->second);
  if (boardIt == m_virtualSwitchBoards.end()) {
    return false;
  }

  auto& boardState = boardIt->second;
  for (size_t i = 0; i < boardState.switchNumbers.size(); ++i) {
    if (boardState.switchNumbers[i] != number) {
      continue;
    }

    const uint8_t normalizedState = state == 0 ? 0 : 1;
    if (boardState.switchStates[i] == normalizedState) {
      return true;
    }

    boardState.switchStates[i] = normalizedState;
    boardState.dirty = true;
    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      const auto switchIt = m_switchNumberToIndex.find(number);
      if (switchIt != m_switchNumberToIndex.end() &&
          switchIt->second < ppuc::v2::kMaxSwitchBits) {
        ppuc::v2::SetBitmapBit(m_switchBitmap, switchIt->second,
                               normalizedState != 0);
      }
    }
    {
      std::lock_guard<std::mutex> lock(m_switchesQueueMutex);
      m_switches.push(new PPUCSwitchState(number, normalizedState));
    }
    return true;
  }

  return false;
}

bool RS485Comm::IsSwitchVirtualized(uint16_t number) const {
  const_cast<RS485Comm*>(this)->EnsureConfiguredBoardPresenceKnown();
  return m_virtualSwitchOwnerByNumber.find(number) !=
         m_virtualSwitchOwnerByNumber.end();
}

bool RS485Comm::IsBoardPresent(uint8_t board) const {
  return m_presentBoards.find(board) != m_presentBoards.end();
}

bool RS485Comm::IsBoardVirtualized(uint8_t board) const {
  const_cast<RS485Comm*>(this)->EnsureConfiguredBoardPresenceKnown();
  return m_virtualSwitchBoards.find(board) != m_virtualSwitchBoards.end();
}

void RS485Comm::SetActiveSwitchBoards(const std::vector<uint8_t>& boards) {
  m_switchBoardCounter = 0;
  for (const uint8_t board : boards) {
    if (m_switchBoardCounter >= RS485_COMM_MAX_BOARDS || board >= RS485_COMM_MAX_BOARDS) {
      break;
    }
    m_switchBoards[m_switchBoardCounter++] = board;
  }
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
  buffer[4] = m_epoch;
  buffer[5] = event->boardId;
  buffer[6] = event->topic;
  buffer[7] = event->index;
  buffer[8] = event->key;
  buffer[9] = static_cast<uint8_t>((event->value >> 24) & 0xff);
  buffer[10] = static_cast<uint8_t>((event->value >> 16) & 0xff);
  buffer[11] = static_cast<uint8_t>((event->value >> 8) & 0xff);
  buffer[12] = static_cast<uint8_t>(event->value & 0xff);
  const uint16_t crc = ppuc::v2::Crc16Ccitt(
      buffer, ppuc::v2::kHeaderBytes + ppuc::v2::kConfigPayloadBytes);
  buffer[13] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[14] = static_cast<uint8_t>(crc & 0xff);

  if (m_skippedBoards.find(buffer[5]) != m_skippedBoards.end()) {
    if (m_debug) {
      DebugPrintf(
          "Skipping V2 ConfigFrame for board=%u topic=%u index=%u key=%u due to forced virtualization",
          buffer[5], buffer[6], buffer[7], buffer[8]);
    }
    delete event;
    return true;
  }

  delete event;

  for (uint8_t attempt = 0; attempt < RS485_COMM_CONFIG_ACK_RETRIES; ++attempt) {
    if (!WriteBytes("ConfigFrame", buffer, sizeof(buffer))) {
      return false;
    }

    if (m_debug) {
      DebugPrintf(
          "Sent V2 ConfigFrame board=%u topic=%u index=%u key=%u seq=%u attempt=%u",
          buffer[5], buffer[6], buffer[7], buffer[8], buffer[3],
          static_cast<unsigned>(attempt + 1));
    }

    if (ReceiveConfigAck(buffer[5], buffer[6], buffer[7], buffer[8])) {
      m_presentBoards.insert(buffer[5]);
      return true;
    }

    sp_flush(m_pSerialPort, SP_BUF_INPUT);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  fprintf(stderr,
          "ERROR: Config not acknowledged: board=%u topic=%u index=%u key=%u\n",
          buffer[5], buffer[6], buffer[7], buffer[8]);
  return false;
}

bool RS485Comm::SendSetupFrame() {
  if (m_pSerialPort == NULL ||
      !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig)) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kSetupFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameSetup,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = m_epoch;
  buffer[5] = static_cast<uint8_t>((m_runtimeConfig.coilBits >> 8) & 0xff);
  buffer[6] = static_cast<uint8_t>(m_runtimeConfig.coilBits & 0xff);
  buffer[7] = static_cast<uint8_t>((m_runtimeConfig.lampBits >> 8) & 0xff);
  buffer[8] = static_cast<uint8_t>(m_runtimeConfig.lampBits & 0xff);
  buffer[9] = static_cast<uint8_t>((m_runtimeConfig.switchBits >> 8) & 0xff);
  buffer[10] = static_cast<uint8_t>(m_runtimeConfig.switchBits & 0xff);
  const uint16_t crc = ppuc::v2::Crc16Ccitt(
      buffer, ppuc::v2::kHeaderBytes + ppuc::v2::kSetupPayloadBytes);
  buffer[11] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[12] = static_cast<uint8_t>(crc & 0xff);

  if (WriteBytes("SetupFrame", buffer, sizeof(buffer))) {
    if (m_debug) {
      DebugPrintf("Sent V2 SetupFrame coil=%u lamp=%u switch=%u seq=%u",
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
  bool success = true;

  while (expected != ppuc::v2::kNoBoard && hops++ < RS485_COMM_MAX_BOARDS) {
    if (m_virtualSwitchBoards.find(expected) != m_virtualSwitchBoards.end()) {
      next = GetLogicalNextSwitchBoard(expected);
      if (!SendVirtualSwitchReply(expected, next, &hadState)) {
        success = false;
        break;
      }
      expected = next;
      continue;
    }

    if (!ReceiveSwitchStateFrame(expected, &next, &hadState)) {
      success = false;
      break;
    }
    expected = next;
  }

  if (success) {
    m_switchReplyMisses = 0;
  } else {
    ++m_switchReplyMisses;
    if (m_debug || m_debugErrors) {
      ErrorPrintf("Missed V2 switch reply chain %u time(s)",
                  m_switchReplyMisses);
    }
    if (m_switchReplyMisses >= RS485_COMM_SWITCH_REPLY_MISS_THRESHOLD) {
      m_needSessionResync = true;
    }
  }
}

uint8_t RS485Comm::GetLogicalNextSwitchBoard(uint8_t board) const {
  for (uint8_t i = 0; i < m_switchBoardCounter; ++i) {
    if (m_switchBoards[i] != board) {
      continue;
    }
    if (i + 1 < m_switchBoardCounter) {
      return m_switchBoards[i + 1];
    }
    return ppuc::v2::kNoBoard;
  }

  return ppuc::v2::kNoBoard;
}

bool RS485Comm::SendVirtualSwitchReply(uint8_t board, uint8_t nextBoard,
                                       bool* outHadState) {
  auto boardIt = m_virtualSwitchBoards.find(board);
  if (boardIt == m_virtualSwitchBoards.end() || m_pSerialPort == NULL ||
      !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig)) {
    return false;
  }

  auto& boardState = boardIt->second;
  const bool sendState = boardState.dirty;
  if (outHadState) {
    *outHadState = sendState;
  }

  const size_t switchBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.switchBits);
  const size_t payloadBytes =
      sendState ? ppuc::v2::SwitchPayloadBytes(m_runtimeConfig)
                : ppuc::v2::SwitchNoChangePayloadBytes();
  const size_t frameBytes =
      ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;
  uint8_t buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kSwitchStatusBytes +
                 ppuc::v2::kMaxSwitchBytes + ppuc::v2::kCrcBytes];

  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(
      sendState ? ppuc::v2::kFrameSwitchState : ppuc::v2::kFrameSwitchNoChange,
      sendState ? ppuc::v2::kFlagKeyframe : ppuc::v2::kFlagNone);
  buffer[2] = nextBoard;
  // Virtualized switch replies are host-synthesized chain continuations, not
  // a new host output snapshot. Reusing the last output sequence avoids making
  // the next real OutputStateFrame appear to jump by more than +1 on the
  // boards, which they correctly report as a sequence gap.
  buffer[3] = m_lastOutputSequenceSent;
  buffer[4] = m_epoch;
  buffer[5] = m_epoch;
  buffer[6] = m_lastOutputSequenceSent;
  buffer[7] = ppuc::v2::kStatusInSync;
  buffer[8] = 0;

  if (sendState) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    memcpy(&buffer[9], m_switchBitmap, switchBytes);
  }

  const uint16_t crc = ppuc::v2::Crc16Ccitt(
      buffer, ppuc::v2::kHeaderBytes + payloadBytes);
  buffer[ppuc::v2::kHeaderBytes + payloadBytes] =
      static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[ppuc::v2::kHeaderBytes + payloadBytes + 1] =
      static_cast<uint8_t>(crc & 0xff);

  if (m_debug) {
    DebugPrintf("Sent virtual V2 switch %s frame for board token %u -> %u",
                sendState ? "state" : "no-change", board, nextBoard);
  }

  if (!WriteBytes(sendState ? "VirtualSwitchStateFrame"
                            : "VirtualSwitchNoChangeFrame",
                  buffer, frameBytes)) {
    return false;
  }

  boardState.dirty = false;
  return true;
}

bool RS485Comm::ResyncSession() {
  ++m_epoch;
  if (m_debug || m_debugErrors) {
    ErrorPrintf("Starting V2 session resync epoch=%u", m_epoch);
  }
  if (!SendSetupFrame()) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if (!SendMappingFrames()) {
    return false;
  }
  // Drop any stale switch replies that were still in flight from the previous
  // epoch before the runtime loop starts polling again.
  sp_flush(m_pSerialPort, SP_BUF_INPUT);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  m_switchReplyMisses = 0;
  m_nextAllowedResyncAt =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(RS485_COMM_RESYNC_COOLDOWN_MS);
  m_needSessionResync = false;
  return true;
}

void RS485Comm::EnsureConfiguredBoardPresenceKnown() {
  if (m_boardPresenceFinalized) {
    return;
  }

  FinalizeConfiguredBoardPresence();
}

void RS485Comm::FinalizeConfiguredBoardPresence() {
  if (m_boardPresenceFinalized) {
    return;
  }

  printf("Configured boards:");
  if (m_configuredBoards.empty()) {
    printf(" none\n");
  } else {
    for (const uint8_t board : m_configuredBoards) {
      printf(" %u", board);
    }
    printf("\n");
  }

  m_virtualSwitchBoards.clear();
  m_virtualSwitchOwnerByNumber.clear();

  for (const uint8_t board : m_configuredBoards) {
    if (m_skippedBoards.find(board) != m_skippedBoards.end()) {
      VirtualSwitchBoardState boardState;
      boardState.board = board;
      const auto switches = m_switchNumbersByBoard.find(board);
      if (switches != m_switchNumbersByBoard.end()) {
        boardState.switchNumbers = switches->second;
        boardState.switchStates.assign(boardState.switchNumbers.size(), 0);
        for (const uint16_t switchNumber : boardState.switchNumbers) {
          m_virtualSwitchOwnerByNumber[switchNumber] = board;
        }
      }
      m_virtualSwitchBoards[board] = boardState;
      printf("Board %u skipped; virtualized with %zu switch(es).\n", board,
             boardState.switchNumbers.size());
      continue;
    }

    if (m_presentBoards.find(board) != m_presentBoards.end()) {
      printf("Board %u found.\n", board);
      continue;
    }

    VirtualSwitchBoardState boardState;
    boardState.board = board;
    const auto switches = m_switchNumbersByBoard.find(board);
    if (switches != m_switchNumbersByBoard.end()) {
      boardState.switchNumbers = switches->second;
      // Virtual switches start open until explicitly driven by the host.
      boardState.switchStates.assign(boardState.switchNumbers.size(), 0);
      for (const uint16_t switchNumber : boardState.switchNumbers) {
        m_virtualSwitchOwnerByNumber[switchNumber] = board;
      }
    }
    m_virtualSwitchBoards[board] = boardState;
    printf("Board %u missing; virtualized with %zu switch(es).\n", board,
           boardState.switchNumbers.size());
  }

  if (m_debug || m_debugErrors) {
    for (const auto& [board, state] : m_virtualSwitchBoards) {
      ErrorPrintf("Virtualized missing board %u with %zu switch(es)", board,
                  state.switchNumbers.size());
    }
  }

  m_boardPresenceFinalized = true;
}

bool RS485Comm::SendResetFrame() {
  if (m_pSerialPort == NULL) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kResetFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] =
      ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameReset, ppuc::v2::kFlagNone);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = m_epoch;
  const uint16_t crc = ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes);
  buffer[5] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[6] = static_cast<uint8_t>(crc & 0xff);

  if (WriteBytes("ResetFrame", buffer, sizeof(buffer))) {
    if (m_debug) {
      DebugPrintf("Sent V2 ResetFrame seq=%u", buffer[3]);
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

  // Mapping bursts are large and immediately follow setup/cutover. Pace them
  // like config frames so boards have time to switch from fallback RX to the
  // steady-state V2 receive path without dropping frames.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  uint8_t buffer[ppuc::v2::kMappingFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameMapping,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = m_epoch;
  buffer[5] = domain;
  buffer[6] = 0;
  buffer[7] = static_cast<uint8_t>((index >> 8) & 0xff);
  buffer[8] = static_cast<uint8_t>(index & 0xff);
  buffer[9] = static_cast<uint8_t>((number >> 8) & 0xff);
  buffer[10] = static_cast<uint8_t>(number & 0xff);
  const uint16_t crc = ppuc::v2::Crc16Ccitt(
      buffer, ppuc::v2::kHeaderBytes + ppuc::v2::kMappingPayloadBytes);
  buffer[11] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[12] = static_cast<uint8_t>(crc & 0xff);

  return WriteBytes("MappingFrame", buffer, sizeof(buffer));
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
  if (m_pSerialPort == NULL ||
      !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig) ||
      !ppuc::v2::IsValidBoard(nextBoard)) {
    return false;
  }

  const size_t coilBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.coilBits);
  const size_t lampBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.lampBits);
  const size_t payloadBytes = coilBytes + lampBytes + ppuc::v2::kGiBytes;
  const size_t frameBytes =
      ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;
  uint8_t buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxCoilBytes +
                 ppuc::v2::kMaxLampBytes + ppuc::v2::kGiBytes +
                 ppuc::v2::kCrcBytes];

  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameOutputState,
                                            ppuc::v2::kFlagKeyframe);
  buffer[2] = nextBoard;
  buffer[3] = m_sequence++;
  buffer[4] = m_epoch;
  m_lastOutputSequenceSent = buffer[3];

  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    memcpy(&buffer[5], m_coilBitmap, coilBytes);
    memcpy(&buffer[5 + coilBytes], m_lampBitmap, lampBytes);
    memset(&buffer[5 + coilBytes + lampBytes], 0, ppuc::v2::kGiBytes);
    for (uint8_t giString = 0; giString < ppuc::v2::kGiStrings; ++giString) {
      ppuc::v2::SetPackedNibble(&buffer[5 + coilBytes + lampBytes], giString,
                                m_giLevels[giString]);
    }
  }

  const uint16_t crc =
      ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes + payloadBytes);
  buffer[5 + payloadBytes] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[6 + payloadBytes] = static_cast<uint8_t>(crc & 0xff);

  return WriteBytes("OutputStateFrame", buffer, frameBytes);
}

bool RS485Comm::ReceiveConfigAck(uint8_t boardId, uint8_t topic, uint8_t index,
                                 uint8_t key) {
  if (m_pSerialPort == NULL) {
    return false;
  }

  uint8_t header[ppuc::v2::kHeaderBytes];
  uint8_t buffer[ppuc::v2::kConfigAckFrameBytes];
  auto start = std::chrono::steady_clock::now();

  while ((std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start))
             .count() < RS485_COMM_CONFIG_ACK_TIMEOUT_US) {
    if ((int)sp_input_waiting(m_pSerialPort) <= 0) {
      continue;
    }

    sp_blocking_read(m_pSerialPort, &header[0], 1,
                     RS485_COMM_SERIAL_READ_TIMEOUT);
    if (header[0] != ppuc::v2::kSyncByte) {
      continue;
    }

    sp_blocking_read(m_pSerialPort, &header[1], ppuc::v2::kHeaderBytes - 1,
                     RS485_COMM_SERIAL_READ_TIMEOUT);
    const ppuc::v2::FrameType frameType = ppuc::v2::ExtractType(header[1]);
    if (frameType != ppuc::v2::kFrameConfigAck) {
      size_t payloadBytes = 0;
      switch (frameType) {
        case ppuc::v2::kFrameSetup:
          payloadBytes = ppuc::v2::kSetupPayloadBytes;
          break;
        case ppuc::v2::kFrameMapping:
          payloadBytes = ppuc::v2::kMappingPayloadBytes;
          break;
        case ppuc::v2::kFrameConfig:
          payloadBytes = ppuc::v2::kConfigPayloadBytes;
          break;
        case ppuc::v2::kFrameOutputState:
          payloadBytes = ppuc::v2::OutputPayloadBytes(m_runtimeConfig);
          break;
        case ppuc::v2::kFrameSwitchState:
          payloadBytes = ppuc::v2::SwitchPayloadBytes(m_runtimeConfig);
          break;
        case ppuc::v2::kFrameSwitchNoChange:
          payloadBytes = ppuc::v2::SwitchNoChangePayloadBytes();
          break;
        case ppuc::v2::kFrameReset:
        case ppuc::v2::kFrameHeartbeat:
        case ppuc::v2::kFrameError:
          payloadBytes = 0;
          break;
        default:
          if (m_debug) {
            DebugPrintf(
                "Ignoring unexpected V2 frame type 0x%02X while waiting for config ack",
                static_cast<unsigned>(frameType));
          }
          continue;
      }

      uint8_t discard[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxCoilBytes +
                      ppuc::v2::kMaxLampBytes + ppuc::v2::kGiBytes +
                      ppuc::v2::kCrcBytes];
      if (payloadBytes + ppuc::v2::kCrcBytes > sizeof(discard)) {
        if (m_debug || m_debugErrors) {
          ErrorPrintf(
              "Cannot discard V2 frame type 0x%02X with %zu-byte payload while waiting for config ack",
              static_cast<unsigned>(frameType), payloadBytes);
        }
        continue;
      }
      sp_blocking_read(m_pSerialPort, discard, payloadBytes + ppuc::v2::kCrcBytes,
                       RS485_COMM_SERIAL_READ_TIMEOUT);
      continue;
    }

    memcpy(buffer, header, ppuc::v2::kHeaderBytes);
    sp_blocking_read(m_pSerialPort, &buffer[ppuc::v2::kHeaderBytes],
                     ppuc::v2::kConfigAckPayloadBytes + ppuc::v2::kCrcBytes,
                     RS485_COMM_SERIAL_READ_TIMEOUT);

    const uint16_t receivedCrc =
        (static_cast<uint16_t>(buffer[ppuc::v2::kConfigAckFrameBytes - 2]) << 8) |
        static_cast<uint16_t>(buffer[ppuc::v2::kConfigAckFrameBytes - 1]);
    const uint16_t calculatedCrc = ppuc::v2::Crc16Ccitt(
        buffer, ppuc::v2::kHeaderBytes + ppuc::v2::kConfigAckPayloadBytes);
    if (receivedCrc != calculatedCrc) {
      ErrorPrintf("Invalid V2 config ack CRC: got=%04X expected=%04X",
                  receivedCrc, calculatedCrc);
      continue;
    }

    if (buffer[5] != boardId || buffer[6] != topic || buffer[7] != index ||
        buffer[8] != key) {
      ErrorPrintf("Unexpected V2 config ack: board=%u topic=%u index=%u key=%u",
                  buffer[5], buffer[6], buffer[7], buffer[8]);
      continue;
    }

    if (buffer[9] != ppuc::v2::kConfigAckAccepted) {
      ErrorPrintf(
          "Rejected V2 config ack: board=%u topic=%u index=%u key=%u status=%u",
          boardId, topic, index, key, buffer[9]);
      return false;
    }

    return true;
  }

  ErrorPrintf("Timed out waiting for V2 config ack: board=%u topic=%u index=%u key=%u",
              boardId, topic, index, key);
  return false;
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
  if (m_pSerialPort == NULL ||
      !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig)) {
    return false;
  }

  const size_t switchBytes = ppuc::v2::BitsToBytes(m_runtimeConfig.switchBits);
  uint8_t header[ppuc::v2::kHeaderBytes];
  uint8_t buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kSwitchStatusBytes +
                 ppuc::v2::kMaxSwitchBytes +
                 ppuc::v2::kCrcBytes];

  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  while ((std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start))
             .count() < 20000) {
    if ((int)sp_input_waiting(m_pSerialPort) <= 0) {
      continue;
    }

    sp_blocking_read(m_pSerialPort, &header[0], 1,
                     RS485_COMM_SERIAL_READ_TIMEOUT);
    if (header[0] != ppuc::v2::kSyncByte) {
      continue;
    }

    sp_blocking_read(m_pSerialPort, &header[1], ppuc::v2::kHeaderBytes - 1,
                     RS485_COMM_SERIAL_READ_TIMEOUT);
    const ppuc::v2::FrameType frameType = ppuc::v2::ExtractType(header[1]);
    size_t payloadBytes = 0;
    if (frameType == ppuc::v2::kFrameSwitchState) {
      payloadBytes = ppuc::v2::SwitchPayloadBytes(m_runtimeConfig);
      if (outHadState) {
        *outHadState = true;
      }
      if (m_debug) {
        DebugPrintf("Received V2 switch state frame for board token %u",
                    expectedBoard);
      }
    } else if (frameType == ppuc::v2::kFrameSwitchNoChange) {
      payloadBytes = ppuc::v2::SwitchNoChangePayloadBytes();
      if (outHadState) {
        *outHadState = false;
      }
      if (m_debug) {
        DebugPrintf("Received V2 switch no-change frame for board token %u",
                    expectedBoard);
      }
    } else {
      if (m_debug) {
        DebugPrintf(
            "Ignoring unexpected V2 frame type 0x%02X while waiting for switch reply",
            static_cast<unsigned>(frameType));
      }
      continue;
    }

    memcpy(buffer, header, ppuc::v2::kHeaderBytes);
    sp_blocking_read(m_pSerialPort, &buffer[ppuc::v2::kHeaderBytes],
                     payloadBytes + ppuc::v2::kCrcBytes,
                     RS485_COMM_SERIAL_READ_TIMEOUT);

    if (outNextBoard) {
      *outNextBoard = header[2];
    }

    const uint8_t epochSeen = buffer[ppuc::v2::kHeaderBytes];
    const uint8_t lastHostSequenceSeen = buffer[ppuc::v2::kHeaderBytes + 1];
    const uint8_t statusFlags = buffer[ppuc::v2::kHeaderBytes + 2];

    if (epochSeen != m_epoch) {
      if (m_debug || m_debugErrors) {
        ErrorPrintf("V2 switch reply epoch mismatch: board=%u seen=%u expected=%u",
                    expectedBoard, epochSeen, m_epoch);
      }
      return false;
    }
    if (lastHostSequenceSeen != m_lastOutputSequenceSent) {
      if (m_debug || m_debugErrors) {
        ErrorPrintf(
            "V2 switch reply sequence mismatch: board=%u seen=%u expected=%u",
            expectedBoard, lastHostSequenceSeen, m_lastOutputSequenceSent);
      }
      return false;
    }
    if ((statusFlags & (ppuc::v2::kStatusNeedsSetup |
                        ppuc::v2::kStatusMappingIncomplete |
                        ppuc::v2::kStatusSequenceGap)) != 0) {
      if (m_debug || m_debugErrors) {
        ErrorPrintf("V2 switch reply requested resync: board=%u flags=0x%02X",
                    expectedBoard, statusFlags);
      }
      return false;
    }

    const size_t frameBytes =
        ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;
    const uint16_t receivedCrc =
        (static_cast<uint16_t>(buffer[frameBytes - 2]) << 8) |
        static_cast<uint16_t>(buffer[frameBytes - 1]);
    const uint16_t calculatedCrc =
        ppuc::v2::Crc16Ccitt(buffer, frameBytes - ppuc::v2::kCrcBytes);
    if (receivedCrc != calculatedCrc) {
      if (m_debug || m_debugErrors) {
        ErrorPrintf("Invalid V2 switch frame CRC: got=%04X expected=%04X",
                    receivedCrc, calculatedCrc);
      }
      return false;
    }

    if (frameType == ppuc::v2::kFrameSwitchState) {
      ApplySwitchBitmapDiff(
          &buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kSwitchStatusBytes],
          switchBytes);
      if (m_debug) {
        DebugPrintf("Applied V2 switch bitmap diff for board token %u",
                    expectedBoard);
      }
    }
    return true;
  }

  if (m_debug || m_debugErrors) {
    ErrorPrintf("Timed out waiting for V2 switch reply for board token %u",
                expectedBoard);
  }
  sp_flush(m_pSerialPort, SP_BUF_INPUT);
  return false;
}
