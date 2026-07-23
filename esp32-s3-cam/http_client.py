import cv2
import numpy as np
import requests
import threading
import time
from collections import deque


ESP32_IP = "192.168.213.13"
STREAM_URL = f"http://{ESP32_IP}:81/stream"

# 只保留最新一張 JPEG，舊影格會自動丟掉
latest_frames = deque(maxlen=1)

# 用來通知所有執行緒停止
stop_event = threading.Event()


def receive_stream():
    print("目前狀態：Opening stream:", STREAM_URL)

    try:
        response = requests.get(
            STREAM_URL,
            stream=True,
            timeout=(5, 15),
            headers={
                "Cache-Control": "no-cache",
                "Pragma": "no-cache"
            }
        )

        response.raise_for_status()

    except requests.exceptions.RequestException as error:
        print("目前狀態：連線失敗")
        print(error)
        stop_event.set()
        return

    print("目前狀態：已連線到 ESP32 MJPEG stream")

    buffer = bytearray()

    try:
        while not stop_event.is_set():
            # 每次多讀一些資料，減少頻繁呼叫造成的負擔
            chunk = response.raw.read(16384)

            if not chunk:
                print("目前狀態：沒有收到資料，串流可能已中斷")
                break

            buffer.extend(chunk)

            while True:
                # JPEG 開頭：FF D8
                start = buffer.find(b"\xff\xd8")

                if start == -1:
                    # 沒有找到 JPEG 開頭，避免 buffer 不斷增加
                    if len(buffer) > 200_000:
                        buffer.clear()
                    break

                # JPEG 結尾：FF D9
                end = buffer.find(b"\xff\xd9", start + 2)

                if end == -1:
                    # 找到開頭，但資料還沒有收完整
                    if start > 0:
                        del buffer[:start]
                    break

                jpg = bytes(buffer[start:end + 2])

                # 刪除已處理的資料
                del buffer[:end + 2]

                # 只保留最新影格
                latest_frames.append(jpg)

    except requests.exceptions.RequestException as error:
        print("目前狀態：接收串流時發生錯誤")
        print(error)

    except Exception as error:
        print("目前狀態：接收執行緒發生錯誤")
        print(error)

    finally:
        response.close()
        stop_event.set()
        print("目前狀態：接收串流結束")


def main():
    receiver_thread = threading.Thread(
        target=receive_stream,
        daemon=True
    )

    receiver_thread.start()

    frame_counter = 0
    fps = 0.0
    last_fps_time = time.perf_counter()

    # 避免同一張 JPEG 重複解碼
    last_jpg = None

    try:
        while not stop_event.is_set():
            if not latest_frames:
                # 等待第一張影格
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

                time.sleep(0.001)
                continue

            # 取得目前最新影格
            jpg = latest_frames[-1]

            # 若仍是同一張，不要重複解碼
            if jpg is last_jpg:
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

                time.sleep(0.001)
                continue

            last_jpg = jpg

            img_array = np.frombuffer(
                jpg,
                dtype=np.uint8
            )

            frame = cv2.imdecode(
                img_array,
                cv2.IMREAD_COLOR
            )

            if frame is None:
                continue

            frame_counter += 1

            now = time.perf_counter()
            elapsed = now - last_fps_time

            if elapsed >= 1.0:
                fps = frame_counter / elapsed
                frame_counter = 0
                last_fps_time = now

            cv2.putText(
                frame,
                f"FPS: {fps:.1f}",
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 255, 0),
                2,
                cv2.LINE_AA
            )

            cv2.imshow(
                "ESP32-S3 CAM Low Latency Stream",
                frame
            )

            key = cv2.waitKey(1) & 0xFF

            if key == ord("q"):
                print("目前狀態：使用者按下 q，結束")
                break

    except KeyboardInterrupt:
        print("\n目前狀態：Ctrl+C 結束")

    finally:
        stop_event.set()

        # 最多等待接收執行緒一秒
        receiver_thread.join(timeout=1)

        cv2.destroyAllWindows()

        print("目前狀態：程式結束")


if __name__ == "__main__":
    main()