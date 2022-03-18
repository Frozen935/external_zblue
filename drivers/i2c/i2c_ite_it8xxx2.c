/*
 * Copyright (c) 2020 ITE Corporation. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ite_it8xxx2_i2c

#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <drivers/pinmux.h>
#include <errno.h>
#include <soc.h>
#include <soc_dt.h>
#include <sys/util.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_ite_it8xxx2, CONFIG_I2C_LOG_LEVEL);

#include "i2c-priv.h"

/* Start smbus session from idle state */
#define I2C_MSG_START BIT(5)

#define I2C_LINE_SCL_HIGH BIT(0)
#define I2C_LINE_SDA_HIGH BIT(1)
#define I2C_LINE_IDLE (I2C_LINE_SCL_HIGH | I2C_LINE_SDA_HIGH)

/*
 * Structure i2c_alts_cfg is about the alternate function
 * setting of i2c, this config will be used at initial
 * time and recover bus.
 */
struct i2c_alts_cfg {
	/* Pinmux control group */
	const struct device *pinctrls;
	/* GPIO pin */
	uint8_t pin;
	/* Alternate function */
	uint8_t alt_fun;
};

struct i2c_it8xxx2_config {
	void (*irq_config_func)(void);
	uint32_t bitrate;
	uint8_t *base;
	uint8_t i2c_irq_base;
	uint8_t port;
	/* I2C alternate configuration */
	const struct i2c_alts_cfg *alts_list;
	/* GPIO handle */
	const struct device *gpio_dev;
	uint32_t clock_gate_offset;
};

enum i2c_pin_fun {
	SCL = 0,
	SDA,
};

enum i2c_ch_status {
	I2C_CH_NORMAL = 0,
	I2C_CH_REPEAT_START,
	I2C_CH_WAIT_READ,
	I2C_CH_WAIT_NEXT_XFER,
};

struct i2c_it8xxx2_data {
	enum i2c_ch_status i2ccs;
	struct i2c_msg *msgs;
	struct k_mutex mutex;
	struct k_sem device_sync_sem;
	/* Index into output data */
	size_t widx;
	/* Index into input data */
	size_t ridx;
	/* operation freq of i2c */
	uint32_t bus_freq;
	/* Error code, if any */
	uint32_t err;
	/* address of device */
	uint16_t addr_16bit;
	/* Frequency setting */
	uint8_t freq;
	/* wait for stop bit interrupt */
	uint8_t stop;
};

enum i2c_host_status {
	/* Host busy */
	HOSTA_HOBY = 0x01,
	/* Finish Interrupt */
	HOSTA_FINTR = 0x02,
	/* Device error */
	HOSTA_DVER = 0x04,
	/* Bus error */
	HOSTA_BSER = 0x08,
	/* Fail */
	HOSTA_FAIL = 0x10,
	/* Not response ACK */
	HOSTA_NACK = 0x20,
	/* Time-out error */
	HOSTA_TMOE = 0x40,
	/* Byte done status */
	HOSTA_BDS = 0x80,
	/* Error bit is set */
	HOSTA_ANY_ERROR = (HOSTA_DVER | HOSTA_BSER | HOSTA_FAIL |
			   HOSTA_NACK | HOSTA_TMOE),
	/* W/C for next byte */
	HOSTA_NEXT_BYTE = HOSTA_BDS,
	/* W/C host status register */
	HOSTA_ALL_WC_BIT = (HOSTA_FINTR | HOSTA_ANY_ERROR | HOSTA_BDS),
};

enum i2c_reset_cause {
	I2C_RC_NO_IDLE_FOR_START = 1,
	I2C_RC_TIMEOUT,
};

static int i2c_parsing_return_value(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;

	if (!data->err) {
		return 0;
	}

	/* Connection timed out */
	if (data->err == ETIMEDOUT) {
		return -ETIMEDOUT;
	}

	/* The device does not respond ACK */
	if (data->err == HOSTA_NACK) {
		return -ENXIO;
	} else {
		return -EIO;
	}
}

