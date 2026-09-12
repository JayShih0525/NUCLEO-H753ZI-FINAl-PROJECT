#include "transport.h"
#include "protocol.h"
#include <esp_system.h>
#include <esp_timer.h>

namespace demo_protocol {
namespace {
char g_bootId[33] = {};
esp_reset_reason_t g_resetReason;
uint32_t g_shortWrites = 0;
uint32_t g_writeFailures = 0;
size_t g_lastRequested = 0;
size_t g_lastWritten = 0;

bool writeExact(const uint8_t *data, size_t length) {
  size_t offset = 0;
  uint32_t lastProgress = millis();
  while (offset < length) {
    if (!demo_transport::connected()) return false;
    const size_t wanted = length - offset;
    const size_t written = demo_transport::write(data + offset, wanted);
    if (written < wanted) ++g_shortWrites;
    offset += written;
    if (written > 0) lastProgress = millis();
    else if (millis() - lastProgress >= 2000) {
      ++g_writeFailures;
      g_lastRequested = length;
      g_lastWritten = offset;
      return false;
    } else delay(1);
  }
  return true;
}

bool readExact(uint8_t *output, size_t length, uint32_t timeoutMs) {
  size_t offset = 0;
  uint32_t lastProgress = millis();

  while (offset < length) {
    if (!demo_transport::connected()) return false;
    const int available = demo_transport::io().available();
    if (available > 0) {
      const size_t wanted = min(length - offset, static_cast<size_t>(available));
      const size_t received = demo_transport::io().readBytes(output + offset, wanted);
      if (received > 0) {
        offset += received;
        lastProgress = millis();
      } else {
        if (millis() - lastProgress >= timeoutMs) return false;
        delay(1);
      }
      continue;
    }

    if (millis() - lastProgress >= timeoutMs) {
      return false;
    }
    delay(1);
  }

  return true;
}

}  // namespace

bool readLine(char *output, size_t capacity, uint32_t timeoutMs) {
  if (output == nullptr || capacity < 2) {
    return false;
  }

  size_t length = 0;
  uint32_t lastProgress = millis();

  while (true) {
    if (!demo_transport::connected()) return false;
    if (demo_transport::io().available() <= 0) {
      const uint32_t waitMs = demo_transport::wifiMode() && length > 0 ? 10000 : timeoutMs;
      if (millis() - lastProgress >= waitMs) {
        if (demo_transport::wifiMode() && length > 0) {
          Serial.printf("[TCP] partial command timeout bytes=%u\n", static_cast<unsigned int>(length));
          demo_transport::closeConnection();
        }
        output[0] = '\0';
        return false;
      }
      delay(1);
      continue;
    }

    const int value = demo_transport::io().read();
    if (value < 0) {
      if (millis() - lastProgress >= timeoutMs) {
        if (demo_transport::wifiMode()) demo_transport::closeConnection();
        return false;
      }
      delay(1);
      continue;
    }
    lastProgress = millis();

    if (value == '\n') {
      output[length] = '\0';
      return true;
    }
    if (value == '\r') {
      continue;
    }

    if (length + 1 >= capacity) {
      if (demo_transport::wifiMode()) demo_transport::closeConnection();
      output[0] = '\0';
      return false;
    }
    output[length++] = static_cast<char>(value);
  }
}

bool readFrame(uint8_t *output, size_t capacity, size_t &length, uint32_t timeoutMs) {
  uint8_t header[4];
  length = 0;

  if (!readExact(header, sizeof(header), timeoutMs)) {
    return false;
  }

  const uint32_t announced =
      (static_cast<uint32_t>(header[0]) << 24) |
      (static_cast<uint32_t>(header[1]) << 16) |
      (static_cast<uint32_t>(header[2]) << 8) |
      static_cast<uint32_t>(header[3]);

  if (announced > capacity || (announced > 0 && output == nullptr)) {
    uint8_t discard[64];
    uint32_t remaining = announced;
    while (remaining > 0) {
      const size_t chunk = min(static_cast<uint32_t>(sizeof(discard)), remaining);
      if (!readExact(discard, chunk, timeoutMs)) {
        break;
      }
      remaining -= chunk;
    }
    return false;
  }

  if (announced > 0 && !readExact(output, announced, timeoutMs)) {
    return false;
  }

  length = announced;
  return true;
}

bool writeFrame(const uint8_t *data, size_t length) {
  const uint8_t header[4] = {
      static_cast<uint8_t>((length >> 24) & 0xff),
      static_cast<uint8_t>((length >> 16) & 0xff),
      static_cast<uint8_t>((length >> 8) & 0xff),
      static_cast<uint8_t>(length & 0xff),
  };

  if (length > 0 && data == nullptr) return false;
  if (!writeExact(header, sizeof(header))) return false;
  if (length > 0 && !writeExact(data, length)) return false;
  demo_transport::flush();
  return true;
}

void initializeBootDiagnostics() {
  g_resetReason = esp_reset_reason();
  uint8_t id[16];
  esp_fill_random(id, sizeof(id));
  for (size_t i = 0; i < sizeof(id); ++i) {
    snprintf(g_bootId + i * 2, 3, "%02x", id[i]);
  }
}

void writeBootInfo() {
  const char *reason = "OTHER";
  switch (g_resetReason) {
    case ESP_RST_UNKNOWN: reason = "UNKNOWN"; break;
    case ESP_RST_POWERON: reason = "POWERON"; break;
    case ESP_RST_EXT: reason = "EXT"; break;
    case ESP_RST_SW: reason = "SW"; break;
    case ESP_RST_PANIC: reason = "PANIC"; break;
    case ESP_RST_INT_WDT: reason = "INT_WDT"; break;
    case ESP_RST_TASK_WDT: reason = "TASK_WDT"; break;
    case ESP_RST_WDT: reason = "WDT"; break;
    case ESP_RST_DEEPSLEEP: reason = "DEEPSLEEP"; break;
    case ESP_RST_BROWNOUT: reason = "BROWNOUT"; break;
    case ESP_RST_SDIO: reason = "SDIO"; break;
    default: break;
  }
  // Diagnostic identifier only; not the persistent, authenticated device identity.
  demo_transport::io().printf("BOOT boot_id=%s reset_reason=%d reset_name=%s uptime_ms=%llu\n",
      g_bootId, static_cast<int>(g_resetReason), reason,
      static_cast<unsigned long long>(esp_timer_get_time() / 1000));
  demo_transport::flush();
}

void writeTxInfo() {
  // Only emit when explicitly requested BETWEEN transactions, never inside
  // a binary response: diagnostic text would itself corrupt the stream.
  demo_transport::io().printf("TX short_writes=%lu failures=%lu last_requested=%u last_written=%u\n",
      static_cast<unsigned long>(g_shortWrites), static_cast<unsigned long>(g_writeFailures),
      static_cast<unsigned int>(g_lastRequested), static_cast<unsigned int>(g_lastWritten));
  demo_transport::flush();
}

void writeLine(const char *line) {
  if (demo_transport::wifiMode()) {
    if (!writeExact(reinterpret_cast<const uint8_t *>(line), strlen(line)) ||
        !writeExact(reinterpret_cast<const uint8_t *>("\n"), 1)) {
      demo_transport::closeConnection();
    }
  } else {
    demo_transport::io().println(line);
  }
  demo_transport::flush();
}

}  // namespace demo_protocol
