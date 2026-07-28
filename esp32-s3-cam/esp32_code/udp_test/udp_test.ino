#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"
#include <lwip/inet.h>

// =====================================================
// Wi-Fi 設定
// =====================================================

const char* WIFI_SSID = "HITRON-9DF0";
const char* WIFI_PASSWORD = "E91IO8N07HQY";

// 改成執行 Python 電腦的 IP
IPAddress RECEIVER_IP(192, 168, 213, 12);

constexpr uint16_t RECEIVER_PORT = 5005;

// ESP32 本機 UDP port
constexpr uint16_t LOCAL_UDP_PORT = 5006;

// =====================================================
// Camera 設定
// =====================================================

#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_JPEG_QUALITY 28
#define CAMERA_XCLK_FREQ 20000000

// 目標 FPS
constexpr uint32_t TARGET_FPS = 15;
constexpr uint32_t FRAME_INTERVAL_MS = 1000 / TARGET_FPS;

// =====================================================
// UDP 封包設定
// =====================================================

/*
 * Ethernet 常見 MTU 是 1500 bytes。
 *
 * UDP payload 不要太接近 1500，避免 IP fragmentation。
 * Header 16 bytes + JPEG payload 1200 bytes。
 */
constexpr size_t UDP_PAYLOAD_SIZE = 1200;

// 固定識別碼："CAM1"
constexpr uint32_t PACKET_MAGIC = 0x43414D31;

// 封包 flags
constexpr uint16_t FLAG_FIRST_PACKET = 0x0001;
constexpr uint16_t FLAG_LAST_PACKET  = 0x0002;

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

// =====================================================
// UDP Header
// =====================================================

/*
 * 所有整數都使用 Network Byte Order，也就是 Big Endian。
 *
 * Python 對應格式：
 *
 * !IIHHHH
 *
 * magic        uint32_t
 * frame_id     uint32_t
 * packet_id    uint16_t
 * packet_count uint16_t
 * payload_size uint16_t
 * flags        uint16_t
 *
 * 總共 16 bytes。
 */
struct __attribute__((packed)) UdpFrameHeader
{
    uint32_t magic;
    uint32_t frameId;

    uint16_t packetId;
    uint16_t packetCount;

    uint16_t payloadSize;
    uint16_t flags;
};

static_assert(
    sizeof(UdpFrameHeader) == 16,
    "UdpFrameHeader size must be 16 bytes"
);

// =====================================================
// 全域變數
// =====================================================

WiFiUDP udp;

uint32_t nextFrameId = 0;

uint32_t capturedFrames = 0;
uint32_t sentFrames = 0;
uint32_t failedFrames = 0;
uint32_t failedPackets = 0;

uint64_t sentBytes = 0;

uint32_t lastStatusTime = 0;
uint32_t lastSentFrames = 0;
uint64_t lastSentBytes = 0;

// =====================================================
// Wi-Fi
// =====================================================

bool connectWiFi()
{
    Serial.println();
    Serial.println("Connecting to Wi-Fi...");

    WiFi.mode(WIFI_STA);

    /*
     * 關閉 Wi-Fi 省電，降低等待與延遲。
     * 代價是耗電增加。
     */
    WiFi.setSleep(false);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t startTime = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - startTime > 20000)
        {
            Serial.println();
            Serial.println("Wi-Fi connection timeout.");
            return false;
        }
    }

    Serial.println();
    Serial.println("Wi-Fi connected.");

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Receiver IP: ");
    Serial.println(RECEIVER_IP);

    Serial.print("Receiver port: ");
    Serial.println(RECEIVER_PORT);

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    if (!udp.begin(LOCAL_UDP_PORT))
    {
        Serial.println("udp.begin() failed.");
        return false;
    }

    Serial.print("Local UDP port: ");
    Serial.println(LOCAL_UDP_PORT);

    return true;
}

// =====================================================
// Camera
// =====================================================

