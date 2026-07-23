#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// =====================================================
// Wi-Fi 設定
// =====================================================

const char* WIFI_SSID = "HITRON-9DF0";
const char* WIFI_PASSWORD = "E91IO8N07HQY";

// HTTP Server Port
#define HTTP_PORT 81

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
// Camera 設定
// =====================================================

// VGA = 640 × 480
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA

// 數字越小，畫質越好，但資料越大
// 建議 VGA 使用 20～30
#define CAMERA_JPEG_QUALITY 24

// 相機時脈
#define CAMERA_XCLK_FREQ 10000000

// 每傳完一張影格後的延遲
#define STREAM_DELAY_MS 10

// =====================================================
// 全域變數
// =====================================================

httpd_handle_t camera_httpd = nullptr;

uint32_t streamFrameCount = 0;
uint32_t captureCount = 0;
uint32_t captureFailCount = 0;

unsigned long lastStatusTime = 0;

// =====================================================
// MJPEG 格式
// =====================================================

static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=frame";

static const char* STREAM_BOUNDARY =
    "\r\n--frame\r\n";

static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %u\r\n"
    "\r\n";

// =====================================================
// Wi-Fi 連線
// =====================================================

bool connectWiFi()
{
    Serial.println();
    Serial.println("開始連接 Wi-Fi");
    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);

    // 關閉 Wi-Fi 省電，降低串流延遲
    WiFi.setSleep(false);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startTime = millis();
    const unsigned long timeoutMs = 20000;

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - startTime >= timeoutMs)
        {
            Serial.println();
            Serial.println("Wi-Fi 連線逾時");
            return false;
        }
    }

    Serial.println();
    Serial.println("Wi-Fi 連線成功");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    return true;
}

// =====================================================
// Camera 初始化
// =====================================================

bool setupCamera()
{
    Serial.println();
    Serial.println("開始初始化相機");

    if (!psramFound())
    {
        Serial.println("錯誤：沒有偵測到 PSRAM");
        Serial.println("請確認 Arduino IDE 的 PSRAM 設為 OPI PSRAM");
        return false;
    }

    Serial.print("PSRAM size: ");
    Serial.println(ESP.getPsramSize());

    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    // 8-bit 相機資料線
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    // Clock 與同步腳位
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;

    // SCCB 控制腳位
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = CAMERA_XCLK_FREQ;
    config.pixel_format = PIXFORMAT_JPEG;

    config.frame_size = CAMERA_FRAME_SIZE;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;

    // 使用兩個 frame buffer，並優先取得最新影格
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t error = esp_camera_init(&config);

    if (error != ESP_OK)
    {
        Serial.printf(
            "相機初始化失敗，錯誤碼：0x%x\n",
            error
        );

        Serial.println("可能原因：");
        Serial.println("1. Camera pin 設定錯誤");
        Serial.println("2. PSRAM 未開啟");
        Serial.println("3. 相機排線接觸不良");
        Serial.println("4. 供電不足");

        return false;
    }

    Serial.println("相機初始化成功");

    sensor_t* sensor = esp_camera_sensor_get();

    if (sensor == nullptr)
    {
        Serial.println("無法取得 Camera sensor");
        return false;
    }

    Serial.printf(
        "Camera sensor PID: 0x%04X\n",
        sensor->id.PID
    );

    if (sensor->id.PID == OV3660_PID)
    {
        Serial.println("偵測到 OV3660");

        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 1);

        sensor->set_brightness(sensor, 1);
        sensor->set_contrast(sensor, 1);
        sensor->set_saturation(sensor, -1);

        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);

        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
    }

    // 確認解析度
    sensor->set_framesize(
        sensor,
        CAMERA_FRAME_SIZE
    );

    Serial.println("相機設定完成：VGA 640 × 480");

    return true;
}

// =====================================================
// /capture 單張 JPEG
// =====================================================

