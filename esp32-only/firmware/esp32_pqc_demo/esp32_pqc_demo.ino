#include <Arduino.h>

#include "crypto_demo.h"
#include "camera_demo.h"

namespace {

void pqcTask(void *parameter) {
  (void)parameter;

  if (!initializeCrypto()) {
    Serial.println("PQC initialization failed; reset the board to retry.");
    vTaskDelete(nullptr);
    return;
  }

  runProtocolLoop();
}

}  // namespace

void setup() {
  Serial.begin(460800);
  delay(1500);

  Serial.println();
  Serial.println("ESP32-S3-CAM ML-KEM + AES-GCM + ML-DSA demo");

  Serial.println("Initializing OV2640 camera...");
  if (initializeCamera()) {
    Serial.println("OV2640 camera ready (STREAM mode)");
  } else {
    Serial.println("Camera unavailable; crypto commands remain usable");
  }

  const BaseType_t result = xTaskCreate(
      pqcTask,
      "pqc-demo",
      98304,
      nullptr,
      1,
      nullptr);

  if (result != pdPASS) {
    Serial.println("ERR unable to allocate the 96 KB PQC task stack");
  }
}

void loop() {
  delay(1000);
}
