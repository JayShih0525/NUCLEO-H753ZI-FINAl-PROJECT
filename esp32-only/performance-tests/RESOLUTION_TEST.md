# 串流解析度比較

更新日期：2026-09-18。主線新增 `--resolution qvga|vga|svga`，支援單台 `host.pqc_camera_demo` 與多台 `host.multi_camera`，預設仍為 QVGA。隔離 baseline 未修改。

| 參數 | 解析度 | 相對 QVGA 像素數 |
| --- | --- | --- |
| qvga | 320 × 240 | 1 倍 |
| vga | 640 × 480 | 4 倍 |
| svga | 800 × 600 | 6.25 倍 |

串流 JPEG quality 固定 15；PHOTO 模式維持 SVGA、quality 12，不用 PHOTO 模式作為串流效能比較。像素倍數不是 JPEG 大小或 FPS 的固定倍數，結果也受畫面內容、曝光與網路狀況影響。

兩台上傳更新的韌體一次，之後可透過 Host 參數切換，不需要每換解析度就重燒。非預設模式使用 `CAMERA_MODE STREAM VGA` 或 `CAMERA_MODE STREAM SVGA`；仍是一筆設定與回覆，沒有增加逐幀封包。設定在建立 session 前執行，每次重連都重新套用。舊韌體不支援時會報錯，不會假裝已切換。

範例（IP 依實際網路修改）：

```powershell
python -u -m host.multi_camera --device camera1=192.168.1.111 --device camera2=192.168.1.112 --seconds 60 --rekey-every 10 --rekey-mode pipeline --display --resolution vga
```

下一輪只將 `vga` 改成 `svga`。保持相機位置、場景、光線、quality、換鑰間隔與網路不變，不刻意斷網。先各跑 60 秒；不要同時調整恢復期限。

TXT 每幀列出實際尺寸；JSONL 的 `camera_configuration` 保存要求的解析度與 quality，`camera_timing` 保存每幀實際寬高。多台 summary 的啟動指令包含解析度參數，並已有 FPS、JPEG bytes、拍攝加密、接收、解碼與換鑰等待統計。檢查輸出尺寸確實為 640x480 或 800x600，再比較速度；高解析度實測尚待完成。
