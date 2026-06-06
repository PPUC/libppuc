#include "RS485Comm.h"

#include <algorithm>
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

namespace {
const char* SwitchStatusFlagName(uint8_t flag) {
  switch (flag) {
    case ppuc::v2::kStatusInSync:
      return "in-sync";
    case ppuc::v2::kStatusNeedsSetup:
      return "needs-setup";
    case ppuc::v2::kStatusMappingIncomplete:
      return "mapping-incomplete";
    case ppuc::v2::kStatusSequenceGap:
      return "sequence-gap";
    case ppuc::v2::kStatusParserResynced:
      return "parser-resynced";
    case ppuc::v2::kStatusSwitchOverflow:
      return "switch-overflow";
    default:
      return "unknown";
  }
}
}  // namespace

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
  m_nextSwitchPollAt = std::chrono::steady_clock::now();
  m_nextSwitchRefreshAt = std::chrono::steady_clock::time_point::max();
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

void RS485Comm::SetSwitchReplyDelayUs(uint32_t delayUs) {
  m_switchReplyDelayUs = delayUs;
}

void RS485Comm::SetSwitchRefreshIdleMs(uint32_t idleMs) {
  m_switchRefreshIdleMs = idleMs;
  m_nextSwitchRefreshAt =
      idleMs == 0
          ? std::chrono::steady_clock::time_point::max()
          : std::chrono::steady_clock::now() + std::chrono::milliseconds(idleMs);
}

