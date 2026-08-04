#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include "esp_camera.h"
#include <lwip/inet.h>

// ============================== Wi-Fi ==============================
const char* WIFI_SSID = "HITRON-9DF0";
const char* WIFI_PASSWORD = "E91IO8N07HQY";
IPAddress RECEIVER_IP(192, 168, 213, 12);
constexpr uint16_t RECEIVER_PORT = 5005;
constexpr uint16_t LOCAL_UDP_PORT = 5006;

// ============================== Camera ==============================
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_JPEG_QUALITY 28
#define CAMERA_XCLK_FREQ 20000000
constexpr uint32_t TARGET_FPS = 2;  // SPI blocking demo: begin low, then increase
constexpr uint32_t FRAME_INTERVAL_MS = 1000 / TARGET_FPS;

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// ============================== SPI ==============================
// ESP32-S3 master -> STM32H753 SPI1 slave
#define SPI_SCK  1
#define SPI_MISO 2
#define SPI_MOSI 3
#define SPI_CS   14

constexpr uint32_t SPI_FREQUENCY = 1000000;  // first test at 1 MHz
constexpr size_t SPI_PAYLOAD_SIZE = 1024;
constexpr uint32_t SPI_MAGIC = 0x53504931;    // "SPI1"
constexpr uint8_t SPI_CMD_ENCRYPT = 0x01;
constexpr uint8_t SPI_STATUS_OK = 0x80;

struct __attribute__((packed)) SpiChunkHeader {
  uint32_t magic;
  uint32_t frameId;
  uint16_t chunkId;
  uint16_t totalChunks;
  uint16_t payloadLength;
  uint8_t command;
  uint8_t status;
};

struct __attribute__((packed)) SpiChunkPacket {
  SpiChunkHeader header;
  uint8_t payload[SPI_PAYLOAD_SIZE];
};

static_assert(sizeof(SpiChunkHeader) == 16, "SPI header must be 16 bytes");
static_assert(sizeof(SpiChunkPacket) == 1040, "SPI packet must be 1040 bytes");

SPIClass h753SPI(FSPI);
static SpiChunkPacket spiRequest;
static SpiChunkPacket spiResponse;
static uint8_t* encryptedFrame = nullptr;
static size_t encryptedCapacity = 0;

// ============================== UDP ==============================
constexpr size_t UDP_PAYLOAD_SIZE = 1200;
constexpr uint32_t UDP_MAGIC = 0x43414D31; // "CAM1"
constexpr uint16_t FLAG_FIRST_PACKET = 0x0001;
constexpr uint16_t FLAG_LAST_PACKET = 0x0002;

struct __attribute__((packed)) UdpFrameHeader {
  uint32_t magic;
  uint32_t frameId;
  uint16_t packetId;
  uint16_t packetCount;
  uint16_t payloadSize;
  uint16_t flags;
};
static_assert(sizeof(UdpFrameHeader) == 16, "UDP header must be 16 bytes");

WiFiUDP udp;
uint32_t nextFrameId = 0;
uint32_t sentFrames = 0;
uint32_t failedFrames = 0;

bool connectWiFi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > 20000) return false;
  }
  Serial.printf("\nESP32 IP: %s\n", WiFi.localIP().toString().c_str());
  return udp.begin(LOCAL_UDP_PORT) == 1;
}

bool setupCamera() {
  if (!psramFound()) {
    Serial.println("PSRAM not found");
    return false;
  }
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAMERA_XCLK_FREQ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("esp_camera_init failed: 0x%X\n", err);
    return false;
  }
  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor && sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_contrast(sensor, 1);
    sensor->set_saturation(sensor, -1);
  }
  for (int i = 0; i < 5; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(100);
  }
  return true;
}

bool setupSpi() {
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);
  h753SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
  Serial.println("SPI pins: SCK=1, MISO=2, MOSI=3, CS=14");
  return true;
}

void spiTransferPacket(const void* tx, void* rx) {
  h753SPI.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_CS, LOW);
  delayMicroseconds(50);
  h753SPI.transferBytes(
      reinterpret_cast<const uint8_t*>(tx),
      reinterpret_cast<uint8_t*>(rx),
      sizeof(SpiChunkPacket));
  delayMicroseconds(20);
  digitalWrite(SPI_CS, HIGH);
  h753SPI.endTransaction();
}

bool encryptChunkOnH753(uint32_t frameId, uint16_t chunkId,
                        uint16_t totalChunks, const uint8_t* input,
                        uint16_t length, uint8_t* output) {
  if (!input || !output || length == 0 || length > SPI_PAYLOAD_SIZE) return false;

  memset(&spiRequest, 0, sizeof(spiRequest));
  memset(&spiResponse, 0, sizeof(spiResponse));
  spiRequest.header.magic = SPI_MAGIC;
  spiRequest.header.frameId = frameId;
  spiRequest.header.chunkId = chunkId;
  spiRequest.header.totalChunks = totalChunks;
  spiRequest.header.payloadLength = length;
  spiRequest.header.command = SPI_CMD_ENCRYPT;
  memcpy(spiRequest.payload, input, length);

  // Transaction 1: send request. Bytes received here are intentionally ignored.
  spiTransferPacket(&spiRequest, &spiResponse);

  // STM32 returns from HAL_SPI_TransmitReceive(), encrypts, then waits for transaction 2.
  delayMicroseconds(300);

  SpiChunkPacket dummy = {};
  memset(&spiResponse, 0, sizeof(spiResponse));
  // Transaction 2: generate clocks and receive the prepared encrypted response.
  spiTransferPacket(&dummy, &spiResponse);

  if (spiResponse.header.magic != SPI_MAGIC ||
      spiResponse.header.status != SPI_STATUS_OK ||
      spiResponse.header.frameId != frameId ||
      spiResponse.header.chunkId != chunkId ||
      spiResponse.header.payloadLength != length) {
    Serial.printf("SPI response invalid: frame=%lu chunk=%u magic=%08lX status=%02X len=%u\n",
                  static_cast<unsigned long>(frameId), chunkId,
                  static_cast<unsigned long>(spiResponse.header.magic),
                  spiResponse.header.status, spiResponse.header.payloadLength);
    return false;
  }

  memcpy(output, spiResponse.payload, length);
  return true;
}

