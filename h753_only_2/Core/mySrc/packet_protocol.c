#include "packet_protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define PROTO_CHUNK_SIZE 512

static Protocol_Status_t SendChunked(Transport_t *t, const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    uint32_t sent = 0;

    while (sent < len) {
        uint32_t remain = len - sent;
        uint32_t chunk = (remain > PROTO_CHUNK_SIZE) ? PROTO_CHUNK_SIZE : remain;

        if (t->write(t, data + sent, chunk, timeout_ms) != TRANSPORT_OK) {
            return PROTO_ERR_TX_DATA;
        }

        sent += chunk;
    }

    return PROTO_OK;
}

Protocol_Status_t Protocol_RecvByte(Transport_t *t, uint8_t *out_byte, uint32_t timeout_ms)
{
    if (t == NULL || out_byte == NULL) return PROTO_ERR_NULL_PTR;

    if (t->read(t, out_byte, 1, timeout_ms) != TRANSPORT_OK) {
        return PROTO_ERR_RX_DATA;
    }

    return PROTO_OK;
}

Protocol_Status_t Protocol_SendByte(Transport_t *t, uint8_t byte, uint32_t timeout_ms)
{
    if (t == NULL) return PROTO_ERR_NULL_PTR;

    if (t->write(t, &byte, 1, timeout_ms) != TRANSPORT_OK) {
        return PROTO_ERR_TX_DATA;
    }

    return PROTO_OK;
}

Protocol_Status_t Protocol_ReceivePacket(Transport_t *t,
                                          uint8_t *buffer,
                                          uint32_t max_data_size,
                                          uint32_t *out_len,
                                          uint32_t timeout_ms)
{
    if (t == NULL || buffer == NULL || out_len == NULL || max_data_size == 0) {
        return PROTO_ERR_NULL_PTR;
    }

    uint8_t len_bytes[4];
    uint32_t len;
    uint32_t received = 0;

    if (t->read(t, len_bytes, 4, timeout_ms) != TRANSPORT_OK) {
        Protocol_SendByte(t, PROTO_ERR_RX_LEN, timeout_ms);
        return PROTO_ERR_RX_LEN;
    }

    len = ((uint32_t)len_bytes[0] << 24) |
          ((uint32_t)len_bytes[1] << 16) |
          ((uint32_t)len_bytes[2] << 8)  |
          ((uint32_t)len_bytes[3]);

    if (len > max_data_size) {
        Protocol_SendByte(t, PROTO_ERR_LEN_TOO_BIG, timeout_ms);
        return PROTO_ERR_LEN_TOO_BIG;
    }

    while (received < len) {
        uint32_t remain = len - received;
        uint32_t chunk = (remain > PROTO_CHUNK_SIZE) ? PROTO_CHUNK_SIZE : remain;

        if (t->read(t, buffer + received, chunk, timeout_ms) != TRANSPORT_OK) {
            Protocol_SendByte(t, PROTO_ERR_RX_DATA, timeout_ms);
            return PROTO_ERR_RX_DATA;
        }

        received += chunk;
    }

    *out_len = len;
    Protocol_SendByte(t, PROTO_OK, timeout_ms);

    return PROTO_OK;
}

Protocol_Status_t Protocol_SendPacket(Transport_t *t,
                                       const uint8_t *data,
                                       uint32_t max_data_size,
                                       uint32_t len,
                                       uint32_t timeout_ms)
{
    if (t == NULL) return PROTO_ERR_NULL_PTR;

    if (len > max_data_size) {
        return PROTO_ERR_LEN_TOO_BIG;
    }

    uint8_t len_bytes[4] = {
        (uint8_t)((len >> 24) & 0xFF),
        (uint8_t)((len >> 16) & 0xFF),
        (uint8_t)((len >> 8) & 0xFF),
        (uint8_t)(len & 0xFF)
    };

    if (t->write(t, len_bytes, 4, timeout_ms) != TRANSPORT_OK) {
        return PROTO_ERR_TX_DATA;
    }

    return SendChunked(t, data, len, timeout_ms);
}

void Protocol_Printf(Transport_t *t, uint32_t timeout_ms, const char *format, ...)
{
    char buffer[256];
    va_list args;
    int len;

    if (t == NULL || format == NULL) return;

    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len <= 0) return;
    if (len >= (int)sizeof(buffer)) len = sizeof(buffer) - 1;

    t->write(t, (uint8_t *)buffer, (uint32_t)len, timeout_ms);
}

Protocol_Status_t Protocol_SendResponse(Transport_t *t,
                                         uint8_t status,
                                         const uint8_t *payload,
                                         uint32_t payload_len,
                                         uint32_t timeout_ms)
{
    if (t == NULL) return PROTO_ERR_NULL_PTR;

    if (Protocol_SendByte(t, status, timeout_ms) != PROTO_OK) {
        return PROTO_ERR_TX_DATA;
    }

    /* payload_len 當自己的 max_data_size 傳進去，因為這裡沒有上限檢查的需求
     * （呼叫端本來就是要送多少就送多少，不像收資料那樣需要防止對方亂塞爆 buffer）。 */
    return Protocol_SendPacket(t, payload, payload_len, payload_len, timeout_ms);
}

Protocol_Status_t Protocol_SendResponseMsg(Transport_t *t,
                                            uint8_t status,
                                            const char *msg,
                                            uint32_t timeout_ms)
{
    uint32_t len = (msg == NULL) ? 0 : (uint32_t)strlen(msg);
    return Protocol_SendResponse(t, status, (const uint8_t *)msg, len, timeout_ms);
}

Protocol_Status_t Protocol_SendResponseHeader(Transport_t *t,
                                               uint8_t status,
                                               uint32_t total_len,
                                               uint32_t timeout_ms)
{
    if (t == NULL) return PROTO_ERR_NULL_PTR;

    if (Protocol_SendByte(t, status, timeout_ms) != PROTO_OK) {
        return PROTO_ERR_TX_DATA;
    }

    uint8_t len_bytes[4] = {
        (uint8_t)((total_len >> 24) & 0xFF),
        (uint8_t)((total_len >> 16) & 0xFF),
        (uint8_t)((total_len >> 8) & 0xFF),
        (uint8_t)(total_len & 0xFF)
    };

    if (t->write(t, len_bytes, 4, timeout_ms) != TRANSPORT_OK) {
        return PROTO_ERR_TX_DATA;
    }

    return PROTO_OK;
}

Protocol_Status_t Protocol_SendRaw(Transport_t *t,
                                    const uint8_t *data,
                                    uint32_t len,
                                    uint32_t timeout_ms)
{
    if (t == NULL) return PROTO_ERR_NULL_PTR;

    return SendChunked(t, data, len, timeout_ms);
}