static int i2c_get_line_levels(const struct device *dev)
{
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	return (IT8XXX2_SMB_SMBPCTL(base) &
		(IT8XXX2_SMB_SMBDCS | IT8XXX2_SMB_SMBCS));
}

static int i2c_is_busy(const struct device *dev)
{
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	return (IT8XXX2_SMB_HOSTA(base) &
		(HOSTA_HOBY | HOSTA_ALL_WC_BIT));
}

static int i2c_bus_not_available(const struct device *dev)
{
	if (i2c_is_busy(dev) ||
		(i2c_get_line_levels(dev) != I2C_LINE_IDLE)) {
		return -EIO;
	}

	return 0;
}

static void i2c_reset(const struct device *dev)
{
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	/* bit1, kill current transaction. */
	IT8XXX2_SMB_HOCTL(base) = IT8XXX2_SMB_KILL;
	IT8XXX2_SMB_HOCTL(base) = 0;
	/* W/C host status register */
	IT8XXX2_SMB_HOSTA(base) = HOSTA_ALL_WC_BIT;
}

/*
 * Set i2c standard port (A, B, or C) runs at 400kHz by using timing registers
 * (offset 0h ~ 7h).
 */
static void i2c_standard_port_timing_regs_400khz(uint8_t port)
{
	/* Port clock frequency depends on setting of timing registers. */
	IT8XXX2_SMB_SCLKTS(port) = 0;
	/* Suggested setting of timing registers of 400kHz. */
	IT8XXX2_SMB_4P7USL = 0x3;
	IT8XXX2_SMB_4P0USL = 0;
	IT8XXX2_SMB_300NS = 0x1;
	IT8XXX2_SMB_250NS = 0x5;
	IT8XXX2_SMB_45P3USL = 0x6a;
	IT8XXX2_SMB_45P3USH = 0x1;
	IT8XXX2_SMB_4P7A4P0H = 0;
}

/* Set clock frequency for i2c port A, B , or C */
static void i2c_standard_port_set_frequency(const struct device *dev,
					    int freq_hz, int freq_set)
{
	const struct i2c_it8xxx2_config *config = dev->config;

	/*
	 * If port's clock frequency is 400kHz, we use timing registers
	 * for setting. So we can adjust tlow to meet timing.
	 * The others use basic 50/100/1000 KHz setting.
	 */
	if (freq_hz == I2C_BITRATE_FAST) {
		i2c_standard_port_timing_regs_400khz(config->port);
	} else {
		IT8XXX2_SMB_SCLKTS(config->port) = freq_set;
	}

	/* This field defines the SMCLK0/1/2 clock/data low timeout. */
	IT8XXX2_SMB_25MS = I2C_CLK_LOW_TIMEOUT;
}

static int i2c_it8xxx2_configure(const struct device *dev,
				 uint32_t dev_config_raw)
{
	const struct i2c_it8xxx2_config *config = dev->config;
	struct i2c_it8xxx2_data *const data = dev->data;
	uint32_t freq_set;

	if (!(I2C_MODE_MASTER & dev_config_raw)) {
		return -EINVAL;
	}

	if (I2C_ADDR_10_BITS & dev_config_raw) {
		return -EINVAL;
	}

	data->bus_freq = I2C_SPEED_GET(dev_config_raw);

	switch (data->bus_freq) {
	case I2C_SPEED_DT:
		freq_set = IT8XXX2_SMB_SMCLKS_50K;
		break;
	case I2C_SPEED_STANDARD:
		freq_set = IT8XXX2_SMB_SMCLKS_100K;
		break;
	case I2C_SPEED_FAST:
		freq_set = IT8XXX2_SMB_SMCLKS_400K;
		break;
	case I2C_SPEED_FAST_PLUS:
		freq_set = IT8XXX2_SMB_SMCLKS_1M;
		break;
	default:
		return -EINVAL;
	}

	i2c_standard_port_set_frequency(dev, config->bitrate, freq_set);

	return 0;
}

