#include <Arduino.h>
#include "esp_camera.h"

// =====================================================
// UART 設定
// =====================================================

// 先用 115200 確認穩定，再改成 460800 或 921600
#define UART_BAUD_RATE       460800

// 約每 500 ms 傳一張，也就是約 2 FPS
// 115200 傳 JPEG 很慢，不建議設定得太短
#define FRAME_INTERVAL_MS    50

#define FRAME_MAGIC_TEXT     "CAM1"
#define FRAME_MAGIC_SIZE     4

// 避免異常超大影像
#define MAX_JPEG_SIZE        (1024U * 1024U)

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
// 全域變數
// =====================================================

static uint32_t g_frameId = 0;
static uint32_t g_lastFrameTime = 0;

// =====================================================
// UART 工具
// =====================================================

static void writeUint32BE(uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(value & 0xFF);

    Serial0.write(bytes, sizeof(bytes));
}

static bool writeAll(const uint8_t* data, size_t length)
{
    if (data == nullptr && length > 0) {
        return false;
    }

    size_t totalWritten = 0;

    while (totalWritten < length) {
        size_t remaining = length - totalWritten;

        // 分段傳輸，避免一次塞太多資料
        size_t blockSize = remaining > 1024 ? 1024 : remaining;

        size_t written = Serial0.write(
            data + totalWritten,
            blockSize
        );

        if (written == 0) {
            delay(1);
            continue;
        }

        totalWritten += written;
    }

    return true;
}

// =====================================================
// Camera 初始化
// =====================================================

static bool initCamera()
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

    // UART 測試先使用 QVGA
    config.frame_size = FRAMESIZE_QVGA;

    // 數字越大，JPEG 品質越低、檔案越小
    config.jpeg_quality = 25;

    if (psramFound()) {
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    esp_err_t result = esp_camera_init(&config);

    return result == ESP_OK;
}

// =====================================================
// 影像驗證與傳輸
// =====================================================

static bool isValidJpeg(const camera_fb_t* frame)
{
    if (frame == nullptr) {
        return false;
    }

    if (frame->buf == nullptr || frame->len < 4) {
        return false;
    }

    if (frame->len > MAX_JPEG_SIZE) {
        return false;
    }

    bool hasStartMarker =
        frame->buf[0] == 0xFF &&
        frame->buf[1] == 0xD8;

    bool hasEndMarker =
        frame->buf[frame->len - 2] == 0xFF &&
        frame->buf[frame->len - 1] == 0xD9;

    return hasStartMarker && hasEndMarker;
}

static bool sendFrame(const camera_fb_t* frame)
{
    if (!isValidJpeg(frame)) {
        return false;
    }

    // 封包格式：
    //
    // 0~3   : "CAM1"
    // 4~7   : frameId，Big Endian
    // 8~11  : JPEG length，Big Endian
    // 12~N  : JPEG data

    size_t written = Serial0.write(
        reinterpret_cast<const uint8_t*>(FRAME_MAGIC_TEXT),
        FRAME_MAGIC_SIZE
    );

    if (written != FRAME_MAGIC_SIZE) {
        return false;
    }

    writeUint32BE(g_frameId);
    writeUint32BE(static_cast<uint32_t>(frame->len));

    if (!writeAll(frame->buf, frame->len)) {
        return false;
    }

    // 等待 UART TX buffer 傳送完成
    Serial0.flush();

    g_frameId++;

    return true;
}

// =====================================================
// Arduino setup / loop
// =====================================================

void setup()
{
    // USB CDC On Boot = Disabled
    // 使用 CH340 對應的硬體 UART0
    Serial0.begin(UART_BAUD_RATE);

    delay(2000);

    if (!initCamera()) {
        // 初始化失敗時，每秒送出 ERR1，方便 Python 或終端機判斷
        while (true) {
            Serial0.write("ERR1", 4);
            Serial0.flush();
            delay(1000);
        }
    }

    // 丟棄前幾張可能不穩定的影像
    for (int i = 0; i < 3; i++) {
        camera_fb_t* frame = esp_camera_fb_get();

        if (frame != nullptr) {
            esp_camera_fb_return(frame);
        }

        delay(100);
    }

    // 等待 Python 開啟 serial port
    delay(1500);
}

void loop()
{
    uint32_t now = millis();

    if (now - g_lastFrameTime < FRAME_INTERVAL_MS) {
        delay(1);
        return;
    }

    g_lastFrameTime = now;

    camera_fb_t* frame = esp_camera_fb_get();

    if (frame == nullptr) {
        delay(10);
        return;
    }

    sendFrame(frame);

    esp_camera_fb_return(frame);
}