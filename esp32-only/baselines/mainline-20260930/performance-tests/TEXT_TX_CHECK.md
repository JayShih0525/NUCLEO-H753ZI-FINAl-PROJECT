# 文字回覆完整寫入檢查

20260915_201239 三次 TCP 連線皆建立，但 INFO 或 BOOT_INFO 回覆逾時。原本的 io().printf() 繞過 TX 計數且未檢查回傳值，因此命令結束不能證明回覆已完整交給 TCP。

本次把 crypto_demo.cpp 及 protocol.cpp 中格式化的協定文字（包含 INFO、BOOT、MEM、相機狀態、KEM 狀態、ERR、TX_INFO）改走 writeFormatted。使用固定 512-byte 暫存格式化，再交给 writeExact；短寫入續傳，失敗就關閉 TCP 並在 UART 印 [TX_TEXT]。不輸出截斷的文字，超過 511 bytes 就拒絕並關閉連線。原本 writeLine 仍維持原有檢查。

正常回覆內容、換行與命令不變，Host 不需改參數。TX 計數現在也涵蓋這些文字，所以相機命令的 tx_calls 通常會由 5 變為 6、tx_bytes 包含文字狀態；不應把增加誤判成合併失效。TX_INFO 回覆中的統計是格式化當下的快照。

開機確認 `[BUILD] tx-text-checked-v2 coalesce=1 frame_trace=0`。需重新燒錄。固定路由器、位置、供電及原測試指令，先測 60 秒，保存 Monitor 與 trace。若仍失敗，對照同一命令的 TX 結果：完整寫入仍不代表網路已送達，還需看封包。

本次不提高逾時、不省略認證、不解決底層 write 可能阻塞十秒的問題。正常路徑不增加額外 TCP 診斷文字。失敗訊息只到 UART。

原生 C++ 測試直接編譯 protocol.cpp，驗證格式與換行、部分寫入續傳、零進度失敗關閉、過長文字不傳送、BOOT 回覆格式。這不是 ESP32 Arduino 工具鏈編譯或實機驗收。
