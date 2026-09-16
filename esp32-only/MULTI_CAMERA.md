# N 台 WiFi 接收入口（2026-09-16）

主線新增 `host/multi_camera.py`，一台電腦對 N 台 ESP32，每台一個既有相機 Host 程序。沒有寫死兩台，也沒有改動韌體協定、密碼演算法或隔離基準。本版尚未加入 ESP32 認證 Laptop 的雙向認證。

## 每次連線用參數指定 IP

在 `esp32-only` 啟用 `.venv` 後執行。以下 IP 是範例，換成当前位址：

```powershell
# 只檢查清單與公鑰，不連線
python -m host.multi_camera --device camera1=192.168.1.102 --device camera2=192.168.1.103 --dry-run

# 同時接收兩台，之後可繼續追加 --device camera3=IP
python -u -m host.multi_camera --device camera1=192.168.1.102 --device camera2=192.168.1.103 --seconds 60 --rekey-every 10 --display

# 同一入口也能只接收一台
python -u -m host.multi_camera --device camera2=172.20.10.13 --seconds 60 --display
```

有 `--device` 時，只啟動列出的裝置，並使用參數中的 IP，無須因換熱點而修改設定檔。未提供 `--device` 時，使用設定中 enabled=true 的項目與 host；目前範例全部 disabled，避免誤連線。

`devices.json` 已在本機建立，Git 忽略；首次 clone 可由 `devices.example.json` 複製。它只保存裝置名称與公鑰配對等設定。公鑰相對路徑以 JSON 所在資料夾為準，不是 terminal 當前位置。

```json
{
  "devices": [
    {"name": "camera1", "host": "REPLACE_WITH_CAMERA1_IP", "port": 9000, "trust_key": "host/camera1.pub", "enabled": false},
    {"name": "camera2", "host": "REPLACE_WITH_CAMERA2_IP", "port": 9000, "trust_key": "host/camera2.pub", "enabled": false}
  ]
}
```

新增第 N 台：先可信登錄取得獨立 `.pub`，再加入清單，之後用 `--device 名稱=IP` 啟動。每台必須不同身分，不要複製另一台的私鑰。程式會拒絕重複名稱、相同 host/port、相同公鑰、缺失公鑰及尺寸錯誤。別名解析成同一 IP 不在靜態檢查範圍，請勿用兩個不同名稱連到同一塊板。

主線原本預設 `trusted_device.pub` 已依本機改名更新為 `camera1.pub`；多裝置入口一律明確指定公鑰，不使用自動尋找或失敗降級。副檔名實際是 `.pub`，內容是 1312-byte ML-DSA 公鑰。

## 程序與紀錄隔離

每台獨立 TCP、信任 bytes、session、epoch、rekey、replay guard 與 WiFi supervisor。啟動前驗證所有選定設定；啟動時固定公鑰快照至該輪紀錄，子程序再沿用現有認證流程。

```text
diagnostics/multi/時間戳/
  summary.json
  camera1/
    terminal.txt
    device.pub                 本輪公鑰快照，非私鑰
    trace.jsonl
    trace_reconnect.jsonl
    trace_attemptN.jsonl       有重試才產生
  camera2/...
```

terminal 顯示各台啟動／退出狀態，以及約每秒一次的平均 FPS 與 frame 編號。FPS 沿用子程序的累計平均值，不是最近一秒的瞬時 FPS；重連後重新累計，沒有新幀時不重複顯示舊值。完整 PASS、握手、記憶體與錯誤仍寫入各台 terminal.txt。視窗各有名稱，不保存 AVI。某台失敗會留下非零 exit code，其他台持續運行；全部結束後，任一失敗使啟動器回傳 1。

Ctrl+C 通知所有仍在執行的子程序中止，等待最多約 5 秒；若仍卡住則強制結束並等待程序退出，summary 標記 forced_stop。中止回傳 130，不當作成功驗收。中止可能沒有最後摘要簽章／RESET_SESSION；它的目的在釋放本機程序與 socket，下一次必須重新建立 session。

`--seconds` 沿用既有相機計時行為，不是全系統嚴格牆鐘期限。launcher 不盲目重啟驗證失敗的程序，只有相機內原本可重試的 TCP 錯誤由 supervisor 處理。單台 q／Esc 行為沿用舊流程，不影響其他台。

## 驗收界線

本機回歸測試檢查 N 台設定、IP 覆寫、拒絕錯誤設定、獨立公鑰／紀錄、部分啟動失敗清理與強制收尾，另用三個真實本機測試程序確認其中一台失敗不影響其餘兩台。沒有連線 ESP32；同時雙台 FPS、WiFi 穩定性與實際視窗操作仍需實機測試。

支援 N 台是程式無固定台數限制，不保證網路或電腦可無限擴展。先同時兩台短測，再逐台中斷確認另一台不受影響。若目前單台握手仍不穩，先用各台 trace 找出失敗階段，不將多程序啟動成功當作網路問題已修復。
