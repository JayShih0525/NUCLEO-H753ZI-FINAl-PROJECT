# Rekey inline v5

目標是縮短初始握手及每次 rekey 的同步等待。主線需重新燒錄，build marker 為 `rekey-inline-v5`。INFO 回覆 `proto=5 inline_rekey=1` 時，Host 才使用新版 WiFi 請求。UART 宣告 inline_rekey=0，保留 READY 流程；舊版 Host 的原命令在新版韌體也保留。

每個新版請求是一次 Host write：ASCII 命令＋換行＋4-byte 大端長度＋payload。

| 命令 | payload | 回應 |
|---|---|---|
| AUTH_KEM_INLINE | 32-byte 新挑戰 | OK＋KEM 公鑰 frame＋DSA 簽章 frame |
| KEM_DECAPSULATE_INLINE | 1088-byte KEM ciphertext | KEM_OK 狀態行 |
| CONFIRM_SESSION_INLINE | 32-byte 新挑戰 | OK＋32-byte proof frame |

各階段不再先回 READY，每輪少三個同步往返；階段之間的依賴仍保留。Host 先驗證 DSA 再 encapsulate，確認共享秘密後才取圖。未改 ML-KEM 輸入 bytes、演算法、認證 transcript、換鑰間隔與 GCM；不代表取得 NIST 認證。

長度／狀態錯誤會回 ERR、清除 session 並關閉新版 TCP 連線，防止尚未消耗的 payload 成為下一條命令。拒絕或逾時後不在相同 byte stream 自動重試舊格式。舊版相容選擇只根據連線初始 INFO；此能力宣告不是經認證的安全協商，兩種格式均保持同樣密碼驗證。

Host trace 的 `handshake_timing.wire_mode` 標記 `inline-v5` 或 `ready-legacy`。新版不記不存在的 `*_ready_ms`；`auth_response_ms`、`kem_response_ms`、`confirm_response_ms` 改從傳送合併請求前開始計時，包含送出及回應等待。跨版本應比較 handshake total／rekey total，不把舊 response 欄位直接與新版視為相同區間。

多裝置啟動命令不變，原本的同筆回應合併仍有效。燒錄後跑雙台60秒、rekey=10，使用結束後 summary.json 比較；隔離基準不變。

驗證：86 項 Host 測試通過，包含完整 inline 握手、分段接收、一次寫入、版本能力、錯誤輸入／簽章拒絕；三組既有 C++ transport/protocol native harness 通過。尚無實際 Arduino 完整編譯與實機 v5 驗收，native harness 未覆蓋整個 crypto_demo.cpp。
