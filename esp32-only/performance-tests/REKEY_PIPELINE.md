# 提前換鑰實驗：把準備工作放進影像串流期間

日期：2026-09-17。這是主線的 **opt-in 原型**，不修改 `baselines/single-device-wifi-v1/`。
預設仍為 `--rekey-mode blocking`；只有明確指定 `pipeline` 才啟用新流程。
需要重新燒錄主線 sketch；開機 build marker 為 `rekey-pipeline-v1`。WiFi INFO 必須包含 `proto=5 inline_rekey=1 pipeline_rekey=1`。
`pipeline_rekey=1` 表示韌體支援，不保證當下能分配 worker stack；配置失敗會明確報錯。

## 起因：我們已解決什麼，現在還卡在哪裡？

早期每輪換鑰需要停下影像，依序要求 READY、傳 challenge、公鑰認證、KEM ciphertext、金鑰確認。
前一版 inline v5 把命令與資料合併送出，移除三次 READY 等待，且保留 DSA 認證與確認。
雙台五分鐘 `20260917_181551_821053` 的串流 FPS 約 20.48／23.81；每次 rekey 仍需約 119／113 ms。

後續 `20260917_182957_598365` 關 display 並未提升整體 FPS，但該輪網路等待也增加，不能單獨判定 display 無成本。
`20260917_183316_273472` 將 interval 改成 30，FPS 約 22.65／23.45，單次 rekey 仍需約 113／124 ms。
這些不同輪次不是嚴格控制網路與場景的因果實驗。

因此這次保持「每 10 張最多使用同一把影像金鑰」的設定，嘗試在舊 session 還能傳影像時，先準備下一組。
目的不是省略簽章、降低驗證或增加舊金鑰使用次數，而是縮短換鑰邊界的停頓。

## 新手先看這個流程

初次連線仍完整執行現有裝置認證、ML-KEM 與 session confirmation。
之後保留兩個位置：`active` 是現在傳影像的金鑰，`pending` 是尚未啟用的下一組。

```text
初始握手 → active epoch E
  第一張請求附帶新 challenge
    ├─ 主 task：拍照、GCM 加密、傳影像
    └─ 背景 task：生成下一組 KEM、公鑰與 context 的 DSA 簽章
  後續影像回應附帶已完成的公鑰＋簽章
    Host 驗章 → encapsulate
  下一筆影像請求附帶 KEM ciphertext
    ├─ 主 task：繼續用 E 傳影像
    └─ 背景 task：decapsulate、產生新金鑰 confirmation proof
  後續影像回應附帶 proof；Host 驗證 → pending ready
  第 N 張：仍使用 E，達到使用上限
  下一次請求：附帶 commit token
    ESP32 驗證 → 原子地切換 session → 用 E+1 傳第一張
    Host 必須用新金鑰驗證該張影像
```

這不是固定在第 3、6、9 張各完成一個步驟。背景運算與網路有快慢，因此狀態隨每筆回應推進。
切換後第一張附帶 commit；第二張開始準備再下一組。每次依然產生新 KEM 金鑰與新的 DSA 證明。

**如果第 N 張已到，但 pending 還沒好：停止索取舊金鑰影像，只交換準備狀態。**
每次未完成的 poll 間隔 10 ms。5 秒準備等待預算在每次 I/O 前檢查；已進行的 I/O 仍受現有 socket timeout 限制，
所以這不是保證牆鐘時間恰好 5 秒的硬截止。逾時走既有 TCP 重連、重新認證流程。
不會在相同 byte stream 靜默降級或重送一段不確定完成到哪裡的握手。

## 為什麼沒有把簽章拆成十份？

一次附帶完整公鑰與簽章，約 3.6 KB；另一筆請求附帶完整 1088-byte KEM ciphertext。
避免新增應用層碎片編號、重組與多次確認。TCP 本身仍可能把它拆成多個 segment，單次 write 不代表單個網路封包。
這些握手資料原流程本來也要傳；本次改變的是時機、組裝方式與狀態管理。
新控制標頭、HMAC 和狀態會增加少量資料，且背景簽章仍消耗 CPU；效能改善不能預先保證。

## 實作分工與 RTOS

