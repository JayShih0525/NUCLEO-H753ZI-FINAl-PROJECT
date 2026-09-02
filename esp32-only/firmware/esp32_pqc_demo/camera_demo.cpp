#include "camera_demo.h"

#include <Arduino.h>

namespace {

constexpr int CAM_PIN_PWDN = -1;
constexpr int CAM_PIN_RESET = -1;
constexpr int CAM_PIN_XCLK = 15;
constexpr int CAM_PIN_SIOD = 4;
constexpr int CAM_PIN_SIOC = 5;
constexpr int CAM_PIN_D0 = 11;
constexpr int CAM_PIN_D1 = 9;
constexpr int CAM_PIN_D2 = 8;
constexpr int CAM_PIN_D3 = 10;
constexpr int CAM_PIN_D4 = 12;
constexpr int CAM_PIN_D5 = 18;
constexpr int CAM_PIN_D6 = 17;
constexpr int CAM_PIN_D7 = 16;
constexpr int CAM_PIN_VSYNC = 6;
constexpr int CAM_PIN_HREF = 7;
constexpr int CAM_PIN_PCLK = 13;

bool g_cameraReady = false;
bool g_photoMode = false;

bool applyMode(framesize_t size, int quality, bool photoMode) {
  if (!g_cameraReady) {
    return false;
  }
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr || sensor->set_framesize(sensor, size) != 0 ||
      sensor->set_quality(sensor, quality) != 0) {
    return false;
  }
  g_photoMode = photoMode;

  // Throw away the first frame after a mode change so its dimensions and
  // exposure belong to the new mode.
  camera_fb_t *stale = esp_camera_fb_get();
  if (stale != nullptr) {
    esp_camera_fb_return(stale);
  }
  return true;
}

}  // namespace

bool initializeCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Allocate buffers large enough for photo mode, then run in QVGA stream
  // mode by default. Both frame buffers live in the 8 MB PSRAM.
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 15;
  config.fb_count = 2;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    Serial.printf("ERR CAMERA_INIT 0x%X\n", static_cast<unsigned int>(result));
    return false;
  }

  g_cameraReady = true;
  if (!setCameraStreamMode()) {
    Serial.println("ERR CAMERA_STREAM_MODE");
    return false;
  }

  for (int i = 0; i < 2; ++i) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame != nullptr) {
      esp_camera_fb_return(frame);
    }
  }
  return true;
}

bool cameraIsReady() {
  return g_cameraReady;
}

bool setCameraPhotoMode() {
  return applyMode(FRAMESIZE_SVGA, 12, true);
}

bool setCameraStreamMode() {
  return applyMode(FRAMESIZE_QVGA, 15, false);
}

const char *cameraModeName() {
  return g_photoMode ? "PHOTO" : "STREAM";
}

camera_fb_t *captureCameraFrame() {
  return g_cameraReady ? esp_camera_fb_get() : nullptr;
}

void releaseCameraFrame(camera_fb_t *frame) {
  if (frame != nullptr) {
    esp_camera_fb_return(frame);
  }
}
