# 凍結基準：單台 ESP32 WiFi v1

建立日期：2026-09-16。來源 commit 與檔案 SHA-256 見 `snapshot.json`。

這是目前單台 WiFi 加密影像的完整原始碼副本。後續雙向認證、多裝置及效能改進都在 `esp32-only/host/`、`esp32-only/firmware/` 主線進行，**不回寫此資料夾**。這是可執行基準，不是穩定版認證：目前仍存在間歇 WiFi／握手／重連停頓。

## 包含什麼

- `firmware/esp32_pqc_demo/`：完整 sketch、相機、PQC、協定、WiFi、vendored KEM 與授權。
- `host/`：完整 Host 相依模組、影像／文字／登錄入口，不匯入外面的主線模組。
- `tests/`：同版本 Python 與 C++ native 回歸測試。
- `requirements.txt`：同版本 Python 依賴。
- 本機的 `wifi_config.h`、`trusted_device.pub`、`camera2.pub` 已複製，Git 忽略。其他電腦 clone 後不會有它們，需自行設定／可信登錄。

沒有複製 `.venv`、Arduino core 或外部 MLDSA44 library。原始碼獨立，但工具鏈與安裝的依賴仍由本機提供；要避免將來依賴升級影響此基準，可在基準外另建專用 venv，依 requirements 安裝。實際板型及已成功的 Arduino core／MLDSA44 library 版本仍需另行保存。

## 操作

從 `esp32-only` 執行，先啟用目前可用的 `.venv`。以下 IP 必須換成 Monitor 當前值：

```powershell
./baselines/single-device-wifi-v1/run_camera.ps1 -DeviceIp 192.168.1.102 -Seconds 60
# 第二台公鑰也有保留，但此入口每次只操作指定的一台
./baselines/single-device-wifi-v1/run_camera.ps1 -DeviceIp 192.168.1.103 -TrustKey host/camera2.pub -Seconds 60
```

IP 是範例，不是固定設定。可加 `-NoDisplay` 關閉視窗、`-RekeyEvery 10` 指定輪替、`-Python C:/完整路徑/python.exe` 使用特定環境。`TrustKey` 相對路徑以本基準目錄為準；啟動器自動切換工作目錄，執行前會比對雜湊。

紀錄寫入 `esp32-only/diagnostics/single-device-wifi-v1/時間戳/`，不寫回此副本。record 模式不保存 AVI。依既有行為，Seconds 不是嚴格總牆鐘期限，網路操作與收尾可能超時。

若需燒錄此基準，Arduino 必須開啟**本資料夾內** `firmware/esp32_pqc_demo/esp32_pqc_demo.ino`，使用副本自己的 `wifi_config.h`。主線韌體未來更改協定後，不能假設仍相容此 Host；回到基準測試時需燒錄配對的基準韌體。NVS 身分／信任資料並不包含在原始碼副本中，不能把這份副本當作 Flash 備份。

## 驗證與保護界線

```powershell
./baselines/single-device-wifi-v1/verify_snapshot.ps1
# 以下測試須先 cd 到本基準目錄
python -B -m unittest discover -s tests -p 'test_*.py'
```

`snapshot.json` 核對建立時的程式、測試、依賴清單與說明；不收錄本機公鑰、WiFi 密碼或自動生成紀錄。它用來偵測意外變更，並非防惡意修改的數位簽章。`baselines/AGENTS.md` 要求後續開發不得修改既有基準；未設唯讀 ACL，使用者仍可自行修改檔案。

只有需要新的基準時才另建 v2，不同步更新 v1。請保留外部測試證據及實際使用的環境版本。
