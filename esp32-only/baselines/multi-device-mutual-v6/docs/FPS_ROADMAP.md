# FPS 改善方向備忘

2026-09-16。目標接近 30 FPS，但不要透過放寬 rekey 間隔掩蓋停頓。先固定解析度、JPEG 品質、rekey 次數、供電、網路及測試版本，比較每台 FPS、P95 幀間隔、最長停頓與錯誤率。

| 方向 | 能解決什麼 | 現在的建議 |
|---|---|---|
| 傳輸／協定等待 | receive_ms、request_to_status_ms、往返及背壓 | 第一優先，先處理握手／讀寫／重連期限，量測後減少往返。已有小 frame 合併與非阻塞 TX，不代表等待已消失 |
| Rekey pipeline | 同步重新認證與換鑰造成的畫面停頓 | 保留可調間隔，之後提前準備下一個 session；需要額外記憶體及清楚的金鑰切換邊界 |
| RTOS | 讓相機、加密、傳輸有機會重疊 | 專案已用 FreeRTOS。下一步是有界佇列／雙緩衝，不是單純多開 task；必須處理 buffer 所有權、背壓、session 切換及丟幀規則 |
| DMA | 降低相機資料搬運所需 CPU 工作 | 相機驅動已有 DMA 路徑，不能當成從零加入。PSRAM DMA 是否啟用需查實際 core／driver 編譯設定，再做 A/B 測試；不會直接加速 DSA 或 WiFi 等待 |
| Interrupt | 及時通知資料完成或事件 | ISR 保持短，只通知 task；不要在 ISR 內跑 KEM、DSA、JPEG 或等待網路。加中斷不是 FPS 加速開關 |
| Clock／cycle | 處理 CPU 計算瓶頸，或研究相機輸出速率 | CPU 時脈與 camera XCLK 不同。現行 XCLK=20 MHz；CPU 實際頻率應由板端確認。先量每段計算耗時，再考慮調整；不要盲目提高 Flash／PSRAM／XCLK |

目前 camera 配置已有兩個 frame buffer 和 GRAB_LATEST；再增加 buffer 可能增加 RAM 或延遲，並非保證增加有效影像幀率。CPU cycles 只描述執行計算的成本，秒級 socket 等待不能直接解釋成密碼計算太慢。

後续安全優化須保持：同一 key 的 nonce 不重複、每張影像明確綁定 session／epoch、驗證失敗不能展示、緩衝不可在傳送前被覆寫。降低停頓與改變安全邊界要分開設計與測試。

建議順序：**可靠收發 → 雙台隔離驗收 → 定位 rekey／傳輸成本 → 有界 pipeline → DMA／時脈實驗**。N 台啟動器只隔離 Host 工作，不會自動提高單台 FPS。

官方依據：

- [Espressif esp32-camera](https://github.com/espressif/esp32-camera)：frame buffer、連續擷取及 ESP32-S2/S3 的 PSRAM DMA 選項。
- [Espressif camera HAL](https://github.com/espressif/esp32-camera/blob/master/driver/cam_hal.c)：driver 的 DMA／buffer 路徑；實際版本需與本機相符。
- [ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)：排程與不可阻塞的執行區段限制。
