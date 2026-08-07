#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include "transport.h"
#include <stdint.h>

/*
 * 每個 handler 收到 opcode 之後自己負責用 Protocol_ReceivePacket/SendPacket
 * 讀寫後續 payload（跟原本 *_UART_*Task() 函式做的事一樣，只是不再直接碰
 * UART_HandleTypeDef，改用傳進來的 Transport_t*）。
 */
typedef void (*CommandHandler_t)(Transport_t *t);

typedef struct {
    uint8_t opcode;
    CommandHandler_t handler;
} CommandEntry_t;

#define COMMAND_TABLE_MAX_ENTRIES 32

/* 初始化空的指令表 */
void Dispatcher_Init(void);

/* 各 app 模組在自己的 _Init() 裡呼叫，把 opcode 註冊進表裡 */
uint8_t Dispatcher_Register(uint8_t opcode, CommandHandler_t handler);

/* 讀 1 byte opcode，查表並呼叫對應 handler；查不到就回 CMD_UNKNOWN status */
void Dispatcher_RunOnce(Transport_t *t, uint32_t timeout_ms);

#endif /* COMMAND_DISPATCHER_H */
