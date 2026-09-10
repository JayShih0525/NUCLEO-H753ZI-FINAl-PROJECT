# ML-DSA-44 獨立驗證

本資料夾新增診斷工具，不修改既有 Host、測試或韌體。每項檢查顯示在 terminal，並自動保存 UTF-8 TXT 與 JSON 至 `results/`，檔名含模式與時間。不保存私鑰；公鑰只記錄 SHA-256 指紋。

## 執行

使用裝有本專案依賴的 Python 環境。從 `esp32-only` 執行：

```powershell
# 完全本機測試，不需要板子
python -u dsa-validation/verify_dsa.py

# 板子使用既有整合韌體；關閉 Serial Monitor 與相機 Host
python -u dsa-validation/verify_dsa.py --mode device --port COM3 --baud 921600
```

依賴不足時，在選定的環境執行 `python -m pip install -r dsa-validation/requirements.txt`。直接執行腳本不要求目前目錄為 host，也不需要 OpenCV。

device 模式會開啟序列埠並調整 DTR/RTS；部分板子可能因此重置。建議在相機長測之前獨立執行，先等板子 READY。工具使用 INFO、GET_DSA_PUBLIC_KEY、DSA_SIGN；不建立 AES session、不更改 rekey interval、不送 RESET_SESSION。開始前若板子留有上次傳輸殘留，先手動 Reset。測試完成後建議再 Reset，讓 30 分鐘影像測試從明確基準開始。

## 20 項檢查

| 項目 | 數量 | 預期 |
|---|---:|---|
| 原始 32-byte 摘要 | 1 | ACCEPT |
| 摘要首／中／末 byte 翻轉一個 bit | 3 | REJECT |
| 訊息追加／截斷 | 2 | REJECT |
| 簽章首／中／末 byte 翻轉一個 bit | 3 | REJECT |
| 簽章截斷／追加／空值 | 3 | REJECT |
| 換成另一組有效公鑰 | 1 | REJECT |
| 公鑰截斷／追加 | 2 | REJECT |
| 另一私鑰簽的資料搭配原公鑰 | 1 | REJECT |
| 另一簽署者搭配自己的公鑰 | 1 | ACCEPT |
| 重複驗證原本有效資料與簽章 | 1 | ACCEPT，展示沒有防重播 |
| 1-byte 與 4096-byte 合法訊息 | 2 | ACCEPT |

PASS 表示符合預期，不是每項都驗證成功。Malformed input 的 ValueError 記為格式拒絕，和密碼驗證回傳 false 分開記錄；其他未預期例外直接標示 ERROR，不冒充成功的負向測試。

竄改發生在 Host 記憶體，不是攔截 UART 或注入硬體傳輸錯誤。負向資料不送入韌體，避免打亂既有協定。4096 bytes 是現有 DSA_SIGN 的訊息 buffer 上限。

## 協作與限制

local：Python 產生 DSA keypair、簽章、驗證。

device：ESP32 提供啟動時產生的 DSA 公鑰；Host 送測試訊息；ESP32 用私鑰簽章；Host 用 pqcrypto 驗證與做竄改測試。另一組錯誤公鑰及對照簽章在 PC 產生。

簽章與公鑰、訊息共同滿足驗證關係；改訊息後，原簽章不再符合。簽章不是加密訊息，也不是用公鑰解密私鑰加密過的摘要。

工具簽署 SHA-256 摘要 bytes，和現有相機流程一致；這是將摘要作為普通 ML-DSA 訊息，不宣稱使用標準 HashML-DSA 模式。另以原始短訊息／4096-byte 訊息檢查介面。

此測試不完成憑證、持久身分、可信公鑰佈署、防重播或雙向裝置認證。公鑰仍從同一 serial 連線取得。既有韌體沒有公開任意 DSA_VERIFY 命令，因此 device 模式是 ESP32 sign → PC verify，不是 PC sign → ESP32 verify，也不是完整標準合規或側通道測試。

目前相機流程由 Host 累積 transcript、送摘要給 ESP32 簽，並非 ESP32 自行核對整份影像紀錄。

## Rekey 和 DSA 的關係

現有成功訊息數達門檻後：清除舊共享秘密 → 清除／重產 KEM keypair → Host 取得新公鑰並 encapsulate → ESP32 decapsulate → 新共享秘密作為新 AES key，epoch 增加。DSA 身分 key 不隨這個 rekey 輪替；它與通訊 KEM keypair 是不同用途的金鑰。

## 結果判讀

正常結束應為 `SUMMARY: 20/20 PASS`，exit code 0。任何 FAIL／ERROR 回傳非零。TXT 適合閱讀，JSON 可供後續統計，包含每項預期、實際結果與驗證耗時。verify_ms 只量測 PC 驗證，不含 UART 傳輸；device 顯示的 elapsed_ms 來自韌體簽章回應。

確認 device 模式的實際結果後，再做 30 分鐘串流。local 通過不能替代 device 通過；DSA 通過也不能證明上次 UART 短讀問題已修復。