void RS485Comm::SetCoilHoldFrames(uint8_t holdFrames) {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  m_coilHoldFrameCount = holdFrames;
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

int64_t RS485Comm::SwitchReplyWindowUs() const {
  const uint32_t boardCount = m_switchBoardCounter == 0 ? 1 : m_switchBoardCounter;
  // Preserve the previously working fixed host-side reply window when no
  // experimental per-board delay is configured. The CLI knob adds budget on
  // top of this known baseline instead of replacing it.
  // The host window needs to cover one polling cycle plus the board-side
  // debounce interval and a bit of serial/loop jitter. The previous 30 ms
  // baseline is still marginal on some runs with trough/outhole switches.
  const int64_t baseUs = 40000;
  const int64_t configuredDelayUs =
      static_cast<int64_t>(m_switchReplyDelayUs) * static_cast<int64_t>(boardCount);
  return baseUs + configuredDelayUs;
}

uint32_t RS485Comm::SwitchReadTimeoutMs() const {
  // Keep the total chain window generous, but avoid a disproportionately long
  // per-read block when the configured board-side reply delay is very small.
  const uint32_t derivedMs =
      static_cast<uint32_t>((m_switchReplyDelayUs + 999) / 1000) + 1;
  return std::max<uint32_t>(RS485_COMM_SERIAL_READ_TIMEOUT, derivedMs);
}

bool RS485Comm::WriteBytes(const char* context, const uint8_t* buffer,
                           size_t size) {
  if (m_pSerialPort == NULL) {
    return false;
  }

  const int written = sp_blocking_write(m_pSerialPort, buffer, size,
                                        RS485_COMM_SERIAL_WRITE_TIMEOUT);
  if (written == static_cast<int>(size)) {
    return true;
  }

  if (m_debug || m_debugErrors) {
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
  }

  return false;
}

void RS485Comm::Run() {
  m_stopRequested = false;
  m_nextSwitchPollAt =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(RS485_COMM_SWITCH_POLL_STARTUP_HOLD_MS);
  m_nextSwitchRefreshAt =
      m_switchRefreshIdleMs == 0
          ? std::chrono::steady_clock::time_point::max()
          : std::chrono::steady_clock::now() +
                std::chrono::milliseconds(m_switchRefreshIdleMs);
  m_pThread = new std::thread([this]() {
    LogMessage("RS485Comm run thread starting");

    while (!m_stopRequested) {
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
        if (event->sourceId == EVENT_SOURCE_EFFECT) {
          std::this_thread::sleep_for(
              std::chrono::microseconds(RS485_COMM_EFFECT_EVENT_SPACING_US));
        }
        delete event;
      }

      if (!m_runtimeEnabled) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      if (m_needSessionResync) {
        if (!ResyncSession()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
      }

      const auto now = std::chrono::steady_clock::now();
      uint8_t nextBoard = ppuc::v2::kNoBoard;
      if (m_switchBoardCounter > 0 && now >= m_nextSwitchPollAt) {
        nextBoard = m_switchBoards[0];
      }
      const bool sendSwitchRefresh =
          nextBoard != ppuc::v2::kNoBoard && m_switchRefreshIdleMs > 0 &&
          now >= m_nextSwitchRefreshAt;

      QueuedOutputSnapshot snapshot;
      bool haveQueuedSnapshot = false;
      if (!sendSwitchRefresh) {
        std::lock_guard<std::mutex> lock(m_outputQueueMutex);
        if (!m_outputSnapshots.empty()) {
          snapshot = m_outputSnapshots.front();
          m_outputSnapshots.pop();
          haveQueuedSnapshot = true;
        }
      }

      if (!haveQueuedSnapshot) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(RS485_COMM_OUTPUT_FRAME_INTERVAL_MS));
      }

      uint8_t coilBitmap[ppuc::v2::kMaxCoilBytes] = {0};
      uint8_t lampBitmap[ppuc::v2::kMaxLampBytes] = {0};
      uint8_t giLevels[ppuc::v2::kGiStrings] = {0};
      uint8_t holdFrames[ppuc::v2::kMaxCoilBits] = {0};
      {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (haveQueuedSnapshot) {
          memcpy(coilBitmap, snapshot.coilBitmap, sizeof(coilBitmap));
          memcpy(lampBitmap, snapshot.lampBitmap, sizeof(lampBitmap));
          memcpy(giLevels, snapshot.giLevels, sizeof(giLevels));
        } else {
          memcpy(coilBitmap, m_coilBitmap, sizeof(coilBitmap));
          memcpy(lampBitmap, m_lampBitmap, sizeof(lampBitmap));
          memcpy(giLevels, m_giLevels, sizeof(giLevels));
        }
        memcpy(holdFrames, m_coilHoldFrames, sizeof(holdFrames));
        ApplyCoilHoldover(coilBitmap, holdFrames);
      }

      const bool sent =
          sendSwitchRefresh
              ? SendSwitchRefreshFrame(nextBoard)
              : SendOutputStateFrameFromBuffers(nextBoard, coilBitmap,
                                                lampBitmap, giLevels);
      if (!sent) {
        continue;
      }
      if (!sendSwitchRefresh) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        ConsumeCoilHoldoverLocked(holdFrames);
      }
      if (nextBoard != ppuc::v2::kNoBoard) {
        ReceiveSwitchStateChain(nextBoard);
        if (sendSwitchRefresh && m_switchRefreshIdleMs > 0) {
          m_nextSwitchRefreshAt =
              std::chrono::steady_clock::now() +
              std::chrono::milliseconds(m_switchRefreshIdleMs);
        }
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
        const bool coilOn = event->value != 0;
        ppuc::v2::SetBitmapBit(m_coilBitmap, it->second, coilOn);
        if (coilOn) {
          m_coilHoldFrames[it->second] = m_coilHoldFrameCount;
        }
        QueueOutputSnapshotLocked();
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

    case EVENT_READ_SWITCHES:
      delete event;
      return;

    case EVENT_SOURCE_EFFECT:
      {
        std::lock_guard<std::mutex> lock(m_eventQueueMutex);
        if (m_events.size() >= RS485_COMM_QUEUE_SIZE_MAX) {
          delete event;
          return;
        }
        m_events.push(event);
      }
      return;
  }

  // Legacy RS485 event packets are no longer part of the active v2 transport.
  // Drop any unhandled event types instead of queueing them for the removed
  // legacy wire path.
  delete event;
}

void RS485Comm::ClearQueuedEvents() {
  std::lock_guard<std::mutex> lock(m_eventQueueMutex);
  while (!m_events.empty()) {
    delete m_events.front();
    m_events.pop();
  }
}

void RS485Comm::ClearQueuedOutputSnapshots() {
  std::lock_guard<std::mutex> lock(m_outputQueueMutex);
  while (!m_outputSnapshots.empty()) {
    m_outputSnapshots.pop();
  }
}

void RS485Comm::ClearOutputState() {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  memset(m_coilBitmap, 0, sizeof(m_coilBitmap));
  memset(m_coilHoldFrames, 0, sizeof(m_coilHoldFrames));
  memset(m_lampBitmap, 0, sizeof(m_lampBitmap));
  memset(m_giLevels, 0, sizeof(m_giLevels));
}

void RS485Comm::ApplyCoilHoldover(uint8_t* coils,
                                  const uint8_t* holdFrames) const {
  const uint16_t coilBits =
      std::min<uint16_t>(m_runtimeConfig.coilBits, ppuc::v2::kMaxCoilBits);
  for (uint16_t i = 0; i < coilBits; ++i) {
    if (holdFrames[i] == 0) {
      continue;
    }
    ppuc::v2::SetBitmapBit(coils, i, true);
  }
}

void RS485Comm::ConsumeCoilHoldoverLocked(const uint8_t* holdFrames) {
  const uint16_t coilBits =
      std::min<uint16_t>(m_runtimeConfig.coilBits, ppuc::v2::kMaxCoilBits);
  for (uint16_t i = 0; i < coilBits; ++i) {
    if (holdFrames[i] == 0 || m_coilHoldFrames[i] == 0) {
      continue;
    }
    --m_coilHoldFrames[i];
  }
}

void RS485Comm::QueueOutputSnapshotLocked() {
  std::lock_guard<std::mutex> queueLock(m_outputQueueMutex);
  if (m_outputSnapshots.size() >= RS485_COMM_OUTPUT_QUEUE_SIZE_MAX) {
    if (m_debug || m_debugErrors) {
      ErrorPrintf("Dropping oldest queued output snapshot: queue_full");
    }
    m_outputSnapshots.pop();
  }

  QueuedOutputSnapshot snapshot;
  memcpy(snapshot.coilBitmap, m_coilBitmap, sizeof(snapshot.coilBitmap));
  memcpy(snapshot.lampBitmap, m_lampBitmap, sizeof(snapshot.lampBitmap));
  memcpy(snapshot.giLevels, m_giLevels, sizeof(snapshot.giLevels));
  m_outputSnapshots.push(snapshot);
}

bool RS485Comm::SendOutputsOffFrame() {
  return SendOutputStateFrame(ppuc::v2::kNoBoard);
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

  // Once the worker thread is down, no queued state changes will be transmitted
  // anymore. Drop them and send a deterministic all-off snapshot instead.
  ClearQueuedEvents();
  ClearQueuedOutputSnapshots();
  ClearOutputState();
  SendOutputsOffFrame();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  SendOutputsOffFrame();
  sp_drain(m_pSerialPort);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Some USB-RS485 adapters/drivers keep modem-control/flow-control state
  // across close/open cycles. Drain and explicitly deassert those lines before
  // closing so the next run starts from a neutral adapter state.
  sp_drain(m_pSerialPort);
  sp_flush(m_pSerialPort, SP_BUF_INPUT);
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

bool RS485Comm::RestartBoards() {
  if (m_pSerialPort == NULL) {
    return false;
  }

  m_configFailed = false;
  m_configEarlyAbortLogged = false;
  m_initialConfigAckMissStreak = 0;
  memset(m_initialConfigAckMissesByBoard, 0,
         sizeof(m_initialConfigAckMissesByBoard));
  m_configEarlyAbortBoard = ppuc::v2::kNoBoard;
  m_configAckFailedBoards.clear();
  if (!SendRestartFrame()) {
    return false;
  }
  sp_drain(m_pSerialPort);
  // Soft restart keeps the RP2040 alive, but a board with heavier local
  // teardown work (for example WS2812/effects state on the first board on the
  // bus) may need a little longer before it can reliably acknowledge the first
  // config frame of the next session.
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  sp_flush(m_pSerialPort, SP_BUF_INPUT);
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  return true;
}

bool RS485Comm::ResetBoards() {
  if (m_pSerialPort == NULL) {
    return false;
  }

  m_configFailed = false;
  m_configEarlyAbortLogged = false;
  m_initialConfigAckMissStreak = 0;
  memset(m_initialConfigAckMissesByBoard, 0,
         sizeof(m_initialConfigAckMissesByBoard));
  m_configEarlyAbortBoard = ppuc::v2::kNoBoard;
  m_configAckFailedBoards.clear();
  if (!SendResetFrame()) {
    return false;
  }
  std::this_thread::sleep_for(
      std::chrono::milliseconds(WAIT_FOR_IO_BOARD_RESET));
  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  return true;
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
  memset(m_activeBoards, 0, sizeof(m_activeBoards));
  m_presentBoards.clear();
  m_boardPresenceFinalized = false;
  m_configFailed = false;
  m_configEarlyAbortLogged = false;
  m_initialConfigAckMissStreak = 0;
  memset(m_initialConfigAckMissesByBoard, 0,
         sizeof(m_initialConfigAckMissesByBoard));
  m_configEarlyAbortBoard = ppuc::v2::kNoBoard;
  m_configAckFailedBoards.clear();

  return true;
}

void RS485Comm::RegisterSwitchBoard(uint8_t number) {
  if (m_switchBoardCounter < RS485_COMM_MAX_BOARDS &&
      number < RS485_COMM_MAX_BOARDS) {
    m_switchBoards[m_switchBoardCounter] = number;
    m_switchBoardCounter++;
  }
}

bool RS485Comm::IsBoardActive(uint8_t number) const {
  return IsBoardPresent(number);
}

void RS485Comm::SetConfiguredBoards(const std::vector<uint8_t>& boards) {
  m_configuredBoards = boards;
  m_boardPresenceFinalized = false;
}

void RS485Comm::SetSwitchNumbersByBoard(
    const std::unordered_map<uint8_t, std::vector<uint16_t>>& switchesByBoard) {
  m_switchNumbersByBoard = switchesByBoard;
  RebuildSwitchOwnershipMasks();
  m_boardPresenceFinalized = false;
}

void RS485Comm::SetSkippedBoards(const std::set<uint8_t>& boards) {
  m_skippedBoards = boards;
  m_boardPresenceFinalized = false;
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
    if (m_presentBoards.find(board) != m_presentBoards.end()) {
      printf("Board %u found.\n", board);
      continue;
    }

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

    if (m_skippedBoards.find(board) != m_skippedBoards.end()) {
      printf("Board %u skipped; virtualized with %zu switch(es).\n", board,
             boardState.switchNumbers.size());
    } else {
      printf("Board %u missing; virtualized with %zu switch(es).\n", board,
             boardState.switchNumbers.size());
    }
  }

  m_boardPresenceFinalized = true;
}

bool RS485Comm::IsBoardPresent(uint8_t board) const {
  return m_presentBoards.find(board) != m_presentBoards.end();
}

bool RS485Comm::IsBoardVirtualized(uint8_t board) const {
  const_cast<RS485Comm*>(this)->EnsureConfiguredBoardPresenceKnown();
  return m_virtualSwitchBoards.find(board) != m_virtualSwitchBoards.end();
}

bool RS485Comm::HadConfigurationFailure() const { return m_configFailed; }

bool RS485Comm::ShouldAbortConfigurationEarly() const {
  if (m_configEarlyAbortBoard != ppuc::v2::kNoBoard) {
    return true;
  }

  return m_presentBoards.empty() &&
         m_initialConfigAckMissStreak >=
             RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD;
}

std::vector<uint8_t> RS485Comm::GetMissingConfiguredBoards() const {
  std::vector<uint8_t> missingBoards;
  missingBoards.reserve(m_configuredBoards.size());
  for (const uint8_t board : m_configuredBoards) {
    if (m_skippedBoards.find(board) != m_skippedBoards.end()) {
      continue;
    }
    if (m_presentBoards.find(board) != m_presentBoards.end()) {
      continue;
    }
    missingBoards.push_back(board);
  }
  return missingBoards;
}

void RS485Comm::SetActiveSwitchBoards(const std::vector<uint8_t>& boards) {
  m_switchBoardCounter = 0;
  for (const uint8_t board : boards) {
    if (m_switchBoardCounter >= RS485_COMM_MAX_BOARDS ||
        board >= RS485_COMM_MAX_BOARDS) {
      break;
    }
    m_switchBoards[m_switchBoardCounter++] = board;
  }
}

void RS485Comm::RebuildSwitchOwnershipMasks() {
  memset(m_switchOwnershipMaskByBoard, 0, sizeof(m_switchOwnershipMaskByBoard));
  for (const auto& [board, switchNumbers] : m_switchNumbersByBoard) {
    if (board >= RS485_COMM_MAX_BOARDS) {
      continue;
    }
    for (const uint16_t switchNumber : switchNumbers) {
      const auto it = m_switchNumberToIndex.find(switchNumber);
      if (it == m_switchNumberToIndex.end()) {
        continue;
      }
      ppuc::v2::SetBitmapBit(m_switchOwnershipMaskByBoard[board], it->second,
                             true);
    }
  }
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
    NoteSwitchActivity(number);
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

  RebuildSwitchOwnershipMasks();
}

void RS485Comm::SetButtonSwitchNumbers(const std::set<uint16_t>& numbers) {
  m_buttonSwitchNumbers = numbers;
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

uint32_t RS485Comm::GetCleanSwitchReplyChainCount() const {
  return m_cleanSwitchReplyChainCount.load();
}

bool RS485Comm::SendConfigEvent(ConfigEvent* event) {
  if (m_pSerialPort == NULL || !event) {
    delete event;
    return false;
  }

  if (ShouldAbortConfigurationEarly()) {
    delete event;
    return false;
  }

  // Wait a bit to not exceed the output buffer in case of large configurations.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

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
      m_initialConfigAckMissStreak = 0;
      if (buffer[5] < RS485_COMM_MAX_BOARDS) {
        m_initialConfigAckMissesByBoard[buffer[5]] = 0;
      }
      m_presentBoards.insert(buffer[5]);
      if (buffer[5] < RS485_COMM_MAX_BOARDS) {
        m_activeBoards[buffer[5]] = true;
      }
      return true;
    }

    sp_flush(m_pSerialPort, SP_BUF_INPUT);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  ErrorPrintf("Missing V2 config ack: board=%u topic=%u index=%u key=%u",
              buffer[5], buffer[6], buffer[7], buffer[8]);
  m_configFailed = true;
  if (m_presentBoards.empty() &&
      m_initialConfigAckMissStreak <
          RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD) {
    ++m_initialConfigAckMissStreak;
  }
  if (buffer[5] < RS485_COMM_MAX_BOARDS &&
      m_initialConfigAckMissesByBoard[buffer[5]] <
          RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD) {
    ++m_initialConfigAckMissesByBoard[buffer[5]];
    if (m_presentBoards.find(buffer[5]) == m_presentBoards.end() &&
        m_initialConfigAckMissesByBoard[buffer[5]] >=
            RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD) {
      m_configEarlyAbortBoard = buffer[5];
    }
  }
  m_configAckFailedBoards.insert(buffer[5]);
  if (!m_configEarlyAbortLogged && ShouldAbortConfigurationEarly()) {
    if (m_configEarlyAbortBoard != ppuc::v2::kNoBoard) {
      printf(
          "PPUC: board %u missed its first %u config ACKs; aborting startup attempt early.\n",
          static_cast<unsigned>(m_configEarlyAbortBoard),
          static_cast<unsigned>(RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD));
    } else {
      printf(
          "PPUC: the first %u config frames received no ACKs; aborting startup attempt early.\n",
          static_cast<unsigned>(RS485_COMM_INITIAL_CONFIG_ACK_MISS_THRESHOLD));
    }
    m_configEarlyAbortLogged = true;
  }
  return false;
}

bool RS485Comm::ReceiveConfigAck(uint8_t boardId, uint8_t topic, uint8_t index,
                                 uint8_t key) {
  if (m_pSerialPort == NULL) {
    return false;
  }

  auto start = std::chrono::steady_clock::now();
  uint8_t header[ppuc::v2::kHeaderBytes];
  uint8_t buffer[ppuc::v2::kConfigAckFrameBytes];
  auto readExact = [this](uint8_t* dst, size_t bytes) -> bool {
    // libserialport may return fewer bytes than requested even with the
    // blocking API. During Linux board configuration that caused partial
    // config-ack frames to be parsed as failures, forcing retries.
    size_t totalRead = 0;
    while (totalRead < bytes) {
      const int read = sp_blocking_read(
          m_pSerialPort, dst + totalRead, bytes - totalRead,
          RS485_COMM_SERIAL_READ_TIMEOUT);
      if (read <= 0) {
        if (m_debug) {
          DebugPrintf("Timed out reading V2 config ack bytes (%zu/%zu read)",
                      totalRead, bytes);
        }
        return false;
      }
      totalRead += static_cast<size_t>(read);
    }
    return true;
  };

  while ((std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start))
             .count() < RS485_COMM_CONFIG_ACK_TIMEOUT_US) {
    if ((int)sp_input_waiting(m_pSerialPort) <= 0) {
      continue;
    }

    if (!readExact(&header[0], 1)) {
      continue;
    }
    if (header[0] != ppuc::v2::kSyncByte) {
      continue;
    }

    if (!readExact(&header[1], ppuc::v2::kHeaderBytes - 1)) {
      continue;
    }
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
        case ppuc::v2::kFrameRestart:
        case ppuc::v2::kFrameReset:
        case ppuc::v2::kFrameHeartbeat:
        case ppuc::v2::kFrameError:
          payloadBytes = 0;
          break;
        default:
          return false;
      }
      uint8_t discard[ppuc::v2::kHeaderBytes + ppuc::v2::kConfigPayloadBytes +
                      ppuc::v2::kCrcBytes];
      if (payloadBytes + ppuc::v2::kCrcBytes > sizeof(discard)) {
        return false;
      }
      // Config startup is synchronous, but stale bytes from an earlier frame
      // can still appear here. Drain the full frame before looking for the ack.
      if (!readExact(discard, payloadBytes + ppuc::v2::kCrcBytes)) {
        continue;
      }
      continue;
    }

    memcpy(buffer, header, ppuc::v2::kHeaderBytes);
    if (!readExact(&buffer[ppuc::v2::kHeaderBytes],
                   ppuc::v2::kConfigAckPayloadBytes + ppuc::v2::kCrcBytes)) {
      continue;
    }

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
    ++m_cleanSwitchReplyChainCount;
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

  if (m_switchReplyDelayUs > 0) {
    std::this_thread::sleep_for(
        std::chrono::microseconds(m_switchReplyDelayUs));
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
  m_needSessionResync = false;
  m_nextSwitchPollAt =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(RS485_COMM_SWITCH_POLL_STARTUP_HOLD_MS);
  return true;
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

bool RS485Comm::SendRestartFrame() {
  if (m_pSerialPort == NULL) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kRestartFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameRestart,
                                            ppuc::v2::kFlagNone);
  buffer[2] = ppuc::v2::kNoBoard;
  buffer[3] = m_sequence++;
  buffer[4] = m_epoch;
  const uint16_t crc = ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes);
  buffer[5] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[6] = static_cast<uint8_t>(crc & 0xff);

  if (WriteBytes("RestartFrame", buffer, sizeof(buffer))) {
    if (m_debug) {
      DebugPrintf("Sent V2 RestartFrame seq=%u", buffer[3]);
    }
    return true;
  }

  return false;
}

