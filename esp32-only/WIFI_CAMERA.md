# 單台 WiFi 加密相機影像

> 2026-09-14 更新：WiFi record 模式現在會在 socket 錯誤後保留視窗並自動重連、重新認證；下方早期「手動重跑」敘述已被此功能取代。請閱讀 `PROGRESS_AFTER_DSA.md`，並重新燒錄包含短寫入後 closeConnection 的韌體。驗證失敗仍停止，--seconds 包含重試時間。

沿用目前已驗證文字訊息及自動重連的 WiFi 韌體。它的 CAMERA_CAPTURE_ENCRYPTED 已經透過共用 transport 輸出 TCP；本次只新增 Host 的 TCP 選擇，不需為此重新燒錄。

## 執行

電腦與 ESP32 連同一熱點，確認 Monitor 的目前 IP。在 esp32-only 的虛擬環境執行：

```powershell
python -u -m host.pqc_camera_demo --host 172.20.10.13 --mode record --seconds 60 --rekey-every 10 --memory-every 10 --display
```

TCP port 預設 9000，可用 --tcp-port 改變。不要同時執行文字 Host；板端同時服務一個 client。保留現有 trusted_device.pub，不自動從網路信任新公鑰。

record 模式不存影片；JSONL 仍自動存入 diagnostics/camera_TIMESTAMP.jsonl，帶 transport=tcp、IP、TCP 端點、命令及寫入紀錄。terminal 顯示 frame、epoch、FPS；可按 q 或 Esc 結束視窗。沒有 --host 時維持原 UART 使用方式。

TCP 模式不執行 UART 開埠控制、啟動排空或 RECOVER，同時拒絕 UART 專用 --inject-camera-timeout-at。接收逾時或斷線時停止並關閉 TCP，保留錯誤；ESP32 恢復熱點後手動重跑 Host，重新認證。max-recoveries 不會讓 TCP 自動重試。

## 保留的驗證

初次 session、每次 rekey 都執行原 DSA 身分驗證、ML-KEM 交換及 session key confirmation。影像經 AES-GCM 驗證後檢查 frame_id 與 epoch，再由 OpenCV 解碼與顯示。正常結束保留摘要簽章測試、RESET_SESSION、開機及 TX 快照。

## 驗收

先跑 60 秒，確認畫面、連續 frame_id、多次 rekey、run_complete 與記憶體趨勢。若失敗，保留 JSONL 及 Monitor 尾端；如有 Wireshark，擷取同一輪 TCP 流量。不要把文字測試成功直接當成大影像封包也已驗收。

24 項相機本機回歸測試通過（含新 TCP 逾時與資源清理路徑）。本次尚未實際連接 ESP32 驗證畫面與 FPS，這仍是單裝置模式，未實作多視窗／多裝置信任管理。
