#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// =====================================================
// Wi-Fi 設定
// =====================================================

const char* WIFI_SSID = "HITRON-9DF0";
const char* WIFI_PASSWORD = "E91IO8N07HQY";

// =====================================================
// HTTP Server 設定
// =====================================================

#define HTTP_PORT 81

// =====================================================
// Camera 設定
// =====================================================

// VGA = 640 × 480
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA

/*
 * JPEG Quality：
 *
 * 數字越小：
 * - 畫質越好
 * - JPEG 越大
 * - 移動時越容易卡
 *
 * 數字越大：
 * - 畫質稍差
 * - JPEG 越小
 * - 傳輸較穩定
 */
#define CAMERA_JPEG_QUALITY 28

/*
 * 10 MHz 比較穩定。
 *
 * 之後可以測試 20 MHz，
 * 但如果出現 FB-OVF、破圖或 camera_fb_get failed，
 * 就改回 10 MHz。
 */
#define CAMERA_XCLK_FREQ 10000000

/*
 * 每張傳送完成後讓出 CPU。
 * 1 ms 比原本的 10 ms 更適合低延遲。
 */
#define STREAM_DELAY_MS 1

// Wi-Fi 最多等待 20 秒
#define WIFI_TIMEOUT_MS 20000

// 每 5 秒輸出一次狀態
#define STATUS_INTERVAL_MS 5000

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

httpd_handle_t cameraHttpd = nullptr;

// 只允許一個串流 Client
volatile bool streamClientActive = false;

// 統計資料
uint32_t streamFrameCount = 0;
uint32_t captureCount = 0;
uint32_t captureFailCount = 0;

uint32_t lastFrameCount = 0;

uint64_t intervalJpegBytes = 0;
uint32_t intervalJpegCount = 0;
size_t intervalMaxJpegSize = 0;

unsigned long lastStatusTime = 0;

// =====================================================
// MJPEG MIME 格式
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

    /*
     * 關閉 Wi-Fi 省電。
     * 可以降低封包傳送的等待時間，
     * 但耗電量會增加。
     */
    WiFi.setSleep(false);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    unsigned long startTime = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - startTime >= WIFI_TIMEOUT_MS)
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
        Serial.println(
            "請確認 Arduino IDE 的 PSRAM 設定為 OPI PSRAM"
        );

        return false;
    }

    Serial.print("PSRAM size: ");
    Serial.println(ESP.getPsramSize());

    Serial.print("Free PSRAM: ");
    Serial.println(ESP.getFreePsram());

    camera_config_t config = {};

    // XCLK 使用的 LEDC Timer
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    // Camera 8-bit 資料線
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    // Camera Clock 和同步腳位
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;

    // SCCB 控制介面
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = CAMERA_XCLK_FREQ;
    config.pixel_format = PIXFORMAT_JPEG;

    config.frame_size = CAMERA_FRAME_SIZE;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;

    /*
     * 使用兩個 Frame Buffer。
     *
     * 當上一張正在透過 Wi-Fi 傳送時，
     * 相機可以同時準備下一張。
     */
    config.fb_count = 2;

    /*
     * Frame Buffer 放到 PSRAM。
     */
    config.fb_location = CAMERA_FB_IN_PSRAM;

    /*
     * 優先取得最新影格。
     *
     * 如果程式處理速度跟不上，
     * Camera Driver 可以丟掉較舊的影格，
     * 避免相機端延遲一直累積。
     */
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
        Serial.println("無法取得 Camera Sensor");
        return false;
    }

    Serial.printf(
        "Camera Sensor PID: 0x%04X\n",
        sensor->id.PID
    );

    if (sensor->id.PID == OV3660_PID)
    {
        Serial.println("偵測到 OV3660");

        // 依照鏡頭安裝方向調整
        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 1);

        // 畫面參數
        sensor->set_brightness(sensor, 1);
        sensor->set_contrast(sensor, 1);
        sensor->set_saturation(sensor, -1);

        // 白平衡
        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);

        // 自動曝光與增益
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);

        // Sensor 端的畫質處理
        sensor->set_bpc(sensor, 1);
        sensor->set_wpc(sensor, 1);
        sensor->set_raw_gma(sensor, 1);
        sensor->set_lenc(sensor, 1);
    }

    sensor->set_framesize(
        sensor,
        CAMERA_FRAME_SIZE
    );

    /*
     * 丟棄前五張影格。
     *
     * 讓自動曝光、白平衡和增益先穩定，
     * 避免剛啟動時畫面忽明忽暗。
     */
    for (int i = 0; i < 5; i++)
    {
        camera_fb_t* frame = esp_camera_fb_get();

        if (frame != nullptr)
        {
            esp_camera_fb_return(frame);
        }

        delay(100);
    }

    Serial.println("相機設定完成：VGA 640 × 480");

    Serial.print("JPEG quality: ");
    Serial.println(CAMERA_JPEG_QUALITY);

    Serial.print("XCLK: ");
    Serial.print(CAMERA_XCLK_FREQ / 1000000);
    Serial.println(" MHz");

    return true;
}