bool RS485Comm::SendSwitchRefreshFrame(uint8_t nextBoard) {
  if (m_pSerialPort == NULL ||
      !ppuc::v2::IsValidRuntimeConfig(m_runtimeConfig) ||
      !ppuc::v2::IsValidBoard(nextBoard)) {
    return false;
  }

  uint8_t buffer[ppuc::v2::kSwitchRefreshFrameBytes];
  buffer[0] = ppuc::v2::kSyncByte;
  buffer[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameSwitchRefresh,
                                            ppuc::v2::kFlagNone);
  buffer[2] = nextBoard;
  buffer[3] = m_sequence++;
  buffer[4] = m_epoch;
  m_lastOutputSequenceSent = buffer[3];
  const uint16_t crc = ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes);
  buffer[5] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[6] = static_cast<uint8_t>(crc & 0xff);

  if (m_debug) {
    DebugPrintf("Sent V2 SwitchRefreshFrame seq=%u firstBoard=%u", buffer[3],
                nextBoard);
  }
  return WriteBytes("SwitchRefreshFrame", buffer, sizeof(buffer));
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

  uint8_t coilBitmap[ppuc::v2::kMaxCoilBytes] = {0};
  uint8_t lampBitmap[ppuc::v2::kMaxLampBytes] = {0};
  uint8_t giLevels[ppuc::v2::kGiStrings] = {0};
  uint8_t holdFrames[ppuc::v2::kMaxCoilBits] = {0};
  std::lock_guard<std::mutex> lock(m_stateMutex);
  memcpy(coilBitmap, m_coilBitmap, sizeof(coilBitmap));
  memcpy(lampBitmap, m_lampBitmap, sizeof(lampBitmap));
  memcpy(giLevels, m_giLevels, sizeof(giLevels));
  memcpy(holdFrames, m_coilHoldFrames, sizeof(holdFrames));
  ApplyCoilHoldover(coilBitmap, holdFrames);
  return SendOutputStateFrameFromBuffers(nextBoard, coilBitmap, lampBitmap,
                                         giLevels);
}

