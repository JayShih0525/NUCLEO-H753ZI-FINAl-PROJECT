#ifndef ESP32_ONLY_PROTOCOL_H
#define ESP32_ONLY_PROTOCOL_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace demo_protocol {

bool readLine(char *output, size_t capacity, uint32_t timeoutMs);
bool readFrame(uint8_t *output, size_t capacity, size_t &length, uint32_t timeoutMs = 10000);
void writeFrame(const uint8_t *data, size_t length);
void writeLine(const char *line);

}  // namespace demo_protocol

#endif
