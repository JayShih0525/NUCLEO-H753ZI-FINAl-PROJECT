#ifndef PACKET_PROTOCOL_H
#define PACKET_PROTOCOL_H

#include "transport.h"
#include "pqc_identity.h"
#include <stdint.h>

/*
 * Packet Protocol 層：跟原本 uart3_protocol.c 做的事情一樣（length-prefixed 封包），
 * 差別是完全不碰 UART_HandleTypeDef，只透過 Transport_t 溝通。
 *
 * Frame 格式維持跟原本一樣：
 *   [4 bytes big-endian length][payload...]
 * 之後 status byte 一樣是先送/收 1 byte。
 *
 * 指令本身則改用「opcode 層」(見 command_dispatcher.h)，
 * 不再用 ASCII 字串 + '\n' 分行 —— 那個作法依賴 UART 的串流特性，
 * 換到 SPI/I2C 這種 master-clocked、沒有獨立換行判斷的匯流排上會直接不能用。
 */

typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_NULL_PTR,
    PROTO_ERR_RX_LEN,
    PROTO_ERR_RX_DATA,
    PROTO_ERR_TX_DATA,
    PROTO_ERR_LEN_TOO_BIG,
} Protocol_Status_t;

/* 收 1 byte status/opcode 用 */
Protocol_Status_t Protocol_RecvByte(Transport_t *t, uint8_t *out_byte, uint32_t timeout_ms);
Protocol_Status_t Protocol_SendByte(Transport_t *t, uint8_t byte, uint32_t timeout_ms);

/* 對應原本 UART3_ReceivePacket / UART3_SendPacket，只是吃 Transport_t* */
Protocol_Status_t Protocol_ReceivePacket(Transport_t *t,
                                          uint8_t *buffer,
                                          uint32_t max_data_size,
                                          uint32_t *out_len,
                                          uint32_t timeout_ms);

Protocol_Status_t Protocol_SendPacket(Transport_t *t,
                                       const uint8_t *data,
                                       uint32_t max_data_size,
                                       uint32_t len,
                                       uint32_t timeout_ms);

/* debug/log 用，純文字、無框架。開機 banner 之類的一次性訊息才用這個，
 * 一般指令回應一律改用下面的 Protocol_SendResponse()，不要再用這個
 * 送指令的成功/失敗結果 —— 混用會讓 host 沒辦法預判下一段是文字還是封包。 */
void Protocol_Printf(Transport_t *t, uint32_t timeout_ms, const char *format, ...);

/*
 * 統一的「指令回應」格式： [1 byte status][4 byte BE length][payload]
 * 所有 *_app.c 的 handler 回應都要走這個函式，不要再直接混用
 * Protocol_Printf / Protocol_SendPacket。
 */
Protocol_Status_t Protocol_SendResponse(Transport_t *t,
                                         uint8_t status,
                                         const uint8_t *payload,
                                         uint32_t payload_len,
                                         uint32_t timeout_ms);

/* 方便函式：status != RESP_OK 時常用，msg 用 strlen() 自動算長度 */
Protocol_Status_t Protocol_SendResponseMsg(Transport_t *t,
                                            uint8_t status,
                                            const char *msg,
                                            uint32_t timeout_ms);

/*
 * 低階版本：回應資料要「拼接好幾段不連續的 buffer」時用（例如 AES-GCM encrypt
 * 一次要回 nonce+ciphertext+tag 三段），避免多開一份跟 payload 一樣大的暫存
 * buffer 才能湊出連續記憶體給 Protocol_SendResponse()。
 *
 * 用法：先呼叫一次 Protocol_SendResponseHeader() 送 status + 總長度，
 * 再依序呼叫 Protocol_SendRaw() 把各段資料送出，段數、每段長度都自己算，
 * 只要總長度跟 header 裡宣告的一致即可。
 */
Protocol_Status_t Protocol_SendResponseHeader(Transport_t *t,
                                               uint8_t status,
                                               uint32_t total_len,
                                               uint32_t timeout_ms);

Protocol_Status_t Protocol_SendRaw(Transport_t *t,
                                    const uint8_t *data,
                                    uint32_t len,
                                    uint32_t timeout_ms);

#endif /* PACKET_PROTOCOL_H */