static esp_err_t captureHandler(httpd_req_t* request)
{
    captureCount++;

    camera_fb_t* frameBuffer = esp_camera_fb_get();

    if (frameBuffer == nullptr)
    {
        captureFailCount++;

        Serial.println(
            "/capture：camera_fb_get failed"
        );

        httpd_resp_send_500(request);
        return ESP_FAIL;
    }

    if (frameBuffer->format != PIXFORMAT_JPEG)
    {
        Serial.println(
            "/capture：影格不是 JPEG"
        );

        esp_camera_fb_return(frameBuffer);
        httpd_resp_send_500(request);

        return ESP_FAIL;
    }

    httpd_resp_set_type(
        request,
        "image/jpeg"
    );

    httpd_resp_set_hdr(
        request,
        "Access-Control-Allow-Origin",
        "*"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    httpd_resp_set_hdr(
        request,
        "Pragma",
        "no-cache"
    );

    esp_err_t result = httpd_resp_send(
        request,
        reinterpret_cast<const char*>(frameBuffer->buf),
        frameBuffer->len
    );

    esp_camera_fb_return(frameBuffer);

    return result;
}

// =====================================================
// /stream MJPEG 串流
// =====================================================

static esp_err_t streamHandler(httpd_req_t* request)
{
    Serial.println(
        "Client connected to /stream"
    );

    esp_err_t result = httpd_resp_set_type(
        request,
        STREAM_CONTENT_TYPE
    );

    if (result != ESP_OK)
    {
        Serial.println(
            "設定 MJPEG Content-Type 失敗"
        );

        return result;
    }

    httpd_resp_set_hdr(
        request,
        "Access-Control-Allow-Origin",
        "*"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    httpd_resp_set_hdr(
        request,
        "Pragma",
        "no-cache"
    );

    httpd_resp_set_hdr(
        request,
        "Connection",
        "keep-alive"
    );

    char headerBuffer[128];

    while (true)
    {
        camera_fb_t* frameBuffer =
            esp_camera_fb_get();

        if (frameBuffer == nullptr)
        {
            captureFailCount++;

            Serial.println(
                "/stream：camera_fb_get failed"
            );

            delay(20);
            continue;
        }

        if (frameBuffer->format != PIXFORMAT_JPEG)
        {
            Serial.println(
                "/stream：影格不是 JPEG"
            );

            esp_camera_fb_return(frameBuffer);

            delay(10);
            continue;
        }

        // 傳送 boundary
        result = httpd_resp_send_chunk(
            request,
            STREAM_BOUNDARY,
            strlen(STREAM_BOUNDARY)
        );

        if (result != ESP_OK)
        {
            esp_camera_fb_return(frameBuffer);
            break;
        }

        // 建立 JPEG part header
        size_t headerLength = snprintf(
            headerBuffer,
            sizeof(headerBuffer),
            STREAM_PART,
            static_cast<unsigned int>(frameBuffer->len)
        );

        if (
            headerLength == 0 ||
            headerLength >= sizeof(headerBuffer)
        )
        {
            Serial.println(
                "建立 MJPEG header 失敗"
            );

            esp_camera_fb_return(frameBuffer);
            break;
        }

        // 傳送 JPEG header
        result = httpd_resp_send_chunk(
            request,
            headerBuffer,
            headerLength
        );

        if (result != ESP_OK)
        {
            esp_camera_fb_return(frameBuffer);
            break;
        }

        // 傳送 JPEG 資料
        result = httpd_resp_send_chunk(
            request,
            reinterpret_cast<const char*>(frameBuffer->buf),
            frameBuffer->len
        );

        esp_camera_fb_return(frameBuffer);

        if (result != ESP_OK)
        {
            break;
        }

        streamFrameCount++;

        if (STREAM_DELAY_MS > 0)
        {
            delay(STREAM_DELAY_MS);
        }
        else
        {
            delay(1);
        }
    }

    Serial.println(
        "Client disconnected from /stream"
    );

    return result;
}

// =====================================================
// / 首頁
// =====================================================

static esp_err_t indexHandler(httpd_req_t* request)
{
    static const char html[] =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' "
        "content='width=device-width, initial-scale=1.0'>"

        "<title>ESP32-S3 Camera</title>"

        "<style>"
        "body {"
        "  background: #111;"
        "  color: #eee;"
        "  font-family: Arial, sans-serif;"
        "  text-align: center;"
        "  margin: 0;"
        "  padding: 20px;"
        "}"

        "img {"
        "  width: 100%;"
        "  max-width: 640px;"
        "  height: auto;"
        "  border: 1px solid #555;"
        "}"

        "a {"
        "  color: #64b5f6;"
        "}"
        "</style>"
        "</head>"

        "<body>"
        "<h2>ESP32-S3 CAM OV3660</h2>"
        "<p>Resolution: VGA 640 × 480</p>"
        "<p>JPEG quality: 24</p>"

        "<p>"
        "<a href='/capture' target='_blank'>"
        "Capture Image"
        "</a>"
        "</p>"

        "<img src='/stream' "
        "alt='ESP32 Camera Stream'>"

        "</body>"
        "</html>";

    httpd_resp_set_type(
        request,
        "text/html; charset=UTF-8"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    return httpd_resp_send(
        request,
        html,
        HTTPD_RESP_USE_STRLEN
    );
}

// =====================================================
// 啟動 HTTP Server
// =====================================================

bool startCameraServer()
{
    httpd_config_t serverConfig =
        HTTPD_DEFAULT_CONFIG();

    serverConfig.server_port = HTTP_PORT;
    serverConfig.ctrl_port = HTTP_PORT + 1;

    serverConfig.stack_size = 8192;
    serverConfig.max_open_sockets = 4;
    serverConfig.lru_purge_enable = true;

    httpd_uri_t indexUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t captureUri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = captureHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t streamUri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = streamHandler,
        .user_ctx = nullptr
    };

    esp_err_t result = httpd_start(
        &camera_httpd,
        &serverConfig
    );

    if (result != ESP_OK)
    {
        Serial.printf(
            "HTTP Server 啟動失敗：0x%x\n",
            result
        );

        return false;
    }

    if (
        httpd_register_uri_handler(
            camera_httpd,
            &indexUri
        ) != ESP_OK
    )
    {
        Serial.println("註冊首頁失敗");
        return false;
    }

    if (
        httpd_register_uri_handler(
            camera_httpd,
            &captureUri
        ) != ESP_OK
    )
    {
        Serial.println("註冊 /capture 失敗");
        return false;
    }

    if (
        httpd_register_uri_handler(
            camera_httpd,
            &streamUri
        ) != ESP_OK
    )
    {
        Serial.println("註冊 /stream 失敗");
        return false;
    }

    IPAddress ip = WiFi.localIP();

    Serial.println();
    Serial.println("HTTP Server 啟動成功");

    Serial.print("首頁：http://");
    Serial.print(ip);
    Serial.print(":");
    Serial.println(HTTP_PORT);

    Serial.print("單張照片：http://");
    Serial.print(ip);
    Serial.print(":");
    Serial.print(HTTP_PORT);
    Serial.println("/capture");

    Serial.print("MJPEG 串流：http://");
    Serial.print(ip);
    Serial.print(":");
    Serial.print(HTTP_PORT);
    Serial.println("/stream");

    return true;
}

// =====================================================
// 狀態輸出
// =====================================================

void printStatus()
{
    unsigned long now = millis();

    if (now - lastStatusTime < 10000)
    {
        return;
    }

    lastStatusTime = now;

    Serial.print("狀態：WiFi=");

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("OK");
    }
    else
    {
        Serial.print("NO");
    }

    Serial.print(", IP=");
    Serial.print(WiFi.localIP());

    Serial.print(", RSSI=");
    Serial.print(WiFi.RSSI());
    Serial.print(" dBm");

    Serial.print(", capture=");
    Serial.print(captureCount);

    Serial.print(", stream_frames=");
    Serial.print(streamFrameCount);

    Serial.print(", capture_fail=");
    Serial.print(captureFailCount);

    Serial.print(", free_heap=");
    Serial.print(ESP.getFreeHeap());

    Serial.print(", free_psram=");
    Serial.println(ESP.getFreePsram());
}

// =====================================================
// Setup
// =====================================================

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    delay(2000);

    Serial.println();
    Serial.println("===================================");
    Serial.println("ESP32-S3 CAM OV3660");
    Serial.println("Standalone MJPEG Server");
    Serial.println("===================================");

    if (!setupCamera())
    {
        Serial.println(
            "程式停止：Camera 初始化失敗"
        );

        while (true)
        {
            delay(1000);
        }
    }

    if (!connectWiFi())
    {
        Serial.println(
            "程式停止：Wi-Fi 連線失敗"
        );

        while (true)
        {
            delay(1000);
        }
    }

    if (!startCameraServer())
    {
        Serial.println(
            "程式停止：HTTP Server 啟動失敗"
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println("Setup 完成");
}

// =====================================================
// Loop
// =====================================================

void loop()
{
    printStatus();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(
            "Wi-Fi 已斷線，嘗試重新連線"
        );

        WiFi.disconnect();
        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );

        delay(3000);
    }

    delay(100);
}