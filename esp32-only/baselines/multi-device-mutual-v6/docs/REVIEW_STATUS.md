# 2026-09-15 整理與檢視報告

## 範圍

本次是資料夾整理、現行呼叫關係盤點與文件重寫，暫停新增功能。檢視重點包含所有 Host 模組、整合 sketch 與第一方韌體模組、獨立相機／DSA 工具、benchmark／分析器及回歸測試。上游 ML-KEM 盤點其來源、授權與檔案用途，沒有重新證明或逐行稽核密碼演算法。

沒有修改本次開始前已存在的功能變更，也沒有燒錄、連線板子、提交或推送 Git。工作目錄原有的程式差異依舊保留，不能把整份 `git diff` 都當成本次整理造成的變更。

## 整理結果

- 重寫根 README：目前階段、歷程、限制、啟動方式、閱讀順序及下一步。
- 新增 FILE_GUIDE：按檔案說明入口、依賴與實际作用，標明不能刪的共用模組及測試替身。
- 六份舊說明移入 `archive/history/`，原始內容保留。
- 舊 WiFi-only sketch、個人 note 與兩張照片移入本機封存，不直接刪除。
- 保留 diagnostics／results 原位，避免失去失敗證據或使 manifest 內路徑失效。
- 保留本機 `.pub`、`wifi_config.h` 與測試依賴。最外層 `.gitignore` 已排除這些既有本機路徑，本次不擴大忽略所有 `.pub` 或所有圖片。

## 檢視後仍需處理的問題

| 優先順序 | 問題 | 實際影響／後續方向 |
|---|---|---|
| 1 | 傳送限制不是端到端期限 | `writeExact` 限制不能保證整個命令或重連在同一時間內完成。需要統一各階段 deadline 與取消傳播 |
| 1 | 接收錯誤與恢復分類未完全一致 | supervisor 主要重試 TcpTransportError；協定層其他 TimeoutError 可能直接終止。應明確區分網路失敗、資料格式錯誤、認證失敗，不能全部盲目重試 |
| 1 | 半包後的狀態邊界 | 等 READY／payload 時中斷，需確認兩端都丟棄舊 session 和殘留資料，不能只延長 timeout |
| 2 | 顯示與 socket 等待仍耦合 | 保留視窗不代表 UI 永不凍結；主執行緒的等待可能延後 q／Esc 處理 |
| 2 | `--seconds` 非严格牆鐘預算 | 建立 session、收尾及單次阻塞使總時間超出預期；duration_expired_disconnected 不應當成成功 |
| 2 | 雙台目前靠獨立程序 | 已可選兩把不同公鑰，但尚無整合裝置清單／面板，亦未完成並行實機驗收 |
| 3 | 頻繁同步 rekey 造成停頓 | 保留可調間隔，之後比較減少往返或提前準備；不要先把間隔調大掩蓋問題 |
| 3 | 安全邊界仍不完整 | 缺 Host 身分認證；NVS 私鑰保護、用途分離、撤銷與憑證尚待設計；最後 Host 摘要簽章不是板端獨立稽核 |
| 3 | 重現版本資訊不足 | Python 有版本清單，但 Arduino core／外部 MLDSA44 未完整鎖版；benchmark 的 dirty 檔名不包含當時內容 |
| 4 | 相容參數及大型模組 | record 不再錄影卻沿用名稱／output-fps 等舊參數；crypto_demo.cpp 混合命令與密碼狀態，之後可漸進拆分 |

上述是程式檢視可指出的限制，不是已證明每次 WiFi 停頓的根因。供電、天線附近物體、熱點／路由器及驅動行為仍需控制變因才能判定。

## 本次驗證

整理後執行：

- Host Python unittest：**65 項通過**。
- performance-tests 分析器 unittest：**3 項通過**。
- `wifi-reconnect-native/test.cpp`：G++ C++17 編譯並執行通過。
- `wifi-reconnect-native/frame_writer_test.cpp`：編譯並執行通過。
- `text-tx-native/test.cpp`：編譯並執行通過。

C++ 使用各 harness 所在資料夾作為 include path，例如：

```powershell
g++ -std=c++17 -Itests/wifi-reconnect-native tests/wifi-reconnect-native/test.cpp -o "$env:TEMP/pqc_wifi_test.exe"
& "$env:TEMP/pqc_wifi_test.exe"
```

以上皆為本機測試，不需要板子。沒有本輪 Arduino 工具鏈編譯、DSA device 測試或 WiFi 實機長測，因此不能藉此宣布硬體傳輸已修好。現行版本仍停留在 README 所述的「功能已接通、穩定性尚待驗收」。
