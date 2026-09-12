# 第一版 WiFi 加密訊息

## ESP32 自動恢復 WiFi／TCP 服務

新版在初次 WiFi 等待 20 秒仍未成功時，繼續初始化並背景重試，不再要求 Reset。程式停用底層 autoReconnect，由協定任務的 serviceNetwork 統一重試：初次延後 1 秒，每次呼叫 reconnect 後預留 10 秒連線時間，再加上 1、2、4、8、16、30 秒（封頂）退避。重試持續到熱點恢復，不阻塞在無限 while 內。

WiFi 事件回呼只更新原子斷線計數及原因，不操作 TCP 或密碼。connected() 同時檢查 WiFi 狀態與斷線事件世代，即使熱點快速恢復，也不沿用斷線前的 TCP。協定任務返回後清除舊 session、關閉 client/listener，再於取得 IP 後啟動 listener。底層正在阻塞的函式仍需返回或逾時，並非可即刻中斷所有操作。

啟動／重連成功會顯示 `[WIFI] online ip=... port=9000`。Host 尚未自動重連：當它退出，等板端 online，再用目前 IP 重跑原指令，重新執行 DSA/KEM/金鑰確認。

驗收：重新 Upload 並保留 NVS；先正常跑訊息，再關手機熱點約 30 秒後重開，全程不按 Reset。確認 Monitor 顯示 disconnected、retry、online。重新執行 Host 應完整通過。另測 ESP32 開機時熱點未開，稍後開啟能否自動連入。

原生 C++ 狀態測試位於 tests/wifi-reconnect-native，使用 fake Arduino/WiFi API 編譯正式 transport.cpp，涵蓋初次離線、退避、恢復 listener、清除舊 client 及快速斷線事件；已通過。它不驗證真實 WiFi 驅動、DHCP 或 Arduino 編譯，仍需板端驗收。此節更新取代下方早期版本「WiFi 失聯不自動恢復」的描述。

## 逾時診斷對照

Host JSONL 每輪有 run_id，tcp_connected 保存本機 IP/port 與 ESP32 端點；每條命令有 command_id，tcp_write_complete/error 保存送出長度與耗時，command_sent 代表寫入與 flush 呼叫返回，不代表裝置已收到。沒有記錄訊息明文或金鑰。

Serial Monitor 的 accepted 紀錄帶 conn、peer IP/port；透過 peer 對照 Host 的 local 端點。conn 在重啟後歸零。正常一問一答時 Host command_id 對照板端 cmd；message_index 是應用訊息編號，與包含握手命令的 cmd 不同。若命令遺失或解析失敗，兩端編號可能分歧，這也是診斷線索。兩端時鐘不同，不能直接相減推算單向網路延遲。

板端 begin 記錄 WiFi 狀態、RSSI；end 記錄命令耗時與 tx_calls/tx_bytes/tx_ms/tx_max_ms/tx_short。寫入統計涵蓋 writeFrame 和 writeLine（包括 READY），不包含直接 printf 的狀態行；呼叫一直阻塞時尚不會產生 end 統計。WiFi 斷線事件另記 uptime_ms 與 reason，僅輸出 Serial，不插入 TCP。Serial 診斷有額外成本，不是零干擾量測。

需重新燒錄本次韌體。先開 Monitor 921600、等待初始化，再跑相同 100 次測試；保留整段 Monitor 與 JSONL。固定訊息大小、熱點和換鑰間隔，不調大 Host timeout、不重試。板端 writeLine 現在也補送短寫，送不完則關 TCP；其餘 timeout 保持現有設定。

事件 API 參考：[Espressif WiFi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html)。本次 Host 10 項回歸測試通過；Arduino 編譯與事件輸出需實機驗收。

目的：先讓電腦與單台 ESP32 透過同一手機熱點的 TCP，完成既有 DSA 身分驗證、ML-KEM session 建立、AES-GCM 雙向加密訊息及參數化 rekey。相機 Host 目前仍使用 UART；此版本不宣稱已完成 WiFi 影像、多裝置或自動無線重連。

