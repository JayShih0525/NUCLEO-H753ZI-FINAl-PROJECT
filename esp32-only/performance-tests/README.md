# 加密相機效能量測

目標是找出接近 30 FPS 的限制，再逐項改善。這一階段只有量測，尚未新增連續推流或 RTOS 流水線。基準提交為 `44d5b3b6`；韌體與安全驗證流程維持原本版本，不需重新燒錄。

## 執行（在 esp32-only，啟用原本的 .venv）

```powershell
python performance-tests/run_benchmark.py --host 172.20.10.13 --seconds 60 --rekey-every 10 --memory-every 10 --display
```

每輪會建立 results/時間戳/，包含 terminal.txt、trace.jsonl、summary.json 與 manifest.json（命令、Git 版本及未提交檔案清單）。不存錄影；results 已加入最外層 gitignore。工具使用目前 Python 執行正式 host.pqc_camera_demo，不複製另一份加密協定。必須先完成可信任裝置登錄。

接著用相同命令移除 `--display`，比較顯示開關；解析度、JPEG 品質、rekey 間隔、記憶體查詢頻率及網路環境保持一致。兩組各跑三次再比較，避免把熱點的偶發延遲當成改善。不要同時啟動其他 Host。

## 報表怎麼讀

- stream_fps：串流迴圈的幀數／時間，含 rekey、記憶體查詢、顯示；不含起始握手及最後簽章收尾。
- interval_fps：相鄰通過驗證幀間隔倒數；舊 trace 也能計算，但不是整輪 FPS。復原邊界不跨段計算。
- request_to_status_ms：發出拍照命令到收到狀態，包含網路、裝置排程、拍照和加密。
- device_capture_encrypt_ms：韌體原有 elapsed_ms，拍照、配置及加密的合計；未拆開各項。
- receive_ms：狀態後收完四個二進位區塊的 Host 耗時，含 trace 寫入與緩衝；不是純 WiFi 傳送時間。
- validate_ms：metadata、AES-GCM、JPEG 首尾及序號驗證合計。
- decode_ms：OpenCV JPEG 解碼與尺寸檢查；display_ms：imshow + waitKey，並非螢幕真正呈現延遲。
- rekey_ms：完整重新認證、交換及金鑰確認的 Host 耗時。保留原本 rekey 參數。

每個指標列出樣本數、平均、P95（nearest rank）和最大值。不同階段有重疊，不能全部相加。沒有量到的項目省略或 null，不代表零。complete=false 表示未正常完成或 trace 損壞；正常完成但有 recoveries 也要另外檢查。

計時及既有逐行 flush、terminal 輸出本身也有成本；比較時保持相同量測設定。尚未分離板端 capture/AES/TX、未計算相機曝光到螢幕的端到端延遲，也不能單靠這份報表判定 TCP 重傳原因。

## 舊紀錄與下一步

```powershell
python performance-tests/analyze_trace.py diagnostics/camera_20260913_200602_431876.jsonl
```

reports/ 放可追蹤的比較報告，results/ 放本機原始資料。先確認主因，再視量測結果補板端計時、改連續傳送或有限佇列的拍照／加密／傳送流水線。每次只改一項，保留身份驗證、GCM、防重播及換鑰，驗證 FPS、P95 停頓、記憶體與錯誤率一起改善。


/* request_to_status_ms, receive_ms, verified_interval_ms, rekey_ms */
