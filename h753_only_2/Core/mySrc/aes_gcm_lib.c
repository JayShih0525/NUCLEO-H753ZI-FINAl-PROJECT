#include "aes_gcm_lib.h"
#include "main.h"
#include "mbedtls/gcm.h"
#include <string.h>

/*
 * 混合版本：資料量在安全範圍內走硬體 CRYP 加速，超過範圍自動退回
 * 軟體 mbedtls（原本的實作）。對外介面（AESGCM_SetKey/Encrypt/Decrypt）
 * 完全不變，aesgcm_app.c 不用修改。
 *
 * ============================================================
 * 硬體安全上限是怎麼來的（不是猜的，是逐步二分搜尋在真實硬體上
 * 逼出來的精確邊界）：
 * ============================================================
 *
 *   65532 bytes (16383 words) → 已知答案測試，3/3 全部正確
 *   65536 bytes (16384 words) → 已知答案測試，3/3 全部錯誤
 *
 * 錯誤模式是「不回報任何錯誤（HAL_OK），但算出來的 tag/ciphertext
 * 是錯的」——完全沒有 HAL_BUSY 或其他訊號可以偵測，重試機制救不了。
 *
 * 16384 = 2^14，推測 CRYP 周邊內部某個計數器/位址欄位只有 14-bit
 * 寬，處理到第 16384 個 word 時發生截斷或溢位。這是硬體層級的限制，
 * 不是韌體邏輯可以繞過的，只能避開它——資料量接近或超過這個量級，
 * 一律不使用硬體路徑。
 *
 * 門檻抓 60000 bytes（明顯低於 65532 這條精確邊界，留安全餘裕，
 * 因為我們只在單一顆晶片上測過，且每個點只驗證 3 次，不是統計上
 * 非常大量的樣本）。
 */
#define AES_GCM_HW_MAX_SIZE 60000U

/* ============================================================
 * 硬體路徑（CRYP，DataType=BYTE_SWAP / DataWidthUnit=WORD，
 * 已用已知答案測試逐 byte 驗證過，細節見先前的驗證記錄）
 * ============================================================ */

static CRYP_HandleTypeDef hcryp;
static uint8_t cryp_ready = 0;

__ALIGN_BEGIN static uint32_t key_words[8] __ALIGN_END = {0};
__ALIGN_BEGIN static uint32_t iv_words[4] __ALIGN_END = {0};
__ALIGN_BEGIN static uint32_t header_words[1] __ALIGN_END = {0};

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
    hcryp.Init.DataType        = CRYP_BYTE_SWAP;
    hcryp.Init.KeySize          = CRYP_KEYSIZE_256B;
    hcryp.Init.pKey             = (uint32_t *)key_words;
    hcryp.Init.pInitVect        = (uint32_t *)iv_words;
    hcryp.Init.Algorithm        = CRYP_AES_GCM;
    hcryp.Init.Header           = (uint32_t *)header_words;
    hcryp.Init.HeaderSize       = 0;
    hcryp.Init.DataWidthUnit    = CRYP_DATAWIDTHUNIT_WORD;
    hcryp.Init.HeaderWidthUnit  = CRYP_HEADERWIDTHUNIT_BYTE;
    hcryp.Init.KeyIVConfigSkip  = CRYP_KEYIVCONFIG_ALWAYS;
}

static uint8_t HW_Encrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint8_t *tag)
{
    if (!cryp_ready) return AES_GCM_ERR_SETKEY;

    /* 硬體路徑要求 4 對齊（WORD 模式）；呼叫端（aes_gcm_app.py）
     * 已經保證會先補零到 4 的倍數，這裡再檢查一次防呆。 */
    if ((input_len % 4U) != 0U) return AES_GCM_ERR_ENCRYPT;

    BuildIV(nonce);

    if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK) return AES_GCM_ERR_ENCRYPT;

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

static uint8_t HW_Decrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                           const uint8_t *tag, uint8_t *output)
{
    uint8_t computed_tag[AES_GCM_TAG_SIZE];
    uint8_t diff = 0;

    if (!cryp_ready) return AES_GCM_ERR_SETKEY;
    if ((input_len % 4U) != 0U) return AES_GCM_ERR_DECRYPT;

    BuildIV(nonce);

    if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK) return AES_GCM_ERR_DECRYPT;

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

    return (diff != 0) ? AES_GCM_ERR_AUTH_FAIL : AES_GCM_OK;
}