## 燒錄與設定

1. 電腦與 ESP32 連同一個 2.4 GHz 熱點。熱點必須允許裝置間通訊。
2. 在 firmware/esp32_pqc_demo 使用 wifi_config.h；若不存在，從 wifi_config.example.h 複製，填入自己的熱點名稱與密碼。PQC_USE_WIFI=true 啟用 TCP，false 保留 UART。沒有設定檔時預設 UART，不自動改變傳輸模式。wifi_config.h 已被 Git 忽略。
3. 重新編譯並 Upload 原 esp32_pqc_demo.ino，保留原 QIO／OPI PSRAM 設定及 NVS。Host 端沿用已登錄的 trusted_device.pub，不透過未信任 WiFi 自動登錄公鑰。
4. Serial Monitor 設為 921600，找 `WIFI TCP ready ip=... port=9000`，等待密碼初始化完成。WiFi 連線最多等 20 秒，失敗時印出 WIFI_CONNECT_TIMEOUT，修正設定後 Reset。
5. 從 esp32-only 執行，以實際 IP 替換下例：

```powershell
python -u -m host.pqc_wifi_demo --host 192.168.1.100 --message "Hello encrypted WiFi" --exchange-count 20 --rekey-every 10
```

成功會看到 Device identity、session key confirmation、encrypted WiFi echo 的 PASS，以及最後 `Authenticated WiFi encrypted message test passed.`。測試不用開 COM；取得 IP 後，ESP32 可只接電源。TCP 斷線會讓 Host 停止並關 socket；韌體回到等待連線時清除 session，新連線重新產生 KEM 金鑰。

## 為什麼增加 AES_ECHO

舊 AES_DECRYPT 測試會把解密結果明文回傳；AES_ENCRYPT 測試要求 Host 先送明文。它們適合驗證密碼互通，不能直接宣稱訊息全程加密。因此 WiFi 模式拒絕這兩個命令，改以 AES_ECHO 接收密文並以另一個 nonce 加密回覆。

Host 的 AAD 綁定用途、epoch 與訊息編號；使用同一 AAD 驗證回覆，並拒絕與請求相同的 nonce，避免把原請求反射成回覆。文字控制訊息、狀態、握手公鑰與長度仍可見；加密的是訊息內容，不是所有流量。Host 顯示成功前會核對明文與 session 狀態。

## 模組

- transport.h/.cpp：選擇 UART 或單一 TCP client，WiFi 連線與等待新 client。
- protocol.cpp：相同長度封包格式改經 transport 收發；TCP 不呼叫不同 Arduino core 版本語意有差異的 flush。
- crypto_demo.cpp：共用密碼流程、新增 AES_ECHO、TCP 連線間清除 session。
- host/tcp_connection.py：提供 read／readline／write，使用 sendall，沿用 SerialProtocol 的分段組包。
- host/pqc_wifi_demo.py：獨立文字驗證入口，共用現有裝置認證與加密函式。

Arduino API 參考：https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html

## 驗證及限制

6 項本機測試通過：真實 AES-GCM 加密回覆、舊訊息回覆拒絕、反射拒絕、TCP 碎片組包、EOF 停止及例外關閉。測試使用模擬 socket，尚未在此環境編譯 Arduino 或完成實體熱點驗收。

目前同時只服務一個 TCP client，UART 不同時接受協定命令。ESP32 尚未認證 Host；能到達 TCP port 的其他裝置仍可能操作服務或造成阻塞。控制命令沒有全面認證，未新增 DoS 防護。WiFi 的傳输加密並不取代既有 DSA 信任建立，也不是完整 TLS 協定。

共用 KEM 秘密的金鑰用途分離與板端重播防護尚待後續設計；此版本先提供可測試的基本功能。WiFi 失聯不自動延續舊 session。獨立 wifi/testWifi 保留為連線範例，其中原本的真實密碼仍需在提交前自行移除或改成本機設定引用。
