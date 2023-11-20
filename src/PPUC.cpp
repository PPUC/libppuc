#include "PPUC.h"
#include "Event.h"
#include "RS485Comm.h"

PPUC::PPUC()
{
    m_rom = (char *)malloc(16);
    m_serial = (char *)malloc(128);

    m_pRS485Comm = new RS485Comm();
}

PPUC::~PPUC()
{
    m_pRS485Comm->Disconnect();
    free(m_pRS485Comm);
}

void PPUC::SetLogMessageCallback(PPUC_LogMessageCallback callback, const void *userData)
{
    m_pRS485Comm->SetLogMessageCallback(callback, userData);
}

#ifdef __ANDROID__
void PPUC::SetAndroidGetJNIEnvFunc(PPUC_AndroidGetJNIEnvFunc func)
{
    m_pRS485Comm->SetAndroidGetJNIEnvFunc(func);
}
#endif

void PPUC::Disconnect()
{
    m_pRS485Comm->Disconnect();
}

void PPUC::LoadConfiguration(const char *configFile)
{
    // Load config file. But options set via command line are preferred.
    m_ppucConfig = YAML::LoadFile(configFile);
    m_debug = m_ppucConfig["debug"].as<bool>();
    std::string c_rom = m_ppucConfig["rom"].as<std::string>();
    strcpy(m_rom, c_rom.c_str());
    std::string c_serial = m_ppucConfig["serialPort"].as<std::string>();
    strcpy(m_serial, c_serial.c_str());
    std::string c_platform = m_ppucConfig["platform"].as<std::string>();
    m_platform = PLATFORM_WPC;
    if (strcmp(c_platform.c_str(), "DE") == 0)
    {
        m_platform = PLATFORM_DATA_EAST;
    }
    else if (strcmp(c_platform.c_str(), "SYS11") == 0)
    {
        m_platform = PLATFORM_SYS11;
    }
}

void PPUC::SetDebug(bool debug)
{
    m_pRS485Comm->SetDebug(debug);
    m_debug = debug;
}

bool PPUC::GetDebug()
{
    return m_debug;
}

void PPUC::SetRom(const char *rom)
{
    strcpy(m_rom, rom);
}

const char *PPUC::GetRom()
{
    return m_rom;
}

void PPUC::SetSerial(const char *serial)
{
    strcpy(m_serial, serial);
}

const char *PPUC::GetSerial()
{
    return m_serial;
}

void PPUC::SendLedConfigBlock(const YAML::Node &items, uint32_t type, uint8_t board, uint32_t port)
{
    for (YAML::Node n_item : items)
    {
        uint8_t index = 0;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            board,
            (uint8_t)CONFIG_TOPIC_LAMPS,
            index++,
            (uint8_t)CONFIG_TOPIC_PORT,
            port));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            board,
            (uint8_t)CONFIG_TOPIC_LAMPS,
            index++,
            (uint8_t)CONFIG_TOPIC_TYPE,
            type));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            board,
            (uint8_t)CONFIG_TOPIC_LAMPS,
            index++,
            (uint8_t)CONFIG_TOPIC_NUMBER,
            n_item["number"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            board,
            (uint8_t)CONFIG_TOPIC_LAMPS,
            index++,
            (uint8_t)CONFIG_TOPIC_LED_NUMBER,
            n_item["ledNumber"].as<uint32_t>()));
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            board,
            (uint8_t)CONFIG_TOPIC_LAMPS,
            index++,
            (uint8_t)CONFIG_TOPIC_BRIGHTNESS,
            n_item["brightness"].as<uint32_t>()));

        uint32_t color;
        std::stringstream ss;
        ss << std::hex << n_item["color"].as<std::string>();
        ss >> color;
        m_pRS485Comm->SendConfigEvent(new ConfigEvent(
            board,
            (uint8_t)CONFIG_TOPIC_LAMPS,
            index++,
            (uint8_t)CONFIG_TOPIC_COLOR,
            color));
    }
}

