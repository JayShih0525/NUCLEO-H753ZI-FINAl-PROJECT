#include "aes_gcm_lib.h"
#include "main.h"
#include <string.h>

/*
 * 硬體加速版本，用 STM32H753 內建的 CRYP 周邊做 AES-256-GCM。
 *
 * [更新 2] 從 DataWidthUnit=BYTE 改成 DataWidthUnit=WORD。
 *
 * 背景：demo 實測（200+ 張連續資料，涵蓋各種長度）發現一個非常明確的
 * 規律——plaintext 長度是 4 的倍數（mod4==0）時 100% 成功，只要有餘數
 * （mod4 是 1/2/3）幾乎必然導致 InvalidTag。這代表 BYTE width unit
 * 在處理「大量完整 block 之後、還有一段不滿 4 bytes 尾巴」這個情境時
 * 不可靠（我們的已知答案測試只測過 33 bytes 這種小資料量，沒有覆蓋到
 * 這個規模才會出現的問題；DeInit+Init 那輪測試已經排除是「呼叫間狀態
 * 殘留」的問題，跟連續呼叫的節奏無關，是長度本身的問題）。
 *
 * 既然問題根源不確定（沒有 stm32h7xx_hal_cryp.c 原始碼，無法從暫存器
 * 層級確認），與其繼續猜暫存器設定，改成「保證輸入永遠對齊」——
 * Python 端（aes_gcm_app.py）會在送資料進來之前，先把 plaintext 補零
 * 到 4 的倍數，讓這裡收到的 input_len 保證是 4 的倍數。這裡因此可以
 * 放心改用 WORD 模式，完全不會走到那個不可靠的路徑，而不是「湊巧不會
 * 觸發」。
 *
 * AESGCM_Encrypt/Decrypt 加了一個防禦性檢查：如果 input_len 不是 4
 * 的倍數（代表呼叫端沒有照約定先做 padding），直接回傳錯誤，而不是
 * 硬送進硬體、又產生一個算不出正確結果的密文。
 */

static CRYP_HandleTypeDef hcryp;
static uint8_t cryp_ready = 0;

__ALIGN_BEGIN static uint32_t key_words[8] __ALIGN_END = {0};
__ALIGN_BEGIN static uint32_t iv_words[4] __ALIGN_END = {0};
__ALIGN_BEGIN static uint32_t header_words[1] __ALIGN_END = {0}; /* HeaderSize=0，內容不會被用到 */

static void BuildIV(const uint8_t *nonce)
{
    iv_words[0] = ((uint32_t)nonce[0] << 24) | ((uint32_t)nonce[1] << 16) |
                  ((uint32_t)nonce[2] << 8)  | ((uint32_t)nonce[3]);
    iv_words[1] = ((uint32_t)nonce[4] << 24) | ((uint32_t)nonce[5] << 16) |
                  ((uint32_t)nonce[6] << 8)  | ((uint32_t)nonce[7]);
    iv_words[2] = ((uint32_t)nonce[8] << 24) | ((uint32_t)nonce[9] << 16) |
                  ((uint32_t)nonce[10] << 8) | ((uint32_t)nonce[11]);
    iv_words[3] = 0x00000002U;
}

static void CRYP_ApplyBaseConfig(void)
{
    hcryp.Init.DataType        = CRYP_BYTE_SWAP;   /* 已用已知答案測試驗證過，不動 */
    hcryp.Init.KeySize          = CRYP_KEYSIZE_256B;
    hcryp.Init.pKey             = (uint32_t *)key_words;
    hcryp.Init.pInitVect        = (uint32_t *)iv_words;
    hcryp.Init.Algorithm        = CRYP_AES_GCM;
    hcryp.Init.Header           = (uint32_t *)header_words;
    hcryp.Init.HeaderSize       = 0;
    /*
     * CRYP_DATAWIDTHUNIT_WORD：這個巨集名稱沒有逐字在 header 裡確認過
     * 存在，數值上推測是 0（不特別設定時的預設值）。
     *
     * 重要更正：我之前講「這個模式已經驗證過」是錯的——回頭確認，
     * 我們從最早第一輪已知答案測試開始，就已經寫死用 BYTE 模式
     * （CRYP_DATAWIDTHUNIT_BYTE），從來沒有真的用 WORD 模式在硬體上
     * 測試過已知答案。也就是說，這次改用 WORD 模式，是一個全新、
     * 還沒被驗證過的設定，不是「切回已驗證過的東西」。
     *
     * 換上這版之後，強烈建議先跑一輪已知答案測試（可以直接沿用之前
     * 那組非零 nonce 的測試向量，但把 plaintext 補齊成 4 的倍數、
     * 用 WORD 模式送——理想上再測一個「刻意夠大、涵蓋多個 block」的
     * payload，不要只測 33 bytes 那種小資料），確認 WORD 模式本身在
     * 你的硬體上是正確的，再上 demo，避免這次又是「編得過、能跑、
     * 但算錯」的情況。
     */
    hcryp.Init.DataWidthUnit    = CRYP_DATAWIDTHUNIT_WORD;
    hcryp.Init.HeaderWidthUnit  = CRYP_HEADERWIDTHUNIT_BYTE; /* HeaderSize=0，這個值不影響結果，
                                                                 用已經確認存在的巨集，不猜測
                                                                 CRYP_HEADERWIDTHUNIT_WORD 存不存在 */
    hcryp.Init.KeyIVConfigSkip  = CRYP_KEYIVCONFIG_ALWAYS;
}