| 檔案 | 實際作用 |
|---|---|
| `firmware/esp32_pqc_demo/rekey_pipeline.cpp/.h` | 新增 pending workspace、背景 KEM／DSA／confirmation 工作、狀態發布、commit 與清理 |
| `firmware/esp32_pqc_demo/pipeline_rules.h` | 共用且可單獨測試的請求長度、epoch／count、換鑰門檻檢查 |
| `firmware/esp32_pqc_demo/crypto_demo.cpp` | 驗證控制請求、組裝影像與附帶回應、嚴格門檻、active session 切換、命令隔離 |
| `host/rekey_pipeline.py` | Host 的 pending 狀態、驗章、封裝、確認、邊界等待及切換 |
| `host/pqc_camera_demo.py` | 接上 pipeline 取圖與原有 GCM、重播防護、transcript、重連流程 |
| `host/serial_protocol.py` | 單次 write 送出命令＋長度＋完整控制 payload；接收仍支援碎片 |
| `host/pqc_host_demo.py` | INFO capability 解析；初次握手仍沿用 inline v5 |
| `host/multi_camera.py` | 將 `--rekey-mode` 傳給每個相互隔離的相機程序 |
| `host/multi_summary.py` | 測試結束後彙整 pipeline 指標，不增加網路查詢 |

背景 task 名稱 `pqc-next`，`xTaskCreate` priority=1，不固定 CPU core。
`freertos/FreeRTOS.h` 與 `freertos/task.h` 在新 `.cpp` 引入。
使用 task notification 派送工作，以及 `std::atomic<State>` 的 release/acquire 發布輸入與結果。
狀態是 `EMPTY → WORKING → OFFER → WORKING → CONFIRMED → EMPTY`，運算失敗則進入 `FAILED`。

只有主 protocol task 讀寫 TCP；worker 不碰相機、active session、網路或主 task 的 DSA 暫存區。
因此不會讓背景簽章資料插進 JPEG 中間。DSA_SIGN 收尾前會等待 worker 結束並丟棄 pending。
pipeline 期間只允許新相機命令、必要的唯讀診斷、DSA 收尾與 RESET；拒絕會改動 active crypto 狀態的舊命令。

第一次使用 pipeline 才配置約 **48 KiB internal task stack** 與約 **9.4 KiB PSRAM workspace**。
另外主韌體有約 3.7 KB 的回應暫存。沒有全張影像的額外副本；相機加密緩衝沿用原流程。
worker 與 workspace 配置成功後保留到重啟供下次重用，非每輪重複配置；因此用過 pipeline 後 internal free 會低於首次使用前。
結束／斷線會等正在運算的工作完成，再清除 pending workspace；不強制刪掉正在跑密碼運算的 task。
這裡的清除指受管理的工作區，並非證明 CPU、task stack、Python immutable bytes 等所有位置都能安全抹除。

## Wire format 與驗證條件

這是在 proto=5 下新增明確能力，不改既有命令格式。舊 Host 仍能使用原流程。
Host 明確選 pipeline 但裝置沒有能力時直接拒絕，不偷偷改模式。

新命令為 `CAMERA_PIPELINED\n`，後面接 4-byte 大端長度與 payload，一次 Host write。

| payload 欄位 | 長度 | 用意 |
|---|---:|---|
| action | 1 | 0 無控制變更、1 START、2 POLL、3 INSTALL、4 COMMIT |
| capture | 1 | 1 要影像；0 僅允許達上限後等待準備 |
| active epoch | 4 | 必須等於 ESP32 目前 epoch |
| 已完成張數 | 4 | 必須等於 ESP32 目前 count |
| data | 0／32／1088 | challenge、ciphertext 或 commit token，依 action 精確檢查 |
| request HMAC | 32 | 綁定 domain 與以上所有欄位，使用目前 session key |

capture=1 的回應仍為狀態行＋metadata／nonce／ciphertext／tag 四個 framed 欄位，最後增加一個 framed extension。
capture=0 為 `PENDING\n`＋extension。`writeResponse` 統一組裝；不新增獨立 READY。

