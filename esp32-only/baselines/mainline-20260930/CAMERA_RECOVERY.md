# 相機接收逾時復原

## 停止路徑驗證

`tests/test_camera_recovery_flow.py` 使用正式 main 流程與 JSONL 寫檔，模擬 UART、相機接收及握手結果，驗證：兩次復原後第三次逾時停止；上限 0 不開始復原；裝置認證失敗或 session key confirmation 失敗後不再請求影像。各案例也檢查連線 context 結束、視窗清理、run_error 存在且沒有 run_complete。這是主程式控制流程測試，不是硬體攻擊或真實簽章驗證測試；密碼驗證另由既有測試涵蓋。

復原額度耗盡新增 `recovery_stopped`（reason=recovery_limit_reached）；同步／設定／握手丟出例外新增 `recovery_failed`，保存錯誤類型與原因。terminal 會顯示 `[RECOVERY STOP]`，原例外仍傳出，程序以非零退出。

本版只復原 record 模式的 request_encrypted_frame 接收逾時。上限為每輪累計 2 次，可用 --max-recoveries 0 關閉。同步或重新握手本身失敗即停止，不套用無限重試。USB 拔除、GCM／DSA／序號錯誤、封包長度錯誤、rekey 或 MEMORY_INFO 逾時仍停止。

## 原理與邊界

1. 放棄不完整影像及舊 session 的使用；記錄 recovery_start，不計入成功幀。
2. Host 發送換行加 RECOVER 隨機 128-bit token。此操作僅可用在相機命令之後，因為相機命令不接收 Host binary payload；不能套用到中斷的 KEM／簽章輸入。
3. 韌體在上一命令返回後才處理 RECOVER，清除 session 並重新產生 KEM keypair，回覆獨立行 RECOVERED token。
4. Host 持續排空舊 bytes，最多 20 秒／2 MiB，以本次 token 辨識回應邊界；舊標記不算成功。ACK 不是身分認證，後續仍必須驗證固定信任 DSA 公鑰、新 challenge 與 session key confirmation。
5. 設定串流、重新認證成功後才重建接收 guard。因被放棄的幀會造成序號缺口，新的安全 session 從首個合法幀建立基準，之後恢復嚴格連號。transcript 加入 recovery-segment；不宣稱跨復原沒有丟幀。

RECOVER 文字命令未認證，和原 RESET_SESSION 一樣可造成服務中斷，不提供互相認證或抗 DoS。它不讀出私鑰。20 秒限制針對同步讀取迴圈；底層 write/flush、後續握手另有其限制，不能視為整個復原的硬性總時限。不完整 bytes 只記錄數量，不把密文或金鑰寫到 trace。

Python 的 None 只是停止使用舊金鑰參照，不能宣稱已安全抹除 Python 記憶體。

## 實機驗收

需要重新編譯 Upload 整合韌體，保留 NVS。先正常跑 60 秒，再刻意於第 20 次相機請求的 tag header 讀取 3 bytes 後放棄，確認可復原：

```powershell
python -u -m host.pqc_camera_demo --port COM3 --baud 921600 --mode record --seconds 60 --memory-every 10 --display --inject-camera-timeout-at 20
```

這是 Host 一次性故障注入，不是物理 UART 丟 byte：剩餘 bytes 故意留給排空流程，測試未知邊界復原。正常使用移除注入參數。terminal 應出現 RECOVERY，trace 有 injected_camera_timeout → recovery_start → recovery_boundary → recovery_complete，最後 recoveries=1 且後續影像成功。若錯誤、上限耗盡、同步或認證失敗則保留紀錄並停止。錄影仍不存檔。

本機測試使用模擬 serial 與真實 AES-GCM；Arduino 編譯、實體 UART 與整輪故障注入仍需板端驗收。