uint8_t AESGCM_SetKey(const uint8_t *key, uint16_t key_len)
{
    if (key == NULL) return AES_GCM_ERR_NULL_PTR;
    if (key_len != AES_GCM_KEY_SIZE) return AES_GCM_ERR_SETKEY;

    for (int i = 0; i < 8; i++)
    {
        const uint8_t *k = key + (i * 4);
        key_words[i] = ((uint32_t)k[0] << 24) | ((uint32_t)k[1] << 16) |
                       ((uint32_t)k[2] << 8)  | ((uint32_t)k[3]);
    }

    if (!cryp_ready)
    {
        hcryp.Instance = CRYP;
        CRYP_ApplyBaseConfig();

        if (HAL_CRYP_Init(&hcryp) != HAL_OK)
        {
            return AES_GCM_ERR_SETKEY;
        }

        cryp_ready = 1;
    }
    else
    {
        CRYP_ApplyBaseConfig();

        if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK)
        {
            return AES_GCM_ERR_SETKEY;
        }
    }

    return AES_GCM_OK;
}

uint8_t AESGCM_Encrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                        uint8_t *output, uint8_t *tag)
{
    if (nonce == NULL || input == NULL || output == NULL || tag == NULL)
    {
        return AES_GCM_ERR_NULL_PTR;
    }

    if (!cryp_ready)
    {
        return AES_GCM_ERR_SETKEY;
    }

    /* 防禦性檢查：WORD 模式要求長度一定要是 4 的倍數。Python 端
     * （aes_gcm_app.py）約定會先 padding 好才送過來，這裡再檢查一次，
     * 如果違反約定，直接回錯誤，不要硬送進硬體產生一個算不出正確
     * 結果的密文（那種錯誤會很難查，這次的教訓）。 */
    if ((input_len % 4U) != 0U)
    {
        return AES_GCM_ERR_ENCRYPT;
    }

    BuildIV(nonce);

    if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK)
    {
        return AES_GCM_ERR_ENCRYPT;
    }

    /* WORD 模式下 Size 參數的單位是「word 數」，不是 byte 數。 */
    if (HAL_CRYP_Encrypt(&hcryp, (uint32_t *)(uintptr_t)input, (uint16_t)(input_len / 4U),
                          (uint32_t *)output, HAL_MAX_DELAY) != HAL_OK)
    {
        return AES_GCM_ERR_ENCRYPT;
    }

    if (HAL_CRYPEx_AESGCM_GenerateAuthTAG(&hcryp, (uint32_t *)tag, HAL_MAX_DELAY) != HAL_OK)
    {
        return AES_GCM_ERR_ENCRYPT;
    }

    return AES_GCM_OK;
}

uint8_t AESGCM_Decrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                        const uint8_t *tag, uint8_t *output)
{
    uint8_t computed_tag[AES_GCM_TAG_SIZE];
    uint8_t diff = 0;

    if (nonce == NULL || input == NULL || tag == NULL || output == NULL)
    {
        return AES_GCM_ERR_NULL_PTR;
    }

    if (!cryp_ready)
    {
        return AES_GCM_ERR_SETKEY;
    }

    if ((input_len % 4U) != 0U)
    {
        return AES_GCM_ERR_DECRYPT;
    }

    BuildIV(nonce);

    if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK)
    {
        return AES_GCM_ERR_DECRYPT;
    }

    if (HAL_CRYP_Decrypt(&hcryp, (uint32_t *)(uintptr_t)input, (uint16_t)(input_len / 4U),
                          (uint32_t *)output, HAL_MAX_DELAY) != HAL_OK)
    {
        return AES_GCM_ERR_DECRYPT;
    }

    if (HAL_CRYPEx_AESGCM_GenerateAuthTAG(&hcryp, (uint32_t *)computed_tag, HAL_MAX_DELAY) != HAL_OK)
    {
        return AES_GCM_ERR_DECRYPT;
    }

    for (int i = 0; i < AES_GCM_TAG_SIZE; i++)
    {
        diff |= (uint8_t)(computed_tag[i] ^ tag[i]);
    }

    if (diff != 0)
    {
        return AES_GCM_ERR_AUTH_FAIL;
    }

    return AES_GCM_OK;
}
