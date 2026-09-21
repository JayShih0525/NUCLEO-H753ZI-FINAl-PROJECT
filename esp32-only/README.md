# ESP32-S3 加密相機：目前狀態與閱讀入口

> 2026-09-17：新增 [提前換鑰實驗與完整技術說明](performance-tests/REKEY_PIPELINE.md)。主線重新燒錄後，可用 `--rekey-mode pipeline` 啟用背景準備＋影像附帶換鑰；預設 `blocking` 保留原流程。新原型尚待實機驗收，凍結副本不變。

> 前一版 [inline v5 握手](performance-tests/REKEY_INLINE.md) 已有雙台五分鐘完成紀錄 `20260917_181551_821053`，串流約 20.48／23.81 FPS，無重連。這是當輪結果，不保證所有網路都達到相同效能。下方舊 build marker 與進度屬歷程快照。

> 2026-09-16 主線更新：新增 [N 台接收啟動器](MULTI_CAMERA.md)，IP 可在每次執行以 `--device 名稱=IP` 傳入；公鑰預設已改為 `host/camera1.pub`。目前完成本機程序隔離與測試，尚未完成多台 ESP32 同時實機驗收。下方歷史進度表是整理時快照。另見 [FPS 改善方向](docs/FPS_ROADMAP.md)。

> 2026-09-16：已建立獨立的 [單台 WiFi 凍結基準](baselines/single-device-wifi-v1/README.md)。後續功能改進只修改主線，勿同步修改此副本。副本啟動器將紀錄寫到外部 diagnostics；保留的是目前可執行版本，不代表 WiFi 穩定性已通過驗收。

整理日期：2026-09-15。本文件以目前工作目錄的程式為準，包含尚未提交的修改。歷史測試只代表當時版本；本次整理沒有重新做硬體驗收。

這個專案讓 ESP32-S3／OV2640 拍攝 JPEG，透過 UART 或 WiFi TCP 傳到電腦。ML-KEM-768 建立共享秘密，AES-256-GCM 保護影像，ML-DSA-44 證明裝置身分與握手資料來源。目標是可恢復連線、可量測且能擴展至兩台裝置的加密影像系統。

## 1. 現在到哪裡？

**目前是「單裝置 WiFi 加密影像已實現，正在處理傳輸穩定性；多裝置已完成各自選擇信任公鑰的準備」。** 尚不能宣稱 WiFi 已穩定、雙裝置已通過或接近 30 FPS。

| 部分 | 已有功能／證據 | 尚未完成 |
|---|---|---|
| 相機與 UART | 獨立相機測試、整合加密影像；舊版本曾完成 50 分鐘及多輪短測 | 舊長測不能替目前 WiFi 韌體背書 |
| 密碼功能 | KEM、GCM、DSA 正負向驗證，定期 rekey | 不等於標準合規、側通道或完整安全稽核 |
| 裝置身分 | NVS 持久 DSA 身分、指紋登錄、固定公鑰驗證、新鮮握手及 session key confirmation | ESP32 尚未驗證 Laptop 身分；憑證、撤銷與安全金鑰儲存未完整實現 |
| 防重播 | 相機端到端 GCM 驗證，Host 檢查 epoch／frame_id 等順序狀態 | 不代表所有命令都有完整的認證與防重播 |
| WiFi | 文字與影像傳輸，ESP32 可僅接行動電源；曾完成五分鐘測試 | 初連線、握手、傳送與恢復仍有間歇停頓 |
| 斷線恢復 | ESP32 重試 WiFi；Host 重建 TCP、重新認證與換 session，保留顯示流程 | 所有阻塞路徑、截止時間與錯誤分類仍需整理 |
| 多裝置 | 每個程序用 `--trust-key` 指定一把公鑰，不必改原始碼 | 同時雙台吞吐、整合啟動器／面板未驗收 |
| 效能 | 分段計時、benchmark、封包與 Monitor 對照；小封包合併及受限送出 | 未完成相機／加密／傳輸流水線、提前換鑰；30 FPS 是目標 |

最新案例 `20260915_223252_404943`：裝置身分驗證曾通過，後續 KEM 階段等待回應逾時，重連也未能在測試預算內恢復，最後 `duration_expired_disconnected`，沒有驗證成功的影像。這是未通過的一輪，並非正常串流滿時間。

## 2. 新手從哪裡讀？

不要先從 PQC 數學實作開始。先追一張影像如何走完整條路，再深入演算法。

