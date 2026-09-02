#include <Arduino.h>
#include "esp_camera.h"
#include "camera_board.h"

namespace {

constexpr uint32_t SERIAL_BAUD = 460800;
uint32_t g_photoId = 0;
bool g_cameraReady = false;

void writeU32Be(uint32_t value) {
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(value >> 24),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value)};
  Serial.write(bytes, sizeof(bytes));
}

void discardWarmupFrames() {
  for (int i = 0; i < 3; ++i) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    delay(100);
  }
}

void captureAndSendPhoto() {
  if (!g_cameraReady) {
    Serial.println("ERR CAMERA_NOT_READY");
    return;
  }

  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr || frame->buf == nullptr || frame->len == 0) {
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
    Serial.println("ERR CAPTURE_FAILED");
    return;
  }

  // Binary frame: CAM1 + photo_id (BE u32) + jpeg_length (BE u32) + JPEG.
  Serial.write(reinterpret_cast<const uint8_t *>("CAM1"), 4);
  writeU32Be(g_photoId++);
  writeU32Be(static_cast<uint32_t>(frame->len));
  Serial.write(frame->buf, frame->len);
  Serial.flush();
  esp_camera_fb_return(frame);
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(250);
  delay(1500);

  Serial.printf("PHOTO_TEST BOOT baud=%lu psram=%u\n",
                static_cast<unsigned long>(SERIAL_BAUD), ESP.getPsramSize());

  const esp_err_t result = initCamera(FRAMESIZE_SVGA, 12, 1);
  if (result != ESP_OK) {
    Serial.printf("ERR CAMERA_INIT 0x%X\n", static_cast<unsigned int>(result));
    return;
  }

  discardWarmupFrames();
  g_cameraReady = true;
  Serial.println("PHOTO_CAMERA_READY send CAPTURE");
}

void loop() {
  if (!Serial.available()) {
    delay(5);
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();
  if (command == "CAPTURE") {
    captureAndSendPhoto();
  } else if (command == "INFO") {
    sensor_t *sensor = esp_camera_sensor_get();
    Serial.printf("INFO ready=%u sensor_pid=0x%04X psram=%u\n",
                  g_cameraReady ? 1 : 0,
                  sensor == nullptr ? 0 : sensor->id.PID,
                  ESP.getPsramSize());
  } else if (command.length() > 0) {
    Serial.println("ERR UNKNOWN_COMMAND");
  }
}