bool RS485Comm::SendOutputStateFrameFromBuffers(uint8_t nextBoard,
                                                const uint8_t* coils,
                                                const uint8_t* lamps,
                                                const uint8_t* giLevels) {
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

  memcpy(&buffer[5], coils, coilBytes);
  memcpy(&buffer[5 + coilBytes], lamps, lampBytes);
  memset(&buffer[5 + coilBytes + lampBytes], 0, ppuc::v2::kGiBytes);
  for (uint8_t giString = 0; giString < ppuc::v2::kGiStrings; ++giString) {
    ppuc::v2::SetPackedNibble(&buffer[5 + coilBytes + lampBytes], giString,
                              giLevels[giString]);
  }

  const uint16_t crc =
      ppuc::v2::Crc16Ccitt(buffer, ppuc::v2::kHeaderBytes + payloadBytes);
  buffer[5 + payloadBytes] = static_cast<uint8_t>((crc >> 8) & 0xff);
  buffer[6 + payloadBytes] = static_cast<uint8_t>(crc & 0xff);

  return WriteBytes("OutputStateFrame", buffer, frameBytes);
}

void RS485Comm::ApplySwitchBitmapDiff(uint8_t board, const uint8_t* bitmap,
                                      size_t bytes) {
  const uint8_t* ownershipMask =
      board < RS485_COMM_MAX_BOARDS ? m_switchOwnershipMaskByBoard[board]
                                    : nullptr;
  for (uint16_t n = 0; n < m_runtimeConfig.switchBits; ++n) {
    if (ownershipMask && !ppuc::v2::GetBitmapBit(ownershipMask, n)) {
      continue;
    }
    const bool oldState = ppuc::v2::GetBitmapBit(m_switchBitmap, n);
    const bool newState = ppuc::v2::GetBitmapBit(bitmap, n);
    if (oldState != newState) {
      int switchNumber = n;
      if (n < m_switchIndexToNumber.size()) {
        switchNumber = m_switchIndexToNumber[n];
      }
      NoteSwitchActivity(static_cast<uint16_t>(switchNumber));
      std::lock_guard<std::mutex> lock(m_switchesQueueMutex);
      m_switches.push(new PPUCSwitchState(switchNumber, newState ? 1 : 0));
    }
  }

  if (!ownershipMask) {
    memcpy(m_switchBitmap, bitmap, bytes);
    return;
  }

  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t mask = ownershipMask[i];
    m_switchBitmap[i] = static_cast<uint8_t>((m_switchBitmap[i] & ~mask) |
                                             (bitmap[i] & mask));
  }
}

