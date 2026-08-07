#include "command_dispatcher.h"
#include "packet_protocol.h"
#include "command_opcodes.h"
#include <string.h>

static CommandEntry_t command_table[COMMAND_TABLE_MAX_ENTRIES];
static uint8_t command_count = 0;

void Dispatcher_Init(void)
{
    memset(command_table, 0, sizeof(command_table));
    command_count = 0;
}

uint8_t Dispatcher_Register(uint8_t opcode, CommandHandler_t handler)
{
    if (command_count >= COMMAND_TABLE_MAX_ENTRIES) {
        return 0; /* table full */
    }

    command_table[command_count].opcode = opcode;
    command_table[command_count].handler = handler;
    command_count++;

    return 1;
}

static CommandHandler_t FindHandler(uint8_t opcode)
{
    for (uint8_t i = 0; i < command_count; i++) {
        if (command_table[i].opcode == opcode) {
            return command_table[i].handler;
        }
    }
    return NULL;
}

void Dispatcher_RunOnce(Transport_t *t, uint32_t timeout_ms)
{
    uint8_t opcode;

    if (Protocol_RecvByte(t, &opcode, timeout_ms) != PROTO_OK) {
        /* 沒收到完整 opcode（timeout / IO error），直接跳過，下一輪再試 */
        return;
    }

    if (opcode == CMD_CLEAR) {
        t->flush(t);
        Protocol_SendResponse(t, RESP_OK, NULL, 0, timeout_ms);
        return;
    }

    CommandHandler_t handler = FindHandler(opcode);

    if (handler == NULL) {
        Protocol_SendResponseMsg(t, RESP_UNKNOWN_CMD, "UNKNOWN_CMD", timeout_ms);
        return;
    }

    handler(t);
}