bool PPUC::Connect()
{
    if (m_pRS485Comm->Connect(m_serial))
    {
        uint8_t index = 0;
        const YAML::Node &boards = m_ppucConfig["boards"];
        for (YAML::Node n_board : boards)
        {
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                n_board["number"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_PLATFORM,
                0,
                (uint8_t)CONFIG_TOPIC_PLATFORM,
                m_platform));

            if (n_board["pollEvents"].as<bool>())
            {
                m_pRS485Comm->RegisterSwitchBoard(n_board["number"].as<uint8_t>());
            }
        }

        // Send switch configuration to I/O boards
        const YAML::Node &switches = m_ppucConfig["switches"];
        if (switches)
        {
            for (YAML::Node n_switch : switches)
            {
                index = 0;
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_switch["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCHES,
                    index++,
                    (uint8_t)CONFIG_TOPIC_PORT,
                    n_switch["port"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_switch["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCHES,
                    index++,
                    (uint8_t)CONFIG_TOPIC_NUMBER,
                    n_switch["number"].as<uint32_t>()));
            }
        }

        // Send switch matrix configuration to I/O boards
        const YAML::Node &switchMatrix = m_ppucConfig["switchMatrix"];
        if (switchMatrix)
        {
            index = 0;
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                switchMatrix["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                index++,
                (uint8_t)CONFIG_TOPIC_ACTIVE_LOW,
                switchMatrix["activeLow"].as<bool>()));
            m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                switchMatrix["board"].as<uint8_t>(),
                (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                index++,
                (uint8_t)CONFIG_TOPIC_MAX_PULSE_TIME,
                switchMatrix["pulseTime"].as<uint32_t>()));
            const YAML::Node &switcheMatrixColumns = m_ppucConfig["switchMatrix"]["columns"];
            for (YAML::Node n_switchMatrixColumn : switcheMatrixColumns)
            {
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    switchMatrix["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                    index++,
                    (uint8_t)CONFIG_TOPIC_TYPE,
                    MATRIX_TYPE_COLUMN));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    switchMatrix["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                    index++,
                    (uint8_t)CONFIG_TOPIC_NUMBER,
                    n_switchMatrixColumn["number"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    switchMatrix["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                    index++,
                    (uint8_t)CONFIG_TOPIC_PORT,
                    n_switchMatrixColumn["port"].as<uint32_t>()));
            }
            const YAML::Node &switcheMatrixRows = m_ppucConfig["switchMatrix"]["rows"];
            for (YAML::Node n_switchMatrixRow : switcheMatrixRows)
            {
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    switchMatrix["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                    index++,
                    (uint8_t)CONFIG_TOPIC_TYPE,
                    MATRIX_TYPE_ROW));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    switchMatrix["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                    index++,
                    (uint8_t)CONFIG_TOPIC_NUMBER,
                    n_switchMatrixRow["number"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    switchMatrix["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_SWITCH_MATRIX,
                    index++,
                    (uint8_t)CONFIG_TOPIC_PORT,
                    n_switchMatrixRow["port"].as<uint32_t>()));
            }
        }

        // Send PWM configuration to I/O boards
        const YAML::Node &pwmOutput = m_ppucConfig["pwmOutput"];
        if (pwmOutput)
        {
            for (YAML::Node n_pwmOutput : pwmOutput)
            {
                index = 0;
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_PORT,
                    n_pwmOutput["port"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_NUMBER,
                    n_pwmOutput["number"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_POWER,
                    n_pwmOutput["power"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_MIN_PULSE_TIME,
                    n_pwmOutput["minPulseTime"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_MAX_PULSE_TIME,
                    n_pwmOutput["maxPulseTime"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_HOLD_POWER,
                    n_pwmOutput["holdPower"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_HOLD_POWER_ACTIVATION_TIME,
                    n_pwmOutput["holdPowerActivationTime"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_FAST_SWITCH,
                    n_pwmOutput["fastFlipSwitch"].as<uint32_t>()));
                std::string c_type = n_pwmOutput["type"].as<std::string>();
                uint32_t type = PWM_TYPE_SOLENOID; // "coil"
                if (strcmp(c_type.c_str(), "flasher"))
                {
                    type = PWM_TYPE_FLASHER;
                }
                else if (strcmp(c_type.c_str(), "lamp"))
                {
                    type = PWM_TYPE_LAMP;
                }
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_pwmOutput["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_PWM,
                    index++,
                    (uint8_t)CONFIG_TOPIC_TYPE,
                    type));
            }
        }

        // Send LED configuration to I/O boards
        const YAML::Node &ledStripes = m_ppucConfig["ledStripes"];
        if (ledStripes)
        {
            for (YAML::Node n_ledStripe : ledStripes)
            {
                index = 0;
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_ledStripe["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_LED_STRING,
                    index++,
                    (uint8_t)CONFIG_TOPIC_PORT,
                    n_ledStripe["port"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_ledStripe["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_LED_STRING,
                    index++,
                    (uint8_t)CONFIG_TOPIC_AMOUNT_LEDS,
                    n_ledStripe["amount"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_ledStripe["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_LED_STRING,
                    index++,
                    (uint8_t)CONFIG_TOPIC_LIGHT_UP,
                    n_ledStripe["lightUp"].as<uint32_t>()));
                m_pRS485Comm->SendConfigEvent(new ConfigEvent(
                    n_ledStripe["board"].as<uint8_t>(),
                    (uint8_t)CONFIG_TOPIC_LED_STRING,
                    index++,
                    (uint8_t)CONFIG_TOPIC_AFTER_GLOW,
                    n_ledStripe["afterGlow"].as<uint32_t>()));

                SendLedConfigBlock(n_ledStripe["lamps"], LED_TYPE_LAMP, n_ledStripe["board"].as<uint8_t>(), n_ledStripe["port"].as<uint32_t>());
                SendLedConfigBlock(n_ledStripe["flashers"], LED_TYPE_FLASHER, n_ledStripe["board"].as<uint8_t>(), n_ledStripe["port"].as<uint32_t>());
                SendLedConfigBlock(n_ledStripe["gi"], LED_TYPE_GI, n_ledStripe["board"].as<uint8_t>(), n_ledStripe["port"].as<uint32_t>());
            }
        }

        // Wait before continuing.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Tell I/O boards to read initial switch states, for example coin door closed.
        m_pRS485Comm->QueueEvent(new Event(EVENT_READ_SWITCHES));

        m_pRS485Comm->Run();

        return true;
    }

    return false;
}

void PPUC::SetSolenoidState(int number, int state)
{
    uint16_t solNo = number;
    uint8_t solState = state == 0 ? 0 : 1;
    m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_SOLENOID, solNo, solState));
}

void PPUC::SetLampState(int number, int state)
{
    uint16_t lampNo = number;
    uint8_t lampState = state == 0 ? 0 : 1;
    m_pRS485Comm->QueueEvent(new Event(EVENT_SOURCE_LIGHT, lampNo, lampState));
}

PPUCSwitchState *PPUC::GetNextSwitchState()
{
    return m_pRS485Comm->GetNextSwitchState();
}
