# 首次啟動指南

更新：2026-09-30。適用主線 WiFi 雙向認證版本，以一台 Windows 筆電與一台 ESP32-S3／OV2640 為例。

GitHub 提供程式與設定範例，不提供開發者的私鑰、WiFi 密碼或已登錄裝置清單。新使用者需建立自己的身分與信任關係。這些準備通常只做一次，之後開機可直接使用。

## 1. 建立 Python 環境

先安裝 Python 3.12。在 clone 下來、包含 esp32-only 的專案根目錄執行 PowerShell：

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
cd esp32-only
python -m pip install -r requirements.txt
```

以下指令皆從 esp32-only 目錄執行。若 PowerShell 不允許執行啟用腳本，可不啟用環境，將以下指令中的 `python` 改為 `..\.venv\Scripts\python.exe`，安裝依賴也使用這個執行檔。

## 2. 建立自己的筆電身分

```powershell
python -m host.host_identity
```

第一次執行產生：

| 檔案 | 用途 |
|---|---|
| .host-identity/identity.json | 筆電 ML-DSA 公私鑰，私密保存，不提交 Git |
| .host-identity/identity.pub | 筆電公鑰，可提供給負責設定 ESP32 信任清單的人 |
| firmware/esp32_pqc_demo/host_trust.h | ESP32 的可信任筆電公鑰清單，編譯進韌體 |

預設清單只有這台筆電；目前最多四把 Host 公鑰是工具設定的上限，不必準備四台。再次執行會沿用既有身分，不會自動換鑰。不要刪除 identity.json、使用別人的私鑰，或把私鑰燒到 ESP32。

`host_trust.h` 目前不是保存到 NVS 的動態清單；變更它需要重新燒錄。ESP32 自己的裝置身分則保存在該板子的 NVS，兩者不同。

## 3. 設定 WiFi 與燒錄

```powershell
Copy-Item firmware/esp32_pqc_demo/wifi_config.example.h firmware/esp32_pqc_demo/wifi_config.h
```

編輯 wifi_config.h，設定自己的 2.4 GHz WiFi／手機熱點名稱與密碼，保留 `PQC_USE_WIFI true`。本機設定檔已被 Git 忽略。已有設定時不用重新複製覆蓋。

Arduino IDE 開啟 [esp32_pqc_demo.ino](firmware/esp32_pqc_demo/esp32_pqc_demo.ino)，完成以下準備再燒錄：

- 安裝支援 ESP32-S3 的 Espressif Arduino core。
- 安裝提供 `MLDSA44.h` 的外部 ML-DSA-44 函式庫；ML-KEM 原始碼已在專案中，AES 使用 core 的 mbedTLS。
- 核對相機接腳定義與實際板型一致。
- Flash 大小／模式、PSRAM 與 USB／UART 輸出設定需符合實際硬體。不要把其他板子的 QIO／DIO／OPI 組合直接照搬。
- 確認第 2 步的 host_trust.h 存在；它會一起編入韌體。缺少清單時，所有 Host 的網路認證會被拒絕。

**目前建置環境仍有缺口：Arduino core 與外部 ML-DSA 函式庫的確切版本、來源及安裝流程尚未完整鎖定。這份文件不是已在全新電腦完成驗證的一鍵安裝保證。** 遇到缺少 MLDSA44.h 或 API 不相容，需先確認相依套件，不能跳過身分驗證來啟動。

ESP32 首次初始化會建立自己的持久身分。不要複製另一台 ESP32 的私鑰；之後一般更新韌體也不要任意清除 NVS，否則可能需要重新核對指紋與登錄。

## 4. 取得裝置指紋與 IP，登錄 ESP32

Serial Monitor 設為 **921600**，查看開機輸出：

```text
DEVICE_ID_SHA256 裝置的64位十六進位指紋
[WIFI] online ip=裝置IP port=9000 ...
```

應用程式 baud 與 Arduino 上傳速度不同；開機 ROM 訊息可能因 baud 不同而呈現亂碼。等相機與密碼初始化完成再操作。

筆電與 ESP32 需在允許互通的同一區網。將下面內容換成實際值：

```powershell
$cameraIp = '填入ESP32的IP'
$fingerprint = '填入從這台板子核對的64位指紋'
python -m host.enroll_device --host $cameraIp --output host/camera1.pub --fingerprint $fingerprint
```

看到 `[PASS] Trusted device enrolled` 才繼續。工具從裝置取回公鑰，核對指紋後保存；若輸出檔案已存在，會拒絕覆蓋。不要為了消除錯誤而直接信任未知公鑰。

也可用 `python -m host.discover_devices` 查詢公告 IP，但公告不可信，首次登錄指紋仍應從實際控制的板子核對。WiFi 韌體請使用 `--host` 登錄，不要以 UART 命令取代。

此時雙向信任關係是：

```text
ESP32 信任筆電：筆電公鑰 → host_trust.h → 燒錄進韌體
筆電信任 ESP32：核對板子指紋 → enroll_device → host/camera1.pub
```

## 5. 建立裝置清單

在 esp32-only 下建立 devices.json。只有一台裝置就只放一筆：

```json
{
  "devices": [
    {
      "name": "camera1",
      "host": "auto",
      "port": 9000,
      "trust_key": "host/camera1.pub",
      "enabled": true
    }
  ]
}
```

公鑰路徑相對於 devices.json；檔名改了，trust_key 也必須一起改。不要留下沒有公鑰的 camera2 範例：**目前 live_camera 會檢查所有列出的裝置，包含 enabled=false；任一缺少公鑰或設定錯誤會讓整個入口在啟動前報錯。**

有公鑰、設定正確，但 ESP32 沒開機，則只會讓那台等待搜尋，不阻止其他裝置串流。僅把 .pub 放進資料夾不會自動加入清單。JSON 路徑建議用 `/`，避免 Windows 反斜線造成跳脫字元錯誤。

## 6. 啟動與停止

```powershell
python -u -m host.live_camera
```

也可雙擊專案入口的 start_live.cmd，它使用專案根目錄的 .venv。Host 自動搜尋、完成雙向認證並顯示影像；ESP32 後續只需供電，不必維持 USB 資料線連接。網路需允許 TCP 9000 與 mDNS 探索，訪客隔離或封鎖 multicast 可能阻止使用。

- 預設 QVGA、每 10 幀換鑰、pipeline、無執行時間限制。
- 任一影像視窗按 Q／Esc 或關閉視窗，停止全部；terminal Ctrl+C 也可停止。
- 要調整設定，將 live_settings.example.json 複製為 live_settings.json 後編輯，再重啟 Host。
- 記錄存在 diagnostics/live/各台名稱/，依容量輪替；summary.json 是本次執行的摘要，不是所有歷史累計。

詳細行為見 [長期使用入口與重新探索](docs/LONG_RUNNING_HOST.md)。

若要先做 60 秒測試並保留完整診斷：

```powershell
python -u -m host.multi_camera --device camera1=auto --seconds 60 --resolution qvga --rekey-every 10 --rekey-mode pipeline --display
```

結果在 diagnostics/multi/時間戳/。測試與 live 不要同時連同一台 ESP32，目前板端一次處理一個 TCP 用戶端。

## 之後新增裝置

替新 ESP32 燒錄含目前筆電公鑰的韌體 → 核對新板子的指紋 → 登錄為 camera2.pub → 在 devices.json 加入對應項目 → 重開 Host。原有裝置不需重新登錄；單純增加 ESP32 也不需重新產生筆電身分。

更多目前功能、限制與實測結果見 [README](README.md) 與 [CURRENT_STATUS](docs/CURRENT_STATUS.md)。
