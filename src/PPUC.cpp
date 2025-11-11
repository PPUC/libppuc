#include "PPUC.h"

#include <cstring>

#include "Adafruit_NeoPixel.h"
#include "RS485Comm.h"
#include "io-boards/Event.h"
#include "io-boards/PPUCPlatforms.h"

PPUC::PPUC() {
  m_rom = (char*)malloc(16);
  m_serial = (char*)malloc(128);

  m_pRS485Comm = new RS485Comm();
}

PPUC::~PPUC() {
  m_pRS485Comm->Disconnect();
  delete m_pRS485Comm;
}

void PPUC::SetLogMessageCallback(PPUC_LogMessageCallback callback,
                                 const void* userData) {
  m_pRS485Comm->SetLogMessageCallback(callback, userData);
}

void PPUC::Disconnect() { m_pRS485Comm->Disconnect(); }

uint8_t PPUC::ResolveLedType(std::string type) {
  if (type.compare("RGB")) return NEO_RGB;
  if (type.compare("RBG")) return NEO_RBG;
  if (type.compare("GRB")) return NEO_GRB;
  if (type.compare("GBR")) return NEO_GBR;
  if (type.compare("BRG")) return NEO_BRG;
  if (type.compare("BGR")) return NEO_BGR;

  if (type.compare("WRGB")) return NEO_WRGB;
  if (type.compare("WRBG")) return NEO_WRBG;
  if (type.compare("WGRB")) return NEO_WGRB;
  if (type.compare("WGBR")) return NEO_WGBR;
  if (type.compare("WBRG")) return NEO_WBRG;
  if (type.compare("WBGR")) return NEO_WBGR;

  if (type.compare("RWGB")) return NEO_RWGB;
  if (type.compare("RWBG")) return NEO_RWBG;
  if (type.compare("RGWB")) return NEO_RGWB;
  if (type.compare("RGBW")) return NEO_RGBW;
  if (type.compare("RBWG")) return NEO_RBWG;
  if (type.compare("RBGW")) return NEO_RBGW;

  if (type.compare("GWRB")) return NEO_GWRB;
  if (type.compare("GWBR")) return NEO_GWBR;
  if (type.compare("GRWB")) return NEO_GRWB;
  if (type.compare("GRBW")) return NEO_GRBW;
  if (type.compare("GBWR")) return NEO_GBWR;
  if (type.compare("GBRW")) return NEO_GBRW;

  if (type.compare("BWRG")) return NEO_BWRG;
  if (type.compare("BWGR")) return NEO_BWGR;
  if (type.compare("BRWG")) return NEO_BRWG;
  if (type.compare("BRGW")) return NEO_BRGW;
  if (type.compare("BGWR")) return NEO_BGWR;
  if (type.compare("BGRW")) return NEO_BGRW;

  return 0;
}

void PPUC::LoadConfiguration(const char* configFile) {
  // Load config file. But options set via command line are preferred.
  m_ppucConfig = YAML::LoadFile(configFile);
  m_debug = m_ppucConfig["debug"].as<bool>();
  std::string c_rom = m_ppucConfig["rom"].as<std::string>();
  strcpy(m_rom, c_rom.c_str());
  std::string c_serial = m_ppucConfig["serialPort"].as<std::string>();
  strcpy(m_serial, c_serial.c_str());
  std::string c_platform = m_ppucConfig["platform"].as<std::string>();
  m_platform = PLATFORM_WPC;
  if (strcmp(c_platform.c_str(), "DE") == 0) {
    m_platform = PLATFORM_DATA_EAST;
  } else if (strcmp(c_platform.c_str(), "SYS4") == 0) {
    m_platform = PLATFORM_SYS4;
  } else if (strcmp(c_platform.c_str(), "SYS11") == 0) {
    m_platform = PLATFORM_SYS11;
  }
}

void PPUC::SetDebug(bool debug) {
  m_pRS485Comm->SetDebug(debug);
  m_debug = debug;
}

bool PPUC::GetDebug() { return m_debug; }

void PPUC::SetRom(const char* rom) { strcpy(m_rom, rom); }

