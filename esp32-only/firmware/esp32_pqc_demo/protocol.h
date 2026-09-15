#ifndef ESP32_ONLY_PROTOCOL_H
#define ESP32_ONLY_PROTOCOL_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace demo_protocol {

bool readLine(char *output, size_t capacity, uint32_t timeoutMs);
bool readFrame(uint8_t *output, size_t capacity, size_t &length, uint32_t timeoutMs = 10000);
bool writeFrame(const uint8_t *data, size_t length, const char *label = "unlabelled");
void writeTxInfo();
void initializeBootDiagnostics();
void writeBootInfo(bool localOnly = false);
void writeLine(const char *line);
bool writeFormatted(const char *format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace demo_protocol

#endif
