# WiFi 省電確認與顯示接收分離

更新日期：2026-09-18。適用主線 TCP 影像模式，隔離的單台 baseline 未修改。

## 修改目的

最近雙台測試可達約 23／28 FPS，但仍有偶發連線等待與長幀間隔。這次保留 TCP、加密與 pipeline 換鑰協定，先減少顯示對接收的阻塞，並限制單筆命令的等待時間。這些修改不保證消除 WiFi 重傳。

## 三項變更

1. 韌體仍以 `WiFi.setSleep(false)` 關閉省電，新增 `esp_wifi_get_ps` 查詢。每次網路就緒、啟動服務時輸出一次 `[WIFI] power_save`，沒有增加逐幀查詢或網路封包。預期 `actual=0 query_error=0 verified=1`；其他值需要檢查，不可視為關閉成功。
2. TCP 串流開啟 `--display` 時，主執行緒負責 OpenCV 視窗，背景執行緒負責接收、驗證、JPEG 解碼與換鑰。容量一張的共用槽只保留最新已驗證影像；顯示來不及時覆蓋舊影像，不略過任何接收端的安全檢查。每台仍有自己的 Host 程序。
3. 新增 `--response-timeout`，預設 10 秒，TCP 建連採此逾時，每筆命令的傳送與所有回覆欄位也共用此總期限。零星收到資料不會刷新期限。超時關閉整條連線，經既有重試退避後重新認證與建立 session，絕不在半個加密封包後接續解析。簽章、GCM 與協定驗證失敗仍停止，不自動忽略。

## 畫面與退出

等待資料超過約一秒會在最後一張影像上顯示 `Waiting for data`，進入重試則顯示重新連線／認證狀態；第一張影像前使用空白底圖。Q、Esc 或關閉視窗會要求接收端停止。接收等待期間約每 0.2 秒檢查取消；建連與傳送階段仍可能等到設定期限才返回。Ctrl+C 也會觸發清理。

## 紀錄解讀

`camera_verified`、解碼及換鑰紀錄維持原本意義。非同步顯示不再寫入舊的逐幀 `display_timing`，避免誤認為它仍阻塞接收。指定 diagnostics 時，結束會另存 `trace_display.json`（依 trace 檔名變化），多台 summary 會將其放入各裝置 `performance.display`。

- `shown`：實際送進視窗的不同影像數。
- `skipped`：被較新已驗證影像覆蓋的待顯示影像數，不是丟包數。
- `imshow_total_ms`：imshow 呼叫累計時間，不包含 GUI 事件等待，不能直接與以前 display_ms 比較。

驗證 FPS 和顯示 FPS 是不同數據；提高前者不代表視窗每幀都呈現。

## Host 斷網後的舊連線清理

Host 關閉 WiFi 時，ESP32 不一定能收到 TCP FIN/RST。單連線服務若持續等待舊 Host，就可能使新 Host 雖然建立 TCP，卻一直收不到 INFO。板端新增 30 秒命令活動期限：接受連線、開始與結束命令時更新時間，期限內沒有新活動就主動關閉舊 TCP。這不需要額外心跳封包。

Monitor 會輸出 `[TCP] idle timeout ... closing stale client`。原本協定主迴圈隨後清除 session 與待換鑰狀態，重新接受連線並產生 KEM 金鑰；持久化 DSA 身分不變。這是對半開連線的防護，仍需實機驗證是否解決目前重連失敗。

恢復測試請使用 180 秒，播放約 20 秒後關閉 Host WiFi 5 至 10 秒再開啟，兩台不按 Reset。總測試時間包含斷線等待；板端可能需要等待剩餘閒置期限，再加上 Host 的重試間隔。測試結束後直接重跑 Host，確認不需 Reset 也能再連線。

## 使用與短測

兩台重新燒錄目前韌體以取得省電確認訊息；原本 multi_camera 指令即可使用新的非同步顯示。範例 IP 請換成當時的位址：

```powershell
python -u -m host.multi_camera --device camera1=192.168.1.111 --device camera2=192.168.1.112 --seconds 60 --rekey-every 10 --rekey-mode pipeline --display --response-timeout 10
```

先做一次正常 60 秒測試，比較 FPS、verified_interval、換鑰與 display 統計。另做一次短暫關閉再開啟熱點的恢復測試，確認視窗保留、重新認證後繼續顯示；勿同時修改換鑰間隔或影像品質。10 秒不是最佳值的結論，先保守保留以免把短暫重傳放大成頻繁重連。
