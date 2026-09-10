# DSA 裝置認證第一版

本次變更新增持久身分、明確公鑰登錄、每次 KEM 交換的挑戰簽章，以及裝置持有 session key 的 HMAC 確認。這是單向裝置認證：PC 認證 ESP32；尚未限制哪些 PC 可以連線。

## 變更與相容性

- 韌體 INFO 版本升為 proto=4；新增 AUTH_KEM、CONFIRM_SESSION。
- ESP32 首次啟動在 NVS 的 pqc-identity / identity-v1 保存一份 DSA 公私鑰 blob。重啟重用，存入失敗、格式損壞或公私鑰不匹配時停止初始化，不靜默產生新身分。
- Host 每次 establish_session（含 rekey）都驗證挑戰與 KEM key 簽章，不回退到不認證模式。
- 正常結束的 transcript 簽章也改用已登錄公鑰驗證。
- 原 DSA_SIGN 保留，但拒絕認證保留前綴，防止它變成可替攻擊者偽造 AUTH_KEM 回應的簽章服務。
- 舊 camera-tests 不變；原 dsa-validation 可繼續執行。
- 新 Host 不相容舊韌體，需要重新燒錄。不得拿舊版 FPS 直接當新版效能，每 10 幀的換金鑰現在多了 DSA 證明與確認傳輸。

## 首次使用

1. 用原 QIO Flash／OPI PSRAM 設定重新編譯、燒錄整合 sketch。保留目前韌體設定的 baud，Host 需一致。
2. 在實體可信任的 USB TTL 連線上開 Serial Monitor，按 Reset，記錄 `DEVICE_ID_SHA256` 後面的 64 位十六進位字串。等 PQC-DEMO READY。
3. 再 Reset 一次，確認指紋相同。關閉 Monitor。
4. 在 esp32-only 下執行（以實際指紋替換佔位字串）：

```powershell
python -m host.enroll_device --port COM3 --baud 921600 --fingerprint "貼上64位指紋"
```

公鑰保存於 host/trusted_device.pub。登錄要求指紋匹配，且不自動覆寫已有信任檔。信任建立依靠當下實體連線與人工確認，從不可信網路取得同一份指紋沒有認證價值。

5. 執行原加密 Host 命令，先測短流程，不直接開始 50 分鐘：

```powershell
python -m host.pqc_camera_demo --port COM3 --baud 921600 --mode record --seconds 60 --output authenticated_test.avi
```

每次 session 建立應出現：

```text
[PASS] Device identity and fresh KEM proof verified: ...
[PASS] Device possession of session key confirmed: epoch=...
```

如果遇到 SESSION_ACTIVE，原異常清理限制仍存在，先手動 Reset；這次沒有把未知邊界的自動重連混入認證修改。

## 線上格式

AUTH_KEM：Host 送文字命令，裝置 READY，Host 送 32-byte 隨機 challenge 長度封包；裝置 OK，送 1184-byte KEM public key 與 2420-byte DSA signature，均使用原長度封包。

簽章訊息固定為 `ASCII("esp32-only/auth-kem/v1") + NUL + challenge[32] + kem_public_key[1184]`。Host 使用已登錄 DSA public key 驗證。另一連線／交換的新挑戰不能使用舊簽章；沒有持久的 challenge 資料庫，依賴 256-bit 隨機值的新鮮性。

Host 隨後執行原 KEM 封裝與解封裝。CONFIRM_SESSION 再送新的 32-byte challenge，裝置回應 32-byte HMAC-SHA256：

`HMAC(shared_secret, ASCII("esp32-only/confirm/v1") + NUL + challenge[32] + kem_pk[1184] + kem_ct[1088] + epoch[4,BE])`

Host constant-time 比對通過才從 establish_session 回傳。確認不增加影像訊息計數，不使用 GCM nonce。這是此研究協定的 key confirmation，不宣稱是標準 TLS 或形式化驗證過的完整 AKE。共享秘密目前仍直接用於 AES 與 HMAC，後續 key schedule 應做用途分離。

## 測試與驗收

新增單元測試（不接板）：

```powershell
python -m unittest discover -s tests -p test_device_auth.py -v
```

覆蓋原 proof、舊 proof 配新 challenge、替換 KEM key、錯誤 DSA key、簽章修改／截斷、跨 domain 簽章、登錄指紋不符、缺少信任檔、confirmation 欄位綁定與錯誤 proof 拒絕。

實機必驗：

- 首次開機與再開機指紋一致。
- 錯誤指紋登錄失敗且不建立檔案。
- 缺少信任檔時認證失敗，沒有回退。
- 60 秒串流至少跨一次 rekey，每次均有身份與 key confirmation PASS。
- 修改／替換信任檔為另一個合法公鑰後無法建立 session（先備份原公鑰）。
- 在隔離測試環境重播舊 challenge proof 被新 challenge 拒絕。
- DSA_SIGN 發送保留 domain 開頭的合法長度訊息回覆 ERR RESERVED_AUTH_DOMAIN。
- 既有 DSA 20 項回歸測試仍符合預期。

## 尚未完成與安全邊界

這不是完整憑證系統；沒有 CA、有效期、撤銷列表或 Host 身分認證。可信公鑰檔需要受本機存取權限保護。NVS 存的是原始私鑰，未新增 Flash Encryption／Secure Boot；實體 Flash 讀取與修改不在此版本防護範圍。全清 Flash／更換 partition 可能清掉身份，必須明確重新登錄。

此版本阻止新挑戰接受舊認證 proof，沒有完成影像 frame 防重播、UART 邊界復原、DoS 防護或低停頓 rekey。原裸 KEM／控制命令仍存在供測試，裝置不是對未授權 Host 封鎖服務。Host 的認證成功才接受 session 是本版保證的範圍。

啟動時與正常結束後的自測功能不等於安全握手。不要在進行中的 session 插入 SELFTEST（現有自測使用共用工作陣列）。先前 50 分鐘成功是舊版 baseline，新版需重新驗證。
