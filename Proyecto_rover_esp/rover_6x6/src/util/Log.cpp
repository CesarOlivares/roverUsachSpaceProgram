#include "Log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

volatile bool g_quiet = false;

void emit(const char *level, const char *tag, const char *fmt, va_list args)
{
    char line[192];
    int n = snprintf(line, sizeof(line), "%s (%s) ", level, tag);
    if (n < 0) return;
    if ((size_t)n < sizeof(line)) {
        int m = vsnprintf(line + n, sizeof(line) - n, fmt, args);
        if (m > 0) n += m;
    }
    if ((size_t)n > sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    Serial.write((const uint8_t *)line, n);
}

}  // namespace

namespace Log {

void info(const char *tag, const char *fmt, ...)
{
    if (g_quiet) return;
    va_list args;
    va_start(args, fmt);
    emit("I", tag, fmt, args);
    va_end(args);
}

void warn(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit("W", tag, fmt, args);
    va_end(args);
}

void quiet() { g_quiet = true; }

}  // namespace Log
