#include "PPUC.h"

#include <chrono>
#include <stdarg.h>
#include <thread>

void CALLBACK
OnLogMessage(const char* format, va_list args, const void* pUserData) {
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    printf("%s\n", buffer);
}

int
main(int argc, const char* argv[]) {
    PPUC* pPPUC = new PPUC();
    pPPUC->SetLogMessageCallback(OnLogMessage, NULL);
}