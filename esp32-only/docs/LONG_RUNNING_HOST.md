# 長期使用入口與重新探索（2026-09-30）

9/30 的長期入口只改 Host。10/4 主線新增 v7 控制通道，現在使用此入口須重新燒錄 v7；保留既有 NVS 身分與信任檔，所有 baseline 不修改。見 [v7 規格與验收](CONTROL_RECORDS_V7.md)。

## 啟動與設定

在 esp32-only 執行 `python -u -m host.live_camera`，或雙擊 start_live.cmd（使用專案 .venv）。沿用 devices.json、各台 .pub 與 .host-identity，不複製 baseline 私鑰、不重做登錄。2026-10-01 更新：只啟動 enabled=true（省略時為 true）的裝置，enabled=false 不讀取其公鑰。每台設定獨立檢查，缺檔／格式錯誤顯示 [SKIP]；名稱或公鑰身分重複時跳過全部衝突項目，其餘合法裝置照常啟動。live 的 host 欄位不作連線地址，仍透過探索尋找；因此不以舊 IP 判斷衝突。與有限時間 multi_camera --discover 的選擇方式不同。選取及跳過原因存 diagnostics/live/startup.json，每次啟動覆寫；有跳過錯誤時，合法裝置仍運行，但最後回傳非零退出碼。無合法啟用裝置或整份 JSON 解析／結構錯誤則停止。

不用提供 IP 或秒數。每台獨立搜尋；找不到就等待重試，其他台仍可串流。Q / Esc 或關閉任一影像視窗會停止全部，terminal Ctrl+C 也可停止。網路操作有期限，停止可能需等待探索或初次 socket connect 返回；超過收尾期限才強制結束，並提示摘要可能不完整。單台身分／協定驗證失敗會停止該台並在 terminal 顯示原因，不無限重試驗章失敗。

可將 live_settings.example.json 複製為 live_settings.json 後修改解析度等參數。預設 QVGA、rekey=10、pipeline、display=true、normal 紀錄。沒有以放寬換鑰間隔換 FPS。長期入口要求 mutual-auth v7 與記錄層，不接受舊版韌體。一般模式不額外查詢 MEMORY_INFO；需要效能比較及記憶體資料請使用原 multi_camera 測試入口。

## 重連與安全邊界

啟動 live 模式先探索，再固定所選裝置公鑰進行既有握手。傳輸錯誤後先重試原 IP；累計兩次傳輸失敗後重新探索。找到同一可信指紋的新地址才更新 IP / port，但公告仍非認證證據。每次 run_once 都重新驗證裝置與 Host，重新建立 key、epoch、replay guard；不恢復半包或沿用舊 session。

缺少公告或本機網路暫時不可用會重試。相同指紋公告多個地址視為歧義並停止，不隨機選擇。這仍不能避免惡意公告造成拒絕服務；不把 mDNS 當安全通道。

multi_camera 的有限時間子程序也自動帶 --rediscover，兩次 TCP 失敗後會重新探索。它的初次 --discover 仍需所選裝置全部被找到，不同於 live 的各台獨立等待。直接 pqc_camera_demo 可用 --rediscover 啟用；有限時間與既有診斷輸出維持。

## 顯示與容量

畫面有搜尋／連線認證／重連狀態；超過一秒未發布新驗證影像，顯示 last verified Ns ago。這是 Host 收到最後驗證影像的年齡，不是感測器拍攝到螢幕的延遲。

live 使用 diagnostics/live/裝置名稱/ 固定目錄，不每次建立新時間戳。terminal.txt、trace.jsonl、reconnect.jsonl 各自預設最大 2 MiB，加 3 份備份：每台約 24 MiB 上限，加小型 summary/display JSON。設定限制對單筆過大內容採省略標示。normal 只留每秒 FPS 與必要事件；diagnostic 保留詳細 trace，但仍輪替，舊事件可能被覆蓋。不要用輪替 trace 的殘餘內容推算完整測試百分位數。

summary.json 在本次正常收尾時保存本次所有已驗證幀數、含搜尋／重連的總平均 FPS、握手次數與嘗試錯誤數。計數器記憶體固定，不保存無限樣本陣列；沒有輸出 P95。啟動時先標記 running；強制終止時不得將其視為完整摘要。只允許一個 live launcher 使用這組固定紀錄，避免兩次開啟互相覆蓋。

原 multi_camera 保留完整逐幀記錄與離線精確統計，尚未設容量限制，因此仍適用有限時間測試。長期記錄輪替不會清除之前其他 diagnostics 資料夾。

## 建議一次驗收

1. 正常啟動 live，兩台顯示影像。
2. 關閉手機熱點約 10 秒，再開啟；不按板子 Reset、不重啟 Host，觀察重新認證恢復。
3. 按 Q 停止全部，確認各台 summary 的 termination 與幀數。

手機分到相同 IP 也能驗證斷網恢復，但不算實際換 IP 驗收；地址變更、歧義、驗章錯誤停止等分支先由本機測試覆蓋。10/4 已另加 v7 控制通道，Host 公鑰撤銷方式仍留待下一階段。
