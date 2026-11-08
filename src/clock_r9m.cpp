/*
 * System clock for the R9M.
 *
 * The Arduino "bluepill_f103c8" variant assumes an 8 MHz crystal and sets the
 * PLL to x9 (72 MHz). The R9M has a 12 MHz crystal, so that setting runs the
 * F103 at 108 MHz (APB1 at 54 MHz), out of spec, and gives a 72 MHz USB clock
 * where USB needs exactly 48 MHz. This replaces the variant's weak
 * SystemClock_Config():
 *
 *   12 MHz HSE x6 = 72 MHz SYSCLK/HCLK, APB1 = 36 MHz, APB2 = 72 MHz,
 *   USB = 72 / 1.5 = 48 MHz, ADC = 72 / 6 = 12 MHz
 */
#include <Arduino.h>

#if HSE_VALUE != 12000000U
#error "clock_r9m.cpp expects the R9M 12 MHz crystal (HSE_VALUE=12000000U)"
#endif

extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};
    RCC_PeriphCLKInitTypeDef periph = {};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL6;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();

    periph.PeriphClockSelection = RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_USB;
    periph.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    periph.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
        Error_Handler();
}