bool setupCamera()
{
    Serial.println();
    Serial.println("Initializing camera...");

    if (!psramFound())
    {
        Serial.println("PSRAM not found.");
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

    const esp_err_t error = esp_camera_init(&config);

    if (error != ESP_OK)
    {
        Serial.printf(
            "esp_camera_init failed: 0x%X\n",
            error
        );

        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();

    if (sensor == nullptr)
    {
        Serial.println("Cannot get camera sensor.");
        return false;
    }

    Serial.printf(
        "Camera sensor PID: 0x%04X\n",
        sensor->id.PID
    );

    if (sensor->id.PID == OV3660_PID)
    {
        Serial.println("OV3660 detected.");

        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 1);

        sensor->set_brightness(sensor, 1);
        sensor->set_contrast(sensor, 1);
        sensor->set_saturation(sensor, -1);

        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);

        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);

        sensor->set_bpc(sensor, 1);
        sensor->set_wpc(sensor, 1);
        sensor->set_raw_gma(sensor, 1);
        sensor->set_lenc(sensor, 1);
    }

    sensor->set_framesize(
        sensor,
        CAMERA_FRAME_SIZE
    );

    // 丟掉啟動時曝光不穩定的影格
    for (int i = 0; i < 5; i++)
    {
        camera_fb_t* frame = esp_camera_fb_get();

        if (frame != nullptr)
        {
            esp_camera_fb_return(frame);
        }

        delay(100);
    }

    Serial.println("Camera initialized.");
    Serial.println("Resolution: VGA 640 x 480");

    Serial.print("JPEG quality: ");
    Serial.println(CAMERA_JPEG_QUALITY);

    return true;
}

// =====================================================
// 傳送單一 UDP 封包
// =====================================================

bool sendUdpPacket(
    uint32_t frameId,
    uint16_t packetId,
    uint16_t packetCount,
    const uint8_t* payload,
    uint16_t payloadSize
)
{
    if (
        payload == nullptr ||
        payloadSize == 0 ||
        payloadSize > UDP_PAYLOAD_SIZE
    )
    {
        return false;
    }

    uint16_t flags = 0;

    if (packetId == 0)
    {
        flags |= FLAG_FIRST_PACKET;
    }

    if (packetId == packetCount - 1)
    {
        flags |= FLAG_LAST_PACKET;
    }

    UdpFrameHeader header = {};

    header.magic = htonl(PACKET_MAGIC);
    header.frameId = htonl(frameId);

    header.packetId = htons(packetId);
    header.packetCount = htons(packetCount);

    header.payloadSize = htons(payloadSize);
    header.flags = htons(flags);

    if (!udp.beginPacket(RECEIVER_IP, RECEIVER_PORT))
    {
        return false;
    }

    const size_t headerWritten = udp.write(
        reinterpret_cast<const uint8_t*>(&header),
        sizeof(header)
    );

    if (headerWritten != sizeof(header))
    {
        udp.endPacket();
        return false;
    }

    const size_t payloadWritten = udp.write(
        payload,
        payloadSize
    );

    if (payloadWritten != payloadSize)
    {
        udp.endPacket();
        return false;
    }

    /*
     * UDP 沒有 ACK，也不會等待重傳。
     * 但 endPacket() 仍需要把資料交給網路堆疊。
     */
    const int result = udp.endPacket();

    return result == 1;
}

// =====================================================
// 傳送完整 JPEG Frame
// =====================================================

bool sendJpegFrame(
    const uint8_t* jpegData,
    size_t jpegSize
)
{
    if (
        jpegData == nullptr ||
        jpegSize < 4 ||
        jpegSize > 200000
    )
    {
        return false;
    }

    // 檢查 JPEG SOI：FF D8
    if (
        jpegData[0] != 0xFF ||
        jpegData[1] != 0xD8
    )
    {
        Serial.println("Invalid JPEG SOI.");
        return false;
    }

    const size_t packetCountSize =
        (jpegSize + UDP_PAYLOAD_SIZE - 1) /
        UDP_PAYLOAD_SIZE;

    if (
        packetCountSize == 0 ||
        packetCountSize > UINT16_MAX
    )
    {
        Serial.println("Invalid packet count.");
        return false;
    }

    const uint16_t packetCount =
        static_cast<uint16_t>(packetCountSize);

    const uint32_t frameId = nextFrameId++;

    for (
        uint16_t packetId = 0;
        packetId < packetCount;
        packetId++
    )
    {
        const size_t offset =
            static_cast<size_t>(packetId) *
            UDP_PAYLOAD_SIZE;

        const size_t remaining =
            jpegSize - offset;

        const uint16_t payloadSize =
            static_cast<uint16_t>(
                remaining > UDP_PAYLOAD_SIZE
                    ? UDP_PAYLOAD_SIZE
                    : remaining
            );

        const bool success = sendUdpPacket(
            frameId,
            packetId,
            packetCount,
            jpegData + offset,
            payloadSize
        );

        if (!success)
        {
            failedPackets++;

            /*
             * 這張已經不完整。
             * 不需要繼續浪費時間傳剩下的封包。
             */
            return false;
        }

        /*
         * 每 8 個封包讓網路系統執行一次。
         *
         * 不使用長 delay，避免增加延遲。
         * 如果封包遺失率很高，可以改成 delay(1)。
         */
        if ((packetId & 0x07) == 0x07)
        {
            yield();
        }
    }

    sentBytes += jpegSize;
    return true;
}