static int i2c_it8xxx2_get_config(const struct device *dev,
				  uint32_t *dev_config)
{
	struct i2c_it8xxx2_data *const data = dev->data;
	uint32_t speed;

	if (!data->bus_freq) {
		LOG_ERR("The bus frequency is not initially configured.");
		return -EIO;
	}

	switch (data->bus_freq) {
	case I2C_SPEED_DT:
	case I2C_SPEED_STANDARD:
	case I2C_SPEED_FAST:
	case I2C_SPEED_FAST_PLUS:
		speed = I2C_SPEED_SET(data->bus_freq);
		break;
	default:
		return -ERANGE;
	}

	*dev_config = (I2C_MODE_MASTER | speed);

	return 0;
}

static void i2c_r_last_byte(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	/*
	 * bit5, The firmware shall write 1 to this bit
	 * when the next byte will be the last byte for i2c read.
	 */
	if ((data->msgs->flags & I2C_MSG_STOP) &&
	    (data->ridx == data->msgs->len - 1)) {
		IT8XXX2_SMB_HOCTL(base) |= IT8XXX2_SMB_LABY;
	}
}

static void i2c_w2r_change_direction(const struct device *dev)
{
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	/* I2C switch direction */
	if (IT8XXX2_SMB_HOCTL2(base) & IT8XXX2_SMB_I2C_SW_EN) {
		i2c_r_last_byte(dev);
		IT8XXX2_SMB_HOSTA(base) = HOSTA_NEXT_BYTE;
	} else {
		/*
		 * bit2, I2C switch direction wait.
		 * bit3, I2C switch direction enable.
		 */
		IT8XXX2_SMB_HOCTL2(base) |= IT8XXX2_SMB_I2C_SW_EN |
					    IT8XXX2_SMB_I2C_SW_WAIT;
		IT8XXX2_SMB_HOSTA(base) = HOSTA_NEXT_BYTE;
		i2c_r_last_byte(dev);
		IT8XXX2_SMB_HOCTL2(base) &= ~IT8XXX2_SMB_I2C_SW_WAIT;
	}
}

static int i2c_tran_read(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	if (data->msgs->flags & I2C_MSG_START) {
		/* i2c enable */
		IT8XXX2_SMB_HOCTL2(base) = IT8XXX2_SMB_SMD_TO_EN |
					   IT8XXX2_SMB_I2C_EN |
					   IT8XXX2_SMB_SMHEN;
		/*
		 * bit0, Direction of the host transfer.
		 * bit[1:7}, Address of the targeted slave.
		 */
		IT8XXX2_SMB_TRASLA(base) = (uint8_t)(data->addr_16bit << 1) |
					   IT8XXX2_SMB_DIR;
		/* clear start flag */
		data->msgs->flags &= ~I2C_MSG_START;
		/*
		 * bit0, Host interrupt enable.
		 * bit[2:4}, Extend command.
		 * bit5, The firmware shall write 1 to this bit
		 *       when the next byte will be the last byte.
		 * bit6, start.
		 */
		if ((data->msgs->len == 1) &&
		    (data->msgs->flags & I2C_MSG_STOP)) {
			IT8XXX2_SMB_HOCTL(base) = IT8XXX2_SMB_SRT |
						  IT8XXX2_SMB_LABY |
						  IT8XXX2_SMB_SMCD_EXTND |
						  IT8XXX2_SMB_INTREN;
		} else {
			IT8XXX2_SMB_HOCTL(base) = IT8XXX2_SMB_SRT |
						  IT8XXX2_SMB_SMCD_EXTND |
						  IT8XXX2_SMB_INTREN;
		}
	} else {
		if ((data->i2ccs == I2C_CH_REPEAT_START) ||
		    (data->i2ccs == I2C_CH_WAIT_READ)) {
			if (data->i2ccs == I2C_CH_REPEAT_START) {
				/* write to read */
				i2c_w2r_change_direction(dev);
			} else {
				/* For last byte */
				i2c_r_last_byte(dev);
				/* W/C for next byte */
				IT8XXX2_SMB_HOSTA(base) = HOSTA_NEXT_BYTE;
			}
			data->i2ccs = I2C_CH_NORMAL;
			irq_enable(config->i2c_irq_base);
		} else if (IT8XXX2_SMB_HOSTA(base) & HOSTA_BDS) {
			if (data->ridx < data->msgs->len) {
				/* To get received data. */
				*(data->msgs->buf++) = IT8XXX2_SMB_HOBDB(base);
				data->ridx++;
				/* For last byte */
				i2c_r_last_byte(dev);
				/* done */
				if (data->ridx == data->msgs->len) {
					data->msgs->len = 0;
					if (data->msgs->flags & I2C_MSG_STOP) {
						/* W/C for finish */
						IT8XXX2_SMB_HOSTA(base) =
						HOSTA_NEXT_BYTE;

						data->stop = 1;
					} else {
						data->i2ccs = I2C_CH_WAIT_READ;
						return 0;
					}
				} else {
					/* W/C for next byte */
					IT8XXX2_SMB_HOSTA(base) =
							HOSTA_NEXT_BYTE;
				}
			}
		}
	}
	return 1;

}

