/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "zd35_nand.h"

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#define NAND_NODE DT_NODELABEL(nand)

#define CMD_RESET            0xFF
#define CMD_READ_ID          0x9F
#define CMD_GET_FEATURE      0x0F
#define CMD_SET_FEATURE      0x1F
#define CMD_PAGE_READ        0x13
#define CMD_READ_CACHE       0x03
#define CMD_WRITE_ENABLE     0x06
#define CMD_PROG_LOAD        0x02
#define CMD_PROG_LOAD_RANDOM 0x84
#define CMD_PROG_EXEC        0x10
#define CMD_BLOCK_ERASE      0xD8

#define FEAT_PROTECT 0xA0
#define FEAT_CONFIG  0xB0
#define FEAT_STATUS  0xC0

#define STATUS_OIP      BIT(0)
#define STATUS_E_FAIL   BIT(2)
#define STATUS_P_FAIL   BIT(3)
#define STATUS_ECC_MASK (BIT(4) | BIT(5) | BIT(6))
#define STATUS_ECC_UNCORRECTABLE BIT(5) /* ECCS[2:0] = 010b */
#define CONFIG_ECC_EN   BIT(4)

#define SPI_OP (SPI_OP_MODE_CONTROLLER | SPI_TRANSFER_MSB | SPI_WORD_SET(8))

static const struct spi_dt_spec spi = SPI_DT_SPEC_GET(NAND_NODE, SPI_OP);
static const struct gpio_dt_spec wp = GPIO_DT_SPEC_GET_OR(NAND_NODE, wp_gpios, {0});
static const struct gpio_dt_spec hold = GPIO_DT_SPEC_GET_OR(NAND_NODE, hold_gpios, {0});

static K_MUTEX_DEFINE(nand_mutex);
static uint8_t chip_id[3];
static uint32_t blocks;

static int xfer(const uint8_t *hdr, size_t hdr_len, const void *tx, void *rx, size_t len)
{
	struct spi_buf txb[2] = {
		{.buf = (void *)hdr, .len = hdr_len},
		{.buf = (void *)tx, .len = tx ? len : 0},
	};
	struct spi_buf rxb[2] = {
		{.buf = NULL, .len = hdr_len},
		{.buf = rx, .len = rx ? len : 0},
	};
	struct spi_buf_set txs = {.buffers = txb, .count = tx ? 2 : 1};
	struct spi_buf_set rxs = {.buffers = rxb, .count = 2};

	if (rx == NULL) {
		return spi_write_dt(&spi, &txs);
	}
	return spi_transceive_dt(&spi, &txs, &rxs);
}

static int cmd(uint8_t c)
{
	return xfer(&c, 1, NULL, NULL, 0);
}

static int get_feature(uint8_t reg, uint8_t *val)
{
	uint8_t hdr[2] = {CMD_GET_FEATURE, reg};

	return xfer(hdr, sizeof(hdr), NULL, val, 1);
}

static int set_feature(uint8_t reg, uint8_t val)
{
	uint8_t hdr[3] = {CMD_SET_FEATURE, reg, val};

	return xfer(hdr, sizeof(hdr), NULL, NULL, 0);
}

static int wait_ready(uint32_t timeout_us, uint8_t *status)
{
	uint32_t waited = 0;
	uint8_t st;
	int ret;

	do {
		ret = get_feature(FEAT_STATUS, &st);
		if (ret != 0) {
			return ret;
		}
		if (!(st & STATUS_OIP)) {
			if (status != NULL) {
				*status = st;
			}
			return 0;
		}
		k_busy_wait(20);
		waited += 20;
	} while (waited < timeout_us);
	return -ETIMEDOUT;
}

static uint16_t column(uint32_t block, uint16_t col)
{
	if (IS_ENABLED(CONFIG_STAR_NAND_PLANE_SELECT)) {
		col |= (uint16_t)((block & 1U) << 12);
	}
	return col;
}

static int page_read_to_cache(uint32_t block, uint32_t page, uint8_t *status)
{
	uint32_t row = block * NAND_PAGES_PER_BLOCK + page;
	uint8_t hdr[4] = {CMD_PAGE_READ, (uint8_t)(row >> 16), (uint8_t)(row >> 8), (uint8_t)row};
	int ret = xfer(hdr, sizeof(hdr), NULL, NULL, 0);

	return ret ? ret : wait_ready(1000, status);
}

void nand_lock(void)
{
	k_mutex_lock(&nand_mutex, K_FOREVER);
}

void nand_unlock(void)
{
	k_mutex_unlock(&nand_mutex);
}

