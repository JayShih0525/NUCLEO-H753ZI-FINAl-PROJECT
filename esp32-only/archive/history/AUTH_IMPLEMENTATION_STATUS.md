# 裝置認證第一版驗證狀態

本次執行 `python -m unittest discover -s tests -p test_device_auth.py -v`，10 個測試全部通過，實際執行耗時 0.007 秒（這是整組測試的本機時間，不是板子握手效能）。使用 Python 3.14、pqcrypto 0.4.0 的獨立測試依賴。沒有連接 COM3。

通過項目：原認證證明、舊證明搭配新挑戰被拒絕、替換 KEM 公鑰、錯誤 DSA 身分、修改／截斷簽章、跨 domain 簽章、登錄指紋匹配／不匹配、缺少信任檔、確認資料欄位綁定、錯誤確認 proof 拒絕。

Python 語法與 git diff 空白檢查通過。本機尚未找到可用 Arduino CLI／ESP32 core 編譯環境，因此 C++ 變更尚未完成實際編譯；NVS 持久化、AUTH_KEM、CONFIRM_SESSION、DSA_SIGN 保留 domain 拒絕仍需在板子驗收。

先前 local/device 各 20 PASS，以及 50 分鐘串流，驗證的是修改前版本，不能當作本次韌體變更的實機成績。操作與限制見 esp32-only/AUTHENTICATION.md。
