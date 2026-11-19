/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * LED chain output: SPI2 transmit-only + DMA1 channel 5 on PC3.
 *
 * Each LED data bit becomes one SPI byte at 6 MHz (1.33 us per bit):
 *   0 -> 0xC0 (0.33 us high), 1 -> 0xF8 (0.83 us high).
 * The SPI runs in transmit-only mode (BIDIMODE=1, BIDIOE=1), so it never
 * receives anything. Zephyr's generic SPI driver insists on full-duplex DMA
 * on the STM32F0, and its receive DMA stalled after a few bytes, timing out
 * every frame; this driver avoids receiving altogether. DMA completion is
 * polled while the calling thread sleeps, so no interrupts are involved and
 * interrupts elsewhere cannot disturb the bit timing.
 *
 * Between frames the SPI output rests high, which the first LED does not
 * accept as the latch/reset (it then forwards every frame and never takes its
 * own colour; the others still work because the first LED's output rests
 * low). So PC3 is a plain GPIO driven low between frames and is handed to the
 * SPI only while a frame is sent. Each frame starts and ends with zero bytes,
 * so the hand-over happens while the line is low anyway.
 */

#include "led_spi_dma.h"

#include <errno.h>
#include <soc.h>
#include <string.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>

#define NODE DT_NODELABEL(led_strip)

#define BITS_PER_LED 24
#define ZERO_SYMBOL 0xC0
#define ONE_SYMBOL 0xF8
#define LATCH_US 300
/* Zero bytes before and after the LED data (1.33 us each). */
#define LEAD_BYTES 2
#define TAIL_BYTES 4

/* PC3: the LED data pin (alternate function 1 = SPI2_MOSI, see the DTS). */
#define DATA_PORT GPIOC
#define DATA_PIN 3

PINCTRL_DT_DEFINE(NODE);

static uint8_t tx_buf[LEAD_BYTES + LED_SPI_DMA_MAX_LEDS * BITS_PER_LED + TAIL_BYTES];

static inline void data_pin_gpio_low(void)
{
	DATA_PORT->BRR = BIT(DATA_PIN);
	DATA_PORT->MODER = (DATA_PORT->MODER & ~(3U << (2 * DATA_PIN))) | (1U << (2 * DATA_PIN));
}

static inline void data_pin_spi(void)
{
	DATA_PORT->MODER = (DATA_PORT->MODER & ~(3U << (2 * DATA_PIN))) | (2U << (2 * DATA_PIN));
}

static inline void encode_byte(uint8_t **p, uint8_t v)
{
	for (int b = 7; b >= 0; b--) {
		*(*p)++ = (v & BIT(b)) ? ONE_SYMBOL : ZERO_SYMBOL;
	}
}

int led_spi_dma_init(void)
{
	int ret = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(NODE), PINCTRL_STATE_DEFAULT);

	if (ret != 0) {
		return ret;
	}

	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
	RCC->AHBENR |= RCC_AHBENR_DMAEN;
	(void)RCC->APB1ENR;

	SPI2->CR1 = 0;
	/* 8-bit frames, TX DMA requests. */
	SPI2->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_TXDMAEN;
	/* Master, software NSS, fPCLK/8 = 6 MHz, mode 0, transmit-only. */
	SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (2U << SPI_CR1_BR_Pos) |
		    SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE;
	SPI2->CR1 |= SPI_CR1_SPE;
	/* Rest low until the first frame. */
	data_pin_gpio_low();

	DMA1_Channel5->CCR = 0;
	DMA1_Channel5->CPAR = (uint32_t)&SPI2->DR;
	return 0;
}

int led_spi_dma_show(const struct led_rgb *pixels, size_t count)
{
	uint8_t *p = tx_buf + LEAD_BYTES;
	size_t len;
	int waited_us = 0;

	if (count > LED_SPI_DMA_MAX_LEDS) {
		return -EINVAL;
	}
	for (size_t i = 0; i < count; i++) {
		/* On-wire order is G, R, B, most significant bit first. */
		encode_byte(&p, pixels[i].g);
		encode_byte(&p, pixels[i].r);
		encode_byte(&p, pixels[i].b);
	}
	if (p == tx_buf + LEAD_BYTES) {
		return 0;
	}
	memset(tx_buf, 0, LEAD_BYTES);
	memset(p, 0, TAIL_BYTES);
	len = (size_t)(p + TAIL_BYTES - tx_buf);

	DMA1_Channel5->CCR = 0;
	DMA1->IFCR = DMA_IFCR_CGIF5;
	DMA1_Channel5->CMAR = (uint32_t)tx_buf;
	DMA1_Channel5->CNDTR = len;
	/* Memory -> peripheral, memory increment, 8-bit, high priority. */
	DMA1_Channel5->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_PL_1 | DMA_CCR_EN;
	/* The SPI now shifts the leading zero bytes: take over the (low) line. */
	data_pin_spi();

	/* About 4.2 ms for 131 LEDs; sleep while the DMA works. */
	k_usleep((int32_t)(len * 4 / 3));
	while (!(DMA1->ISR & DMA_ISR_TCIF5) || (SPI2->SR & (SPI_SR_FTLVL | SPI_SR_BSY))) {
		k_usleep(50);
		waited_us += 50;
		if (waited_us > 20000) {
			data_pin_gpio_low();
			DMA1_Channel5->CCR = 0;
			return -ETIMEDOUT;
		}
	}
	/* Done: hold the line low ourselves (the idle SPI output is high). */
	data_pin_gpio_low();
	DMA1_Channel5->CCR = 0;
	DMA1->IFCR = DMA_IFCR_CGIF5;

	/* Hold the line low to latch the data. */
	k_usleep(LATCH_US);
	return 0;
}