static int i2c_tran_write(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	if (data->msgs->flags & I2C_MSG_START) {
		/* i2c enable */
		IT8XXX2_SMB_HOCTL2(base) = IT8XXX2_SMB_SMD_TO_EN |
					   IT8XXX2_SMB_I2C_EN |
					   IT8XXX2_SMB_SMHEN;
		/*
		 * bit0, Direction of the host transfer.
		 * bit[1:7}, Address of the targeted slave.
		 */
		IT8XXX2_SMB_TRASLA(base) = (uint8_t)data->addr_16bit << 1;
		/* Send first byte */
		IT8XXX2_SMB_HOBDB(base) = *(data->msgs->buf++);

		data->widx++;
		/* clear start flag */
		data->msgs->flags &= ~I2C_MSG_START;
		/*
		 * bit0, Host interrupt enable.
		 * bit[2:4}, Extend command.
		 * bit6, start.
		 */
		IT8XXX2_SMB_HOCTL(base) = IT8XXX2_SMB_SRT |
					  IT8XXX2_SMB_SMCD_EXTND |
					  IT8XXX2_SMB_INTREN;
	} else {
		/* Host has completed the transmission of a byte */
		if (IT8XXX2_SMB_HOSTA(base) & HOSTA_BDS) {
			if (data->widx < data->msgs->len) {
				/* Send next byte */
				IT8XXX2_SMB_HOBDB(base) = *(data->msgs->buf++);

				data->widx++;
				/* W/C byte done for next byte */
				IT8XXX2_SMB_HOSTA(base) = HOSTA_NEXT_BYTE;

				if (data->i2ccs == I2C_CH_REPEAT_START) {
					data->i2ccs = I2C_CH_NORMAL;
					irq_enable(config->i2c_irq_base);
				}
			} else {
				/* done */
				data->msgs->len = 0;
				if (data->msgs->flags & I2C_MSG_STOP) {
					/* set I2C_EN = 0 */
					IT8XXX2_SMB_HOCTL2(base) = IT8XXX2_SMB_SMD_TO_EN |
								   IT8XXX2_SMB_SMHEN;
					/* W/C byte done for finish */
					IT8XXX2_SMB_HOSTA(base) = HOSTA_NEXT_BYTE;

					data->stop = 1;
				} else {
					data->i2ccs = I2C_CH_REPEAT_START;
					return 0;
				}
			}
		}
	}
	return 1;

}

static int i2c_transaction(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;

	/* any error */
	if (IT8XXX2_SMB_HOSTA(base) & HOSTA_ANY_ERROR) {
		data->err = (IT8XXX2_SMB_HOSTA(base) & HOSTA_ANY_ERROR);
	} else {
		if (!data->stop) {
			if (data->msgs->flags & I2C_MSG_READ) {
				return i2c_tran_read(dev);
			} else {
				return i2c_tran_write(dev);
			}
		}
		/* wait finish */
		if (!(IT8XXX2_SMB_HOSTA(base) & HOSTA_FINTR)) {
			return 1;
		}
	}
	/* W/C */
	IT8XXX2_SMB_HOSTA(base) = HOSTA_ALL_WC_BIT;
	/* disable the SMBus host interface */
	IT8XXX2_SMB_HOCTL2(base) = 0x00;

	data->stop = 0;
	/* done doing work */
	return 0;
}

