/**
 * @file main_stm32f4.c
 * @brief Parakram firmware entry point for STM32F407 (Nucleo-F407ZG).
 *
 * CubeMX-generated peripheral handles (hi2c1/2, hspi1/2, huart1/2/3,
 * htim3/4, hadc1, hi2s2) are declared extern here and defined in the
 * auto-generated stm32f4xx_hal_msp.c produced by CubeMX.
 * pal_impl.c picks them up via the same extern declarations.
 */

#include "stm32f4xx_hal.h"
#include "parakram_pal.h"
#include "driver_registry.h"
#include "vm.h"
#include "scheduler.h"
#include "event_bus.h"
#include "state_store.h"
#include "watchdog.h"

/* CubeMX-generated peripheral handles */
I2C_HandleTypeDef  hi2c1, hi2c2;
SPI_HandleTypeDef  hspi1, hspi2;
UART_HandleTypeDef huart1, huart2, huart3;
TIM_HandleTypeDef  htim3, htim4;
ADC_HandleTypeDef  hadc1;
I2S_HandleTypeDef  hi2s2;

/* Board pin assignments (Nucleo-F407ZG) */
#define PIN_STATUS_LED  32   /* PC0 = port C pin 0 = PAL pin 32 */
#define PIN_ERROR_LED   33   /* PC1 */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_USART2_Init(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* PAL init — enables DWT cycle counter */
    pal_init();

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_I2C2_Init();
    MX_USART2_Init();

    /* Status LEDs */
    pal_gpio_set_direction(PIN_STATUS_LED, PAL_GPIO_OUTPUT, false, false);
    pal_gpio_set_direction(PIN_ERROR_LED,  PAL_GPIO_OUTPUT, false, false);
    pal_gpio_set_level(PIN_STATUS_LED, 1);
    pal_gpio_set_level(PIN_ERROR_LED,  0);

    /* Firmware subsystems */
    state_store_init();
    event_bus_init();
    driver_registry_init();
    watchdog_init();
    scheduler_init();
    vm_init();

    PAL_LOGI("MAIN", "Parakram STM32F4 v1.0 ready — %lu MHz",
             SystemCoreClock / 1000000UL);

    while (1) {
        scheduler_tick();
        vm_tick();
        pal_feed_watchdog();
    }
}

/* ── Clock: 168 MHz via PLL from 8 MHz HSE ──────────────────────────────── */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 8;
    osc.PLL.PLLN       = 336;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

static void MX_I2C2_Init(void)
{
    hi2c2.Instance             = I2C2;
    hi2c2.Init.ClockSpeed      = 400000;
    hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1     = 0;
    hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
}

static void MX_USART2_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}
