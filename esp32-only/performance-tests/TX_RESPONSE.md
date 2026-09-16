# 合併回應 v4

主線 build marker：`tx-response-v4`。需要重新燒錄；baselines 不變。

`protocol::writeResponse(text, frames, count)` 將文字與各 binary frame 的四位元組長度／內容串成原本完全相同的位元組流，透過固定 4096-byte stack buffer 批次 writeExact。大型 JPEG 不額外配置整張副本；部分写入仍由原本 writeExact 續傳／期限處理，失敗關閉連線。期限仍是每次 writeExact，並非整筆回應總期限。

套用於相機狀態＋metadata＋nonce＋ciphertext＋tag、AUTH_KEM 的 OK＋公鑰＋簽章、CONFIRM_SESSION 的 OK＋proof。READY 仍在讀取 Host 資料前送出，不合併跨越雙方互等的協定階段。writeLine 的文字與換行亦合併。

不改 Host parser、rekey 間隔或驗證。增加約 4 KiB 暫存 stack 與一次資料拷貝；應觀察 PQC stack minimum。相機原本的逐 frame TX label 不經此路徑，仍保留整體 TX 統計與 Host 分段計時。完整握手證明約 3.6 KiB，在無背壓時一次底層寫入；相機回應依總長度批次，不保證一個 TCP segment。

本機 C++ 模擬驗證大小回應位元組一致、跨 buffer 邊界、部分寫入及無進展失敗；未完成 Arduino 工具鏈編譯或實機速度驗收。下一輪沿用雙台60秒、rekey=10、不刻意斷網，對比舊基準和 handshake_timing；不預先宣稱 FPS 提升。
