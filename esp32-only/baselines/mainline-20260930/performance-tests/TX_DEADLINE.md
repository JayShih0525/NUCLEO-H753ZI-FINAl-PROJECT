# 非阻塞 TCP 寫入 v3

第二台 20260915_215212 的第一幀在板端 write 累計阻塞約 20 秒；Host 十秒後已放棄。舊版外層兩秒無進度檢查無法中斷底層 WiFiClient.write 的等待。

本次 transport 使用既有 WiFiClient 的 fd，單次 send(MSG_DONTWAIT) 嘗試，不在底層自行重試。EAGAIN/EWOULDBLOCK/EINTR 交回外層等待；其他 errno 印 TCP_SEND 並關閉連線。writeExact 保留未完成的 offset，兩秒無進度或單次 writeExact 五秒總期限就印 TX_ABORT、更新 TX failure 並關閉 TCP。這是每個 writeExact 的期限，不是整幀／整輪五秒期限；RTOS 排程與清理仍可能有額外耗時。

正常 bytes 與密碼學不變。短寫入計數現在也包含非阻塞背壓返回零，不宜直接與舊版次數比較；要看 failures、TX_ABORT、成功串流及重連狀態。

setup 的 BOOT 改用 localOnly 路徑輸出 Serial；Host BOOT_INFO 仍走檢查過的 TCP 路徑。開機不應再因尚無 client 印 TX_TEXT write failed。

開機版本：[BUILD] tx-nonblocking-v3 coalesce=1 frame_trace=0。需要重新燒錄；先測第二台，使用 Monitor 的當前 IP 與既有 60 秒命令，固定位置與供電，不刻意斷網。保存 Monitor、trace、pcapng。若頻繁 TX_ABORT，不代表硬體已證實故障，只表示阻塞有被及時識別；下一步再與第一台交換相同供電／位置進行控制比較。

參考官方 NetworkClient 實作：https://github.com/espressif/arduino-esp32/blob/master/libraries/Network/src/NetworkClient.cpp 。本機原生測試驗證單次非阻塞、背壓、部分成功、致命錯誤關閉、兩秒無進度、五秒總期限，以及開機不寫 TCP。尚未使用實際 Arduino ESP32 工具鏈編譯或做硬體驗收。
