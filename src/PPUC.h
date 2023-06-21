#pragma once

#define PPUC_VERSION_MAJOR 0 // X Digits
#define PPUC_VERSION_MINOR 1 // Max 2 Digits
#define PPUC_VERSION_PATCH 0 // Max 2 Digits

#define _PPUC_STR(x) #x
#define PPUC_STR(x) _PPUC_STR(x)

#define PPUC_VERSION PPUC_STR(PPUC_VERSION_MAJOR) "." PPUC_STR(PPUC_VERSION_MINOR) "." PPUC_STR(PPUC_VERSION_PATCH)
#define PPUC_MINOR_VERSION PPUC_STR(PPUC_VERSION_MAJOR) "." PPUC_STR(PPUC_VERSION_MINOR)

#if defined(_WIN32) || defined(_WIN64)
#define PPUCAPI __declspec(dllexport)
#define CALLBACK __stdcall
#else
#define PPUCAPI __attribute__ ((visibility ("default")))
#define CALLBACK
#endif

#include <yaml-cpp/yaml.h>

#ifdef __ANDROID__
typedef void* (*PPUC_AndroidGetJNIEnvFunc)();<
#endif

typedef void (CALLBACK *PPUC_LogMessageCallback)(const char* format, va_list args, const void* userData);

struct PPUCSwitchState
{
   int number;
   int state;

   PPUCSwitchState(int n, int s)
   {
      number = n;
      state = s;
   }
};

class RS485Comm;

class PPUCAPI PPUC
{
public:
   PPUC();
   ~PPUC();

#ifdef __ANDROID__
   void SetAndroidGetJNIEnvFunc(PPUC_AndroidGetJNIEnvFunc func);
#endif

   void SetLogMessageCallback(PPUC_LogMessageCallback callback, const void* userData);

   void LoadConfiguration(const char *configFile);
   void SetDebug(bool debug);
   bool GetDebug();
   void SetRom(const char *rom);
   const char * GetRom();
   void SetSerial(const char *serial);
   bool Connect();
   void Disconnect();

   void SetSolenoidState(int number, int state);
   void SetLampState(int number, int state);
   PPUCSwitchState* GetNextSwitchState();

private:
   YAML::Node m_ppucConfig;
   RS485Comm* m_pRS485Comm;

   bool m_debug;
   char *m_rom;
   char *m_serial;
};