void RS485Comm::NoteSwitchActivity(uint16_t switchNumber) {
  if (m_switchRefreshIdleMs == 0 ||
      m_buttonSwitchNumbers.find(switchNumber) != m_buttonSwitchNumbers.end()) {
    return;
  }
  m_nextSwitchRefreshAt =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(m_switchRefreshIdleMs);
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
  auto readExact = [this](uint8_t* dst, size_t bytes,
                          uint32_t timeoutMs) -> bool {
    // Match config-ack handling: libserialport may complete a blocking read
    // with only part of the requested frame on Linux, which would otherwise
    // turn a valid switch reply into a false CRC/sequence/epoch failure.
    size_t totalRead = 0;
    while (totalRead < bytes) {
      const int read =
          sp_blocking_read(m_pSerialPort, dst + totalRead, bytes - totalRead,
                           timeoutMs);
      if (read <= 0) {
        return false;
      }
      totalRead += static_cast<size_t>(read);
    }
    return true;
  };
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  const int64_t switchReplyWindowUs = SwitchReplyWindowUs();
  const uint32_t readTimeoutMs = SwitchReadTimeoutMs();
  auto readOneByteWithinWindow =
      [this, &start, switchReplyWindowUs](uint8_t* dst) -> bool {
    while (true) {
      const int64_t elapsedUs =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsedUs >= switchReplyWindowUs) {
        return false;
      }

      const int64_t remainingUs = switchReplyWindowUs - elapsedUs;
      const uint32_t timeoutMs = static_cast<uint32_t>(
          std::max<int64_t>(1, (remainingUs + 999) / 1000));
      const int read = sp_blocking_read(m_pSerialPort, dst, 1, timeoutMs);
      if (read > 0) {
        return true;
      }
    }
  };
  bool sawAnyReplyBytes = false;
  while ((std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start))
             .count() < switchReplyWindowUs) {
    if (!readOneByteWithinWindow(&header[0])) {
      break;
    }
    sawAnyReplyBytes = true;

    if (header[0] != ppuc::v2::kSyncByte) {
      continue;
    }

    if (!readExact(&header[1], ppuc::v2::kHeaderBytes - 1, readTimeoutMs)) {
      continue;
    }
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
    if (!readExact(&buffer[ppuc::v2::kHeaderBytes],
                   payloadBytes + ppuc::v2::kCrcBytes, readTimeoutMs)) {
      continue;
    }

    if (outNextBoard) {
      *outNextBoard = header[2];
    }

    const uint8_t epochSeen = buffer[ppuc::v2::kHeaderBytes];
    const uint8_t lastHostSequenceSeen = buffer[ppuc::v2::kHeaderBytes + 1];
    const uint8_t statusFlags = buffer[ppuc::v2::kHeaderBytes + 2];
    const bool parserResynced =
        (statusFlags & ppuc::v2::kStatusParserResynced) != 0;
    const bool switchOverflow =
        (statusFlags & ppuc::v2::kStatusSwitchOverflow) != 0;

    if (epochSeen != m_epoch) {
      ErrorPrintf("V2 switch reply epoch mismatch: board=%u seen=%u expected=%u",
                  expectedBoard, epochSeen, m_epoch);
      m_needSessionResync = true;
    }
    if (lastHostSequenceSeen != m_lastOutputSequenceSent) {
      ErrorPrintf(
          "V2 switch reply sequence mismatch: board=%u seen=%u expected=%u",
          expectedBoard, lastHostSequenceSeen, m_lastOutputSequenceSent);
      m_needSessionResync = true;
    }
    if ((statusFlags & (ppuc::v2::kStatusNeedsSetup |
                        ppuc::v2::kStatusMappingIncomplete |
                        ppuc::v2::kStatusSequenceGap)) != 0) {
      ErrorPrintf("V2 switch reply requested resync: board=%u flags=0x%02X",
                  expectedBoard, statusFlags);
      m_needSessionResync = true;
    }

    if (parserResynced || switchOverflow) {
      ErrorPrintf("V2 switch reply status flags: board=%u flags=0x%02X%s%s",
                  expectedBoard, statusFlags,
                  parserResynced ? " parser-resynced" : "",
                  switchOverflow ? " switch-overflow" : "");
    }

    const size_t frameBytes =
        ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;
    const uint16_t receivedCrc =
        (static_cast<uint16_t>(buffer[frameBytes - 2]) << 8) |
        static_cast<uint16_t>(buffer[frameBytes - 1]);
    const uint16_t calculatedCrc =
        ppuc::v2::Crc16Ccitt(buffer, frameBytes - ppuc::v2::kCrcBytes);
    if (receivedCrc != calculatedCrc) {
      ErrorPrintf("Invalid V2 switch frame CRC: got=%04X expected=%04X",
                  receivedCrc, calculatedCrc);
      return false;
    }

    if (frameType == ppuc::v2::kFrameSwitchState) {
      ApplySwitchBitmapDiff(
          expectedBoard,
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
    ErrorPrintf(
        "Timed out waiting for V2 switch reply for board token %u (windowUs=%lld readTimeoutMs=%u sawBytes=%s inputWaiting=%d lastOutputSeq=%u epoch=%u)",
        expectedBoard, static_cast<long long>(switchReplyWindowUs),
        static_cast<unsigned>(readTimeoutMs), sawAnyReplyBytes ? "yes" : "no",
        static_cast<int>(sp_input_waiting(m_pSerialPort)),
        static_cast<unsigned>(m_lastOutputSequenceSent),
        static_cast<unsigned>(m_epoch));
  }
  if (sp_input_waiting(m_pSerialPort) <= 0) {
    sp_flush(m_pSerialPort, SP_BUF_INPUT);
  }
  return false;
}

bool RS485Comm::SendEvent(Event* event) {
  if (!event || m_pSerialPort == NULL) {
    return false;
  }

  uint8_t frame[ppuc::v2::kTriggerFrameBytes];
  frame[0] = ppuc::v2::kSyncByte;
  frame[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameTrigger,
                                           ppuc::v2::kFlagNone);
  frame[2] = ppuc::v2::kNoBoard;
  frame[3] = m_sequence++;
  frame[4] = m_epoch;
  frame[5] = event->sourceId;
  frame[6] = static_cast<uint8_t>((event->eventId >> 8) & 0xFFu);
  frame[7] = static_cast<uint8_t>(event->eventId & 0xFFu);
  frame[8] = event->value;
  const uint16_t crc =
      ppuc::v2::Crc16Ccitt(frame, ppuc::v2::kHeaderBytes +
                                      ppuc::v2::kTriggerPayloadBytes);
  frame[9] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
  frame[10] = static_cast<uint8_t>(crc & 0xFFu);
  return WriteBytes("trigger frame", frame, sizeof(frame));
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
