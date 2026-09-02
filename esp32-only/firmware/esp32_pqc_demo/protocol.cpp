#include "protocol.h"

namespace demo_protocol {
namespace {

bool readExact(uint8_t *output, size_t length, uint32_t timeoutMs) {
  size_t offset = 0;
  uint32_t lastProgress = millis();

  while (offset < length) {
    const int available = Serial.available();
    if (available > 0) {
      const size_t wanted = min(length - offset, static_cast<size_t>(available));
      const size_t received = Serial.readBytes(output + offset, wanted);
      if (received > 0) {
        offset += received;
        lastProgress = millis();
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
    if (Serial.available() <= 0) {
      if (millis() - lastProgress >= timeoutMs) {
        output[0] = '\0';
        return false;
      }
      delay(1);
      continue;
    }

    const int value = Serial.read();
    lastProgress = millis();
    if (value < 0) {
      continue;
    }

    if (value == '\n') {
      output[length] = '\0';
      return true;
    }
    if (value == '\r') {
      continue;
    }

    if (length + 1 >= capacity) {
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

void writeFrame(const uint8_t *data, size_t length) {
  const uint8_t header[4] = {
      static_cast<uint8_t>((length >> 24) & 0xff),
      static_cast<uint8_t>((length >> 16) & 0xff),
      static_cast<uint8_t>((length >> 8) & 0xff),
      static_cast<uint8_t>(length & 0xff),
  };

  Serial.write(header, sizeof(header));
  if (length > 0 && data != nullptr) {
    Serial.write(data, length);
  }
  Serial.flush();
}

void writeLine(const char *line) {
  Serial.println(line);
  Serial.flush();
}

}  // namespace demo_protocol
