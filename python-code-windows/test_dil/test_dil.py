import serial
import struct
import time

PORT = "COM6"
BAUDRATE = 4000000
TIMEOUT = 5

DILITHIUM2_PUBLICKEYBYTES = 1312
DILITHIUM2_BYTES = 2420


def send_cmd(ser, cmd: str):
    ser.write((cmd + "\n").encode())
    ser.flush()


def read_line(ser):
    line = ser.readline()
    return line.decode(errors="ignore").strip()


def send_packet(ser, data: bytes):
    # STM32 UART3_ReceivePacket 使用 4-byte big-endian length
    ser.write(struct.pack(">I", len(data)))
    ser.write(data)
    ser.flush()

    # STM32 收完 packet 會回 1 byte status
    status = ser.read(1)
    if len(status) != 1:
        raise TimeoutError("No status byte from STM32")

    if status[0] != 0x00:
        raise RuntimeError(f"STM32 packet receive error: 0x{status[0]:02X}")


def recv_packet(ser) -> bytes:
    len_bytes = ser.read(4)
    if len(len_bytes) != 4:
        raise TimeoutError("Cannot read packet length")

    length = struct.unpack(">I", len_bytes)[0]

    data = ser.read(length)
    if len(data) != length:
        raise TimeoutError(f"Packet length mismatch: expected {length}, got {len(data)}")

    return data


def main():
    with serial.Serial(PORT, BAUDRATE, timeout=TIMEOUT) as ser:
        time.sleep(1)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        print("[1] Rekey")
        send_cmd(ser, "DILITHIUM_REKEY")
        print("STM32:", read_line(ser))

        print("[2] Get public key")
        send_cmd(ser, "GET_DILITHIUM_PUBLIC_KEY")
        pk = recv_packet(ser)
        print("pk len =", len(pk))

        if len(pk) == DILITHIUM2_PUBLICKEYBYTES:
            print("GET PUBLIC KEY PASS")
        else:
            print("GET PUBLIC KEY FAIL")

        print("[3] Sign message")
        msg = b"hello from python to stm32 dilithium"

        send_cmd(ser, "DILITHIUM_SIGN")
        send_packet(ser, msg)

        sig = recv_packet(ser)
        print("sig len =", len(sig))

        if len(sig) == DILITHIUM2_BYTES:
            print("SIGN PASS")
        else:
            print("SIGN FAIL")

        with open("dilithium_pk.bin", "wb") as f:
            f.write(pk)

        with open("dilithium_sig.bin", "wb") as f:
            f.write(sig)

        with open("dilithium_msg.bin", "wb") as f:
            f.write(msg)

        print("saved: dilithium_pk.bin, dilithium_sig.bin, dilithium_msg.bin")


if __name__ == "__main__":
    main()