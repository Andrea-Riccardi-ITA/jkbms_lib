#include "debug_functions.h"
#include <Arduino.h>
#include <cstdarg>

// Global debug function pointers - these will be set in main.cpp
DebugPrintFunc debugPrintFunc = nullptr;
DebugPrintlnFunc debugPrintlnFunc = nullptr;
DebugPrintSimpleFunc debugPrintSimpleFunc = nullptr;

// Standard Serial debug implementations
void debugPrintSerial(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
}

void debugPrintlnSerial(const char* message) {
    Serial.println(message);
}

void debugPrintSimpleSerial(const char* message) {
    Serial.print(message);
}

// Placeholder functions that do nothing (for disabling debug)
void debugPrintPlaceholder(const char* format, ...) {
    // Do nothing
}

void debugPrintlnPlaceholder(const char* message) {
    // Do nothing
}

void debugPrintSimplePlaceholder(const char* message) {
    // Do nothing
}
