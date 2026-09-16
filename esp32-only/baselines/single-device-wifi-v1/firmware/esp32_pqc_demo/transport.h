#pragma once
#include <Arduino.h>
namespace demo_transport {
bool begin();
bool wifiMode();
bool connected();
void closeConnection();
bool acceptConnection();
Stream &io();
void flush();
void commandBegin(const char *command);
void commandEnd();
size_t write(const uint8_t *data, size_t length);
}