static int i2c_it8xxx2_transfer(const struct device *dev, struct i2c_msg *msgs,
				uint8_t num_msgs, uint16_t addr)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;
	int res;

	/* Lock mutex of i2c controller */
	k_mutex_lock(&data->mutex, K_FOREVER);
	/*
	 * If the transaction of write to read is divided into two
	 * transfers, the repeat start transfer uses this flag to
	 * exclude checking bus busy.
	 */
	if (data->i2ccs == I2C_CH_NORMAL) {
		/* Make sure we're in a good state to start */
		if (i2c_bus_not_available(dev)) {
			/* Recovery I2C bus */
			i2c_recover_bus(dev);
			/*
			 * After resetting I2C bus, if I2C bus is not available
			 * (No external pull-up), drop the transaction.
			 */
			if (i2c_bus_not_available(dev)) {
				/* Unlock mutex of i2c controller */
				k_mutex_unlock(&data->mutex);
				return -EIO;
			}
		}

		msgs->flags |= I2C_MSG_START;
	}

	for (int i = 0; i < num_msgs; i++) {

		data->widx = 0;
		data->ridx = 0;
		data->err = 0;
		data->msgs = &(msgs[i]);
		data->addr_16bit = addr;

		if (msgs->flags & I2C_MSG_START) {
			data->i2ccs = I2C_CH_NORMAL;
			/* enable i2c interrupt */
			irq_enable(config->i2c_irq_base);
		}
		/* Start transaction */
		i2c_transaction(dev);
		/* Wait for the transfer to complete */
		/* TODO: the timeout should be adjustable */
		res = k_sem_take(&data->device_sync_sem, K_MSEC(100));
		/*
		 * The irq will be enabled at the condition of start or
		 * repeat start of I2C. If timeout occurs without being
		 * wake up during suspend(ex: interrupt is not fired),
		 * the irq should be disabled immediately.
		 */
		irq_disable(config->i2c_irq_base);
		/*
		 * The transaction is dropped on any error(timeout, NACK, fail,
		 * bus error, device error).
		 */
		if (data->err) {
			break;
		}

		if (res != 0) {
			data->err = ETIMEDOUT;
			/* reset i2c port */
			i2c_reset(dev);
			LOG_ERR("I2C ch%d:0x%X reset cause %d",
				config->port, data->addr_16bit, I2C_RC_TIMEOUT);
			/* If this message is sent fail, drop the transaction. */
			break;
		}
	}

	/* reset i2c channel status */
	if (data->err || (msgs->flags & I2C_MSG_STOP)) {
		data->i2ccs = I2C_CH_NORMAL;
	}
	/* Unlock mutex of i2c controller */
	k_mutex_unlock(&data->mutex);

	return i2c_parsing_return_value(dev);
}

static void i2c_it8xxx2_isr(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;

	/* If done doing work, wake up the task waiting for the transfer */
	if (!i2c_transaction(dev)) {
		k_sem_give(&data->device_sync_sem);
		irq_disable(config->i2c_irq_base);
	}
}

