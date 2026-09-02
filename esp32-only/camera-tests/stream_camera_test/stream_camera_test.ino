#include <Arduino.h>
#include "esp_camera.h"
#include "camera_board.h"

namespace {

constexpr uint32_t SERIAL_BAUD = 460800;
uint32_t g_frameId = 0;
bool g_cameraReady = false;
bool g_streaming = false;

void writeU32Be(uint32_t value) {
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(value >> 24),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value)};
  Serial.write(bytes, sizeof(bytes));
}

void sendFrame() {
  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr || frame->buf == nullptr || frame->len == 0) {
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    g_streaming = false;
    Serial.println("ERR CAPTURE_FAILED");
    return;
  }

  Serial.write(reinterpret_cast<const uint8_t *>("CAM1"), 4);
  writeU32Be(g_frameId++);
  writeU32Be(static_cast<uint32_t>(frame->len));
  Serial.write(frame->buf, frame->len);
  Serial.flush();
  esp_camera_fb_return(frame);
}

void handleCommand() {
  String command = Serial.readStringUntil('\n');
  command.trim();
  if (command == "STREAM_START") {
    if (g_cameraReady) {
      g_streaming = true;
      Serial.println("STREAM_STARTED");
    } else {
      Serial.println("ERR CAMERA_NOT_READY");
    }
  } else if (command == "STREAM_STOP") {
    g_streaming = false;
    Serial.println("STREAM_STOPPED");
  } else if (command == "INFO") {
    Serial.printf("INFO ready=%u streaming=%u frames=%lu psram=%u\n",
                  g_cameraReady ? 1 : 0,
                  g_streaming ? 1 : 0,
                  static_cast<unsigned long>(g_frameId),
                  ESP.getPsramSize());
  } else if (command.length() > 0) {
    Serial.println("ERR UNKNOWN_COMMAND");
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(50);
  delay(1500);
  Serial.printf("STREAM_TEST BOOT baud=%lu psram=%u\n",
                static_cast<unsigned long>(SERIAL_BAUD), ESP.getPsramSize());

  const esp_err_t result = initCamera(FRAMESIZE_QVGA, 15, 2);
  if (result != ESP_OK) {
    Serial.printf("ERR CAMERA_INIT 0x%X\n", static_cast<unsigned int>(result));
    return;
  }

  for (int i = 0; i < 3; ++i) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
  }

  g_cameraReady = true;
  Serial.println("STREAM_CAMERA_READY send STREAM_START");
}

void loop() {
  if (Serial.available()) {
    handleCommand();
  }

  if (g_streaming) {
    sendFrame();
  } else {
    delay(5);
  }
}
