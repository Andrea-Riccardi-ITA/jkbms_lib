#ifndef DEBUG_FUNCTIONS_H
#define DEBUG_FUNCTIONS_H

#include <Arduino.h>

// Debug output function types
typedef void (*DebugPrintFunc)(const char* format, ...);
typedef void (*DebugPrintlnFunc)(const char* message);
typedef void (*DebugPrintSimpleFunc)(const char* message);

// External debug functions that will be set by main.cpp
extern DebugPrintFunc debugPrintFunc;
extern DebugPrintlnFunc debugPrintlnFunc;
extern DebugPrintSimpleFunc debugPrintSimpleFunc;

// Example implementation functions that can be used in main.cpp:

// Standard Serial debug functions
void debugPrintSerial(const char* format, ...);
void debugPrintlnSerial(const char* message);
void debugPrintSimpleSerial(const char* message);

// Placeholder functions that do nothing (for disabling debug)
void debugPrintPlaceholder(const char* format, ...);
void debugPrintlnPlaceholder(const char* message);
void debugPrintSimplePlaceholder(const char* message);

#endif // DEBUG_FUNCTIONS_H