extension body 的 type：0 EMPTY、1 BUSY、2 OFFER、3 CONFIRMED、4 FAILED。
OFFER 包含 context、公鑰、簽章、keygen_ms、sign_ms；CONFIRMED 包含 proof、decap_ms、worker stack minimum。
body 尾端再接 32-byte HMAC，綁定 reply domain、該筆 request 的 10-byte header 與 body。
COMMIT 的請求用舊 key 認證，回應 extension 與影像使用新 key；Host 在收到並驗證前不能宣告切換成功。

context 為 72 bytes：

```text
SHA256(active KEM public key || active KEM ciphertext || active epoch:u32)  [32]
active epoch:u32                                                         [4]
next epoch:u32                                                          [4]
fresh Host challenge                                                    [32]
```

所有下列 domain 都包含結尾 NUL byte，Python 與 C++ 一致：

```text
DSA message   = "esp32-only/pipeline-auth/v1\0" || context || pending public key
confirm proof = HMAC(next key, "esp32-only/pipeline-confirm/v1\0" || context || pending public key || ciphertext)
commit token  = HMAC(next key, "esp32-only/pipeline-commit/v1\0" || context)
request MAC   = HMAC(active key, "esp32-only/pipeline-request/v1\0" || header || data)
reply MAC     = HMAC(reply session key, "esp32-only/pipeline-reply/v1\0" || request header || body)
```

通用 DSA_SIGN 禁止簽署新的 pipeline-auth 保留 domain，避免它成為偽造裝置認證的簽章工具。
Host 驗證固定信任公鑰、完整 context、簽章、新公鑰與新 secret 不重用，再確認新 key。
ESP32 只有在 count==limit 且 commit token 正確時才能切換；切換後 count 歸零、epoch 加一。
Host 原有 GCM metadata 與 CameraReplayGuard 繼續檢查影像 epoch、frame_id、count 和 rekey flag。
同一 commit 再送一次會因 epoch／count／HMAC 不符而拒絕；等待階段重複的 POLL 沒有生成影像或啟用金鑰的作用。

這仍是**Host 認證 ESP32**。commit 證明對方持有新 session key，不等於 ESP32 已信任 Laptop 的持久身分。
沒有改 ML-KEM／ML-DSA 演算法，不代表此自訂協定取得 NIST 認證或完成正式安全分析。

## 出錯時如何處理？

- 配置失敗、非法長度、狀態錯誤、HMAC／commit 失敗：板端清除 session 並關閉 TCP；不啟用 pending。
- Host 驗章／確認／影像驗證失敗：停止該次執行；不自動放寬安全檢查。
- 一般 TCP timeout／斷線：既有 supervisor 建立新連線、重新認證；新的 Host pipeline 物件不攜帶舊 pending。
- 第 N 張時未完成：只有等待控制封包，沒有第 N+1 張舊金鑰影像。
- commit 已到板端但回應丟失：Host 不猜測 epoch，重連後建立新 session。
- epoch 接近 32-bit 上限：拒絕 pipeline，避免 wraparound。
- 正常測試結束在某個 epoch 中途：收尾時丟棄尚未使用的下一組，再執行既有簽章測試與 RESET。

## 怎麼跑最短而有用的測試？

1. 兩台上傳主線 `firmware/esp32_pqc_demo/esp32_pqc_demo.ino`，WiFi、Flash 與 PSRAM 設定維持你已確認可用的設定。
2. 先跑 pipeline 雙台 60 秒，rekey-every 仍是 10。IP 換成當下地址，信任設定不必改。

```powershell
python -u -m host.multi_camera --device camera1=192.168.1.111 --device camera2=192.168.1.112 --seconds 60 --rekey-every 10 --rekey-mode pipeline --display
```

3. 同一韌體、同一網路與場景，跑一次 blocking 對照：

```powershell
python -u -m host.multi_camera --device camera1=192.168.1.111 --device camera2=192.168.1.112 --seconds 60 --rekey-every 10 --rekey-mode blocking --display
```

不傳 `--rekey-mode` 也是 blocking。切回 blocking 不必重燒；每輪正常收尾或重連都會清除 pending。
本原型 pipeline 只支援 TCP record 模式，interval>=3；較小間隔與 UART 繼續用 blocking。
這個限制是原型排程需要，不是宣稱「3 張」具有密碼學安全門檻。

