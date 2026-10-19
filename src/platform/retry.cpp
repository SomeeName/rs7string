#include "platform/retry.h"
#include <windows.h>

void Rs7_SleepMs(unsigned ms) {
    Sleep(ms);
}
