# 純供電探索、雙向認證與自有裝置攻擊測試計畫

更新：2026-09-21。10 月中目標以 2026-10-15 為規劃日期，僅記錄計畫，不設提醒。

2026-09-22 更新：下方為當時計畫；雙向認證 v6 已實作、待實機驗收，現況與操作見 [MUTUAL_AUTH_V6.md](MUTUAL_AUTH_V6.md)。基準資料夾已由 `20260922_140852_263914` 改名為 `diagnostics/multi/可內網搜尋`。

## 執行順序與目前狀態

1. **區網探索：本版實作，待重新燒錄後實機驗收。** ESP32 連 WiFi 並取得 IP、載入持久 DSA 身分後公告 `_pqc-camera._tcp.local.`。Host 取得 IPv4 與 TCP port，按本地公鑰的 SHA-256 指紋配對。仍由既有握手驗證裝置持有私鑰，不信任公告本身。
2. **雙向身分驗證：下一階段，尚未實作。** ESP32 也要辨識被授權的 Host；未認證者不能取影像或操作敏感命令。必須覆蓋 legacy、inline、pipeline 所有入口，不能只在 Python CLI 加檢查。
3. **前後對照與自有裝置攻擊工具：目標 2026-10-15。** 同時提供正常控制組與負向測試，產出 JSON 與易讀摘要，不把通訊逾時誤判為安全拒絕。

本版仍只有 Host 驗證裝置。公告指紋是公開路由提示，不是通行證；探索與加密不能代替存取授權。

### 2026-09-22 實機回饋與配置順序修正

`20260922_132756_933693` 已成功探索兩台、完成 DSA／KEM／key confirmation，但兩台第一幀都回覆 `ERR PIPELINE_START_FAILED`。啟動紀錄內部記憶體總空間約 91 KiB，最大連續區塊僅 46 KiB，小於 pipeline 動態建立工作任務所需的 48 KiB stack。這是探索功能加入後的記憶體配置回歸，不是這次 WiFi 逾時，也不是公鑰驗證失敗。

修正：WiFi 韌體在載入身分後、公告 mDNS 前預留原本的 48 KiB worker stack 與 PSRAM workspace；worker 先休眠，實際請求 pipeline 時才開始工作。重連沿用同一 worker，不重複建立。UART 不預留，手動 IP／blocking 的 WiFi 使用也會預留這份空間。保留原本 stack 大小，避免靠縮小 stack 引入加密運算溢位風險。

開機應新增 `[PIPELINE] worker reserved stack_bytes=49152 before discovery`。若配置失敗，Serial 會區分 workspace／task 階段並記錄連續空間；初始化停止，不假裝串流可用。修正需重新燒錄，尚待相同雙台 60 秒實機驗收。前述舊樣本保留作為修改前失敗證據。

## 使用方式

在 esp32-only 下安裝探索依賴，再把主線韌體燒錄到每台 ESP32：

```powershell
python -m pip install zeroconf==0.151.3
python -m host.discover_devices --timeout 5
```

上面只列出公告，不建立 TCP、不拍照，輸出 `authenticated: false`。ESP32 開機可看到 `[MDNS]` 紀錄，但日常使用不需要接 Serial Monitor。

依 devices.json 裡的已登錄公鑰探索並啟動所有裝置（包含原先 enabled=false 的項目）：

```powershell
python -u -m host.multi_camera --discover --seconds 60 --resolution qvga --rekey-every 10 --rekey-mode pipeline --display
```

只選一台：

```powershell
python -u -m host.multi_camera --device camera1=auto --seconds 60 --display
```

仍可用 `--device camera1=IP`，也可與另一台的 `=auto` 混用。探索不改 devices.json 或公鑰；找不到、相同指紋對應多個位置、不同身分指向同一位置時，明確報錯，避免任選。沒有自動信任新裝置的功能。