int nand_init(void)
{
	uint8_t cfg;
	uint8_t hdr[2] = {CMD_READ_ID, 0x00};
	int ret;

	/* WP and HOLD have no pull-ups on the board; drive them inactive. */
	if (wp.port != NULL) {
		gpio_pin_configure_dt(&wp, GPIO_OUTPUT_INACTIVE);
	}
	if (hold.port != NULL) {
		gpio_pin_configure_dt(&hold, GPIO_OUTPUT_INACTIVE);
	}
	if (!spi_is_ready_dt(&spi)) {
		return -ENODEV;
	}

	nand_lock();
	ret = cmd(CMD_RESET);
	if (ret == 0) {
		k_msleep(2);
		ret = wait_ready(5000, NULL);
	}
	if (ret == 0) {
		ret = xfer(hdr, sizeof(hdr), NULL, chip_id, sizeof(chip_id));
	}
	if (ret == 0 && (chip_id[0] == 0x00 || chip_id[0] == 0xFF)) {
		ret = -ENODEV;
	}
	/* Unlock all blocks and make sure on-die ECC is enabled. */
	if (ret == 0) {
		ret = set_feature(FEAT_PROTECT, 0x00);
	}
	if (ret == 0) {
		ret = get_feature(FEAT_CONFIG, &cfg);
	}
	if (ret == 0 && !(cfg & CONFIG_ECC_EN)) {
		ret = set_feature(FEAT_CONFIG, cfg | CONFIG_ECC_EN);
	}
	if (ret == 0) {
		blocks = DT_PROP(NAND_NODE, block_count);
	}
	nand_unlock();
	return ret;
}

uint32_t nand_block_count(void)
{
	return blocks;
}

void nand_get_id(uint8_t id[3])
{
	for (int i = 0; i < 3; i++) {
		id[i] = chip_id[i];
	}
}

int nand_read(uint32_t block, uint32_t page, uint16_t col, void *buf, size_t len)
{
	uint8_t status;
	int ret;

	if (block >= blocks || page >= NAND_PAGES_PER_BLOCK ||
	    col + len > NAND_PAGE_SIZE + NAND_SPARE_SIZE) {
		return -EINVAL;
	}

	nand_lock();
	ret = page_read_to_cache(block, page, &status);
	if (ret == 0) {
		uint16_t c = column(block, col);
		uint8_t hdr[4] = {CMD_READ_CACHE, (uint8_t)(c >> 8), (uint8_t)c, 0x00};

		ret = xfer(hdr, sizeof(hdr), NULL, buf, len);
	}
	nand_unlock();

	if (ret == 0 && (status & STATUS_ECC_MASK) == STATUS_ECC_UNCORRECTABLE) {
		ret = -EIO;
	}
	return ret;
}

int nand_load(uint32_t block, uint16_t col, const void *data, size_t len, bool first)
{
	int ret;

	if (block >= blocks || col + len > NAND_PAGE_SIZE + NAND_SPARE_SIZE) {
		return -EINVAL;
	}

	nand_lock();
	ret = first ? cmd(CMD_WRITE_ENABLE) : 0;
	if (ret == 0) {
		uint16_t c = column(block, col);
		uint8_t hdr[3] = {first ? CMD_PROG_LOAD : CMD_PROG_LOAD_RANDOM, (uint8_t)(c >> 8),
				  (uint8_t)c};

		ret = xfer(hdr, sizeof(hdr), data, NULL, len);
	}
	nand_unlock();
	return ret;
}

int nand_program(uint32_t block, uint32_t page)
{
	uint32_t row = block * NAND_PAGES_PER_BLOCK + page;
	uint8_t hdr[4] = {CMD_PROG_EXEC, (uint8_t)(row >> 16), (uint8_t)(row >> 8), (uint8_t)row};
	uint8_t status = 0;
	int ret;

	if (block >= blocks || page >= NAND_PAGES_PER_BLOCK) {
		return -EINVAL;
	}

	nand_lock();
	ret = xfer(hdr, sizeof(hdr), NULL, NULL, 0);
	if (ret == 0) {
		ret = wait_ready(2000, &status);
	}
	nand_unlock();

	if (ret == 0 && (status & STATUS_P_FAIL)) {
		ret = -EIO;
	}
	return ret;
}

int nand_erase(uint32_t block)
{
	uint32_t row = block * NAND_PAGES_PER_BLOCK;
	uint8_t hdr[4] = {CMD_BLOCK_ERASE, (uint8_t)(row >> 16), (uint8_t)(row >> 8), (uint8_t)row};
	uint8_t status = 0;
	int ret;

	if (block >= blocks) {
		return -EINVAL;
	}

	nand_lock();
	ret = cmd(CMD_WRITE_ENABLE);
	if (ret == 0) {
		ret = xfer(hdr, sizeof(hdr), NULL, NULL, 0);
	}
	if (ret == 0) {
		ret = wait_ready(20000, &status);
	}
	nand_unlock();

	if (ret == 0 && (status & STATUS_E_FAIL)) {
		ret = -EIO;
	}
	return ret;
}

bool nand_block_is_bad(uint32_t block)
{
	uint8_t marker = 0;

	/* ECC errors on the spare area still give a usable marker byte. */
	int ret = nand_read(block, 0, NAND_PAGE_SIZE, &marker, 1);

	return (ret != 0 && ret != -EIO) || marker != 0xFF;
}

int nand_mark_bad(uint32_t block)
{
	static const uint8_t zero;
	int ret;

	nand_lock();
	(void)nand_erase(block);
	ret = nand_load(block, NAND_PAGE_SIZE, &zero, 1, true);
	if (ret == 0) {
		ret = nand_program(block, 0);
	}
	nand_unlock();
	return ret;
}
