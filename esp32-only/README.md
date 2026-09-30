# ESP32-S3 加密相機

**第一次從 GitHub 下載？從 [首次啟動指南](FIRST_START.md) 開始：建立自己的筆電身分、設定 WiFi、燒錄、登錄 ESP32，再啟動影像。**

> 2026-09-30 後續 Host 更新：新增 `python -u -m host.live_camera` 無參數長期入口（或 start_live.cmd）、按可信身分重新探索、影像年齡與有界日誌。已保存 baseline 不變；本次新增恢復流程待實機驗收。操作與邊界見 [長期使用](docs/LONG_RUNNING_HOST.md)。

更新：2026-09-30。主線已完成內網探索、雙向身分驗證、多裝置 WiFi 加密影像與背景換鑰。兩台 QVGA / rekey=10 / pipeline 的 10 分鐘測試正常完成，平均 36.31 / 40.50 FPS，無重連或重啟；仍有偶發約 2.5 秒停頓。

完整實作狀態、數據與限制以 [CURRENT_STATUS](docs/CURRENT_STATUS.md) 為準。舊 README 已移至 [歷史版本](archive/history/README_before_20260930_audit.md)，不要使用其中「尚未雙向認證／雙台驗收」等舊狀態判斷現在版本。

## 啟動主線

在 esp32-only 目錄、啟用專案 .venv 後執行：

```powershell
python -m pip install -r requirements.txt
python -u -m host.multi_camera --discover --seconds 600 --resolution qvga --rekey-every 10 --rekey-mode pipeline --display
```

不用先單獨執行 discover_devices。--discover 按 devices.json 的公鑰配對探索全部已設定裝置，包含 disabled 項目；也可用 --device camera1=auto 或 --device camera1=IP 選擇裝置。公告只提供地址，不能代替認證。每台獨立連線與 session，結果在 diagnostics/multi/時間戳/summary.json。

首次環境須由 devices.example.json 建立本機 devices.json，核對實際板子的指紋，再用 host.enroll_device 保存各台 .pub；公鑰路徑相對於設定檔。完整說明見 [多裝置操作](MULTI_CAMERA.md) 與 [v6 建置與認證](docs/MUTUAL_AUTH_V6.md)。.pub 是公鑰，不是 .hub 或文字指紋。

韌體燒錄 firmware/esp32_pqc_demo/esp32_pqc_demo.ino。wifi_config.h 存本機 WiFi 設定；host.host_identity 產生或沿用 .host-identity/identity.json，輸出只含公鑰的 host_trust.h。私鑰不可燒到板子或提交 Git。按實際模組設定 Flash / PSRAM，保留 NVS 裝置身分。應用 Serial 是 921600，與上傳速度不同；WiFi 使用不需 COM 資料線，只需供電。

record 模式目前驗證／顯示影像，不存 AVI。multi_camera 預設 60 秒、pipeline；直接 pqc_camera_demo 預設 10 秒、blocking。測試時明確指定參數。主線尚未加入無限使用入口、IP 變更後重新探索及日誌容量限制。

## 協定如何合作

初始／重連：探索地址 → 本機固定裝置公鑰 → MUTUAL_BEGIN / MUTUAL_FINISH → 雙向 DSA 與 session key proof → AES-GCM 影像。每幀驗證 metadata、tag 與序號；不是每幀簽 DSA。pipeline 在目前 session 下提前準備下一把 key，於門檻切換；blocking 則重新握手。

TCP 是位元組串流，64 KiB 是 Host 一次讀取上限，不是等待集滿的封包大小。半包失敗不直接跳過；TCP 恢復需丟棄舊連線、重新認證。部分文字控制命令仍未全面 MAC／AEAD 保護；不是 TLS 或完整安全認證產品。UART 不包含 WiFi v6 的 Host 存取限制。

## 閱讀與資料夾

1. [主 sketch](firmware/esp32_pqc_demo/esp32_pqc_demo.ino)：初始化、PQC task。
2. [多台入口](host/multi_camera.py) → [相機 Host](host/pqc_camera_demo.py)：啟動、取像、驗證、收尾。
3. [雙向認證](host/mutual_auth.py) 與 [板端命令](firmware/esp32_pqc_demo/crypto_demo.cpp)。
4. [背景換鑰](host/rekey_pipeline.py)、[板端 worker](firmware/esp32_pqc_demo/rekey_pipeline.cpp)。
5. [TCP 接收](host/tcp_connection.py)、[協定](host/serial_protocol.py)、[重連](host/wifi_recovery.py)、[顯示](host/live_display.py)。
6. [逐檔指南](docs/FILE_GUIDE.md) 補充其餘模組。不要先從上游 PQC 數學實作開始。

host / firmware 是主線；tests 是回歸測試；camera-tests 是不含 PQC 的相機基準；dsa-validation 是獨立驗證；performance-tests 是量測與歷史實驗；diagnostics 是本機結果；archive 是歷程；baselines 是凍結來源，後续開發不得同步修改。

歷程：UART 相機整合 → 短讀診斷／恢復 → 持久裝置身分 → WiFi 與多台 → 合併往返 → pipeline → 接收顯示分離 → mDNS → v6 雙向認證 → Host 緩衝讀取。詳細原因見歷史 README 與各階段技術文件；其舊版預設值不代表現行設定。

## 基準與下一步

- [single-device-wifi-v1](baselines/single-device-wifi-v1/README.md)：早期單台副本。
- [multi-device-mutual-v6](baselines/multi-device-mutual-v6/README.md)：9/22 無參數、自動探索、持續顯示、Q / Esc 停止、只輸出 FPS 的使用版。
- [mainline-20260930](baselines/mainline-20260930/README.md)：9/30 來源與測試基準，保留目前主線參數、診斷與接收改善。不是另一份無限使用版。

依序補：重連重新探索 → 主線長期入口與狀態／影像年齡 → 有界日誌 → 控制命令保護／信任管理。範圍與驗收见 CURRENT_STATUS。UDP 未實作，暫不列入本輪。

本機測試：

```powershell
python -m unittest discover -s tests -p "test_*.py"
```

本機測試不能代替板端拒絕未授權身分的實測。工具鏈尚未完全鎖定；舊 UART 長測也不能作為當前 WiFi 韌體的驗收證據。