// =====================================================
// 拍照並傳送
// =====================================================

void captureAndSendFrame()
{
    camera_fb_t* frame = esp_camera_fb_get();

    if (frame == nullptr)
    {
        failedFrames++;
        Serial.println("camera_fb_get failed.");
        return;
    }

    capturedFrames++;

    if (
        frame->format != PIXFORMAT_JPEG ||
        frame->len == 0
    )
    {
        failedFrames++;
        esp_camera_fb_return(frame);
        return;
    }

    const bool success = sendJpegFrame(
        frame->buf,
        frame->len
    );

    esp_camera_fb_return(frame);

    if (success)
    {
        sentFrames++;
    }
    else
    {
        failedFrames++;
    }
}

// =====================================================
// 狀態輸出
// =====================================================

void printStatus()
{
    const uint32_t now = millis();

    if (now - lastStatusTime < 5000)
    {
        return;
    }

    const uint32_t elapsed =
        now - lastStatusTime;

    const uint32_t intervalFrames =
        sentFrames - lastSentFrames;

    const uint64_t intervalBytes =
        sentBytes - lastSentBytes;

    const float fps =
        intervalFrames * 1000.0f /
        static_cast<float>(elapsed);

    const float kilobytesPerSecond =
        intervalBytes * 1000.0f /
        static_cast<float>(elapsed) /
        1024.0f;

    float averageJpegKB = 0.0f;

    if (intervalFrames > 0)
    {
        averageJpegKB =
            static_cast<float>(intervalBytes) /
            intervalFrames /
            1024.0f;
    }

    Serial.print("WiFi=");
    Serial.print(
        WiFi.status() == WL_CONNECTED
            ? "OK"
            : "NO"
    );

    Serial.print(", RSSI=");
    Serial.print(WiFi.RSSI());
    Serial.print(" dBm");

    Serial.print(", FPS=");
    Serial.print(fps, 1);

    Serial.print(", Rate=");
    Serial.print(kilobytesPerSecond, 1);
    Serial.print(" KB/s");

    Serial.print(", AvgJPEG=");
    Serial.print(averageJpegKB, 1);
    Serial.print(" KB");

    Serial.print(", Sent=");
    Serial.print(sentFrames);

    Serial.print(", FailedFrames=");
    Serial.print(failedFrames);

    Serial.print(", FailedPackets=");
    Serial.print(failedPackets);

    Serial.print(", Heap=");
    Serial.print(ESP.getFreeHeap());

    Serial.print(", PSRAM=");
    Serial.println(ESP.getFreePsram());

    lastStatusTime = now;
    lastSentFrames = sentFrames;
    lastSentBytes = sentBytes;
}

// =====================================================
// Setup
// =====================================================

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(false);

    delay(1500);

    Serial.println();
    Serial.println("==============================");
    Serial.println("ESP32-S3 UDP Camera Sender");
    Serial.println("==============================");

    if (!setupCamera())
    {
        Serial.println("Camera initialization failed.");

        while (true)
        {
            delay(1000);
        }
    }

    if (!connectWiFi())
    {
        Serial.println("Wi-Fi initialization failed.");

        while (true)
        {
            delay(1000);
        }
    }

    lastStatusTime = millis();

    Serial.println("UDP camera sender started.");
}

// =====================================================
// Loop
// =====================================================

void loop()
{
    static uint32_t nextFrameTime = 0;

    const uint32_t now = millis();

    if (
        static_cast<int32_t>(
            now - nextFrameTime
        ) >= 0
    )
    {
        /*
         * 使用目前時間重新計算，
         * 避免傳送太慢時累積很多待處理影格。
         */
        nextFrameTime = now + FRAME_INTERVAL_MS;

        captureAndSendFrame();
    }

    printStatus();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Wi-Fi disconnected.");

        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        delay(1000);
    }

    yield();
}