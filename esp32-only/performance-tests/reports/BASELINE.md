# 2026-09-13 改善前基準

使用使用者提供的外部供電 WiFi 五分鐘紀錄 camera_20260913_200602_431876.jsonl。統計可由 analyze_trace.py 重建，數值見 baseline_20260913.json。

正常完成 2790 幀，無 recovery。相鄰驗證幀間隔平均 107.50 ms、P95 313 ms、最大 4796 ms，間隔換算 FPS 約 9.30。此數值不是含啟動與收尾的整輪 FPS。

板端狀態 elapsed_ms 平均 1.92 ms、P95 2 ms、最大 3 ms，僅涵蓋原韌體捕捉到加密等前置處理，不涵蓋整個傳輸及換鑰。這份舊 trace 無新增的 decode/display/rekey 分段事件，因此不能據此分配剩餘時間，也不能判定最長停頓的根因。

下一輪用 --profile 補足 Host 分段。先比顯示開／關，維持 rekey-every=10 與 memory-every=10；此階段不宣稱已提高 FPS。
