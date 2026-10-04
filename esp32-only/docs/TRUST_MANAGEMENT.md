# 信任管理操作（2026-10-04）

這一階段沿用 v7 通道，不更改線上封包、韌體白名單格式或 NVS 裝置身分。

| 管理方向 | 儲存位置 | 變更何時生效 |
|---|---|---|
| 筆電信任哪些 ESP32 | devices.json、裝置 .pub；撤銷表 .trust/revoked_devices.json | 新的認證／讀取信任檔時檢查；操作前停止串流，再啟動驗收 |
| ESP32 信任哪些筆電 | firmware/esp32_pqc_demo/host_trust.h | 修改後編譯並重新燒錄目標 ESP32 才生效 |

**管理前先按 Q 停止 live，也停止其他 camera demo。** 這不是遠端即時踢除功能，不保證已建立的 session 在某個時間內終止。需要立即停止時先結束 Host 串流；Host 白名單改動須完成目標板燒錄。所有已信任筆電的串流權限相同，尚未加入遠端管理員角色。

管理指令不主動連線、不修改 WiFi、不刪除公鑰或私鑰；本機寫入採暫存檔加原子替換，管理程序使用互斥鎖。不要同時用編輯器改信任檔。

## 1. 列出目前設定

在 esp32-only 執行：

```powershell
python -m host.trust_manage device-list
python -m host.trust_manage host-list
```

device-list 顯示名稱、檔案、指紋、enabled 與 revoked；缺失公鑰會顯示 error。enabled=false 只是暫停啟動，並不等於撤銷。host-list 讀的是**本機準備燒錄的白名單**，不能證明板上已更新。

## 2. 新增 ESP32

先以可信的實體 Monitor 核對裝置指紋，沿用 enroll_device 取得公鑰；再註冊到 devices.json。下方 IP 與指紋需換成真實數值。

```powershell
python -m host.enroll_device --host 實際IP --output host/camera3.pub --fingerprint 實際64位指紋
python -m host.trust_manage device-add --name camera3 --public-key host/camera3.pub --fingerprint 實際64位指紋
```

新增條目使用 host=auto、port=9000、enabled=true；live 或 multi_camera --discover 會依身分探索。既有名稱、同公鑰的另一個名稱、指紋不符及已撤銷身分都拒絕加入，不自動取代舊設定。existing devices.json 有失效公鑰時，先修正再新增，避免不完整檢查。

## 3. 撤銷／恢復 ESP32：不需燒錄

```powershell
python -m host.trust_manage device-revoke --fingerprint 實際64位指紋
python -u -m host.live_camera
```

live 應顯示該台已撤銷並 SKIP，其餘合法裝置仍可執行。multi_camera 是嚴格批次入口，選取含撤銷身分時會報錯；也可明確選取其餘裝置。直接 pqc_camera_demo --trust-key 或 .pub 副本亦受相同指紋撤銷表限制。

恢復時先停止 Host，再核對公鑰及指紋：

```powershell
python -m host.trust_manage device-restore --public-key host/camera2.pub --fingerprint 實際64位指紋
python -u -m host.live_camera
```

原公鑰及設定保留，不需重新 enroll。重新 enroll 或改檔名不會自動撤銷拒絕紀錄。撤銷表格式損壞／無法讀取時拒絕認證，不當作空表。首次不存在撤銷表時視為尚未撤銷任何身分。

邊界：這是本機信任政策，不防止本機管理者刪除政策、修改程式或使用凍結 baseline。其他筆電有各自的信任設定；在此撤銷不會同步到它們。

## 4. 新增／撤銷筆電：需重新燒錄

另一台筆電先用 host.host_identity 建立自己的身分，提供 **identity.pub 公鑰**及經其他可信方式核對的指紋；不要傳 identity.json 私鑰。

```powershell
python -m host.trust_manage host-add --public-key laptop2.pub --fingerprint 第二台筆電的64位指紋
python -m host.trust_manage host-list
```

檢查後重新編譯、燒錄需要信任它的 ESP32，保留 NVS。最多四把是目前韌體的設定限制，不代表只能有四個 ESP32。

撤銷筆電：

```powershell
python -m host.trust_manage host-revoke --fingerprint 要撤銷筆電的64位指紋
```

再次編譯、燒錄目標板後，它才會拒絕該筆電的下一次 MUTUAL_BEGIN；Monitor 應看到 `[AUTH] rejected reason=HOST_NOT_TRUSTED`。不能只憑連線逾時判定驗證成功拒絕。未重燒的板仍信任原白名單。

不允許刪除最後一把 Host 公鑰。要更換唯一筆電：先新增新筆電、公鑰燒錄並確認新筆電能認證，再撤銷舊筆電並重燒。若沒有保留的可信筆電可操作，只能經實體燒錄修復白名單；不要清除 NVS 裝置私鑰。

host_identity 重新執行會保留既有 header 中的其他筆電；若目前筆電已被移出 header，它會拒絕默默恢復自己，需以 host-add 明確加入。管理指令只解析程式生成的 header 格式，遇自訂 C++ 內容拒絕覆寫。

## 5. 短測順序

1. 列出兩台 ESP32，記下 camera2 指紋；停止 Host。
2. 撤銷 camera2，啟動 live：camera2 被跳過、camera1 可出圖。
3. 停止後恢復 camera2，再啟動：兩台恢復出圖。這三步不改韌體，可先做。
4. 板端 Host 撤銷獨立測試：需第二台筆電身分，或另外建立**測試身分和獨立 header**，避免覆蓋現用身分。先確保有可恢復的原筆電公鑰，再依第 4 節進行新增、燒錄、撤銷、重燒及觀察 Monitor。

本機測試包含指紋不符、不當新增／恢復、改名公鑰、壞政策檔、寫入失敗、最後一把防護、四把上限、setup 保留名單，以及真實 ML-DSA／ML-KEM 的模擬握手。模擬板會載入移除後的清單驗證原 Host 被拒、保留 Host 可認證；仍不等同燒錄後的實機驗收。

命令替換設定時，`--config`／`--header` 放在子命令之前，例如 `python -m host.trust_manage --config other/devices.json device-list`。所有主線設定共用本專案的 .trust 撤銷表；沒有命令列跳過撤銷的選項。