static int i2c_it8xxx2_init(const struct device *dev)
{
	struct i2c_it8xxx2_data *data = dev->data;
	const struct i2c_it8xxx2_config *config = dev->config;
	uint8_t *base = config->base;
	uint32_t bitrate_cfg;
	int error;

	/*
	 * This register is a pre-define hardware slave A and can
	 * be accessed through I2C0. It is not currently used, so
	 * it can be disabled to avoid illegal access.
	 */
	IT8XXX2_SMB_SFFCTL &= ~IT8XXX2_SMB_HSAPE;

	/* Initialize mutex and semaphore */
	k_mutex_init(&data->mutex);
	k_sem_init(&data->device_sync_sem, 0, K_SEM_MAX_LIMIT);

	/* Enable clock to specified peripheral */
	volatile uint8_t *reg = (volatile uint8_t *)
		(IT8XXX2_ECPM_BASE + (config->clock_gate_offset >> 8));
	uint8_t reg_mask = config->clock_gate_offset & 0xff;
	*reg &= ~reg_mask;

	/* Enable SMBus function */
	/*
	 * bit0, The SMBus host interface is enabled.
	 * bit1, Enable to communicate with I2C device
	 *		  and support I2C-compatible cycles.
	 * bit4, This bit controls the reset mechanism
	 *		  of SMBus master to handle the SMDAT
	 *		  line low if 25ms reg timeout.
	 */
	IT8XXX2_SMB_HOCTL2(base) = IT8XXX2_SMB_SMD_TO_EN | IT8XXX2_SMB_SMHEN;
	/*
	 * bit1, Kill SMBus host transaction.
	 * bit0, Enable the interrupt for the master interface.
	 */
	IT8XXX2_SMB_HOCTL(base) = IT8XXX2_SMB_KILL | IT8XXX2_SMB_SMHEN;
	IT8XXX2_SMB_HOCTL(base) = IT8XXX2_SMB_SMHEN;

	/* W/C host status register */
	IT8XXX2_SMB_HOSTA(base) = HOSTA_ALL_WC_BIT;
	IT8XXX2_SMB_HOCTL2(base) = 0x00;

	/* Set clock frequency for I2C ports */
	if (config->bitrate == I2C_BITRATE_STANDARD ||
		config->bitrate == I2C_BITRATE_FAST ||
		config->bitrate == I2C_BITRATE_FAST_PLUS) {
		bitrate_cfg = i2c_map_dt_bitrate(config->bitrate);
	} else {
		/* Device tree specified speed */
		bitrate_cfg = I2C_SPEED_DT << I2C_SPEED_SHIFT;
	}

	error = i2c_it8xxx2_configure(dev, I2C_MODE_MASTER | bitrate_cfg);
	data->i2ccs = I2C_CH_NORMAL;

	if (error) {
		LOG_ERR("i2c: failure initializing");
		return error;
	}

	/* The pin is set to I2C alternate function of SCL */
	pinmux_pin_set(config->alts_list[SCL].pinctrls,
		       config->alts_list[SCL].pin,
		       config->alts_list[SCL].alt_fun);
	/* The pin is set to I2C alternate function of SDA */
	pinmux_pin_set(config->alts_list[SDA].pinctrls,
		       config->alts_list[SDA].pin,
		       config->alts_list[SDA].alt_fun);

	return 0;
}

static int i2c_it8xxx2_recover_bus(const struct device *dev)
{
	const struct i2c_it8xxx2_config *config = dev->config;
	int i;

	/* Set SCL of I2C as GPIO pin */
	pinmux_pin_input_enable(config->alts_list[SCL].pinctrls,
				config->alts_list[SCL].pin,
				PINMUX_OUTPUT_ENABLED);
	/* Set SDA of I2C as GPIO pin */
	pinmux_pin_input_enable(config->alts_list[SDA].pinctrls,
				config->alts_list[SDA].pin,
				PINMUX_OUTPUT_ENABLED);

	/*
	 * In I2C recovery bus, 1ms sleep interval for bitbanging i2c
	 * is mainly to ensure that gpio has enough time to go from
	 * low to high or high to low.
	 */
	/* Pull SCL and SDA pin to high */
	gpio_pin_set(config->gpio_dev, config->alts_list[SCL].pin, 1);
	gpio_pin_set(config->gpio_dev, config->alts_list[SDA].pin, 1);
	k_msleep(1);

	/* Start condition */
	gpio_pin_set(config->gpio_dev, config->alts_list[SDA].pin, 0);
	k_msleep(1);
	gpio_pin_set(config->gpio_dev, config->alts_list[SCL].pin, 0);
	k_msleep(1);

	/* 9 cycles of SCL with SDA held high */
	for (i = 0; i < 9; i++) {
		/* SDA */
		gpio_pin_set(config->gpio_dev, config->alts_list[SDA].pin, 1);
		/* SCL */
		gpio_pin_set(config->gpio_dev, config->alts_list[SCL].pin, 1);
		k_msleep(1);
		/* SCL */
		gpio_pin_set(config->gpio_dev, config->alts_list[SCL].pin, 0);
		k_msleep(1);
	}
	/* SDA */
	gpio_pin_set(config->gpio_dev, config->alts_list[SDA].pin, 0);
	k_msleep(1);

	/* Stop condition */
	gpio_pin_set(config->gpio_dev, config->alts_list[SCL].pin, 1);
	k_msleep(1);
	gpio_pin_set(config->gpio_dev, config->alts_list[SDA].pin, 1);
	k_msleep(1);

	/* Set GPIO back to I2C alternate function of SCL */
	pinmux_pin_set(config->alts_list[SCL].pinctrls,
		       config->alts_list[SCL].pin,
		       config->alts_list[SCL].alt_fun);
	/* Set GPIO back to I2C alternate function of SDA */
	pinmux_pin_set(config->alts_list[SDA].pinctrls,
		       config->alts_list[SDA].pin,
		       config->alts_list[SDA].alt_fun);

	/* reset i2c port */
	i2c_reset(dev);
	LOG_ERR("I2C ch%d reset cause %d", config->port,
		I2C_RC_NO_IDLE_FOR_START);

	return 0;
}