// =====================================================
// /capture 單張 JPEG
// =====================================================

static esp_err_t captureHandler(httpd_req_t* request)
{
    /*
     * 串流運作時，不允許 /capture 取得相機，
     * 避免與 /stream 搶 Frame Buffer。
     */
    if (streamClientActive)
    {
        httpd_resp_set_status(
            request,
            "503 Service Unavailable"
        );

        return httpd_resp_sendstr(
            request,
            "Capture is unavailable while streaming."
        );
    }

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

    if (
        frameBuffer->format != PIXFORMAT_JPEG ||
        frameBuffer->len == 0
    )
    {
        Serial.println(
            "/capture：影格不是有效 JPEG"
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
    /*
     * 避免瀏覽器和 Python 同時連線，
     * 或同一個使用者開啟兩個串流。
     */
    if (streamClientActive)
    {
        Serial.println(
            "拒絕第二個 Stream Client"
        );

        httpd_resp_set_status(
            request,
            "503 Service Unavailable"
        );

        return httpd_resp_sendstr(
            request,
            "Only one stream client is allowed."
        );
    }

    streamClientActive = true;

    Serial.println();
    Serial.println(
        "Client connected to /stream"
    );

    esp_err_t result = httpd_resp_set_type(
        request,
        STREAM_CONTENT_TYPE
    );

    if (result != ESP_OK)
    {
        streamClientActive = false;
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

    /*
     * MJPEG 是長時間連線。
     */
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

            delay(10);
            continue;
        }

        if (
            frameBuffer->format != PIXFORMAT_JPEG ||
            frameBuffer->len == 0
        )
        {
            captureFailCount++;

            esp_camera_fb_return(frameBuffer);

            delay(1);
            continue;
        }

        /*
         * 必須在歸還 Frame Buffer 前保存長度。
         */
        const size_t jpegLength =
            frameBuffer->len;

        // 1. 傳送 MJPEG Boundary
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

        // 2. 建立該張 JPEG 的 HTTP Header
        int headerLength = snprintf(
            headerBuffer,
            sizeof(headerBuffer),
            STREAM_PART,
            static_cast<unsigned int>(jpegLength)
        );

        if (
            headerLength <= 0 ||
            headerLength >= static_cast<int>(
                sizeof(headerBuffer)
            )
        )
        {
            esp_camera_fb_return(frameBuffer);

            result = ESP_FAIL;
            break;
        }

        // 3. 傳送 JPEG Header
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

        // 4. 傳送 JPEG 內容
        result = httpd_resp_send_chunk(
            request,
            reinterpret_cast<const char*>(
                frameBuffer->buf
            ),
            jpegLength
        );

        /*
         * 傳送後立即歸還 Frame Buffer。
         */
        esp_camera_fb_return(frameBuffer);

        if (result != ESP_OK)
        {
            break;
        }

        streamFrameCount++;

        intervalJpegBytes += jpegLength;
        intervalJpegCount++;

        if (jpegLength > intervalMaxJpegSize)
        {
            intervalMaxJpegSize = jpegLength;
        }

        /*
         * 讓 Wi-Fi、Camera、TCP/IP 與系統 Task 執行。
         */
        delay(STREAM_DELAY_MS);
    }

    /*
     * 如果 Client 正常斷線，結束 Chunked Response。
     * 如果 Socket 已失效，這行可能失敗，可以忽略。
     */
    httpd_resp_send_chunk(
        request,
        nullptr,
        0
    );

    streamClientActive = false;

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
        "<p>JPEG quality: 28</p>"

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

    /*
     * 因為只允許一個 Stream Client，
     * 不需要很多 Socket。
     */
    serverConfig.max_open_sockets = 4;
    serverConfig.lru_purge_enable = true;

    /*
     * 網路或 Client 卡住時，
     * 不要讓 HTTP Handler 永遠等待。
     */
    serverConfig.recv_wait_timeout = 5;
    serverConfig.send_wait_timeout = 3;

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
        &cameraHttpd,
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

    result = httpd_register_uri_handler(
        cameraHttpd,
        &indexUri
    );

    if (result != ESP_OK)
    {
        Serial.println("註冊首頁失敗");

        httpd_stop(cameraHttpd);
        cameraHttpd = nullptr;

        return false;
    }

    result = httpd_register_uri_handler(
        cameraHttpd,
        &captureUri
    );

    if (result != ESP_OK)
    {
        Serial.println("註冊 /capture 失敗");

        httpd_stop(cameraHttpd);
        cameraHttpd = nullptr;

        return false;
    }

    result = httpd_register_uri_handler(
        cameraHttpd,
        &streamUri
    );

    if (result != ESP_OK)
    {
        Serial.println("註冊 /stream 失敗");

        httpd_stop(cameraHttpd);
        cameraHttpd = nullptr;

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
    const unsigned long now = millis();

    if (
        now - lastStatusTime <
        STATUS_INTERVAL_MS
    )
    {
        return;
    }

    const unsigned long elapsed =
        now - lastStatusTime;

    const uint32_t newFrames =
        streamFrameCount - lastFrameCount;

    const float fps =
        newFrames * 1000.0f /
        static_cast<float>(elapsed);

    float averageJpegKB = 0.0f;

    if (intervalJpegCount > 0)
    {
        averageJpegKB =
            static_cast<float>(
                intervalJpegBytes
            ) /
            static_cast<float>(
                intervalJpegCount
            ) /
            1024.0f;
    }

    const float maxJpegKB =
        static_cast<float>(
            intervalMaxJpegSize
        ) /
        1024.0f;

    lastStatusTime = now;
    lastFrameCount = streamFrameCount;

    Serial.print("狀態：WiFi=");
    Serial.print(
        WiFi.status() == WL_CONNECTED
            ? "OK"
            : "NO"
    );

    Serial.print(", IP=");
    Serial.print(WiFi.localIP());

    Serial.print(", RSSI=");
    Serial.print(WiFi.RSSI());
    Serial.print(" dBm");

    Serial.print(", FPS=");
    Serial.print(fps, 1);

    Serial.print(", avg_jpeg=");
    Serial.print(averageJpegKB, 1);
    Serial.print(" KB");

    Serial.print(", max_jpeg=");
    Serial.print(maxJpegKB, 1);
    Serial.print(" KB");

    Serial.print(", stream_frames=");
    Serial.print(streamFrameCount);

    Serial.print(", capture_fail=");
    Serial.print(captureFailCount);

    Serial.print(", free_heap=");
    Serial.print(ESP.getFreeHeap());

    Serial.print(", free_psram=");
    Serial.println(ESP.getFreePsram());

    /*
     * 清除這個統計區間的資料。
     */
    intervalJpegBytes = 0;
    intervalJpegCount = 0;
    intervalMaxJpegSize = 0;
}

// =====================================================
// Wi-Fi 重新連線
// =====================================================

void reconnectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    Serial.println(
        "Wi-Fi 已斷線，嘗試重新連線"
    );

    WiFi.disconnect();

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    const unsigned long startTime = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startTime < 10000
    )
    {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print(
            "Wi-Fi 重新連線成功，IP="
        );

        Serial.println(
            WiFi.localIP()
        );
    }
    else
    {
        Serial.println(
            "Wi-Fi 重新連線失敗"
        );
    }
}

// =====================================================
// Setup
// =====================================================

void setup()
{
    Serial.begin(115200);

    /*
     * 關閉 Camera/Wi-Fi 底層 Debug Log。
     */
    Serial.setDebugOutput(false);

    delay(2000);

    Serial.println();
    Serial.println(
        "==================================="
    );
    Serial.println(
        "ESP32-S3 CAM OV3660"
    );
    Serial.println(
        "Improved MJPEG Server"
    );
    Serial.println(
        "==================================="
    );

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

    lastStatusTime = millis();

    Serial.println();
    Serial.println("Setup 完成");
}

// =====================================================
// Loop
// =====================================================

void loop()
{
    printStatus();
    reconnectWiFi();

    delay(50);
}