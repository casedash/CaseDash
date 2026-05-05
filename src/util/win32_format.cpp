#include "util/win32_format.h"

#include <cstdio>

std::string FormatHresult(HRESULT value) {
    char buffer[32];
    sprintf_s(buffer, "0x%08lX", static_cast<unsigned long>(value));
    return buffer;
}