/* ============================================================
 * 軟體路徑（mbedtls，原本的實作，資料量沒有硬體那個上限問題）
 * ============================================================ */

static mbedtls_gcm_context gcm_ctx;
static uint8_t sw_key_is_set = 0;

static uint8_t SW_Encrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                           uint8_t *output, uint8_t *tag)
{
    if (!sw_key_is_set) return AES_GCM_ERR_SETKEY;

    int ret = mbedtls_gcm_crypt_and_tag(
        &gcm_ctx, MBEDTLS_GCM_ENCRYPT, input_len,
        nonce, AES_GCM_NONCE_SIZE,
        NULL, 0,
        input, output,
        AES_GCM_TAG_SIZE, tag
    );

    return (ret != 0) ? AES_GCM_ERR_ENCRYPT : AES_GCM_OK;
}

static uint8_t SW_Decrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                           const uint8_t *tag, uint8_t *output)
{
    if (!sw_key_is_set) return AES_GCM_ERR_SETKEY;

    int ret = mbedtls_gcm_auth_decrypt(
        &gcm_ctx, input_len,
        nonce, AES_GCM_NONCE_SIZE,
        NULL, 0,
        tag, AES_GCM_TAG_SIZE,
        input, output
    );

    return (ret != 0) ? AES_GCM_ERR_AUTH_FAIL : AES_GCM_OK;
}

/* ============================================================
 * 對外介面：SetKey 同時把兩條路徑都準備好（呼叫 Encrypt/Decrypt
 * 之前不知道這次資料量會不會超過硬體上限，兩邊都要隨時可用）
 * ============================================================ */

uint8_t AESGCM_SetKey(const uint8_t *key, uint16_t key_len)
{
    if (key == NULL) return AES_GCM_ERR_NULL_PTR;
    if (key_len != AES_GCM_KEY_SIZE) return AES_GCM_ERR_SETKEY;

    /* --- 硬體路徑 key --- */
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

        if (HAL_CRYP_Init(&hcryp) != HAL_OK) return AES_GCM_ERR_SETKEY;

        cryp_ready = 1;
    }
    else
    {
        CRYP_ApplyBaseConfig();

        if (HAL_CRYP_SetConfig(&hcryp, &hcryp.Init) != HAL_OK) return AES_GCM_ERR_SETKEY;
    }

    /* --- 軟體路徑 key ---
     * mbedtls_gcm_init 只需要呼叫一次（配置 context 記憶體），之後
     * 換 key 只要重新呼叫 mbedtls_gcm_setkey 更新內部的 key schedule
     * 即可，不需要每次都重新 init。 */
    if (!sw_key_is_set)
    {
        mbedtls_gcm_init(&gcm_ctx);
    }

    if (mbedtls_gcm_setkey(&gcm_ctx, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
    {
        sw_key_is_set = 0;
        return AES_GCM_ERR_SETKEY;
    }

    sw_key_is_set = 1;

    return AES_GCM_OK;
}

uint8_t AESGCM_Encrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                        uint8_t *output, uint8_t *tag)
{
    if (nonce == NULL || input == NULL || output == NULL || tag == NULL)
    {
        return AES_GCM_ERR_NULL_PTR;
    }

    if (input_len <= AES_GCM_HW_MAX_SIZE)
    {
        return HW_Encrypt(nonce, input, input_len, output, tag);
    }
    else
    {
        return SW_Encrypt(nonce, input, input_len, output, tag);
    }
}

uint8_t AESGCM_Decrypt(const uint8_t *nonce, const uint8_t *input, uint32_t input_len,
                        const uint8_t *tag, uint8_t *output)
{
    if (nonce == NULL || input == NULL || tag == NULL || output == NULL)
    {
        return AES_GCM_ERR_NULL_PTR;
    }

    if (input_len <= AES_GCM_HW_MAX_SIZE)
    {
        return HW_Decrypt(nonce, input, input_len, tag, output);
    }
    else
    {
        return SW_Decrypt(nonce, input, input_len, tag, output);
    }
}