const char* PPUC::GetRom() { return m_rom; }

void PPUC::SetSerial(const char* serial) { strcpy(m_serial, serial); }

const char* PPUC::GetSerial() { return m_serial; }

void PPUC::SendTriggerConfigBlock(const YAML::Node& items, uint32_t type,
                                  uint8_t board, uint32_t port) {
  if (items) {
    for (YAML::Node n_item : items) {
      uint8_t index = 0;
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER, index++,
                          (uint8_t)CONFIG_TOPIC_PORT, port));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER, index++,
                          (uint8_t)CONFIG_TOPIC_TYPE, type));
      std::string c_source = n_item["source"].as<std::string>();
      uint32_t source = EVENT_SOURCE_SWITCH;
      if (strcmp(c_source.c_str(), "S") == 0) {
        source = EVENT_SOURCE_SOLENOID;
      } else if (strcmp(c_source.c_str(), "L") == 0) {
        source = EVENT_SOURCE_LIGHT;
      }
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_TRIGGER, index++,
                          (uint8_t)CONFIG_TOPIC_SOURCE, source));
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          board, (uint8_t)CONFIG_TOPIC_TRIGGER, index++,
          (uint8_t)CONFIG_TOPIC_NUMBER, n_item["number"].as<uint32_t>()));
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
          (uint8_t)CONFIG_TOPIC_VALUE, n_item["value"].as<uint32_t>()));
    }
  }
}

void PPUC::SendLedConfigBlock(const YAML::Node& items, uint32_t type,
                              uint8_t board, uint32_t port) {
  if (items) {
    for (YAML::Node n_item : items) {
      if (m_debug) {
        // @todo user logger
        printf("Description: %s\n",
               n_item["description"].as<std::string>().c_str());
      }

      uint8_t index = 0;
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_PORT, port));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_TYPE, type));
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
          (uint8_t)CONFIG_TOPIC_NUMBER, n_item["number"].as<uint32_t>()));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_LED_NUMBER,
                          n_item["ledNumber"].as<uint32_t>()));

      uint32_t color;
      std::stringstream ss;
      ss << std::hex << n_item["color"].as<std::string>();
      ss >> color;
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(board, (uint8_t)CONFIG_TOPIC_LAMPS, index++,
                          (uint8_t)CONFIG_TOPIC_COLOR, color));

      m_lamps.push_back(
          PPUCLamp(board, port, (uint8_t)type, n_item["number"].as<uint8_t>(),
                   n_item["description"].as<std::string>(), color));
    }
  }
}

