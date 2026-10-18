# 開發驗證紀錄

日期：2026-10-18。這是測試工具的建置／驗收狀態，不是演算法效能結果。

## 已確認

- Arduino ESP32 core 3.3.11、ESP32-S3 Dev Module、240 MHz、16 MB Flash、OPI PSRAM 設定完整編譯成功。
- 實際使用現有 PQClean ML-KEM-768 和 mldsa 0.2.0 來源；準備後副本 SHA-256 與原始來源一致，來源 manifest 與成功 build evidence 保存在本機。
- 最後成功 build：程式儲存空間 326679 bytes，約 **319.02 KiB**；全域變數 131364 bytes，約 **128.29 KiB**；編譯器列出剩餘動態 RAM 196316 bytes，約 **191.71 KiB**。
- 上述是整份 benchmark 韌體編譯結果，包含 96 KiB 靜態 worker Stack；不是演算法的實際執行峰值，也不是開機後實測 free heap。
- Host 的 17 項測試涵蓋協定完整性、統計、暫存檔輸出、錯誤及逾時處理、條件不同時拒絕比較、HELLO 識別碼避免開機 READY 誤判。全部通過。人造測試資料不放在正式 results。
- 本次未修改相機主線韌體／Host，也未讀取、覆寫或刪除板上的 NVS／身分 key。

## 還沒確認，不能當成實測完成

- Windows 列出 COM1（內建序列埠）和 COM4（Bluetooth）；沒有 USB ESP32 序列埠。沙箱外也再確認一次。
- 因此尚未燒錄此 benchmark、尚未進行 ESP32 真實耗時與記憶體量測；沒有捏造 benchmark 成績。
- 尚未實機驗證 worker Stack 裕量、局部 Heap 監控、反覆執行及中斷後重跑。
- 須確認板子目前 NVS 分割設定與 build.py 的固定 partition scheme 一致；燒錄前不做全片擦除。

## 板子接上後的驗收順序

1. 使用 TTL Type-C、可傳資料的 USB 線，列出真正的 COM 埠，關閉 Serial Monitor／相機 Host。
2. 用 build.py 的 `--upload --port 實際COM` 燒錄。原應用程式會換成 benchmark。
3. 先跑 `--rounds 2 --warmups 1 --message-bytes 32`：應收到 28 筆樣本、14 組統計、完整 end、exit code 0，summary.json 的 complete 為 true。
4. 再跑 100 次、暖機 3 次、32 bytes；應收到 1400 筆樣本。確認每一筆 rc=0、validation_ok=true、局部 Heap monitor 成功、Stack 沒接近耗盡。
5. 同樣條件跑三輪，以 compare.py 產生總表；保存結果資料夾及 label。另測 1024、4096 bytes 時分開比較，不混成相同訊息大小的結果。
6. 人為按 RESET 中斷一輪，應得到未完成結果，不能被 compare.py 當成完整測試。
7. 測試完要使用相機，燒回原本 esp32_pqc_demo；核對原身分／信任關係仍存在，再恢復影像流程。
