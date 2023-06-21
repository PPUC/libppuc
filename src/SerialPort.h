#pragma once

#ifndef __ANDROID__
#include "serialib/serialib.h"
#endif

#include <inttypes.h>

#ifdef __ANDROID__
typedef void* (*AndroidGetJNIEnvFunc)();
#endif

class SerialPort
{
public:
#ifdef __ANDROID__
   void SetAndroidGetJNIEnvFunc(AndroidGetJNIEnvFunc func);
#endif

   void SetReadTimeout(int timeout);
   void SetWriteTimeout(int timeout);
   bool Open(const char *pDevice, int baudRate, int dataBits, int stopBits, int parity);
   bool IsOpen();
   void Close();
   int Available();
   void ClearDTR();
   void SetDTR();
   void ClearRTS();
   void SetRTS();
   int WriteBytes(uint8_t *pBytes, int size);
   int WriteChar(uint8_t byte);
   int ReadBytes(uint8_t *pBytes, int size);
   int ReadChar(uint8_t *pByte);
   uint8_t ReadByte();

private:
   int m_readTimeout = 0;
   int m_writeTimeout = 0;
#ifndef __ANDROID__
   serialib m_seriallib;
#else
   AndroidGetJNIEnvFunc m_androidGetJNIEnvFunc = nullptr;
#endif
};