static const struct i2c_driver_api i2c_it8xxx2_driver_api = {
	.configure = i2c_it8xxx2_configure,
	.get_config = i2c_it8xxx2_get_config,
	.transfer = i2c_it8xxx2_transfer,
	.recover_bus = i2c_it8xxx2_recover_bus,
};

#define I2C_ITE_IT8XXX2_INIT(inst)                                              \
	BUILD_ASSERT((DT_INST_PROP(inst, clock_frequency) ==                    \
		     50000) ||                                                  \
		     (DT_INST_PROP(inst, clock_frequency) ==                    \
		     I2C_BITRATE_STANDARD) ||                                   \
		     (DT_INST_PROP(inst, clock_frequency) ==                    \
		     I2C_BITRATE_FAST) ||                                       \
		     (DT_INST_PROP(inst, clock_frequency) ==                    \
		     I2C_BITRATE_FAST_PLUS), "Not support I2C bit rate value"); \
	static void i2c_it8xxx2_config_func_##inst(void);                       \
	static const struct i2c_alts_cfg                                        \
		i2c_alts_##inst[DT_INST_NUM_PINCTRLS_BY_IDX(inst, 0)] =         \
			IT8XXX2_DT_ALT_ITEMS_LIST(inst);                        \
										\
	static const struct i2c_it8xxx2_config i2c_it8xxx2_cfg_##inst = {       \
		.base = (uint8_t *)(DT_INST_REG_ADDR(inst)),                    \
		.irq_config_func = i2c_it8xxx2_config_func_##inst,              \
		.bitrate = DT_INST_PROP(inst, clock_frequency),                 \
		.i2c_irq_base = DT_INST_IRQN(inst),                             \
		.port = DT_INST_PROP(inst, port_num),                           \
		.alts_list = i2c_alts_##inst,                                   \
		.gpio_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, gpio_dev)),     \
		.clock_gate_offset = DT_INST_PROP(inst, clock_gate_offset),     \
	};                                                                      \
										\
	static struct i2c_it8xxx2_data i2c_it8xxx2_data_##inst;                 \
										\
	I2C_DEVICE_DT_INST_DEFINE(inst, i2c_it8xxx2_init,                       \
				  NULL,                                         \
				  &i2c_it8xxx2_data_##inst,                     \
				  &i2c_it8xxx2_cfg_##inst,                      \
				  POST_KERNEL,                                  \
				  CONFIG_KERNEL_INIT_PRIORITY_DEVICE,           \
				  &i2c_it8xxx2_driver_api);                     \
										\
	static void i2c_it8xxx2_config_func_##inst(void)                        \
	{                                                                       \
		IRQ_CONNECT(DT_INST_IRQN(inst),                                 \
			0,                                                      \
			i2c_it8xxx2_isr,                                        \
			DEVICE_DT_INST_GET(inst), 0);                           \
	}

DT_INST_FOREACH_STATUS_OKAY(I2C_ITE_IT8XXX2_INIT)
