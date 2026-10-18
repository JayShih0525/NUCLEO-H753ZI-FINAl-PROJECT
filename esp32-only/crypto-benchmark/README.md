# ESP32-S3：ML-KEM / ML-DSA 獨立 benchmark

這份只做密碼演算法量測，跟原本的加密相機程式分開。沒有相機、WiFi、AES、握手、影像傳送或自動換鑰；UART 只用來下測試指令和收結果。PC 不執行密碼演算法，六個操作都在 ESP32 上做。

原本 `firmware/esp32_pqc_demo`、Host、NVS 身分與信任清單不修改。**但把這份燒進板子後，板子就暫時不能跑相機；要恢復相機需重新燒回原本的韌體。**

## 測什麼

| 演算法 | 分開量測的操作 | 輸出／驗證方式 |
|---|---|---|
| ML-KEM-768 | KeyGen、Encaps、Decaps | encaps/decaps 後比較 32-byte 共享秘密；竄改密文後檢查秘密不同 |
| ML-DSA-44 | KeyGen、Sign、Verify | 檢查簽章長度與有效性；竄改簽章後必須拒絕 |
| baseline | 不執行密碼操作 | 觀察計時與記憶體框架底噪，不自動相減 |

每一個樣本都是：準備輸入 → 新任務量測單一操作 → 另一個任務檢查正確性 → 輸出結果。準備和檢查都不在計時區間、也不污染被量測任務的 Stack watermark。每次重新產生所需的隨機金鑰，不是拿一組固定金鑰重複跑。

分兩個 pass：`timing` 不開局部 Heap 監控；`memory` 開啟局部 Heap 低水位監控。正式比較耗時用 timing、記憶體用 memory，不把兩輪混在一起平均。兩輪都記錄時間與 Stack，方便檢查差異。

預設每個操作、每個 pass 暖機 3 次，再記錄 30 次；共 7 × 2 × 30 = **420 筆樣本**，暖機不列入統計。DSA 預設簽原始 32 bytes 測試訊息，並非先做 SHA-256 或 HashML-DSA；可測 0～4096 bytes。簽章採函式庫預設的隨機化 API，rejection sampling 會使時間有變動，不能只看單一數字。

## 1. 安裝與編譯

在專案根目錄 `C:\Users\macho\Desktop\project` 的 PowerShell 執行。沿用現有 `.venv`；若換一台電腦，先安裝 Arduino IDE 和 **esp32 by Espressif Systems**、mldsa 函式庫，以及 Python 虛擬環境。

```powershell
.\.venv\Scripts\python.exe -m pip install -r esp32-only\crypto-benchmark\requirements.txt

.\.venv\Scripts\python.exe esp32-only\crypto-benchmark\build.py --dsa-library "C:\Users\macho\OneDrive\文件\Arduino\libraries\mldsa"
```

這台電腦的 mldsa 是 0.2.0。其他電腦把 `--dsa-library` 換成實際函式庫根目錄，不是 `src`。CLI 若不在預設位置，補 `--cli "完整的 arduino-cli.exe 路徑"`。來源準備後，後續不更動程式時可省略 `--dsa-library`。

建置設定固定為 ESP32S3 Dev Module、240 MHz、16 MB Flash、OPI 8 MB PSRAM、QIO、3 MB app partition、Core 1。適用目前的 S3 N16R8，不適用早期 AI Thinker ESP32-CAM。實際 FQBN 和編譯輸出保存在 `build/build_info.json`、`build/compile.log`。這份使用 IDF 局部 Heap 監控 API，已選用的核心為 3.3.11；舊版核心不保證能編譯。

`prepare.py` 把主線已 vendored 的 PQClean ML-KEM-768，以及指定 mldsa 的完整 `src` 原樣複製到此 sketch 的 generated `src`。不改動上游程式，也不影響已安裝的函式庫。保留原始授權到 `licenses/`，每個來源檔有 SHA-256，並記錄 harness、建置腳本和 Git HEAD；來源指紋跟版本會隨結果保存。Arduino 不會搬 `.inc` 到 sketch 暫存目錄，所以產生 `build_opt.h` 讓編譯器找到原樣的 `zetas.inc`。**建議用 build.py，不要跳過來源準備就直接開 IDE 上傳。**

