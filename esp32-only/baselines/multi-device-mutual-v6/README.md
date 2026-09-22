# 里程碑：內網探索＋多台相機＋雙向認證 v6

建立：2026-09-22。獨立保存當前主線 host、firmware、tests、docs 與依賴清單；舊 baseline 不變。韌體沿用主線 v6，無須只為此啟動器再次燒錄。雙向認證仍待實機驗收，這是功能版本快照，不是長期穩定性或安全認證完成的證明。

## 無參數啟動

直接雙擊 **start.cmd** 即可；不需修改 PowerShell 執行原則。

在此目錄執行：

```powershell
.\run_camera.ps1
```

也可以在已啟用專案虛擬環境的終端機執行 `python -B -u .\run_camera.py`。PowerShell 啟動器優先使用專案原有 .venv；原始碼、公鑰與設定都使用本副本，不匯入主線程式。

- 自動搜尋 devices.json 中所有已登錄裝置，不用輸入 IP。尚未上線的裝置持續搜尋。
- 固定 QVGA，pipeline，rekey=10，無總時間限制。要改解析度請編輯 settings.py 的 RESOLUTION。
- 每台相機有獨立視窗；任何相機視窗按 **Q／Esc** 或關閉視窗，停止全部。終端機按 **Q／Esc／Ctrl+C** 也可停止全部。
- 只輸出相機名稱與 FPS。FPS 是最近輸出區間的驗證影像速率，不是從啟動起的累積平均；正常串流約每秒更新。探索或 socket 等待時可能較晚更新。
- 不寫影片、圖片、TXT、JSONL、summary 或 display 紀錄。原有詳細測試 CLI 仍保留供參考，但上述無參數啟動器不呼叫其記錄流程。
- 斷線後重新探索 IP、重新雙向認證；傳輸錯誤可重試，簽章、GCM、防重播或身分驗證錯誤會停止全部並顯示錯誤視窗，不會被 FPS-only 模式隱藏。沒有 GUI 可用時僅用 stderr 顯示必要錯誤。
- 停止時不再等完整測試收尾；關閉 TCP，ESP32 清除 session。探索／連線中的取消可能要等目前有期限的操作結束，不是無上限阻塞。

## 本機身分檔案

本副本保留一份 `.host-identity/identity.json`，含 Host 私鑰與公鑰；來源為主線相同身分，不是另外產生的新身分。這讓主線之後更改身分檔案不會直接改到 baseline。不要分享這份資料夾中的私鑰。

devices.json、host/camera*.pub、韌體 wifi_config.h、host_trust.h 都是本機副本。這些檔案及 .host-identity 已排除 Git；整個 baseline 直接複製給別人會包含敏感檔案，不能當公開分享包。依賴套件與 Python 執行環境共用，並非完整虛擬機快照。

## 凍結與檢查

執行 `.\verify_snapshot.ps1` 檢查程式快照。snapshot.json 排除可調整的 settings.py、所有本機金鑰／網路設定及 manifest 自身。後續主線開發不得同步修改此副本，除非明確要求。

這個精簡啟動器保留每幀 GCM、序號檢查、pipeline 換鑰及双向認證，但省略測試版 SELFTEST／診斷查詢／結尾示範簽章與效能摘要；它是持續顯示用途，不代替安全與效能測試工具。完整 v6 說明見 docs/MUTUAL_AUTH_V6.md。