1. 讀本文件與 [逐檔指南](docs/FILE_GUIDE.md)，認識各資料夾責任。
2. 讀 [主 sketch](firmware/esp32_pqc_demo/esp32_pqc_demo.ino)：`setup()` → 通訊／相機 → PQC task → 命令迴圈。
3. 讀 [相機 Host](host/pqc_camera_demo.py)：參數、開啟連線、建立 session、接收與驗證、rekey、收尾。
4. 對照 [密碼與命令處理](firmware/esp32_pqc_demo/crypto_demo.cpp) 和 [共用 Host](host/pqc_host_demo.py)：找 `AUTH_KEM`、`KEM_DECAPSULATE`、`CONFIRM_SESSION`、`CAMERA_CAPTURE_ENCRYPTED`。
5. 讀 [板端協定](firmware/esp32_pqc_demo/protocol.cpp)、[傳輸層](firmware/esp32_pqc_demo/transport.cpp)、[Host 協定](host/serial_protocol.py)：理解 TCP 是位元組流，單次讀取不等於完整封包。
6. 讀 [身分驗證](host/device_auth.py)、[NVS 身分](firmware/esp32_pqc_demo/device_identity.cpp)、[防重播](host/camera_replay.py)。
7. 讀 [WiFi 恢復](host/wifi_recovery.py)、[UART 恢復](host/camera_recovery.py) 及其 tests，最後讀 benchmark 與 JSONL。
8. 有需要才深入 `src/mlkem768/`；保留上游授權及來源版本，不把它當成自製演算法。

## 3. 資料夾用途

```text
esp32-only/
  firmware/esp32_pqc_demo/  現行整合韌體：燒錄這個 sketch
  host/                     現行電腦端：以 python -m host... 啟動
  tests/                    不需板子的回歸測試與 C++ 模擬測試
  camera-tests/             不含 PQC 的獨立相機基準，保留用於排錯
  dsa-validation/           DSA 本機／板端獨立正負向驗證
  performance-tests/        共用 Host 的量測入口、分析器與實驗說明
  diagnostics/              本機診斷紀錄，不提交 Git
  docs/                     現行逐檔閱讀與檢視結果
  archive/history/          歷史說明，不能當現行操作規格
  _local_archive/            本機舊實驗／個人資料／產物，不提交 Git
```

`pqc_host_demo.py` 雖然名字看起來像舊 demo，仍提供相機及 WiFi 使用的握手函式，不能刪。`camera-tests/` 不是整合 firmware 的重複備份，而是隔離故障的工具。`tests/` 多數在本機執行，不是全部都需要接 ESP32。

## 4. 從開始到現在，為何做這些修改？

| 階段 | 遇到的問題 | 處理方向及目的 | 判讀限制 |
|---|---|---|---|
| 獨立相機 | 先確認 OV2640 能拍照、連續輸出 | 建立 photo／stream 基準，將相機問題與 PQC 分開 | 基準模式沒有加密 |
| UART 整合 | Python 匯入路徑、boot／baud 設定、預設執行時間造成混淆 | 用模組入口；確認板型、Flash、PSRAM 與應用程式 baud | ROM 開機訊息與應用程式 baud 可以不同 |
| 長測與診斷 | 短讀曾讓 Host traceback 結束，無法判定重啟或封包中斷 | 記錄 boot_id、記憶體、TX、JSONL；完善清理與 UART 恢復 | 沒再失敗不等於找出最初根因 |
| DSA 完整性測試 | 只有成功驗章不足以了解保護範圍 | 加入修改訊息／簽章／公鑰等負向測試 | 重播有效簽章本身仍會通過 |
| 真正身分驗證 | 同一連線提供的公鑰不能直接當可信身分 | NVS 持久身分、人工核對指紋登錄、公鑰固定信任、挑戰簽章、key confirmation | 目前主要是 Host 認證 ESP32 |
| 序號與恢復 | 截斷、殘留資料及 rekey 邊界容易打亂狀態 | GCM metadata、epoch／frame 檢查；失敗後重新建立可信 session | 不能跳過驗章來換取恢復速度 |
| WiFi 文字→影像 | 解除 UART 速度與實體資料線限制 | 先 encrypted echo，再重用同一加密相機流程 | WiFi 模式仍需供電，但不需 COM 資料連線 |
| WiFi 重連 | 熱點斷線、假 online 的 `0.0.0.0`、傳送卡住 | 有效 IP 檢查、重試、Host supervisor、重新認證 | 恢復速度仍受其他阻塞路徑影響 |
| 效能量測 | FPS 低，不能只猜是加密太慢 | 分段時間、pcap、Monitor；合併小 frame、文字 TX 統計、非阻塞 TCP send 與寫入期限 | 網路／JPEG 大小／版本不同不能直接比 FPS |
| 多裝置信任準備 | 換板需修改預設公鑰，容易選錯 | `--trust-key` 固定每個程序的裝置公鑰 | 尚未完成雙台並行驗收 |

