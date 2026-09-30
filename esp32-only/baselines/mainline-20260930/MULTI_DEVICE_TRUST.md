# 每個程序選擇自己的裝置信任公鑰

> 2026-09-16：主線預設檔名已改為 `host/camera1.pub`，下方 `trusted_device.pub` 是舊檔名。現已新增 [N 台共用啟動器](MULTI_CAMERA.md)，不用手動開 N 個 terminal；公鑰隔離原則不變，雙向 Host 認證仍未加入。

不必再編輯 device_auth.py。預設已恢復第一台 host/trusted_device.pub；第二台使用 host/camera2.pub，既有檔案未覆蓋。--trust-key 可用於相機 Host、WiFi 文字 Host、UART Host 與 benchmark。

在 esp32-only 執行，IP 請依每台 Monitor 當前輸出替換：

```powershell
# 第一台
python performance-tests/run_benchmark.py --host 192.168.1.122 --trust-key host/trusted_device.pub --seconds 60 --rekey-every 10 --memory-every 10 --display
# 第二台
python performance-tests/run_benchmark.py --host 192.168.1.123 --trust-key host/camera2.pub --seconds 60 --rekey-every 10 --memory-every 10 --display
```

每個程序載入指定的公鑰，不在所有公鑰中任意尋找能通過的一把。公鑰錯誤／缺失就停止；沒有自動信任或降級。IP 是連線目的地，不是身分證明。

相機程序在啟動前讀取公鑰 bytes，固定用於首次握手、每次 rekey、重連及最後摘要簽章。重連不重新讀取可能被修改的檔案；要換信任需結束後重新啟動。每個 SerialProtocol 保存自己的公鑰，各程序的 TCP、session、epoch、replay guard 分開。

可在兩個 PowerShell 視窗各執行一條命令，形成兩個獨立接收程序，benchmark 會各自建立結果資料夾。這不是雙台整合面板，尚未加入共用裝置清單／自動探索／整合啟動器；雙台同時吞吐仍待實機驗證。先分別跑通再同時測，IP 改變只改 --host，公鑰不需重新登錄。

新增裝置仍用 enroll_device --host IP --output host/cameraN.pub --fingerprint 已確認的指紋。不要串接公鑰或覆蓋原裝置檔案。
