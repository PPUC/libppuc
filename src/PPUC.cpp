#include "PPUC.h"
#include "Event.h"
#include "RS485Comm.h"

PPUC::PPUC()
{
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
}

void PPUC::SetDebug(bool debug)
{
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

bool PPUC::Connect()
{
   if (m_pRS485Comm->Connect(m_serial))
   {
      const YAML::Node &boards = m_ppucConfig["boards"];
      for (YAML::Node n_board : boards)
      {
         if (n_board["pollEvents"].as<bool>())
         {
            m_pRS485Comm->RegisterSwitchBoard(n_board["number"].as<UINT8>());
         }
      }

      // Send switch configuration to I/O boards
      const YAML::Node &switches = m_ppucConfig["switches"];
      for (YAML::Node n_switch : switches)
      {
         UINT8 index = 0;
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_switch["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_SWITCHES,
             index++,
             (UINT8)CONFIG_TOPIC_PORT,
             n_switch["port"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_switch["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_SWITCHES,
             index++,
             (UINT8)CONFIG_TOPIC_NUMBER,
             n_switch["number"].as<UINT32>()));
      }

      // Send PWM configuration to I/O boards
      const YAML::Node &pwmOutput = m_ppucConfig["pwmOutput"];
      for (YAML::Node n_pwmOutput : pwmOutput)
      {
         UINT8 index = 0;
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_PORT,
             n_pwmOutput["port"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_NUMBER,
             n_pwmOutput["number"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_POWER,
             n_pwmOutput["power"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_MIN_PULSE_TIME,
             n_pwmOutput["minPulseTime"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_MAX_PULSE_TIME,
             n_pwmOutput["maxPulseTime"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_HOLD_POWER,
             n_pwmOutput["holdPower"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_HOLD_POWER_ACTIVATION_TIME,
             n_pwmOutput["holdPowerActivationTime"].as<UINT32>()));
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_FAST_SWITCH,
             n_pwmOutput["fastFlipSwitch"].as<UINT32>()));
         std::string c_type = n_pwmOutput["type"].as<std::string>();
         UINT32 type = 1; // "coil"
         if (strcmp(c_type.c_str(), "flasher"))
         {
            type = 2;
         }
         else if (strcmp(c_type.c_str(), "lamp"))
         {
            type = 3;
         }
         m_pRS485Comm->SendConfigEvent(new ConfigEvent(
             n_pwmOutput["board"].as<UINT8>(),
             (UINT8)CONFIG_TOPIC_PWM,
             index++,
             (UINT8)CONFIG_TOPIC_TYPE,
             type));
      }

      // Wait before continuing.
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      // Tell I/O boards to read initial switch states, for example coin door closed.
      m_pRS485Comm->QueueEvent(new Event(EVENT_READ_SWITCHES, 1));

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
