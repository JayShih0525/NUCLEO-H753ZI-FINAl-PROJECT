# check ip : ifconfig | grep "inet "

import socket
import datetime

HOST = "0.0.0.0"
PORT = 5555

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((HOST, PORT))
server.listen(1)

print(f"[SERVER] Listening on {HOST}:{PORT}")
print("[SERVER] Waiting for STM32 / Ai-WB2 connection...")

conn, addr = server.accept()
print(f"[SERVER] Connected by {addr}")

try:
    while True:
        data = conn.recv(4096)

        if not data:
            print("[SERVER] Client disconnected")
            break

        now = datetime.datetime.now().strftime("%H:%M:%S")
        print(f"[{now}] Received raw bytes:", data)
        print(f"[{now}] Received text:", data.decode(errors="replace"))

        # 回傳 ACK 給 STM32 / Ai-WB2
        reply = b"ACK_FROM_PYTHON_SERVER"
        conn.sendall(reply)
        print(f"[{now}] Sent reply:", reply)

except KeyboardInterrupt:
    print("\n[SERVER] Stopped by user")

finally:
    conn.close()
    server.close()
    print("[SERVER] Closed")