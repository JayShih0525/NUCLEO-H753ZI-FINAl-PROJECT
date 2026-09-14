# DSA 驗證後的專案變更與閱讀指引

## 原本的基礎

ESP32-S3 CAM 拍攝 JPEG，ML-KEM-768 建立共享秘密，AES-256-GCM 保護影像，ML-DSA-44 驗證裝置提供的握手資料。裝置身分存入 NVS，Host 透過預先確認指紋的公鑰信任裝置。每次 rekey 重新做身分證明與 session key confirmation；不是每幀都簽 DSA。每幀由 GCM 驗證完整性，並檢查 epoch、frame_id 及使用次數。

## 為什麼繼續改

密碼學測試成功，不等於實際串流可以長時間運作。早期 UART 曾收不完整封包而退出；之後長時間及多輪測試通過，但不能因此宣稱所有歷史故障都已找到根因。新增診斷是為了分辨裝置重啟、資源問題、協定錯誤與傳輸停頓。

## 後續新增的功能

1. 診斷與 UART 復原：JSONL 記錄命令、讀取階段、要求／收到的長度、BOOT、記憶體與 TX 狀態。UART 可有限度重新同步並建立新的認證 session；不把未完整收到的密文當成有效影像。
2. WiFi 傳輸：Host 增加 TCP transport，共用原本的長度標頭與密碼學流程。先驗證加密文字 AES_ECHO，再把 CAMERA_CAPTURE_ENCRYPTED 接入 TCP。ESP32 接行動電源即可使用，UART 只需在燒錄或查看 Monitor 時連接。
3. ESP32 網路重連：斷開熱點後由板端重新連 WiFi、重新開啟 listener，清除舊連線的 session，避免必須按 Reset。這與 Host 重建 TCP 是兩個不同工作。
4. 效能量測：performance-tests 共用正式 Host，記錄接收、驗證、JPEG 解碼、顯示與 rekey 時間。結果與 terminal 自動保存、不保存影片。解析度與 rekey 等參數不因測試而自動放寬。
5. 本次 Host 自動重連：WiFi record 模式遇到 socket 錯誤，不關閉影像視窗；回到重新連線、重新驗證與新 session 的完整流程。初次連不上及 rekey 中斷也適用。

## 這次故障證據

20260914_001220 這輪成功驗證 53 幀。下一幀收到 5096 bytes 密文，卻等不到 tag 標頭。板端 cmd=85 的 tx_calls=7、tx_bytes=5140、tx_short=1，對應 metadata、nonce、密文已寫入，tag 標頭寫入回傳零。單次 write 約卡 10 秒，封包擷取同時有 TCP 重傳。

直接失敗機制已確認；造成底層傳送受阻的網路／驅動原因仍未確定。RSSI 強不代表所有封包都能準時送達，connected=1 也不代表應用層仍能順利傳送。

## 本次重連的技術細節

- crypto_demo.cpp：影像 writeFrame 失敗時，清除 session 並 closeConnection；不在半個二進位影像中插入 ERR 文字。
- tcp_connection.py：socket 失敗包裝為 TcpTransportError，與本機檔案權限等 OSError 區分。
- wifi_recovery.py：監督整次 run_once。只有 TcpTransportError 可自動重試；身分、金鑰確認、GCM、序號或一般協定錯誤會停止，不會繞過驗證。
- pqc_camera_demo.py：每次嘗試使用獨立 session、replay guard 與 transcript；失敗後釋放該次連線，視窗在 TCP 重試期間保留。重連面板取代舊影像，避免誤認舊畫面仍是即時畫面。
- 重試間隔為 1、2、4、8 秒，之後維持 8 秒。每次重新載入可信任公鑰，走原本 INFO、設定、身分驗證、KEM 與 key confirmation。失敗幀不重送，不沿用舊金鑰或殘留 bytes。
- --seconds 仍是整輪時間預算，包含重連等待；每次重連不重新獲得完整時長。既有阻塞操作可能讓實際退出稍晚。期限內仍無法恢復，回傳失敗；不是無限執行模式。
- 原 trace 保留；後續為 _attemptN.jsonl，另有 _reconnect.jsonl 記錄嘗試、原因與結束狀態。效能啟動器會把各段摘要合併，不把第一段失敗誤當整輪結果。
- 暫停重試時可按 Q／Esc；Ctrl+C 亦可退出。底層 socket 仍可能阻塞約 10 秒，因此這一版不保證所有網路等待期間視窗都即時回應。尚未加入背景 I/O 執行緒或修改底層 write 的期限。

## 如何驗證

先重新燒錄 firmware/esp32_pqc_demo。Host 不需重新登錄原本可信任公鑰。執行：

```powershell
python performance-tests/run_benchmark.py --host 172.20.10.13 --seconds 180 --rekey-every 10 --memory-every 10 --display
```

先讓畫面正常運作，再暫時關閉熱點、重新開啟。確認視窗保留、重試後重新出現身分與 session 驗證、畫面恢復。IP 若改變，目前仍需使用新 IP 重跑；沒有自動探索。保存同輪 Monitor 與 Wireshark，檢查 failed attempt 和 reconnect 紀錄，不能只看最後 PASS。

這次本機測試模擬網路失敗與重試；實際熱點中斷、板端短寫入與畫面恢復仍需裝置驗收。每段成功結束才有該段最後的摘要簽章；中斷段不宣稱完成收尾簽章。

## 仍待完成

2026-09-14 第二次斷網測試補充：板端曾印出 online ip=0.0.0.0，表示原本只看 WL_CONNECTED 的上線判斷不足。本次改為狀態已連線且 IPv4 非零，才啟動 listener、接受 TCP 及認定既有連線有效。原生模擬測試已涵蓋無 IP 時不接受連線、仍會重試、取得 IP 後恢復。約 109 秒沒有 retry 紀錄的完整原因尚未確認；不宣稱這項修正已解決全部等待問題，仍需重新燒錄實測。

底層寫入期限與視窗等待期間的反應、TCP 根因排查、連續推流／RTOS 流水線、雙裝置信任與接收、ESP32 驗證 Host 身分、不同用途的金鑰派生仍是後續工作。自動重連提升可恢復性，不等於解決網路丟包或達到 30 FPS。
