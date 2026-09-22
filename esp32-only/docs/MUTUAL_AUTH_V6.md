# 雙向身分驗證 v6

更新：2026-09-22。主線已實作，待雙台實機驗收。凍結單台副本未修改。

## 修改前後

原本 Host 驗證 ESP32 的持久 ML-DSA 公鑰，但任何能連到 ESP32 的人都能建立自己的 KEM session 取像。v6 的 WiFi 入口只允許未認證者查 INFO、取得公開裝置公鑰或進行雙向握手；影像、控制、診斷、通用簽章與 echo 都須先通過 Host 認證。

裝置探索照常公開，公告不等於授權。WiFi 舊版 AUTH_KEM、KEM_DECAPSULATE、CONFIRM_SESSION 等入口一律拒絕，避免繞過新增握手。UART 留作原有實體接線實驗，未加入 Host 存取限制；本版不是保護不可信實體存取的方案。

## 本機準備與操作

已在這台 Laptop 建立 `.host-identity/identity.json`（含私鑰）、同目錄 `identity.pub`，並輸出 `firmware/esp32_pqc_demo/host_trust.h`（只有公鑰）。這兩處已加入最外層 .gitignore。Host 私鑰目前是本機明文檔，依賴 Windows 帳戶／檔案權限，未使用 TPM 或密碼加密。

**不要刪除或分享 identity.json，不要把私鑰燒到 ESP32。** 再執行以下指令會沿用既有身分並重建公鑰 header，不會默默換鑰：

```powershell
python -m host.host_identity
```

把主線 `firmware/esp32_pqc_demo/esp32_pqc_demo.ino` 重新燒錄到兩台。沿用已確認能啟動的 Flash／PSRAM 設定，不清除 NVS。開機應顯示：

```text
[AUTH] mutual-v6 required; trusted_hosts=1
[PIPELINE] worker reserved stack_bytes=49152 before discovery
```

若缺少 host_trust.h，仍可編譯，但 trusted_hosts=0、所有網路 Host 握手均被拒絕；沒有自動信任第一位連線者的後門。

接著與上一版使用相同參數：

```powershell
python -u -m host.multi_camera --discover --seconds 60 --resolution qvga --rekey-every 10 --rekey-mode pipeline --display
```

每台 terminal.txt 應有 `[PASS] Mutual ML-DSA identity and bilateral session-key proof verified`，JSONL 有 `mutual_auth_verified`。summary 的 `handshake_total_ms` 及 `handshake_mutual_auth_ms` 可做前後比较。這次初始握手在診斷及相機設定之前，避免未認證者執行控制命令。

新版 WiFi 啟動不再執行舊的 SELFTEST 命令，因為它會覆寫 session buffer；雙向握手、實際 GCM 取像與串流結尾 DSA 正負向檢查仍執行。不要把「未執行 SELFTEST」誤認為自我測試通過。

要授權其他 Laptop，可把它的 identity.pub 作為 `--additional-public-key 路徑` 加入，最多四把公鑰，再重燒兩台。白名單是建置設定，不會因原有 NVS 裝置身分而自動增加。移除／更換 Host 公鑰需重建白名單並重燒；新韌體不保留被移除 Host 的權限。不得在未知網路上自助註冊 Host。

## 握手細節

1. `MUTUAL_BEGIN` 的單筆 payload 是 Host 公鑰 SHA-256（32 bytes）、隨機 Host nonce（32）、rekey limit（4，big-endian）。ESP32 必須在本機白名單找到該指紋。
2. ESP32 產生新的 KEM keypair 及裝置 nonce，建立 1316-byte context：Host 指紋、裝置指紋、Host nonce、裝置 nonce、1184-byte KEM 公鑰、rekey limit。用裝置 DSA 私鑰簽署 `esp32-only/mutual-device/v1\0 || context`，回傳 nonce、公鑰、簽章。
3. Host 先用已固定的裝置公鑰驗章，成功後才 encapsulate。Host 簽署 `esp32-only/mutual-host/v1\0 || context || ciphertext`；另用共享金鑰計算 `HMAC-SHA256(key, esp32-only/mutual-host-key/v1\0 || context || ciphertext)`。
4. `MUTUAL_FINISH` 單筆 payload 為 ciphertext（1088）、Host 簽章（2420）、Host key proof（32）。ESP32 的 challenge 只能使用一次，30 秒過期；檢查 Host 簽章、decapsulation 與 key proof 全部通過才授權這條連線。
5. ESP32 回傳 epoch 與裝置 key proof：`HMAC-SHA256(key, esp32-only/mutual-device-key/v1\0 || context || ciphertext || epoch)`。Host 驗證後才使用 session。所有 domain 的結尾 NUL 都參與計算。

因此簽章不能搬到另一個 nonce、另一台裝置、不同 KEM 公鑰／ciphertext 或不同換鑰間隔。未知 Host、錯誤簽章／MAC、過期／重用 challenge 會關閉連線並清除 session。重連重新驗證；blocking rekey 重做雙向握手；pipeline rekey 以原有 HMAC 與裝置簽章綁定已授權 session，不在每幀增加 DSA 簽章。

## 範圍與限制

本版處理「誰可以建立影像 session」。pipeline 請求已有 MAC，影像已有 GCM；但並非 TLS，也沒有把每個純文字控制／診斷命令和回覆都包成完整認證通道。具有主動 TCP 修改能力的對手仍可能干擾已授權連線的純文字命令或造成拒絕服務，不能宣稱所有控制流均具完整完整性保護。後续測試工具須明確區分此項與未授權取得影像。

Host 為比較舊基準仍支援 v5 韌體；只有明確出現 v6 的 mutual PASS 才代表雙向認證成功。v6 板端沒有降級到 v5 握手的入口。

目前單次 Host identity 載入會驗證公私鑰配對；這筆耗時也算在 mutual_auth_ms。進一步效能優化應等安全行為與實機量測確認後再做。

## 前後比較與驗收

前版基準目錄改名為 `diagnostics/multi/可內網搜尋`，原名 `20260922_140852_263914`。保留原始 trace 內的歷史路徑不改寫；cam1 468 幀、7.58 FPS、0 斷線，cam2 542 幀、8.58 FPS、1 次重連完成。這是單向認證的資料，不是 v6 驗收。

新一輪固定供電、位置、網路、QVGA、rekey=10、pipeline、display 與 60 秒。比較初始／重連握手、FPS、P95 幀間隔、記憶體最低值、worker stack 最低值。網路差異不可全算成認證開銷。

本機測試涵蓋真实 ML-KEM／ML-DSA／HMAC、分段讀取、雙向成功、未知 Host、修改裝置簽章、修改 ciphertext／Host 簽章／MAC、舊證明重播、重用 challenge、錯誤裝置 key proof、舊 inline KEM 禁用。原生 C++ 測試覆蓋實際入口分類；不是完整板端硬體攻擊驗證。

下一步實機確認合法 Host 能啟動、pipeline 多次換鑰、斷線重連；再以獨立測試身分確認板端拒絕，不能只因逾時就判 PASS。10 月中工具與負向測試完整計畫見 [DISCOVERY_AND_AUTH_PLAN.md](DISCOVERY_AND_AUTH_PLAN.md)。
