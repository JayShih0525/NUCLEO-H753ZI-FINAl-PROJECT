# Crypto App 架構重構

## [更新] 修掉「文字 vs 封包」的協定歧義

第一版重構有個漏洞：同一個指令，成功時用 `Protocol_SendPacket`（二進位封包）
回應，失敗時卻用 `Protocol_Printf`（自由格式文字）回應，host 端沒辦法在讀
之前預先知道下一段是文字還是封包，猜錯就會整個協定卡死（這就是實際燒錄後
遇到的 bug）。

現在統一成：**所有指令回應都是 `[1 byte status][4 byte BE length][payload]`**，
不管成功失敗 shape 都一樣，host 只要無條件照這個 shape 讀。細節看
`inc/command_opcodes.h` 的 `RESP_*` 常數，以及 `packet_protocol.h` 的
`Protocol_SendResponse` / `Protocol_SendResponseMsg` / `Protocol_SendResponseHeader`
+ `Protocol_SendRaw`（後兩個是給 AES-GCM encrypt 這種要拼接 nonce+ciphertext+tag
三段不連續 buffer 的情況用）。

配套的 Python host library（opcode 版）在另一個包裡，兩邊的協定常數
(`command_opcodes.h` / `command_opcodes.py`) 是手動同步的，改一邊記得改另一邊。


## 改了什麼

**之前**：三個 `*_uart_app.c` 各自直接呼叫 `HAL_UART_Receive/Transmit`，指令用 ASCII
字串 + `\n` 分行比對（`UART3_ReadLine` + `strcmp`）。要換 transport 等於重寫三份。

**現在**：分成四層，只有最底層知道自己是 UART。

```
main.c
  └─ command_dispatcher.c   (opcode -> handler 查表，不再是 if/else strcmp)
        └─ dilithium_app.c / mlkem_app.c / aesgcm_app.c   (密碼邏輯完全沒動)
              └─ packet_protocol.c   (length-prefixed framing，只依賴 Transport_t)
                    └─ transport_uart.c   (實作 Transport_t，包住 HAL_UART_*)
```

`*_lib.c`（`aes_gcm_lib.c`, `ml_kem_lib.c`, dilithium 的 pqcrystals 呼叫）完全沒有改，
它們本來就已經跟通訊層解耦了，這點你原本的架構做得很好，繼續保留。

## 關鍵決定：指令從 ASCII 字串改成二進位 opcode

這是因為你選了 SPI/I2C 當下一個 transport。原本 `UART3_ReadLine` 靠「讀到 `\n` 就結束」
來判斷一行指令的邊界，這個假設**只在 UART 這種串流、且雙方都不需要對方主動觸發傳輸的
匯流排上成立**。SPI 是 master 決定時脈、逐 byte clock 出來的，STM32 (slave) 沒辦法說
「等我送個換行字元」——所以指令格式必須先改成不依賴分行字元的東西。

新格式（`command_opcodes.h` 定義 opcode 數值）：

```
[1 byte opcode][4 bytes BE length][payload...]      <- host 送
[1 byte status][... response payload 依 handler而定]  <- MCU 回
```

這個 frame 格式在 UART / SPI / I2C / USB CDC 上都適用，因為它不依賴任何 transport 專屬的
「结束符號」，全靠長度欄位。

## 之後要接 SPI/I2C 時還需要什麼

1. **`transport_spi.c`**（還沒寫，需要你確認硬體細節後補）
   - 實作跟 `transport_uart.c` 一樣的 `read/write/flush` 介面
   - SPI slave 的 `read/write` 內部通常會是：等 DMA 完成中斷 → 設旗標/給 semaphore →
     這裡 `while` 忙等（bare-metal）或 `xSemaphoreTake`（有 RTOS）直到 timeout
   - **需要額外硬體資源**：SPI slave 沒辦法主動把資料塞給還沒 clock 的 master，
     所以通常要加一條 **GPIO "DATA_READY" 訊號線**，讓 STM32 告訴 host「我準備好回應了，
     可以來 clock 我」。這就是 `Transport_t.notify_ready()` 這個 function pointer預留的
     用途，UART 用不到、SPI 會用到。
   - I2C 類似，但角色相反：I2C slave 只能等 master 讀/寫，通常會搭配一個 status
     register（先讀 1 byte「忙/閒」再決定要不要繼續 clock）。

2. **決定要不要上 RTOS**：你目前還沒決定，這其實不影響現在這版架構——
   `Transport_t.read/write` 本來就設計成「呼叫者視角是 blocking」，RTOS 只是改變
   `transport_spi.c` 內部怎麼實作那個 blocking（semaphore vs 忙等），上層（dispatcher、
   三個 app 模組）完全不用改。等你決定要上 RTOS 再回來改 `transport_spi.c` 就好。

3. **Host（Python）端要跟著改協定**：`main.py` 目前的 `library/uart3_protocol.py`、
   `ml_kem_app.py`、`aes_gcm_app.py` 應該也是走 ASCII 指令，需要同步改成送
   `command_opcodes.h` 裡對應的 opcode byte。建議把 `command_opcodes.h` 的數值
   對照表也維護一份在 Python 那邊（例如直接寫一個小腳本從這個 header 產生
   Python 常數，避免兩邊手動同步出錯）。

4. **一個小的整合測試**：現在 opcode 表是執行期用 `Dispatcher_Register()` 建立的，
   建議寫一個簡單的 self-test（開機後跑一次 `DILITHIUM_SELFTEST` 邏輯，
   確認 dispatcher 查表跟 handler 呼叫都正常），避免之後改 transport 時不小心把
   某個 app 的 `_Init()` 忘記呼叫。

## 沒有變的部分

- 三個密碼函式庫（`aes_gcm_lib.c`, `ml_kem_lib.c`, dilithium ref 實作）完全沒動。
- 每個 app 模組內部的邏輯（先收 nonce 再收 ciphertext 再收 tag 之類的順序）完全沒動，
  只是底層呼叫從 `UART3_ReceivePacket(huart, ...)` 換成 `Protocol_ReceivePacket(t, ...)`。
- `CLEAR` 指令的行為（清 buffer + 清硬體 RX）現在收斂到 dispatcher 裡統一處理，
  三個模組不用各自重複實作一次。

## 檔案清單

```
inc/transport.h            Transport 介面定義
inc/transport_uart.h
src/transport_uart.c       UART 版 Transport 實作（之後新增 transport_spi.c/h 用同介面）
inc/packet_protocol.h
src/packet_protocol.c      Transport-agnostic 的 length-prefixed framing
inc/command_opcodes.h      MCU <-> host 的 opcode 對照表（協定合約本體）
inc/command_dispatcher.h
src/command_dispatcher.c   opcode -> handler 查表 dispatch
inc/dilithium_app.h
src/dilithium_app.c        從原本 main.c 抽出來的 Dilithium 邏輯
inc/mlkem_app.h
src/mlkem_app.c            重構自 ml_kem_uart_app.c
inc/aesgcm_app.h
src/aesgcm_app.c           重構自 aes_gcm_uart_app.c
src/main.c                 新的 main，只在意 Transport 怎麼 init，其餘交給 dispatcher
```

`uart3_protocol.c/.h` 可以整份刪掉，功能已經被 `transport_uart.c` + `packet_protocol.c`
取代。
