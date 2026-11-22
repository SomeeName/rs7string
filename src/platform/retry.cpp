#include "platform/retry.h"
#include <windows.h>

void Rsxr_SleepMs(unsigned ms) {
    Sleep(ms);
}
