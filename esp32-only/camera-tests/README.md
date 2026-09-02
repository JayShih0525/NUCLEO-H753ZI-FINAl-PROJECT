# ESP32-S3-CAM camera tests

這裡先單獨測相機，不混入 ML-KEM、AES 或 ML-DSA，方便確認 OV2640、PSRAM、UART 和 JPEG 本身是否正常。

兩個 Arduino sketch 使用同一組 N16R8 pin map。Arduino 設定沿用 `esp32-only`：`ESP32S3 Dev Module`、16 MB Flash、OPI PSRAM、TTL Type-C。執行時 UART 是 460800 baud。

## 單張照片

燒錄：

```text
camera-tests/photo_camera_test/photo_camera_test.ino
```

關閉 Arduino Serial Monitor，從 `camera-tests/host` 執行：

```powershell
..\..\..\.venv\Scripts\python.exe capture_photo.py --port COM5 --output captured_photo.jpg --preview
```

韌體收到 `CAPTURE` 後會拍一張 SVGA JPEG，格式是 `CAM1 + frame_id + jpeg_length + jpeg`。

## 連續串流與錄影

燒錄：

```text
camera-tests/stream_camera_test/stream_camera_test.ino
```

錄 10 秒 AVI：

```powershell
..\..\..\.venv\Scripts\python.exe record_stream.py --port COM5 --seconds 10 --output camera_recording.avi
```

不開預覽視窗：

```powershell
..\..\..\.venv\Scripts\python.exe record_stream.py --port COM5 --seconds 10 --output camera_recording.avi --no-display
```

串流版先使用 QVGA、JPEG quality 15、雙 frame buffer。PC 顯示實際接收 FPS，錄影檔使用 MJPEG AVI。這是 UART 相機 baseline，確認成功後再把 JPEG 接入 AES-GCM。

## 2026-08-28 實機結果

- 單張照片：成功取得 800x600 JPEG，36,970 bytes。
- 串流錄影：460800 baud 下，5.05 秒收到 70 張 320x240 JPEG，平均 13.87 FPS。
- AVI 檔可由 OpenCV 重新開啟，70 frames、320x240，第一張可正常解碼。
- 目前板子燒錄的是串流版韌體。
