# 小區塊寫入合併實驗

修改 firmware/esp32_pqc_demo/tx_options.h，重新燒錄即可選擇：

- PQC_COALESCE_SMALL_FRAMES=1（本次預設）：WiFi 下，payload <=32 bytes 的長度標頭與內容合併寫入；0 為原本兩次寫入。
- PQC_TRACE_FRAME_TX=0（預設關閉）：改為 1 時，Monitor 多印每區塊標籤、payload 大小、耗時、成功狀態。completed_bytes 只計完整成功的 writeExact 呼叫，失敗呼叫內的部分進度不包含在內，不是實際網路送達數。

開機會印 [BUILD] tx-small-frame-v1 coalesce=1 frame_trace=0，保存此行以辨識實驗設定。

相機 metadata、nonce、tag 各從兩次精確寫入降為一次，密文仍用原方式，正常完整寫入時每幀 transport write 呼叫從 8 降到 5。部分寫入仍由原 writeExact 續傳，因此實際次數可能更多。這不是承諾 TCP 封包從 8 變成 5；分段由 TCP 堆疊決定。

線上格式與 bytes 完全相同，Host 不需更新協定；不改密碼學、防重播、rekey 間隔、WiFi 省電或阻塞期限。只增加最大 36 bytes 的區域暫存，沒有整張 JPEG 複製。其他 <=32 bytes 的 WiFi 二進位區塊亦適用；UART 維持原寫入方式。

固定家用路由器、相同場景與所有 Host 參數，各跑 60 秒，比較 coalesce=0 和 1。兩組的 frame_trace 必須相同；建議先都用 0 比效能，需定位才都改 1。每組最好三輪，避免偶發網路等待主導一次結果。保留 Monitor、trace、pcapng。

```powershell
python performance-tests/run_benchmark.py --host 192.168.1.122 --seconds 60 --rekey-every 10 --memory-every 10 --display
```

觀察 receive_ms 平均/P95/最大、verified_interval_ms、FPS、TX short/failures，與相近的 JPEG bytes。沒有硬體測試結果前，不宣稱 FPS 已改善。

## 診斷資料的成本

GCM tag、nonce、metadata 與長度標頭每幀共 64 bytes（不含文字狀態），對數 KB JPEG 而言不大，但每次小寫入也可能增加排程成本。它們用於協定與安全，不能為 FPS 任意刪除。

MEMORY_INFO 等額外查詢會占用命令往返；DSA/KEM 是 session 握手成本；terminal、JSONL 每筆 flush、Monitor 都有執行成本。新增 TX_FRAME 只走 UART，不增加 TCP 回應，但 UART 日誌仍會拖慢板端，所以預設關閉。原本逐命令 Monitor 診斷仍維持不變。

本機 C++ 測試覆蓋合併前後 bytes 完全一致、0/32/33 bytes 邊界、大 payload、標頭寫入失敗不繼續與空指標拒絕。尚未使用 Arduino 工具鏈編譯或實機驗證。
