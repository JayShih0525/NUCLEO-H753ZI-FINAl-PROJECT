# 相機 Host 防重播與序號檢查

## 行為

`host/camera_replay.py` 為每條相機連線保存當前 epoch、最後接受的 frame_id，以及目前 session 接受的幀數。正常串流只保留固定數量的整數，不保存歷史影像或所有序號。

`host/pqc_camera_demo.py` 在裝置身分驗證及 session key confirmation 成功後啟用此狀態。接收影像時先檢查 epoch、長度及 GCM 標籤（metadata 是 AAD），再檢查 JPEG 頭尾與序號；全部通過才更新接收狀態。

- 新連線第一幀允許任意合法 frame_id：板子可能已運作一段時間；CAMERA_INFO 是未認證診斷，不能作為安全序號基準。
- 後續 frame_id 必須等於上一幀加一。重複、倒序、跳號及 uint32 回繞皆拒絕。
- rekey 後 epoch 必須加一，session count 歸零，但最後 frame_id 保留，因此影像序號必須接續。
- count 必須從 1 遞增，rekey flag 必須與設定門檻一致。文字 status 未受 GCM 保護，這些只是協定一致性檢查；安全依據是已認證的 metadata 和當前 session 金鑰。
- 不同連線即使 epoch 相同，舊密文也無法通過新 session 金鑰的 GCM 驗證。此性質依賴原有新鮮握手、KEM 與 key confirmation；epoch 本身不是全域 session ID。

拒絕會停止本輪並寫入 run_error；序號／count 檢查拒絕另有 camera_replay_rejected。epoch 與 GCM 失敗發生在更早的驗證階段，不會有這個事件。每次成功啟用或輪替狀態會記錄 camera_replay_session。

本階段沒有新增斷線復原、重送或自動重新認證。UART 一問一答模式選擇嚴格連號；未來允許丟幀的串流需重新設計接收窗口。裝置重啟需要重新建立連線及認證。這也不是 ESP32 接收端命令的全面防重播。

## 驗證

在已安裝專案依賴的虛擬環境，從 esp32-only 執行（不需要 ESP32）：

```powershell
python -m unittest discover -s tests -p test_camera_replay.py -v
```

測試以真實 AES-GCM 密文及模擬序列協定走正式 request_encrypted_frame，驗證合法連續幀、rekey、重播、倒序、跳號、舊 epoch、相同 epoch 的舊金鑰資料、metadata／tag 篡改、status 篡改與計數回繞。測試 JPEG 僅包含頭尾標記，用於驗證接收路徑，不宣稱已測試 OpenCV 解碼或實際 UART。

實機先跑 60 秒，使用原韌體即可，不必重新燒錄：

```powershell
python -u -m host.pqc_camera_demo --port COM3 --baud 921600 --mode record --seconds 60 --memory-every 10 --display
```

terminal 應顯示 replay/order guard enabled，跨多次 rekey 正常完成。JSONL 仍保存，影片不寫檔。
