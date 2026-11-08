#pragma once
/*
 * FrSky R9M (2018/2019) hardware definition, from the ExpressLRS
 * Frsky_TX_R9M.h target. STM32F103C8 @ 12 MHz HSE, SX1276, external PA whose
 * gain is set by an I2C DAC.
 */

// Radio
#define GPIO_PIN_NSS                    PB12
#define GPIO_PIN_DIO0                   PA15
#define GPIO_PIN_MOSI                   PB15
#define GPIO_PIN_MISO                   PB14
#define GPIO_PIN_SCK                    PB13
#define GPIO_PIN_RST                    PC14

// RF switch / PA: APC1 enables the PA, APC2 is driven by the I2C DAC
#define GPIO_PIN_RFamp_APC1             PA6
#define GPIO_PIN_RFswitch_CONTROL       PB3  // HIGH = RX, LOW = TX
#define GPIO_PIN_RX_ENABLE              GPIO_PIN_RFswitch_CONTROL
#define GPIO_PIN_TX_ENABLE              GPIO_PIN_RFamp_APC1

// I2C (power DAC)
#define GPIO_PIN_SDA                    PB7
#define GPIO_PIN_SCL                    PB6

// LEDs. PA11/PA12 are also the STM32 USB D-/D+ pins.
#define GPIO_PIN_LED_RED                PA11
#define GPIO_PIN_LED_GREEN              PA12

// Fan header (fan mod), switched on for > 250mW
#define GPIO_PIN_FAN_EN                 PB9

// Power levels
enum
{
    PWR_10mW = 0,
    PWR_25mW,
    PWR_50mW,
    PWR_100mW,
    PWR_250mW,
    PWR_500mW,
    PWR_1000mW,
    PWR_2000mW,
};

#define POWER_OUTPUT_DAC                0b0001100  // I2C address
#define MinPower                        PWR_10mW
#define MaxPower                        PWR_1000mW
// DAC millivolts for PWR_10mW..PWR_2000mW
#define POWER_OUTPUT_VALUES             {720, 875, 1000, 1140, 1390, 1730, 2100, 2600}
#define POWER_OUTPUT_VALUES_868         {650, 860, 1000, 1160, 1420, 1730, 2100, 2600}
