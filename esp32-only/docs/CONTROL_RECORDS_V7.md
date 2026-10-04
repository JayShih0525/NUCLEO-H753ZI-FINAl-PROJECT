# v7 控制通道規格與驗收

本文件說明 2026-10-04 的第 1 項改動，不是階段進度報告。當日已完成雙台 pipeline、blocking 與斷網恢復短測，使用者也確認停止後能立即再啟動。第 2 項已另加入本機信任管理，操作及尚待實機的撤銷驗收見 [TRUST_MANAGEMENT](TRUST_MANAGEMENT.md)。

## 起因與保護範圍

v6 已有雙向 ML-DSA 認證、ML-KEM、session key proof、影像 AES-GCM 及 pipeline 保護，但一般文字控制命令及回覆沒有統一的完整性與序號驗證。v7 在成功的 MUTUAL_FINISH 之後，把全部應用位元組放入驗證記錄，包括 CAMERA_MODE、RESET_SESSION、診斷命令、文字狀態及二進位長度／資料。必須驗證完整記錄，才交給既有命令或回覆解析器。

未認證的 INFO、GET_DSA_PUBLIC_KEY 與首次握手維持既有格式。公開資訊不是信任依據。v7 的四個握手 domain 從 `/v1\0` 改為 `/v2\0`，使版本由簽章／key proof 綁定；只修改 INFO 無法讓 v7 板子通過舊版握手。live 要求 v7；有限時間工具仍可測試 v5/v6，其較舊安全範圍不變。

## 記錄格式

所有整數 big-endian，每個方向獨立序號，第一筆為 0。

| 欄位 | 長度 | 內容 |
|---|---:|---|
| magic | 4 bytes | `PQR7` |
| session ID | 16 bytes | 由該次完整握手派生 |
| sequence | 8 bytes | 必須恰等於預期值，不接受重複、跳號；不允許繞回 |
| payload length | 4 bytes | 1～4096 |
| payload | 上欄指定 | 原本文字／二進位串流的一段 |
| tag | 32 bytes | HMAC-SHA256(header + payload) |

以共同 KEM secret 為 HMAC key：`seed = SHA256(context + KEM ciphertext) || epoch_u32`；分別計算 `HMAC(secret, "esp32-only/records/v1\0" || label || seed)`。label 為 `session`、`host`、`device`，session 取前 16 bytes，其餘各 32 bytes 作雙向記錄金鑰。context 包含双方身分雜湊、雙方隨機 nonce、KEM 公鑰與換鑰間隔。

這層提供完整性與來源驗證，**不加密控制文字**；影像仍由原本 AES-GCM 加密。對手仍可看見封包大小、時序與控制文字，也仍可阻斷連線，不能宣稱消除拒絕服務。

## 換鑰與失敗

- Pipeline 的 image key／epoch 照原門檻輪替，記錄層是獨立的 connection key，期間持續計數，**不隨每次 pipeline image rekey 輪替**。這個區別也必須納入未來金鑰外洩實驗；記錄金鑰外洩後，單純 image rekey 不會恢復控制通道安全。
- 初次完整握手的最後 proof 仍是原格式；Host 驗證 proof 後與板端一起啟用記錄層。blocking rekey 的握手與最後 proof 由舊記錄層保護，完成後切換新 session ID／金鑰／序號。不能丟棄尚未消費的記錄資料來強制切換。
- RESET_SESSION 只清除影像 session，不移除連線的記錄保護。只有關閉連線才回到初始握手狀態。
- MAC、session、序號或長度錯誤：拒絕資料、停止該連線，不降級到明文。Host 當作協定驗證失敗停止該台；傳輸逾時／斷線仍依原流程重新探索／完整認證。
- 板端尚未收到記錄時仍可每秒回到主迴圈；已收到部分記錄則給最多 10 秒組裝期限，逾時關閉，不把半包拿去解讀。既有命令、傳輸與 stale client 期限仍存在。

## 成本與檔案

不新增網路來回。每個記錄增加 64 bytes；4096 bytes 滿載時約 1.56%，小命令比例較高。每筆需 HMAC；板端新增兩個 4160-byte 固定缓衝區及少量 key/state，不配置完整 JPEG 副本。這是安全補強，尚未用實機量測 v7 的 FPS／CPU／heap 影響。

Host：`host/secure_records.py`、`mutual_auth.py`、`serial_protocol.py`、`pqc_host_demo.py`、`pqc_camera_demo.py`。板端：`secure_records.h/.cpp`、`protocol.h/.cpp`、`crypto_demo.cpp`。既有 baseline 未修改。

## 本機檢查與板端短測

本機：`python -B -m unittest discover -s tests -q`。Windows C++ 交叉驗證：`python -B tests/run_record_native.py`（需 MinGW g++，使用 Windows BCrypt 真實 HMAC 比對 Python）。後者也執行實際 protocol.cpp 的截斷、重播、明文插入、idle timeout 與文字寫入測試。韌體另外由 Arduino CLI 編譯；本機通過不等同實機驗收。

1. 保留既有身分、WiFi 與 Host trust 設定，重新燒錄主線 firmware/esp32_pqc_demo。**不要清除 NVS 或重建金鑰。** 開機應顯示 `mutual-v7 records required`，INFO 應為 `proto=7`。
2. 先用原 multi_camera 指令跑雙台 60 秒、QVGA、pipeline、rekey=10。確認有影像、換鑰持續成功，trace 有 `secure_records_active`。用同環境舊版數據比較 FPS／停頓，不能用不同日期網路狀況下的最高 FPS 當基準。
3. 同一台再跑 30 秒 `--rekey-mode blocking`，確認完整握手第二次之後仍正常。
4. 跑 live，短暫斷網再恢復、按 Q 結束後重新啟動，確認不需 Reset。

以上通過再開始第 2 項。若失敗，保存該次 trace 與 Monitor；無需一開始就安排長時間測試或混入信任資料修改。