早期 UART 曾有 300 秒 4616 幀（15.38 FPS）、3000 秒長測，以及之後 30 分鐘與 60 輪短測的成功紀錄。它們屬於不同歷史版本。WiFi 也曾在僅供電時完成五分鐘，但後續測試仍出現停頓及中斷，不能合併成「現在全部穩定」。詳細原始歷程見 [歷史封存](archive/README.md)。

## 5. 目前協定與金鑰怎麼合作？

```text
事先：核對裝置指紋 → enroll_device 保存可信 DSA 公鑰
每次初連線／rekey／重連：
  Host 新挑戰 → ESP32 用持久 DSA 私鑰簽署新 KEM 握手資料
  Host 用指定 .pub 驗章 → ML-KEM encapsulate／decapsulate
  session key confirmation → 開始 AES-GCM 影像
串流：metadata + nonce + ciphertext + tag → 驗證、序號檢查、顯示
達到 rekey 門檻：重新握手，換 KEM／共享秘密，DSA 身分保持不變
正常收尾：ESP32 簽署 Host 提供的 transcript 摘要，Host 驗章
```

- **不是每張影像都簽 DSA。** 每張影像靠 AES-GCM 驗證；DSA 主要用於握手及正常收尾摘要。
- 最後摘要由 Host 累積後送簽，不能宣稱 ESP32 獨立稽核過全部影像紀錄。
- `.pub` 是原始公鑰資料，不是文字指紋，也不是 `.hub`。一個檔案一把公鑰，不要把兩把串接。
- 指紋是 DSA 公鑰 SHA-256 的 64 個十六進位字元。IP／COM 是位置，不是身分。
- NVS 保存裝置長期身分；一般重開機沿用。清除相關 NVS／全 Flash 可能使身分重新建立，需要重新可信登錄。
- 控制行是文字，二進位資料使用四位元組長度前綴。TCP 會拆分或合併讀取；程式必須讀足長度，失敗不能直接使用不完整資料。
- 未使用 TLS；目前是應用層 PQC＋GCM。明文狀態行不等同經認證的握手證據。

詳見 [身分驗證](AUTHENTICATION.md)、[防重播](REPLAY_PROTECTION.md)、[多裝置信任](MULTI_DEVICE_TRUST.md)、[UART 恢復](CAMERA_RECOVERY.md)。

## 6. 安裝、燒錄與啟動

以下 PowerShell 指令都從 `esp32-only` 執行，先啟用專案 `.venv`。IP 使用 Monitor 當前顯示值，不要照抄舊紀錄。

```powershell
python -m pip install -r requirements.txt
```

Arduino 開啟 `firmware/esp32_pqc_demo/esp32_pqc_demo.ino`。ML-KEM 已隨專案提供，AES 使用 ESP32 core 的 mbedTLS；另需程式引用的 ML-DSA-44 Arduino library。此專案尚未提供完整可重現的 Arduino core／外部 DSA library 版本鎖定；更換環境前應記錄實際已成功的版本。

WiFi 設定由 `wifi_config.example.h` 複製成同資料夾的 `wifi_config.h` 後填寫。真實設定檔不提交 Git。請依實際模組選擇 Flash／PSRAM 設定；不能把某台可用的 QIO／DIO／OPI 組合視為所有板型通用。應用程式 Serial 是 **921600**，上傳速度是另一個設定。

目前 build marker 是 `tx-nonblocking-v3`。看到 WiFi online 只代表網路就緒，仍需等相機與 PQC 初始化完成。WiFi 程式透過 IP:9000，COM 換號不影響 TCP。UART 程式則需要正確 COM，且 Serial Monitor 不能同時占用該埠。

### 登錄與選擇裝置

先從你實際控制的板子確認指紋，再登錄。以下變數必須替換：

```powershell
$cameraIp = '填入裝置IP'
$fingerprint = '填入已核對的64位指紋'
python -m host.enroll_device --host $cameraIp --output host/camera2.pub --fingerprint $fingerprint
```

登錄工具拒絕覆蓋既存輸出。已登錄就直接使用，不需每次重跑。第一台本機檔案為 `host/camera1.pub`，第二台為 `host/camera2.pub`；都保留但不推 Git。兩個獨立程序可各自指定公鑰；這與「同一個 session 接受任意公鑰」不同。

### WiFi 文字、影像與量測

```powershell
python -u -m host.pqc_wifi_demo --host $cameraIp --trust-key host/camera2.pub --message "Hello encrypted WiFi" --exchange-count 20 --rekey-every 10
python -u -m host.pqc_camera_demo --host $cameraIp --trust-key host/camera2.pub --mode record --seconds 60 --rekey-every 10 --memory-every 10 --display
python performance-tests/run_benchmark.py --host $cameraIp --trust-key host/camera2.pub --seconds 60 --rekey-every 10 --memory-every 10 --display
```

