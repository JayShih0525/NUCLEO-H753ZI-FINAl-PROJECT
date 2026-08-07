#ifndef COMMAND_OPCODES_H
#define COMMAND_OPCODES_H

/*
 * Opcode 是 MCU 韌體與 host（不管是 Python + UART，還是之後任何東西）
 * 之間唯一需要對齊的「協定合約」。故意跟 transport 分開放在自己的檔案，
 * 方便 host 端（例如 Python library）直接照抄同一份數值，不會因為 transport
 * 換掉就要重新對照字串指令。
 */

/* Dilithium */
#define CMD_DILITHIUM_SELFTEST      0x01
#define CMD_DILITHIUM_REKEY         0x02
#define CMD_DILITHIUM_GET_PUBKEY    0x03
#define CMD_DILITHIUM_SIGN          0x04
#define CMD_DILITHIUM_VERIFY        0x05

/* ML-KEM */
#define CMD_KEM_GET_PUBKEY          0x10
#define CMD_KEM_DECAPSULATE         0x11
#define CMD_KEM_ENCAPSULATE         0x12
#define CMD_KEM_REKEY               0x13

/* AES-GCM */
#define CMD_AES_ENCRYPT             0x20
#define CMD_AES_DECRYPT             0x21

/* 共用 */
#define CMD_CLEAR                   0xF0

/*
 * Device -> Host 回應狀態碼。
 *
 * 每個指令的回應現在統一是：
 *     [1 byte status][4 byte BE length][payload]
 *
 * status == RESP_OK 時，payload 是該指令定義的實際資料（可能長度 0，例如
 * KEM_DECAPSULATE 成功時不需要回傳任何東西，payload_len=0 即可）。
 *
 * status != RESP_OK 時，payload 是一段 ASCII 說明文字（不含結尾 \0），
 * host 端可以直接印出來當錯誤訊息，不需要另外定義文字格式。
 *
 * 這樣 host 永遠不需要「猜」下一段是文字還是封包 —— 讀 1 byte status，
 * 再無條件讀 4 byte 長度 + payload 就好，徹底消除舊版
 * Protocol_Printf() 自由格式文字跟 Protocol_SendPacket() 二進位封包
 * 混用造成的協定歧義。
 */
#define RESP_OK                 0x00
#define RESP_VERIFY_FAIL        0x02  /* 簽章驗證「失敗」是合法結果，不是 IO 錯誤 */
#define RESP_ERR_RX             0x03
#define RESP_ERR_CRYPTO         0x04
#define RESP_ERR_NOT_READY      0x05  /* 例如 KEM_NOT_READY / NO_KEY */
#define RESP_ERR_LEN            0x06
#define RESP_UNKNOWN_CMD        0xFF

#endif /* COMMAND_OPCODES_H */