第一版在啟動時探索；串流中 IP 若改變，需要重新啟動 Host 才重新探索。原有同 IP 斷線恢復不變。熱點／校園網路若阻擋 multicast 或用戶端互通，仍可能探索不到；保留手動 IP。主機名稱取指紋前 12 字元，配對使用完整 64 字元。

## 必須保留的前後對比

**開始雙向認證實作前先留一份單向認證基準。** 現有參考資料：`diagnostics/multi/20260921_112904_089318`，雙台 QVGA、pipeline、rekey=10、60 秒，整體 FPS 15.37／16.53，無斷線；這是歷史樣本，不能代替同條件控制組。

此次修改前的 Git HEAD 為 `66c12cbc`，工作目錄當時乾淨，可作探索前程式基準。測試資料是否與此 commit 完全一致仍須以當時燒錄版本確認。

每輪記錄 commit、韌體 build、網路、供電、相機位置／場景、解析度、JPEG 品質、rekey 間隔、display。避免把網路或解析度差異當成功能開銷。

| 階段 | 控制組與比較組 | 觀察項目 |
|---|---|---|
| 探索 | 同一新版手動 IP／自動探索 | 找到幾台、探索秒數、首次驗證影像耗時、是否配對正確 |
| 雙向認證 | 本版單向／新版雙向 | 初次握手、rekey、重連耗時、FPS、P95 幀間隔、heap／stack；非法 Host 前後是否能取得影像 |
| 負向測試 | 正常請求／單一攻擊變因 | 明確拒絕原因、是否收到影像、連線與 session 是否清理、之後合法 Host 是否恢復 |

效能先短測 60 秒，差異有意義才追加；必要安全測試不因 FPS 相近而省略。保留 summary、trace、測試工具 JSON。Wireshark 用於失敗或有爭議的樣本，不要求每輪長時間抓包。

## 雙向認證設計與驗收要求

- Host 私鑰保留在 Laptop，ESP32 持久儲存允許的 Host 公鑰；首次加入走有實體控制的登錄流程，不接受任意網路自助加入。
- Host 的證明須綁定新鮮 challenge、雙方身分、本次 KEM／握手內容和協定版本。只簽固定字串不足以防重播或跨 session 搬用。
- 明確區分連線、認證、金鑰確認、可傳影像等狀態；斷線清除授權 session，重連重新驗證。
- 設計公鑰撤銷／更換與遺失筆電處理；先採小型 allowlist，避免在現階段引入完整憑證基礎設施。
- 衡量 ML-DSA 驗章的 RAM、stack 與延遲。避免為認證增加每幀簽章；rekey 必須綁定已授權 session，不能繞過授權。

## 10 月中工具的範圍

建議放 `security-tests/`。只針對明確輸入、屬於自己的測試 ESP32 執行，不自動對探索到的任意裝置發送攻擊。

第一版包含：未授權 Host、錯誤 Host 公鑰／簽章、重播舊認證、跨 session 搬用認證、修改 KEM／認證 transcript、修改 GCM ciphertext／tag、重複或過期資料、截斷／超長輸入，以及拒絕後合法 Host 重連。各案例限制次數、長度與期限；壓力測試需另行明確啟用。

結果分為 PASS（觀察到預期拒絕且沒有影像外洩）、FAIL（觀察到不應允許的成功）、INCONCLUSIVE（逾時、斷網或證據不足）。未授權案例先在舊版重現缺口，再在新版驗證修補；實際通過數據尚未產生，不宣稱 NIST 認證或完整安全稽核。

規劃節點：9 月下旬完成探索與基準；10 月上旬完成雙向認證與負向案例；10 月中整合工具、前後結果及交付報告。若時間不足，優先完成「未授權取像被拒絕、重播被拒絕、正常串流可用」，再擴大案例。

參考 API：[ESPmDNS](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS/src/ESPmDNS.h)、[python-zeroconf](https://python-zeroconf.readthedocs.io/en/latest/api.html)。