`record` 是保留的模式名稱，**目前只驗證／顯示影像，不保存 AVI**。`--output` 在此模式不產生錄影；photo 模式仍可保存單張 JPEG。`--seconds` 預設 10 秒，這不是永久執行模式，也不是嚴格牆鐘截止時間：握手、阻塞讀取及收尾可能讓總時間超過參數。

UART 使用相同 Host 的 `--port COM3 --baud 921600` 並省略 `--host`，且板端必須設定 UART 模式。不要用 UART 登錄命令去操作只處理 TCP 命令的 WiFi 韌體。

### 不需硬體的測試

```powershell
python -m unittest discover -s tests -p "test_*.py"
python -m unittest discover -s performance-tests -p "test_*.py"
python dsa-validation/verify_dsa.py
```

C++ native harness 的作用與檢查結果見 [檢視報告](docs/REVIEW_STATUS.md)。模擬測試不能替代真實 WiFi 斷線、Arduino 編譯與長測。

## 7. 怎麼看量測與失敗？

benchmark 在 `performance-tests/results/時間戳/` 保存 `manifest.json`、`terminal.txt`、`trace*.jsonl`、`summary.json`。Wireshark `.pcapng` 與 `monitor.txt` 放同一輪資料夾，避免混到其他測試。

| 指標 | 意義與注意事項 |
|---|---|
| `request_to_status_ms` | Host 發出取圖要求到收到狀態；包含板端處理及網路等待 |
| `receive_ms` | Host 收取影像 frame 資料的時間；不是純 WiFi 空中傳送時間 |
| `verified_interval_ms` | 相鄰驗證成功影像的間隔，包含中間等待，可能包含 rekey |
| `rekey_ms` | 完整換鑰握手時間，包含認證、KEM、confirmation 與傳輸 |

這些時間有重疊，不能全部相加。分析器的 stream FPS、區間 FPS 與整輪包含初連線／重連的 FPS 也不同。小樣本平均很容易掩蓋最長停頓。

`duration_expired_disconnected` 表示時間預算結束時尚未恢復，`connection_attempt` 是嘗試事件，不是成功。summary 的 `reconnects` 計算傳輸中斷事件，不等於成功重連次數。即使最後 `completed`，仍應檢查各 segment 和中斷次數。

目前 TX 已改為非阻塞 socket send，`writeExact` 有無進展與總時間限制，但限制是**每次 exact write**，不是整個命令、frame 或重連流程的硬性上限。WiFi 模式 `tx_short` 也可能包含暫時無可寫空間，不可直接當成遺失的封包數。詳見 [TX deadline](performance-tests/TX_DEADLINE.md)。

## 8. 下一步優先順序

1. **先整理連線狀態與期限。** 對初握手、讀取、寫入、關閉及重連逐一訂出失敗後的狀態；尤其等待 READY／半包 timeout，確保不沿用殘留資料或未確認的 key。先讓失敗可預測、可定位，再累積長測。
2. **兩台分別短測，再同時接收。** 固定每台 IP／公鑰配對，先用兩個程序隔離狀態，確認其中一台斷線不影響另一台，再做整合入口與面板。
3. **在穩定版本改善 FPS。** 保留 `--rekey-every`；量測後才考慮減少往返、提前準備下一輪握手及相機／加密／傳輸 pipeline。FreeRTOS 已在使用，新增 task 本身不保證加速。
4. **補雙向認證與安全實驗。** ESP32 驗證可信 Host、權限、金鑰用途分離與撤銷；再設計固定條件的重播／篡改／斷線／換鑰實驗。不能只靠成功傳幾次就宣稱某換鑰間隔安全。

本次整理暫停功能修改，沒有要求現在立刻再跑長測。先用這份文件對齊目前邊界，再選一個可驗收的小目標。

## 9. 本次整理保留與封存原則

現行程式、回歸測試、獨立相機基準、實際信任公鑰、WiFi 設定與診斷證據全部保留。舊操作說明移到 `archive/history/`；WiFi-only 舊 sketch、個人筆記與產生的照片移到忽略提交的 `_local_archive/cleanup_20260915/`。沒有刪除舊 log／pcap，避免丟失根因證據或破壞 manifest 內的路徑。

完整清單見 [封存對照表](archive/README.md) 與 [逐檔指南](docs/FILE_GUIDE.md)。歷史文件內的相對路徑、baud、錄影及信任載入行為可能已失效，現行操作以本 README 為準。