先看有沒有驗證錯誤／重啟與記憶體不足，再判斷效能。若有明確改善，再做一次短的斷線重連驗收。
先不用連續跑很多輪或長時間抓包；遇到停頓／錯誤再保留對應 Monitor、JSONL 與必要 pcap。

## summary.json 要讀哪些欄位？

每台 `devices[].performance`：

| 欄位 | 意義 |
|---|---|
| `pipeline_switches` | 已驗證新 epoch 首張影像的成功切換次數 |
| `pipeline_ready_ahead`／`pipeline_ready_ahead_ratio` | 到上限時下一組已準備完成的次數／比例 |
| `pipeline_worker_stack_min_bytes` | 本輪收集到的最小 worker stack 剩餘量；不是 Host 的 stack |
| `metrics.pipeline_keygen_ms`／`pipeline_sign_ms`／`pipeline_decap_ms` | 板端背景運算時間；由 extension 附帶，沒有新增查詢 |
| `metrics.pipeline_prepare_wall_ms` | Host 開始準備到驗證確認完成的牆鐘時間，包含其間照常傳的影像 |
| `metrics.pipeline_boundary_wait_ms` | 已達舊 key 上限後，等待 pending ready 的時間 |
| `metrics.pipeline_boundary_to_verified_ms` | 邊界處開始處理換鑰，到新 epoch 第一張影像通過 GCM／順序／extension 驗證的時間 |
| `metrics.verified_interval_ms` | 連續驗證影像的間隔；比較 P95 與 max，包含換鑰與其他停頓 |
| `total_avg_fps` | 包含啟動與收尾的全程 FPS；terminal 的串流 FPS 定義略有不同 |

一般 metrics 有 n／mean／P95／max／total。背景準備與影像耗時重疊，**不可把所有 total 相加**。
pipeline 的 `boundary_to_verified` 包含新一張影像的請求／傳輸／驗證，與 blocking 的 `rekey_ms` 不是完全相同區間。
公平比較主要看相同 interval 下的每台 FPS、verified interval、錯誤、重連與記憶體。
pipeline 的初次握手仍有 `handshake_timing`；後續換鑰看 `pipeline_*`，不是遺失 rekey 記錄。

## 驗證與限制

本次已通過 **95 項 Host 回歸測試**及 native worker harness；ESP32 Arduino core 3.3.11 完整編譯通過。
本機編譯使用 `esp32:esp32:esp32s3`、DIO、16 MB flash、OPI PSRAM、`huge_app` 分割設定，**沒有上傳到任何裝置**。
這個分割設定只供編譯檢查，不要求你改掉目前已驗證可開機的 IDE 設定。
新的 pipeline 尚未實機量測，不能把前一版五分鐘的成功當成新版本驗收。

Host 測試使用真實 ML-KEM、ML-DSA、HMAC、AES-GCM 與碎片化假 TCP，覆蓋多次 epoch 切換、慢準備、
錯 context／簽章／MAC／confirmation、commit 斷線、不支援能力與等待逾時。假 TCP 不能證明 ESP32 排程或無線效能。
native worker harness 使用實際 `rekey_pipeline.cpp`、真實本機執行緒，替身 crypto／RTOS，檢查配置失敗、
busy 時 join 清理、非法／重複 commit、成功換鑰及運算失敗清理，並檢查實際使用的 request 門檻規則；
它不驗證密碼演算法，也不等於 ESP32 RTOS 實機測試。

本機測試命令（在 esp32-only）：

```powershell
python -m unittest discover -s tests -q
g++ -std=c++17 -pthread -Itests/rekey-worker-native tests/rekey-worker-native/test.cpp -o "$env:TEMP/pqc-rekey-worker-test.exe"
& "$env:TEMP/pqc-rekey-worker-test.exe"
```

硬體上仍需確認：48 KiB stack 的實際餘裕、internal heap 下降後的網路行為、同核心競爭與 WiFi 尾端延遲。
只有縮短等待而未降低總傳輸量，不保證達到 30 FPS。完整影像捕捉／加密／傳輸流水線、Laptop 身分驗證、
正式協定分析與側通道保護不在這次修改範圍。
