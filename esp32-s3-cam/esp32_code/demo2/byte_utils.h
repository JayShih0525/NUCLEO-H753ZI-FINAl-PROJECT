#pragma once

#include <Arduino.h>

// =====================================================
// Small shared big-endian helpers.
// Used by both protocol.cpp (Computer<->ESP32 packets)
// and spi_link.cpp (ESP32<->STM32 SPI headers), so they
// live in one place instead of being duplicated.
// =====================================================

inline void WriteU16BE(uint8_t *destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    destination[1] = static_cast<uint8_t>(value & 0xFFU);
}

inline void WriteU32BE(uint8_t *destination, uint32_t value)
{
    destination[0] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    destination[1] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    destination[2] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    destination[3] = static_cast<uint8_t>(value & 0xFFU);
}

inline uint16_t ReadU16BE(const uint8_t *source)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(source[0]) << 8U) |
        static_cast<uint16_t>(source[1]));
}

inline uint32_t ReadU32BE(const uint8_t *source)
{
    return
        (static_cast<uint32_t>(source[0]) << 24U) |
        (static_cast<uint32_t>(source[1]) << 16U) |
        (static_cast<uint32_t>(source[2]) << 8U) |
        static_cast<uint32_t>(source[3]);
}
