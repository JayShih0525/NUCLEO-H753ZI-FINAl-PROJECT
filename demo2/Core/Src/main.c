#include "main.h"
#include "spi_slave.h"
#include "crypto_app.h"
#include "randombytes.h"

// =====================================================
// NOTE ON THIS FILE
// =====================================================
// Your original SPI-camera CubeMX project already has its
// own main.c / gpio.c / spi.c generated from the .ioc file.
// I never saw that main.c's body - only stm32h7xx_hal_msp.c
// from it. SystemClock_Config below is reused as-is from
// your Dilithium/KEM UART main.c (I *did* see that one in
// full, so it's real). MX_SPI1_Init and the READY pin in
// MX_GPIO_Init are reconstructed from the MSP file you sent
// earlier (SPI1 on PA4/PA5 + PB4/PB5, slave mode, hard NSS)
// plus the READY pin assumption from spi_slave.h (PC6).
//
// If your actual CubeMX project already has MX_SPI1_Init()
// and MX_GPIO_Init() (likely in separate spi.c/gpio.c
// files), use THOSE instead of the versions below - just
// make sure the READY pin still gets configured as a
// push-pull output, and copy the calls at the bottom of
// main() (SPI_SlaveInit / CryptoApp_Init / the
// SPI_HandleOneRequest loop) into your real main() - and
// make sure YOUR real project actually calls HAL_RNG_Init()
// somewhere. This bug (missing RNG init) has now recurred
// twice - if you're hand-merging this into another copy of
// main.c, double check the RNG lines survive the merge.
// =====================================================

SPI_HandleTypeDef hspi1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();

    Random_Init();

    SPI_SlaveInit();
    CryptoApp_Init();

    while (1)
    {
        SPI_HandleOneRequest();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    /*
     * HSI = 64 MHz
     * PLL input  = HSI / PLLM = 64 / 4 = 16 MHz
     * PLL VCO    = 16 * PLLN = 16 * 50 = 800 MHz
     * SYSCLK     = VCO / PLLP = 800 / 2 = 400 MHz
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 50;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK  |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2 |
        RCC_CLOCKTYPE_D3PCLK1 |
        RCC_CLOCKTYPE_D1PCLK1;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }

    // NOTE: SPI1's own kernel clock (RCC_PERIPHCLK_SPI1 / PLL)
    // is already configured inside HAL_SPI_MspInit() in
    // stm32h7xx_hal_msp.c (the file you sent earlier) - it
    // runs automatically when HAL_SPI_Init() is called below,
    // so it is intentionally NOT repeated here.
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    // READY handshake line to the ESP32 - see spi_slave.h
    // (SPI_READY_GPIO_Port / SPI_READY_Pin). Confirm this
    // matches the pin you actually wired; PC6 is carried
    // over from the original project's GPIO init.
    HAL_GPIO_WritePin(SPI_READY_GPIO_Port, SPI_READY_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = SPI_READY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SPI_READY_GPIO_Port, &GPIO_InitStruct);
}

static void MX_SPI1_Init(void)
{
    // Reconstructed from stm32h7xx_hal_msp.c's HAL_SPI_MspInit
    // (SPI1 on PA4/PA5 + PB4/PB5, hard NSS input, slave mode).
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_SLAVE;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 0x0U;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
