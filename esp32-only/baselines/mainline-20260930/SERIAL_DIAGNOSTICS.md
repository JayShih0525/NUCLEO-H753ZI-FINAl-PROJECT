# UART 短讀診斷

## 最新行為：串流驗證不儲存錄影

整合 Host 的 `--mode record` 保留原指令相容性，但現在只接收、解密、驗證與選擇性顯示影像，不建立 VideoWriter 或 AVI。此模式忽略舊的 `--output`／`--output-fps`，JSONL 記錄 `save_video=false`、`output=null`，結束訊息改為 Verified。照片模式仍可儲存 JPEG。下方舊說明中提到 AVI 的段落僅適用於先前版本。

從 esp32-only 執行：

```powershell
python -u -m host.pqc_camera_demo --port COM3 --baud 921600 --mode record --seconds 60 --memory-every 10 --display
```

仍自動保存 diagnostics/camera_TIMESTAMP.jsonl；使用原本 Tee-Object 迴圈也可繼續保存 terminal log。這次只更改整合 Host，獨立 camera-tests 舊錄影工具保持原用途。

2026-09-10 23:06:32 起的 10 輪 trace 全部正常完成，9,219 幀（1566–10784）連續；全部同一 boot_id，輪次之間重啟 0 次，TX 短寫／失敗及 read_error／run_error 均為 0。這是控制線開埠調整後、停用錄影寫檔前的實測結果。

本次保持原長度封包格式，新增 Host JSONL 記錄、韌體 binary writeFrame 短寫補送與計數，以及加密錄影的 finally 收尾。這是診斷／防護變更，尚不能宣稱已定位先前 tag header 3/4 bytes 的根因。

## 操作

### 開埠控制線調整

相機 Host 現在透過 host/serial_connection.py 建立 port=None 的 Serial 物件，先設定 DTR=False、RTS=False，並明確停用 rtscts／dsrdtr 硬體流量控制，最後才 open()。目的是避免程式先以預設控制線狀態開埠、再切換控制線。這不是硬體不重置的保證，仍需比較 boot_id 驗收。

port_control_lines 現在帶 phase=before_open；port_opened 帶 strategy=control_lines_before_open。port_closed 在正常與例外收尾都會記錄，僅代表 close() 返回，測試成功仍須看 run_complete 與程序退出碼。

這次開埠修改只涉及 Host，不必再次燒錄已具 BOOT_INFO 的韌體。用相同命令跑 10 次 × 60 秒，確認相鄰輪次 boot_id 相同、uptime 增加、frame_id 接續。若仍重啟，再檢查實際控制線接線與供電。舊版本其他獨立測試入口未套用此調整。

重新編譯與燒錄整合韌體，保持 QIO Flash、OPI PSRAM、原 baud 與身分登錄。不要開啟全清 Flash，否則可能清除 NVS 身分。

從 esp32-only 執行原錄影命令即可自動記錄；例如：

```powershell
python -u -m host.pqc_camera_demo --port COM3 --baud 921600 --mode record --seconds 300 --memory-every 10 --display --output diagnostic_test.avi
```

terminal 顯示 `[DIAG]` 完整路徑，預設在 diagnostics/camera_時間.jsonl。也可指定 `--diagnostics diagnostics/my_test.jsonl`，拒絕覆蓋同名檔案。terminal 原有輸出仍可用 Tee-Object 保存。

記錄 metadata／nonce／ciphertext／tag 的標頭 hex、宣告長度、實收長度與讀取耗時。每次請求帶 request_index、last_verified_frame、expected_epoch。read_error 保存短標頭 bytes；不記錄 payload、私鑰或共享秘密。最後 run_error 保存 traceback。camera_verified 代表 GCM 與 JPEG 標記檢查通過，尚未表示 OpenCV 解碼或寫檔成功。

每個事件 flush，因此會有磁碟 I/O 成本；此版本 FPS 與無診斷版不宜直接比較。

## 韌體計數

### 每輪開機與傳送快照（2026-09-10）

必須重新燒錄這次韌體，Host 現在會要求 BOOT_INFO。保留 NVS，不必重新登錄信任公鑰。先用原本迴圈跑 10 次 × 60 秒，保留每輪不同名稱的 log、JSONL 與 AVI。

- port_open_begin／port_opened／port_control_lines：記錄開埠時間與 DTR、RTS 狀態；開埠本身可能先觸發硬體重置，設定 False 無法撤銷已發生的脈衝。
- startup_capture：原本等待的 3 秒改為接收並保存啟動 bytes 與替代字元解碼文字，最多 64 KiB，不再 reset_input_buffer 丟掉資料。raw_hex 僅用於命令開始前的啟動資料，不記錄正常加密交易的 payload。
- device_snapshot：每輪開始及 RESET_SESSION 成功後，依序查詢 BOOT_INFO、CAMERA_INFO、TX_INFO，terminal 也顯示。包含 boot_id、reset_reason、reset_name、uptime_ms、frame_id 及 TX 計數。
- session_reset_complete／run_complete／port_closed：分別標示 session 清除、協定流程完成、正常離開序列埠。run_complete 尚不表示 AVI writer 的 finally 收尾已完成；仍應檢查程序退出碼。
- 全部 camera trace 事件都有含時區 wall_time 與 monotonic 時間。例外仍記錄 run_error；封包邊界不明時不額外送診斷命令。

boot_id 是每次開機產生的 128-bit 隨機診斷值，同一次開機保持不變，不寫入 NVS，也不是 DSA 裝置指紋或認證依據。比較前輪 end 與後輪 start：ID 改變且 uptime 變小，支持期間有重新開機；相同 ID 的 TX 計數可直接相減。重啟後 TX 計數歸零，不能跨 boot 相減。PANIC、WDT、BROWNOUT 可提供原因線索，POWERON／EXT 仍須結合接線、供電與開埠時間判讀，不能直接認定是 DTR。

Host 的 7 項診斷單元測試通過；韌體需使用實際 Arduino 環境編譯、燒錄與驗收。

新增 TX_INFO 命令，僅在沒有進行中的操作時手動查詢，回傳：

```text
TX short_writes=0 failures=0 last_requested=0 last_written=0
```

short_writes 計算 Serial.write 回傳少於要求的次數，程式會補送剩餘資料；failures 是無進度達 2 秒的次數；last_requested／last_written 是最後失敗區塊的要求與已寫數量。這個 timeout 無法中止底層 Serial.write 本身的阻塞。計數只涵蓋 binary writeFrame，不涵蓋所有 printf／文字行。Serial.write 成功只表示驅動接受資料，不證明 PC 已收到。

相機任一 binary frame 送出失敗時停止該次回應、釋放密文、清除 session，不在半個封包中插入 ERR 文字。其他命令仍使用原錯誤流程，尚未完成全協定復原。

若 Host 出错，請保存 JSONL 與 terminal log。若要取 TX_INFO，Host 結束後、板子尚未 Reset 時才可能保留計數；開啟 Monitor 可能觸發重置，重置後計數歸零，不能拿零值證明先前沒發生錯誤。資料邊界不明時不自動查詢或重試，必要時 Reset 後開始下一次測試。

## 收尾與驗證

例外／Ctrl+C 會先關閉序列埠，finally 釋放 AVI writer、關閉視窗與診斷檔，再保留原例外。未知協定邊界時不盲目送 RESET_SESSION，因此板端 session 仍可能殘留；終端會提示。

新增 test_serial_diagnostics.py 測試正常碎片接收、3-byte 標頭逾時、payload 逾時不洩漏內容、超長封包診斷。韌體仍需實際 Arduino 編譯與板端串流驗收。
