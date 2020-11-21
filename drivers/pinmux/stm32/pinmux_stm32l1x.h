/*
 * Copyright (c) 2019 Linaro Ltd.
 * Copyright (c) 2019 Centaur Analytics, Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_PINMUX_STM32_PINMUX_STM32L1X_H_
#define ZEPHYR_DRIVERS_PINMUX_STM32_PINMUX_STM32L1X_H_

/**
 * @file Header for STM32L1X pin multiplexing helper
 */

/* Port A */
#define STM32L1X_PINMUX_FUNC_PA0_PWM2_CH1 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PA0_USART2_CTS __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PA0_ADC1_IN0 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA1_PWM2_CH2 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PA1_USART2_RTS __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PA1_ADC1_IN1 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA2_PWM2_CH3 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PA2_USART2_TX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PA2_ADC1_IN2 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA3_PWM2_CH4 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PA3_USART2_RX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PA3_ADC1_IN3 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA4_SPI1_NSS __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PA4_ADC1_IN4 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA4_DAC_OUT1 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA5_SPI1_SCK __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PA5_ADC1_IN5 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA5_DAC_OUT2 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA6_SPI1_MISO __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUPDR_PULL_DOWN)
#define STM32L1X_PINMUX_FUNC_PA6_ADC1_IN6 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA7_SPI1_MOSI __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUPDR_PULL_DOWN)
#define STM32L1X_PINMUX_FUNC_PA7_ADC1_IN7 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PA9_USART1_TX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PA10_USART1_RX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PA11_USART1_CTS __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PA12_USART1_RTS __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PA15_PWM2_CH1 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)

/* Port B */
#define STM32L1X_PINMUX_FUNC_PB0_PWM3_CH3 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB0_ADC1_IN8 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PB1_PWM3_CH4 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB1_ADC1_IN9 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PB3_PWM2_CH2 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB3_SPI1_SCK __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB4_PWM3_CH1 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB4_SPI1_MISO __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUPDR_PULL_DOWN)
#define STM32L1X_PINMUX_FUNC_PB5_PWM3_CH2 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB5_SPI1_MOSI __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUPDR_PULL_DOWN)
#define STM32L1X_PINMUX_FUNC_PB6_PWM4_CH1 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB6_I2C1_SCL __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_4 | STM32_OPENDRAIN_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB7_PWM4_CH2 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB7_I2C1_SDA __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_4 | STM32_OPENDRAIN_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB7_USART1_RX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PB8_PWM4_CH3 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB8_I2C1_SCL __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_4 | STM32_OPENDRAIN_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB9_PWM4_CH4 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB9_I2C1_SDA __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_4 | STM32_OPENDRAIN_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB10_PWM2_CH3 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB10_I2C2_SCL __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_4 | STM32_OPENDRAIN_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB10_USART3_TX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB11_PWM2_CH4 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_1 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB11_I2C2_SDA __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_4 | STM32_OPENDRAIN_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB11_USART3_RX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PB12_PWM10_CH1 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_3 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB12_SPI2_NSS __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PB12_ADC1_IN18 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PB13_PWM9_CH1 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_3 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB13_SPI2_SCK __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB13_ADC1_IN19 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PB14_PWM9_CH2 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_3 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB14_SPI2_MISO __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUPDR_PULL_DOWN)
#define STM32L1X_PINMUX_FUNC_PB14_ADC1_IN20 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PB15_PWM11_CH1 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_3 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PB15_SPI2_MOSI __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_5 | STM32_PUPDR_PULL_DOWN)
#define STM32L1X_PINMUX_FUNC_PB15_ADC1_IN21 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE

/* Port C */
#define STM32L1X_PINMUX_FUNC_PC0_ADC1_IN10 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PC1_ADC1_IN11 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PC2_ADC1_IN12 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PC3_ADC1_IN13 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PC4_ADC1_IN14 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PC5_ADC1_IN15 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PC6_PWM3_CH1 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PC7_PWM3_CH2 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PC8_PWM3_CH3 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PC9_PWM3_CH4 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PC10_USART3_TX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PC10_UART4_TX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_8 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PC11_USART3_RX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PC11_UART4_RX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_8 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PC12_UART5_TX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_8 | STM32_PUSHPULL_PULLUP)

/* Port D */
#define STM32L1X_PINMUX_FUNC_PD2_UART5_RX __DEPRECATED_MACRO \
	(STM32_PINMUX_ALT_FUNC_8 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PD5_USART2_TX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PD6_USART2_RX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PD8_USART3_TX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUSHPULL_PULLUP)
#define STM32L1X_PINMUX_FUNC_PD9_USART3_RX __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_7 | STM32_PUPDR_NO_PULL)
#define STM32L1X_PINMUX_FUNC_PD12_PWM4_CH1 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PD13_PWM4_CH2 __DEPRECATED_MACRO  \
	(STM32_PINMUX_ALT_FUNC_2 | STM32_PUSHPULL_NOPULL)

/* Port E */
#define STM32L1X_PINMUX_FUNC_PE0_PWM10_CH1 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_3 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PE1_PWM11_CH1 __DEPRECATED_MACRO   \
	(STM32_PINMUX_ALT_FUNC_3 | STM32_PUSHPULL_NOPULL)
#define STM32L1X_PINMUX_FUNC_PE7_ADC1_IN22 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PE8_ADC1_IN23 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PE9_ADC1_IN24 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PE10_ADC1_IN25 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE

/* Port F */
#define STM32L1X_PINMUX_FUNC_PF6_ADC1_IN27 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PF7_ADC1_IN28 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PF8_ADC1_IN29 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PF9_ADC1_IN30 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE
#define STM32L1X_PINMUX_FUNC_PF10_ADC1_IN31 __DEPRECATED_MACRO  \
	STM32_MODER_ANALOG_MODE

#endif /* ZEPHYR_DRIVERS_PINMUX_STM32_PINMUX_STM32L1X_H_ */