如果更改 benchmark.cpp 或 build.py，再帶 `--dsa-library` 建置會更新來源指紋。如果更換演算法版本而 generated 副本與原始檔不同，工具會拒絕覆寫；先把本資料夾的 `firmware/esp32_crypto_benchmark/src` 移走備份，再重新準備。請勿刪到主線的 `firmware/esp32_pqc_demo/src`。

## 2. 接板子、燒錄

USB 線接板子的 **TTL Type-C**，不是這份設定下的 OTG 孔。先關閉 Arduino Serial Monitor、舊相機 Host 和其他使用序列埠的工具。

```powershell
# 列出埠，不要把藍牙 COM 當成 ESP32
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v

# COM5 只是範例，換成實際 ESP32 的埠；這一步會取代相機應用程式
.\.venv\Scripts\python.exe esp32-only\crypto-benchmark\build.py --upload --port COM5
```

上傳速度是 **512000 baud**；程式執行後 UART 是 **921600 baud、8N1、無流控**。兩者不同是正常的。若連不上可依開發板方式按 BOOT / RESET，勿隨意把接腳短路。

程式不讀寫 NVS，不會建立、刪除或換掉相機的 DSA 身分；`EraseFlash=none`，不執行全片擦除。固定的 partition scheme 會寫入分割表，**應先確認原相機也使用相同 NVS 分割位置／設定**；不同分割設定不保證原資料仍可見。要完全保險，可先用既有備份流程保存板子 Flash。不要勾選 Erase All Flash。

這份測試產生的金鑰只在 RAM，結果不輸出私鑰、共享秘密或公鑰內容。RF 關閉時以 `bootloader_random_enable()` 啟用 SAR ADC 熵源，避免把無外部熵源時的 RNG 直接當作真隨機；此獨立程式不使用 ADC、相機或 WiFi。它不是身分驗證測試。

## 3. 跑測試並保存

先跑 2 次煙霧測試，確認可完整走到結束：

```powershell
.\.venv\Scripts\python.exe esp32-only\crypto-benchmark\run_benchmark.py --port COM5 --rounds 2 --warmups 1 --message-bytes 32 --label "S3 CAM 短測"
```

再做正式一輪：

```powershell
.\.venv\Scripts\python.exe esp32-only\crypto-benchmark\run_benchmark.py --port COM5 --rounds 100 --warmups 3 --message-bytes 32 --label "S3 CAM 240MHz 32B 第1輪"
```

測較大的原始訊息，換成 `--message-bytes 1024` 或 `4096`。建議相同條件至少獨立跑三輪，板子型號、電源、核心版本和 CPU 時脈一致；每輪可按 RESET 讓啟動條件一致。沒有必要每輪重燒。

工具先送 HELLO，確認是 benchmark 韌體才送 RUN。如果存在本機建置紀錄，也會核對 build_id，防止燒錯版本。埠開啟時不刻意重啟板子，但有些 USB 電路仍會觸發重置；若看不到 READY，按一下 RESET 再執行。

預設 30 秒沒有合法紀錄會中止，總時限 1800 秒；需要時用 `--timeout 60 --total-timeout 3600`。最大 1000 次、暖機最多 20 次。中斷後韌體可能仍在測試，重跑前先按 RESET，避免殘留資料。JSON 損壞、重啟、密碼驗證失敗、重複樣本、少樣本，都會回傳非零結束碼並留下未完成記錄；不要把部分結果當正式成績。

## 保存在哪、怎麼比較

每次建立 `results/日期時間_隨機ID/`，不覆蓋舊結果：

- `raw.jsonl`：接收到的原始序列文字與 PC UTC 時間戳。收到一行就落檔。
- `samples.csv`：逐筆操作、pass、次序、微秒、Stack、Heap 原始值，UTF-8 BOM 可用 Excel 開。
- `summary.csv`：每操作／pass 的 min、mean、median、P95、max、樣本標準差，和最大記憶體觀測值。
- `summary.json`：上述統計、是否完整完成、錯誤原因、測試參數、CPU／Flash／PSRAM、核心／IDF 版本、演算法來源指紋、建置紀錄。
- `report.md`：中文閱讀版，換成 ms／KiB，耗時與記憶體並排。

完全沒收進樣本時只有 raw（若已開埠）及未完成 summary/report，不產生假的數值 CSV。1 筆資料的標準差為空，不偽裝成有足夠樣本。P95 採排序後線性插值；低樣本數的 P95 不適合做強結論。

