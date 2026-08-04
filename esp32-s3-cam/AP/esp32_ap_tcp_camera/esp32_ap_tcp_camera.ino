#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"

// =====================================================
// ESP32-S3 AP 設定
// =====================================================
const char* AP_SSID = "ESP32-CAMERA-TCP";
const char* AP_PASSWORD = "12345678";

constexpr uint16_t TCP_PORT = 5000;
WiFiServer tcpServer(TCP_PORT);

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

bool sendAll(WiFiClient& client, const uint8_t* data, size_t length)
{
    size_t sent = 0;

    while (sent < length && client.connected()) {
        size_t written = client.write(data + sent, length - sent);

        if (written == 0) {
            delay(1);
            continue;
        }

        sent += written;
    }

    return sent == length;
}

void streamToClient(WiFiClient& client)
{
    Serial0.printf("TCP client connected: %s\n",
                  client.remoteIP().toString().c_str());

    uint32_t frameId = 0;
    uint32_t previousFrameTime = 0;

    while (client.connected()) {
        uint32_t now = millis();

        if (now - previousFrameTime < FRAME_INTERVAL_MS) {
            delay(1);
            continue;
        }

        previousFrameTime = now;

        camera_fb_t* frame = esp_camera_fb_get();
        if (frame == nullptr) {
            Serial0.println("Camera capture failed");
            delay(10);
            continue;
        }

        // TCP frame format:
        // [4-byte JPEG length, big endian][JPEG bytes]
        uint32_t jpegLength = static_cast<uint32_t>(frame->len);
        uint8_t lengthHeader[4] = {
            static_cast<uint8_t>((jpegLength >> 24) & 0xFF),
            static_cast<uint8_t>((jpegLength >> 16) & 0xFF),
            static_cast<uint8_t>((jpegLength >> 8) & 0xFF),
            static_cast<uint8_t>(jpegLength & 0xFF)
        };

        bool ok = sendAll(client, lengthHeader, sizeof(lengthHeader)) &&
                  sendAll(client, frame->buf, frame->len);

        frameId++;

        if (frameId % 10 == 0) {
            Serial0.printf("TCP frame=%lu JPEG=%u result=%s\n",
                          static_cast<unsigned long>(frameId),
                          static_cast<unsigned>(frame->len),
                          ok ? "OK" : "FAIL");
        }

        esp_camera_fb_return(frame);

        if (!ok) {
            break;
        }
    }

    client.stop();
    Serial0.println("TCP client disconnected");
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

    tcpServer.begin();
    tcpServer.setNoDelay(true);

    Serial0.println();
    Serial0.println("ESP32 TCP camera AP started");
    Serial0.printf("SSID: %s\n", AP_SSID);
    Serial0.printf("Password: %s\n", AP_PASSWORD);
    Serial0.printf("ESP32 AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial0.printf("TCP port: %u\n", TCP_PORT);
}

void loop()
{
    WiFiClient client = tcpServer.available();

    if (client) {
        client.setNoDelay(true);
        streamToClient(client);
    }

    delay(1);
}
