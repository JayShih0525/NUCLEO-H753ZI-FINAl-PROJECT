#pragma once

#include <Arduino.h>

// =====================================================
// Transport abstraction
// =====================================================
// protocol.cpp (and nothing else) calls these four
// functions to talk to the computer. Today
// transport_uart.cpp implements them over Serial (USB
// CDC). When this becomes WiFi, add transport_wifi.cpp
// implementing the same four functions and swap ONE
// include - camera.cpp, spi_link.cpp and protocol.cpp
// do not change at all.
//
// This file must never include anything transport-
// specific (no <SPI.h>, no WiFi.h) - only the interface.
// =====================================================

bool Transport_Init(void);

// Blocking write with internal chunking/timeout. Returns
// false only on a hard failure (e.g. link stalled).
bool Transport_Write(const uint8_t *data, size_t length);

// Non-blocking: bytes currently ready to read. 0 means
// "nothing waiting right now", not an error.
int Transport_Available(void);

// Reads up to maxLength bytes. Waits up to timeoutMs for
// the FIRST byte to arrive, then drains whatever is
// available in a short trailing window. Returns the
// number of bytes actually read (may be less than
// maxLength, including 0 on timeout).
size_t Transport_Read(uint8_t *buffer, size_t maxLength, uint32_t timeoutMs);