bool ensureEncryptedBuffer(size_t required) {
  if (required <= encryptedCapacity && encryptedFrame) return true;
  if (encryptedFrame) {
    free(encryptedFrame);
    encryptedFrame = nullptr;
    encryptedCapacity = 0;
  }
  encryptedFrame = static_cast<uint8_t*>(ps_malloc(required));
  if (!encryptedFrame) return false;
  encryptedCapacity = required;
  return true;
}

bool encryptFrameOnH753(uint32_t frameId, const uint8_t* jpeg, size_t length) {
  if (!ensureEncryptedBuffer(length)) {
    Serial.println("PSRAM encrypted buffer allocation failed");
    return false;
  }
  size_t countSize = (length + SPI_PAYLOAD_SIZE - 1) / SPI_PAYLOAD_SIZE;
  if (countSize == 0 || countSize > UINT16_MAX) return false;
  uint16_t totalChunks = static_cast<uint16_t>(countSize);

  for (uint16_t chunkId = 0; chunkId < totalChunks; ++chunkId) {
    size_t offset = static_cast<size_t>(chunkId) * SPI_PAYLOAD_SIZE;
    uint16_t chunkLength = static_cast<uint16_t>(min(length - offset, SPI_PAYLOAD_SIZE));
    if (!encryptChunkOnH753(frameId, chunkId, totalChunks,
                            jpeg + offset, chunkLength,
                            encryptedFrame + offset)) {
      return false;
    }
    yield();
  }
  return true;
}

bool sendUdpPacket(uint32_t frameId, uint16_t packetId, uint16_t packetCount,
                   const uint8_t* payload, uint16_t payloadSize) {
  uint16_t flags = 0;
  if (packetId == 0) flags |= FLAG_FIRST_PACKET;
  if (packetId == packetCount - 1) flags |= FLAG_LAST_PACKET;
  UdpFrameHeader header = {};
  header.magic = htonl(UDP_MAGIC);
  header.frameId = htonl(frameId);
  header.packetId = htons(packetId);
  header.packetCount = htons(packetCount);
  header.payloadSize = htons(payloadSize);
  header.flags = htons(flags);
  if (!udp.beginPacket(RECEIVER_IP, RECEIVER_PORT)) return false;
  if (udp.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    udp.endPacket(); return false;
  }
  if (udp.write(payload, payloadSize) != payloadSize) {
    udp.endPacket(); return false;
  }
  return udp.endPacket() == 1;
}

bool sendEncryptedFrame(uint32_t frameId, const uint8_t* data, size_t length) {
  size_t countSize = (length + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
  if (countSize == 0 || countSize > UINT16_MAX) return false;
  uint16_t packetCount = static_cast<uint16_t>(countSize);
  for (uint16_t packetId = 0; packetId < packetCount; ++packetId) {
    size_t offset = static_cast<size_t>(packetId) * UDP_PAYLOAD_SIZE;
    uint16_t payloadLength = static_cast<uint16_t>(min(length - offset, UDP_PAYLOAD_SIZE));
    if (!sendUdpPacket(frameId, packetId, packetCount, data + offset, payloadLength)) return false;
    if ((packetId & 7U) == 7U) yield();
  }
  return true;
}

void captureEncryptAndSend() {
  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame || frame->format != PIXFORMAT_JPEG || frame->len < 4) {
    if (frame) esp_camera_fb_return(frame);
    ++failedFrames;
    return;
  }
  uint32_t frameId = nextFrameId++;
  uint32_t start = millis();
  bool ok = encryptFrameOnH753(frameId, frame->buf, frame->len);
  if (ok) ok = sendEncryptedFrame(frameId, encryptedFrame, frame->len);
  uint32_t elapsed = millis() - start;
  Serial.printf("Frame %lu, JPEG=%u bytes, result=%s, total=%lu ms\n",
                static_cast<unsigned long>(frameId),
                static_cast<unsigned>(frame->len),
                ok ? "OK" : "FAIL",
                static_cast<unsigned long>(elapsed));
  esp_camera_fb_return(frame);
  if (ok) ++sentFrames; else ++failedFrames;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("ESP32 camera -> H753 XOR -> UDP receiver");
  if (!setupCamera() || !setupSpi() || !connectWiFi()) {
    Serial.println("Initialization failed");
    while (true) delay(1000);
  }
}

void loop() {
  static uint32_t nextFrameTime = 0;
  uint32_t now = millis();
  if (static_cast<int32_t>(now - nextFrameTime) >= 0) {
    nextFrameTime = now + FRAME_INTERVAL_MS;
    captureEncryptAndSend();
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(500);
  }
}
