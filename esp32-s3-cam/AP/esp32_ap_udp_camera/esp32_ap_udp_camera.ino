#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"

// =====================================================
// ESP32-S3 AP 設定
// =====================================================
const char* AP_SSID = "ESP32-CAMERA-UDP";
const char* AP_PASSWORD = "12345678";

constexpr uint16_t CONTROL_PORT = 5004;
constexpr uint16_t CAMERA_PORT = 5005;

WiFiUDP controlUdp;
WiFiUDP cameraUdp;

IPAddress receiverIp;
uint16_t receiverPort = CAMERA_PORT;
bool receiverReady = false;

// =====================================================
// UDP 影像封包設定
// =====================================================
constexpr uint32_t UDP_MAGIC = 0x43414D31UL; // "CAM1"
constexpr size_t UDP_PAYLOAD_SIZE = 1200;

constexpr uint16_t FLAG_FIRST = 0x0001;
constexpr uint16_t FLAG_LAST = 0x0002;

struct __attribute__((packed)) UdpFrameHeader {
    uint32_t magic;
    uint32_t frameId;
    uint16_t packetId;
    uint16_t packetCount;
    uint16_t payloadSize;
    uint16_t flags;
};

static_assert(sizeof(UdpFrameHeader) == 16, "UDP header must be 16 bytes");

// =====================================================
// ESP32-S3-CAM + OV3660 腳位
// =====================================================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11

#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

constexpr uint32_t FRAME_INTERVAL_MS = 100; // 約 10 FPS

bool initCamera()
{
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

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 25;
    config.fb_count = psramFound() ? 3 : 1;
    config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial0.printf("Camera init failed: 0x%X\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 1);
        sensor->set_brightness(sensor, 1);
    }

    return true;
}

void checkReceiverRegistration()
{
    int packetSize = controlUdp.parsePacket();
    if (packetSize <= 0) {
        return;
    }

    char message[32] = {};
    int readLength = controlUdp.read(
        reinterpret_cast<uint8_t*>(message),
        sizeof(message) - 1
    );

    if (readLength <= 0) {
        return;
    }

    message[readLength] = '\0';

    if (strncmp(message, "HELLO", 5) == 0) {
        receiverIp = controlUdp.remoteIP();
        receiverPort = CAMERA_PORT;
        receiverReady = true;

        controlUdp.beginPacket(receiverIp, controlUdp.remotePort());
        controlUdp.print("OK");
        controlUdp.endPacket();

        Serial0.printf("UDP receiver registered: %s:%u\n",
                      receiverIp.toString().c_str(),
                      receiverPort);
    }
}

bool sendFrameUdp(const uint8_t* jpegData, size_t jpegLength, uint32_t frameId)
{
    if (!receiverReady || jpegData == nullptr || jpegLength == 0) {
        return false;
    }

    uint16_t packetCount = static_cast<uint16_t>(
        (jpegLength + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE
    );

    for (uint16_t packetId = 0; packetId < packetCount; packetId++) {
        size_t offset = static_cast<size_t>(packetId) * UDP_PAYLOAD_SIZE;
        size_t remaining = jpegLength - offset;
        uint16_t payloadSize = static_cast<uint16_t>(
            remaining > UDP_PAYLOAD_SIZE ? UDP_PAYLOAD_SIZE : remaining
        );

        uint16_t flags = 0;
        if (packetId == 0) {
            flags |= FLAG_FIRST;
        }
        if (packetId == packetCount - 1) {
            flags |= FLAG_LAST;
        }

        UdpFrameHeader header = {};
        header.magic = htonl(UDP_MAGIC);
        header.frameId = htonl(frameId);
        header.packetId = htons(packetId);
        header.packetCount = htons(packetCount);
        header.payloadSize = htons(payloadSize);
        header.flags = htons(flags);

        if (!cameraUdp.beginPacket(receiverIp, receiverPort)) {
            return false;
        }

        cameraUdp.write(
            reinterpret_cast<const uint8_t*>(&header),
            sizeof(header)
        );
        cameraUdp.write(jpegData + offset, payloadSize);

        if (!cameraUdp.endPacket()) {
            return false;
        }

        if ((packetId & 0x0F) == 0x0F) {
            delay(1);
        }
    }

    return true;
}

void setup()
{
    Serial0.begin(115200);
    delay(1000);

    if (!initCamera()) {
        while (true) {
            delay(1000);
        }
    }

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial0.println("Failed to start Access Point");
        while (true) {
            delay(1000);
        }
    }

    controlUdp.begin(CONTROL_PORT);

    Serial0.println();
    Serial0.println("ESP32 UDP camera AP started");
    Serial0.printf("SSID: %s\n", AP_SSID);
    Serial0.printf("Password: %s\n", AP_PASSWORD);
    Serial0.printf("ESP32 AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial0.printf("Registration port: %u\n", CONTROL_PORT);
    Serial0.printf("Camera UDP port: %u\n", CAMERA_PORT);
    Serial0.println("Waiting for Python receiver registration...");
}

void loop()
{
    static uint32_t previousFrameTime = 0;
    static uint32_t frameId = 0;

    checkReceiverRegistration();

    if (!receiverReady) {
        delay(5);
        return;
    }

    uint32_t now = millis();
    if (now - previousFrameTime < FRAME_INTERVAL_MS) {
        delay(1);
        return;
    }

    previousFrameTime = now;

    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
        Serial0.println("Camera capture failed");
        delay(10);
        return;
    }

    bool ok = sendFrameUdp(frame->buf, frame->len, frameId);

    if (frameId % 10 == 0) {
        Serial0.printf("UDP frame=%lu JPEG=%u packets=%u result=%s\n",
                      static_cast<unsigned long>(frameId),
                      static_cast<unsigned>(frame->len),
                      static_cast<unsigned>(
                          (frame->len + UDP_PAYLOAD_SIZE - 1) /
                          UDP_PAYLOAD_SIZE
                      ),
                      ok ? "OK" : "FAIL");
    }

    frameId++;
    esp_camera_fb_return(frame);
}