同一輪的六個操作已直接在 report 表格比較；多輪結果可合成一張 CSV：

```powershell
# 將下面兩個路徑換成真正產生的 summary.json
.\.venv\Scripts\python.exe esp32-only\crypto-benchmark\compare.py "esp32-only\crypto-benchmark\results\第一輪\summary.json" "esp32-only\crypto-benchmark\results\第二輪\summary.json"
```

預設寫到 `results/comparison.csv` 和同名 JSON，可用 `--output` 改名。比較器拒絕未完成結果，並檢查訊息長度、暖機、CPU、核心、來源版本等條件；刻意比較不同設定才加 `--allow-different`，差異條件仍留在 JSON。樣本數不同允許比較，但每組樣本數會保留，不會混成一組平均。

## 記憶體數字怎麼看

**不要只把「公鑰大小 + 私鑰大小」當成演算法 RAM 使用量。** 這裡分開記錄：

1. `primitive_io_bytes`：函式需要的 raw byte 輸入／輸出 buffer 大小，不含 scalar 參數、對齊、任務、演算法暫存。KeyGen 為 KEM 3584 bytes、DSA 3872 bytes；其他操作依實際輸入／輸出計算，DSA Sign/Verify 再加訊息長度。
2. `stack_used_bytes`：獨立任務每次重新建置，用 ESP-IDF 的 byte 單位 Stack high-water mark 估計曾觸寫的 Stack。不是沿用同一任務的累積歷史值。包含切換、計時和框架開銷，沒被寫到的 Stack 區域可能低估；不能當成經形式證明的最小 Stack 大小。
3. `internal_free_before/after/low_bytes`：內部、8-bit 可用 Heap 的前／後／局部最低剩餘值。局部低水位是各 memory region 的低水位加總，不一定同時發生，因此 `before - low` 是系統範圍的最壞觀測指標，不是完全歸屬於這個函式的精確 live allocation peak。
4. `psram_*`：外部 PSRAM 的相同觀測。Internal 和 PSRAM 分開，不把重疊的「總 heap」再加一次。
5. `heap_retained_delta`：before - after，可能正、零或負。跟 transient peak 不同，單筆正值也不能直接斷言是 leak；需觀察多輪趨勢。
6. `internal_largest_after_bytes`：操作後最大連續 free block，輔助檢查碎片化。

每個量測任務預留 **96 KiB 內部靜態 Stack**。KEM / DSA 共用 union buffer，union 約 6.15 KiB，另固定保留 4 KiB 訊息陣列；以 metadata 的 sizeof 值為準。這些在測試前已配置，**不會再出現在操作中 Heap 降幅**，也不是兩種演算法各自的最小需求。其他框架全域變數、TCB、Arduino／RTOS 背景任務仍佔 RAM。

`Stack used + Heap drop + I/O` 可以幫助理解來源，但不是此韌體的總 RAM，也不應直接宣稱是精確演算法峰值。計時包含正常排程／中斷干擾，沒有關閉中斷去美化結果；Heap 也可能受其他系統任務影響。baseline 不相減，避免扣出假精度。

編譯器顯示的 Flash／全域 RAM 是**整份雙演算法測試韌體**，含框架和 Stack 保留量，不是每個演算法的獨立 code size。如果還要比較 Flash 或精確 malloc 歸屬，需要額外做各演算法單獨 build／link map 或 ESP-IDF heap tracing，這份目前不宣稱已完成。

另外，ML-KEM-768（category 3）和 ML-DSA-44（category 2）用途／安全分類不同；本表是在相同板子上量測目前專案選用的版本，不是相同安全強度的替代方案競賽。

## 驗證與目前狀態

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s esp32-only\crypto-benchmark\tests -v
```

測試覆蓋接收、統計、CSV／報告、缺漏／重複／重啟／壞資料／逾時處理、跨輪比較限制。測試中的人造數字只存在單元測試暫存目錄，**不是實機成績**。

本次開發的編譯／實機驗收狀態見 `VALIDATION.md`。在拿到板子的實測前，不填入估計的耗時或 RAM 成績。

方法參考：[Espressif Heap 記憶體 API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/mem_alloc.html)、[Heap 偵錯說明](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/heap_debug.html)、[硬體 RNG 使用條件](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/random.html)。ML-KEM 上游 commit 與授權沿用主線 `UPSTREAM.md`；DSA 實際來源以每輪指紋與隨附授權為準。