bool PPUC::Connect() {
  if (m_pRS485Comm->Connect(m_serial)) {
    uint8_t index = 0;
    const YAML::Node& boards = m_ppucConfig["boards"];
    for (YAML::Node n_board : boards) {
      m_pRS485Comm->SendConfigEvent(new ConfigEvent(
          n_board["number"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PLATFORM, 0,
          (uint8_t)CONFIG_TOPIC_PLATFORM, m_platform));

      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(n_board["number"].as<uint8_t>(),
                          (uint8_t)CONFIG_TOPIC_COIN_DOOR_CLOSED_SWITCH, 0,
                          (uint8_t)CONFIG_TOPIC_NUMBER,
                          m_ppucConfig["coinDoorClosedSwitch"].as<uint8_t>()));
      m_coinDoorClosedSwitch =
          m_ppucConfig["coinDoorClosedSwitch"].as<uint8_t>();

      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(n_board["number"].as<uint8_t>(),
                          (uint8_t)CONFIG_TOPIC_GAME_ON_SOLENOID, 0,
                          (uint8_t)CONFIG_TOPIC_NUMBER,
                          m_ppucConfig["gameOnSolenoid"].as<uint8_t>()));
      m_gameOnSolenoid = m_ppucConfig["gameOnSolenoid"].as<uint8_t>();

      if (n_board["pollEvents"].as<bool>()) {
        m_pRS485Comm->RegisterSwitchBoard(n_board["number"].as<uint8_t>());
      }
    }

    // Send switch configuration to I/O boards
    const YAML::Node& switches = m_ppucConfig["switches"];
    if (switches) {
      for (YAML::Node n_switch : switches) {
        if (m_debug) {
          // @todo user logger
          printf("Description: %s\n",
                 n_switch["description"].as<std::string>().c_str());
        }

        index = 0;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_switch["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_SWITCHES,
            index++, (uint8_t)CONFIG_TOPIC_PORT,
            n_switch["port"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_switch["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_SWITCHES,
            index++, (uint8_t)CONFIG_TOPIC_NUMBER,
            n_switch["number"].as<uint32_t>()));

        m_switches.push_back(PPUCSwitch(
            n_switch["board"].as<uint8_t>(), n_switch["port"].as<uint8_t>(),
            n_switch["number"].as<uint8_t>(),
            n_switch["description"].as<std::string>()));
      }
    }

    // Send switch matrix configuration to I/O boards
    const YAML::Node& switchMatrix = m_ppucConfig["switchMatrix"];
    if (switchMatrix) {
      index = 0;
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                          (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                          (uint8_t)CONFIG_TOPIC_ACTIVE_LOW,
                          switchMatrix["activeLow"].as<bool>()));
      m_pRS485Comm->SendConfigEvent(
          new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                          (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                          (uint8_t)CONFIG_TOPIC_MAX_PULSE_TIME,
                          switchMatrix["pulseTime"].as<uint32_t>()));
      const YAML::Node& switcheMatrixColumns =
          m_ppucConfig["switchMatrix"]["columns"];
      for (YAML::Node n_switchMatrixColumn : switcheMatrixColumns) {
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                            (uint8_t)CONFIG_TOPIC_TYPE, MATRIX_TYPE_COLUMN));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                            (uint8_t)CONFIG_TOPIC_NUMBER,
                            n_switchMatrixColumn["number"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                            (uint8_t)CONFIG_TOPIC_PORT,
                            n_switchMatrixColumn["port"].as<uint32_t>()));
      }
      const YAML::Node& switcheMatrixRows =
          m_ppucConfig["switchMatrix"]["rows"];
      for (YAML::Node n_switchMatrixRow : switcheMatrixRows) {
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                            (uint8_t)CONFIG_TOPIC_TYPE, MATRIX_TYPE_ROW));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                            (uint8_t)CONFIG_TOPIC_NUMBER,
                            n_switchMatrixRow["number"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(switchMatrix["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX, index++,
                            (uint8_t)CONFIG_TOPIC_PORT,
                            n_switchMatrixRow["port"].as<uint32_t>()));
      }
    }

    // Send PWM configuration to I/O boards
    const YAML::Node& pwmOutput = m_ppucConfig["pwmOutput"];
    if (pwmOutput) {
      for (YAML::Node n_pwmOutput : pwmOutput) {
        if (m_debug) {
          // @todo user logger
          printf("Description: %s\n",
                 n_pwmOutput["description"].as<std::string>().c_str());
        }

        index = 0;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_PORT,
            n_pwmOutput["port"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_NUMBER,
            n_pwmOutput["number"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_POWER,
            n_pwmOutput["power"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_MIN_PULSE_TIME,
            n_pwmOutput["minPulseTime"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_MAX_PULSE_TIME,
            n_pwmOutput["maxPulseTime"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_HOLD_POWER,
            n_pwmOutput["holdPower"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_HOLD_POWER_ACTIVATION_TIME,
            n_pwmOutput["holdPowerActivationTime"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_FAST_SWITCH,
            n_pwmOutput["fastFlipSwitch"].as<uint32_t>()));
        std::string c_type = n_pwmOutput["type"].as<std::string>();
        uint32_t type = PWM_TYPE_SOLENOID;  // "coil"
        if (strcmp(c_type.c_str(), "flasher") == 0) {
          type = PWM_TYPE_FLASHER;
        } else if (strcmp(c_type.c_str(), "lamp") == 0) {
          type = PWM_TYPE_LAMP;
        } else if (strcmp(c_type.c_str(), "motor") == 0) {
          type = PWM_TYPE_MOTOR;
        }
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_pwmOutput["board"].as<uint8_t>(), (uint8_t)CONFIG_TOPIC_PWM,
            index++, (uint8_t)CONFIG_TOPIC_TYPE, type));

        const YAML::Node& pwm_effects = n_pwmOutput["effects"];
        if (pwm_effects) {
          for (YAML::Node n_pwm_effect : pwm_effects) {
            index = 0;
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PORT,
                                n_pwmOutput["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_DURATION,
                                n_pwm_effect["duration"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_EFFECT,
                                n_pwm_effect["effect"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_FREQUENCY,
                                n_pwm_effect["frequency"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MAX_INTENSITY,
                                n_pwm_effect["maxIntensity"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MIN_INTENSITY,
                                n_pwm_effect["minIntensity"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MODE,
                                n_pwm_effect["mode"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PRIORITY,
                                n_pwm_effect["priority"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_pwmOutput["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_PWM_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_REPEAT,
                                n_pwm_effect["repeat"].as<int16_t>() == -1
                                    ? 255
                                    : n_pwm_effect["repeat"].as<uint32_t>()));

            SendTriggerConfigBlock(n_pwm_effect["trigger"],
                                   CONFIG_TOPIC_PWM_EFFECT,
                                   n_pwmOutput["board"].as<uint8_t>(),
                                   n_pwmOutput["port"].as<uint32_t>());
          }
        }

        m_coils.push_back(
            PPUCCoil(n_pwmOutput["board"].as<uint8_t>(),
                     n_pwmOutput["port"].as<uint8_t>(), (uint8_t)type,
                     n_pwmOutput["number"].as<uint8_t>(),
                     n_pwmOutput["description"].as<std::string>()));
      }
    }

    // Send LED configuration to I/O boards
    const YAML::Node& ledStripes = m_ppucConfig["ledStripes"];
    if (ledStripes) {
      for (YAML::Node n_ledStripe : ledStripes) {
        index = 0;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_ledStripe["board"].as<uint8_t>(),
            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
            (uint8_t)CONFIG_TOPIC_PORT, n_ledStripe["port"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            n_ledStripe["board"].as<uint8_t>(),
            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
            (uint8_t)CONFIG_TOPIC_TYPE,
            ResolveLedType(n_ledStripe["ledType"].as<std::string>())));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_BRIGHTNESS,
                            n_ledStripe["brightness"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_AMOUNT_LEDS,
                            n_ledStripe["amount"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_AFTER_GLOW,
                            n_ledStripe["afterGlow"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(
            new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                            (uint8_t)CONFIG_TOPIC_LED_STRING, index++,
                            (uint8_t)CONFIG_TOPIC_LIGHT_UP,
                            n_ledStripe["lightUp"].as<uint32_t>()));

        const YAML::Node& segments = n_ledStripe["segments"];
        if (segments) {
          for (YAML::Node n_segment : segments) {
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                                (uint8_t)CONFIG_TOPIC_PORT,
                                n_ledStripe["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                                (uint8_t)CONFIG_TOPIC_NUMBER,
                                n_segment["number"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_ledStripe["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                (uint8_t)CONFIG_TOPIC_FROM, n_segment["from"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_ledStripe["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_LED_SEGMENT, index++,
                (uint8_t)CONFIG_TOPIC_TO, n_segment["to"].as<uint32_t>()));
          }
        }

        const YAML::Node& led_effects = n_ledStripe["effects"];
        if (led_effects) {
          for (YAML::Node n_led_effect : led_effects) {
            index = 0;
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PORT,
                                n_ledStripe["port"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_LED_SEGMENT,
                                n_led_effect["segment"].as<uint32_t>()));
            uint32_t color;
            std::stringstream ss;
            ss << std::hex << n_led_effect["color"].as<std::string>();
            ss >> color;
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_COLOR, color));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_DURATION,
                                n_led_effect["duration"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_EFFECT,
                                n_led_effect["effect"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_REVERSE,
                                n_led_effect["reverse"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_SPEED,
                                n_led_effect["speed"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_MODE,
                                n_led_effect["mode"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_PRIORITY,
                                n_led_effect["priority"].as<uint32_t>()));
            m_pRS485Comm->SendConfigEvent(
                new ConfigEvent(n_ledStripe["board"].as<uint8_t>(),
                                (uint8_t)CONFIG_TOPIC_LED_EFFECT, index++,
                                (uint8_t)CONFIG_TOPIC_REPEAT,
                                n_led_effect["repeat"].as<int16_t>() == -1
                                    ? 255
                                    : n_led_effect["repeat"].as<uint32_t>()));

            SendTriggerConfigBlock(n_led_effect["trigger"],
                                   CONFIG_TOPIC_LED_EFFECT,
                                   n_ledStripe["board"].as<uint8_t>(),
                                   n_ledStripe["port"].as<uint32_t>());
          }
        }

        SendLedConfigBlock(n_ledStripe["lamps"], LED_TYPE_LAMP,
                           n_ledStripe["board"].as<uint8_t>(),
                           n_ledStripe["port"].as<uint32_t>());
        SendLedConfigBlock(n_ledStripe["flashers"], LED_TYPE_FLASHER,
                           n_ledStripe["board"].as<uint8_t>(),
                           n_ledStripe["port"].as<uint32_t>());
        SendLedConfigBlock(n_ledStripe["gi"], LED_TYPE_GI,
                           n_ledStripe["board"].as<uint8_t>(),
                           n_ledStripe["port"].as<uint32_t>());
      }
    }

    // Wait before continuing.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Turn on the GI for non WPC platforms.
    if (PLATFORM_WPC != m_platform) {
      m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_GI, /* string */ 1,
                                         /* full brightness */ 8));
    }

    // Tell I/O boards to read initial switch states, for example coin door
    // closed.
    m_pRS485Comm->QueueEvent(new Event(EVENT_READ_SWITCHES));

    m_pRS485Comm->Run();

    return true;
  }

  return false;
}

void PPUC::SetSolenoidState(int number, int state) {
  uint16_t solNo = number;
  uint8_t solState = state == 0 ? 0 : 1;
  m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, solNo, solState));
}

void PPUC::SetLampState(int number, int state) {
  uint16_t lampNo = number;
  uint8_t lampState = state == 0 ? 0 : 1;
  m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_LIGHT, lampNo, lampState));
}

PPUCSwitchState* PPUC::GetNextSwitchState() {
  return m_pRS485Comm->GetNextSwitchState();
}

void PPUC::StartUpdates() {
  m_pRS485Comm->QueueEvent(new Event(EVENT_RUN, 1, 1));
}

void PPUC::StopUpdates() {
  m_pRS485Comm->QueueEvent(new Event(EVENT_RUN, 1, 0));
}

std::vector<PPUCCoil> PPUC::GetCoils() {
  std::sort(
      m_coils.begin(), m_coils.end(),
      [](const PPUCCoil& a, const PPUCCoil& b) { return a.number < b.number; });

  return m_coils;
}

std::vector<PPUCLamp> PPUC::GetLamps() {
  std::sort(
      m_lamps.begin(), m_lamps.end(),
      [](const PPUCLamp& a, const PPUCLamp& b) { return a.number < b.number; });

  return m_lamps;
}

std::vector<PPUCSwitch> PPUC::GetSwitches() {
  std::sort(m_switches.begin(), m_switches.end(),
            [](const PPUCSwitch& a, const PPUCSwitch& b) {
              return a.number < b.number;
            });

  return m_switches;
}

void PPUC::CoilTest(u_int8_t number) {
  printf("Coil Test\n");
  printf("=========\n");

  for (const auto& coil : GetCoils()) {
    if (coil.type == PWM_TYPE_SOLENOID || coil.type == PWM_TYPE_FLASHER) {
      if (number != 0 && coil.number != number) {
        continue;
      }
      printf("\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\n", coil.board,
             coil.port, coil.number, coil.description.c_str());
      SetSolenoidState(coil.number, 1);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      SetSolenoidState(coil.number, 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
}

void PPUC::LampTest(u_int8_t number) {
  printf("Lamp Test\n");
  printf("=========\n");

  if (number != 0) {
    for (const auto& lamp : GetLamps()) {
      if (lamp.type == LED_TYPE_LAMP && lamp.number == number) {
        printf(
            "\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\Color: "
            "%08X\n",
            lamp.board, lamp.port, lamp.number, lamp.description.c_str(),
            lamp.color);
        SetLampState(lamp.number, 1);
      }
    }

    for (const auto& coil : GetCoils()) {
      if (coil.type == PWM_TYPE_LAMP && coil.number == number) {
        printf("\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\n",
               coil.board, coil.port, coil.number, coil.description.c_str());
        SetSolenoidState(coil.number, 1);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10000));

    for (const auto& lamp : GetLamps()) {
      SetLampState(lamp.number, 0);
    }

    for (const auto& coil : GetCoils()) {
      SetSolenoidState(coil.number, 0);
    }
  } else {
    for (const auto& lamp : GetLamps()) {
      if (lamp.type == LED_TYPE_LAMP) {
        printf(
            "\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\Color: %08X\n",
            lamp.board, lamp.port, lamp.number, lamp.description.c_str(),
            lamp.color);
        SetLampState(lamp.number, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        SetLampState(lamp.number, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }

      for (const auto& coil : GetCoils()) {
        if (coil.type == PWM_TYPE_LAMP) {
          printf("\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\n",
                 coil.board, coil.port, coil.number, coil.description.c_str());
          SetSolenoidState(coil.number, 1);
          std::this_thread::sleep_for(std::chrono::milliseconds(2000));
          SetSolenoidState(coil.number, 0);
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
      }
    }
  }
}

void PPUC::FlasherTest(u_int8_t number) {
  printf("\nFlasher Test\n");
  printf("=========\n");

  for (const auto& lamp : GetLamps()) {
    if (lamp.type == LED_TYPE_FLASHER) {
      if (number != 0 && lamp.number != number) {
        continue;
      }
      printf(
          "\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\Color: "
          "%08X\n",
          lamp.board, lamp.port, lamp.number, lamp.description.c_str(),
          lamp.color);
      for (uint8_t i = 0; i < 3; i++) {
        SetSolenoidState(lamp.number, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetSolenoidState(lamp.number, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    }
  }

  for (const auto& coil : GetCoils()) {
    if (coil.type == PWM_TYPE_FLASHER) {
      if (number != 0 && coil.number != number) {
        continue;
      }
      printf("\nBoard: %d\nPort: %d\nNumber: %d\nDescription: %s\n", coil.board,
             coil.port, coil.number, coil.description.c_str());
      for (uint8_t i = 0; i < 3; i++) {
        SetSolenoidState(coil.number, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SetSolenoidState(coil.number, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    }
  }
}

void PPUC::GITest(u_int8_t number) {
  printf("\nGI Test\n");
  printf("=========\n");

  for (uint8_t i = 1; i <= 8; i++) {
    if (PLATFORM_WPC != m_platform && i > 1) {
      break;
    }

    if (number != 0 && number != i) {
      continue;
    }

    printf("Setting GI String %d to brightness to %d\n", i, 8);
    m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_GI, /* string */ i,
                                       /* full brightness */ 8));
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_GI, /* string */ i, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

void PPUC::SwitchTest() {
  printf("Switch Test\n");
  printf("=========\n");

  PPUCSwitchState* switchState;
  while (true) {
    if ((switchState = GetNextSwitchState()) != nullptr) {
      auto it = std::find_if(m_switches.begin(), m_switches.end(),
                             [switchState](const PPUCSwitch& vswitch) {
                               return vswitch.number == switchState->number;
                             });

      if (it != m_switches.end()) {
        printf("Switch updated: #%d, %d\nDescription: %s", switchState->number,
               switchState->state, it->description.c_str());
      } else {
        printf("Switch updated: #%d, %d\n", switchState->number,
               switchState->state);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}
