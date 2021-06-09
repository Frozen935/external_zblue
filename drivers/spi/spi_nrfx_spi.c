/*
 * Copyright (c) 2017 - 2018, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/spi.h>
#include <nrfx_spi.h>

#define LOG_DOMAIN "spi_nrfx_spi"
#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(spi_nrfx_spi);

#include "spi_context.h"

struct spi_nrfx_data {
	struct spi_context ctx;
	const struct device *dev;
	size_t chunk_len;
	bool   busy;
};

struct spi_nrfx_config {
	nrfx_spi_t	  spi;
	nrfx_spi_config_t config;
};

static inline struct spi_nrfx_data *get_dev_data(const struct device *dev)
{
	return dev->data;
}

static inline const struct spi_nrfx_config *get_dev_config(const struct device *dev)
{
	return dev->config;
}

static inline nrf_spi_frequency_t get_nrf_spi_frequency(uint32_t frequency)
{
	/* Get the highest supported frequency not exceeding the requested one.
	 */
	if (frequency < 250000) {
		return NRF_SPI_FREQ_125K;
	} else if (frequency < 500000) {
		return NRF_SPI_FREQ_250K;
	} else if (frequency < 1000000) {
		return NRF_SPI_FREQ_500K;
	} else if (frequency < 2000000) {
		return NRF_SPI_FREQ_1M;
	} else if (frequency < 4000000) {
		return NRF_SPI_FREQ_2M;
	} else if (frequency < 8000000) {
		return NRF_SPI_FREQ_4M;
	} else {
		return NRF_SPI_FREQ_8M;
	}
}

static inline nrf_spi_mode_t get_nrf_spi_mode(uint16_t operation)
{
	if (SPI_MODE_GET(operation) & SPI_MODE_CPOL) {
		if (SPI_MODE_GET(operation) & SPI_MODE_CPHA) {
			return NRF_SPI_MODE_3;
		} else {
			return NRF_SPI_MODE_2;
		}
	} else {
		if (SPI_MODE_GET(operation) & SPI_MODE_CPHA) {
			return NRF_SPI_MODE_1;
		} else {
			return NRF_SPI_MODE_0;
		}
	}
}

static inline nrf_spi_bit_order_t get_nrf_spi_bit_order(uint16_t operation)
{
	if (operation & SPI_TRANSFER_LSB) {
		return NRF_SPI_BIT_ORDER_LSB_FIRST;
	} else {
		return NRF_SPI_BIT_ORDER_MSB_FIRST;
	}
}

static int configure(const struct device *dev,
		     const struct spi_config *spi_cfg)
{
	struct spi_context *ctx = &get_dev_data(dev)->ctx;
	const nrfx_spi_t *spi = &get_dev_config(dev)->spi;

	if (spi_context_configured(ctx, spi_cfg)) {
		/* Already configured. No need to do it again. */
		return 0;
	}

	if (SPI_OP_MODE_GET(spi_cfg->operation) != SPI_OP_MODE_MASTER) {
		LOG_ERR("Slave mode is not supported on %s",
			    dev->name);
		return -EINVAL;
	}

	if (spi_cfg->operation & SPI_MODE_LOOP) {
		LOG_ERR("Loopback mode is not supported");
		return -EINVAL;
	}

	if ((spi_cfg->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) {
		LOG_ERR("Only single line mode is supported");
		return -EINVAL;
	}

	if (SPI_WORD_SIZE_GET(spi_cfg->operation) != 8) {
		LOG_ERR("Word sizes other than 8 bits"
			    " are not supported");
		return -EINVAL;
	}

	if (spi_cfg->frequency < 125000) {
		LOG_ERR("Frequencies lower than 125 kHz are not supported");
		return -EINVAL;
	}

	ctx->config = spi_cfg;
	spi_context_cs_configure(ctx);

	nrf_spi_configure(spi->p_reg,
			  get_nrf_spi_mode(spi_cfg->operation),
			  get_nrf_spi_bit_order(spi_cfg->operation));
	nrf_spi_frequency_set(spi->p_reg,
			      get_nrf_spi_frequency(spi_cfg->frequency));

	return 0;
}

static void transfer_next_chunk(const struct device *dev)
{
	struct spi_nrfx_data *dev_data = get_dev_data(dev);
	struct spi_context *ctx = &dev_data->ctx;
	int error = 0;

	size_t chunk_len = spi_context_max_continuous_chunk(ctx);

	if (chunk_len > 0) {
		nrfx_spi_xfer_desc_t xfer;
		nrfx_err_t result;

		dev_data->chunk_len = chunk_len;

		xfer.p_tx_buffer = ctx->tx_buf;
		xfer.tx_length   = spi_context_tx_buf_on(ctx) ? chunk_len : 0;
		xfer.p_rx_buffer = ctx->rx_buf;
		xfer.rx_length   = spi_context_rx_buf_on(ctx) ? chunk_len : 0;
		result = nrfx_spi_xfer(&get_dev_config(dev)->spi, &xfer, 0);
		if (result == NRFX_SUCCESS) {
			return;
		}

		error = -EIO;
	}

	spi_context_cs_control(ctx, false);

	LOG_DBG("Transaction finished with status %d", error);

	spi_context_complete(ctx, error);
	dev_data->busy = false;
}

static int transceive(const struct device *dev,
		      const struct spi_config *spi_cfg,
		      const struct spi_buf_set *tx_bufs,
		      const struct spi_buf_set *rx_bufs,
		      bool asynchronous,
		      struct k_poll_signal *signal)
{
	struct spi_nrfx_data *dev_data = get_dev_data(dev);
	int error;

	spi_context_lock(&dev_data->ctx, asynchronous, signal, spi_cfg);

	error = configure(dev, spi_cfg);
	if (error == 0) {
		dev_data->busy = true;

		spi_context_buffers_setup(&dev_data->ctx, tx_bufs, rx_bufs, 1);
		spi_context_cs_control(&dev_data->ctx, true);

		transfer_next_chunk(dev);

		error = spi_context_wait_for_completion(&dev_data->ctx);
	}

	spi_context_release(&dev_data->ctx, error);

	return error;
}

static int spi_nrfx_transceive(const struct device *dev,
			       const struct spi_config *spi_cfg,
			       const struct spi_buf_set *tx_bufs,
			       const struct spi_buf_set *rx_bufs)
{
	return transceive(dev, spi_cfg, tx_bufs, rx_bufs, false, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_nrfx_transceive_async(const struct device *dev,
				     const struct spi_config *spi_cfg,
				     const struct spi_buf_set *tx_bufs,
				     const struct spi_buf_set *rx_bufs,
				     struct k_poll_signal *async)
{
	return transceive(dev, spi_cfg, tx_bufs, rx_bufs, true, async);
}
#endif /* CONFIG_SPI_ASYNC */

static int spi_nrfx_release(const struct device *dev,
			    const struct spi_config *spi_cfg)
{
	struct spi_nrfx_data *dev_data = get_dev_data(dev);

	if (!spi_context_configured(&dev_data->ctx, spi_cfg)) {
		return -EINVAL;
	}

	if (dev_data->busy) {
		return -EBUSY;
	}

	spi_context_unlock_unconditionally(&dev_data->ctx);

	return 0;
}

static const struct spi_driver_api spi_nrfx_driver_api = {
	.transceive = spi_nrfx_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_nrfx_transceive_async,
#endif
	.release = spi_nrfx_release,
};


static void event_handler(const nrfx_spi_evt_t *p_event, void *p_context)
{
	struct spi_nrfx_data *dev_data = p_context;

	if (p_event->type == NRFX_SPI_EVENT_DONE) {
		spi_context_update_tx(&dev_data->ctx, 1, dev_data->chunk_len);
		spi_context_update_rx(&dev_data->ctx, 1, dev_data->chunk_len);

		transfer_next_chunk(dev_data->dev);
	}
}

static int init_spi(const struct device *dev)
{
	struct spi_nrfx_data *dev_data = get_dev_data(dev);
	nrfx_err_t result;

	dev_data->dev = dev;

	/* This sets only default values of frequency, mode and bit order.
	 * The proper ones are set in configure() when a transfer is started.
	 */
	result = nrfx_spi_init(&get_dev_config(dev)->spi,
			       &get_dev_config(dev)->config,
			       event_handler,
			       dev_data);
	if (result != NRFX_SUCCESS) {
		LOG_ERR("Failed to initialize device: %s", dev->name);
		return -EBUSY;
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int spi_nrfx_pm_control(const struct device *dev,
			       enum pm_device_state *state)
{
	int ret = 0;
	struct spi_nrfx_data *data = get_dev_data(dev);
	const struct spi_nrfx_config *config = get_dev_config(dev);
	enum pm_device_state curr_state;

	(void)pm_device_state_get(dev, &curr_state);
	if (*state != curr_state) {
		switch (*state) {
		case PM_DEVICE_STATE_ACTIVE:
			ret = init_spi(dev);
			/* Force reconfiguration before next transfer */
			data->ctx.config = NULL;
			break;

		case PM_DEVICE_STATE_LOW_POWER:
		case PM_DEVICE_STATE_SUSPEND:
		case PM_DEVICE_STATE_OFF:
			if (curr_state == PM_DEVICE_STATE_ACTIVE) {
				nrfx_spi_uninit(&config->spi);
			}
			break;

		default:
			ret = -ENOTSUP;
		}
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

/*
 * Current factors requiring use of DT_NODELABEL:
 *
 * - NRFX_SPI_INSTANCE() requires an SoC instance number
 * - soc-instance-numbered kconfig enables
 * - ORC is a SoC-instance-numbered kconfig option instead of a DT property
 */

#define SPI(idx)			DT_NODELABEL(spi##idx)
#define SPI_PROP(idx, prop)		DT_PROP(SPI(idx), prop)

#define SPI_NRFX_MISO_PULL(idx)				\
	(SPI_PROP(idx, miso_pull_up)			\
		? SPI_PROP(idx, miso_pull_down)		\
			? -1 /* invalid configuration */\
			: NRF_GPIO_PIN_PULLUP		\
		: SPI_PROP(idx, miso_pull_down)		\
			? NRF_GPIO_PIN_PULLDOWN		\
			: NRF_GPIO_PIN_NOPULL)

#define SPI_NRFX_SPI_DEVICE(idx)					       \
	BUILD_ASSERT(							       \
		!SPI_PROP(idx, miso_pull_up) || !SPI_PROP(idx, miso_pull_down),\
		"SPI"#idx						       \
		": cannot enable both pull-up and pull-down on MISO line");    \
	static int spi_##idx##_init(const struct device *dev)		       \
	{								       \
		IRQ_CONNECT(DT_IRQN(SPI(idx)), DT_IRQ(SPI(idx), priority),     \
			    nrfx_isr, nrfx_spi_##idx##_irq_handler, 0);	       \
		int err = init_spi(dev);				       \
		spi_context_unlock_unconditionally(&get_dev_data(dev)->ctx);   \
		return err;					       	       \
	}								       \
	static struct spi_nrfx_data spi_##idx##_data = {		       \
		SPI_CONTEXT_INIT_LOCK(spi_##idx##_data, ctx),		       \
		SPI_CONTEXT_INIT_SYNC(spi_##idx##_data, ctx),		       \
		.busy = false,						       \
	};								       \
	static const struct spi_nrfx_config spi_##idx##z_config = {	       \
		.spi = NRFX_SPI_INSTANCE(idx),				       \
		.config = {						       \
			.sck_pin   = SPI_PROP(idx, sck_pin),		       \
			.mosi_pin  = SPI_PROP(idx, mosi_pin),		       \
			.miso_pin  = SPI_PROP(idx, miso_pin),		       \
			.ss_pin    = NRFX_SPI_PIN_NOT_USED,		       \
			.orc       = CONFIG_SPI_##idx##_NRF_ORC,	       \
			.frequency = NRF_SPI_FREQ_4M,			       \
			.mode      = NRF_SPI_MODE_0,			       \
			.bit_order = NRF_SPI_BIT_ORDER_MSB_FIRST,	       \
			.miso_pull = SPI_NRFX_MISO_PULL(idx),		       \
		}							       \
	};								       \
	DEVICE_DT_DEFINE(SPI(idx),					       \
		      spi_##idx##_init,					       \
		      spi_nrfx_pm_control,				       \
		      &spi_##idx##_data,				       \
		      &spi_##idx##z_config,				       \
		      POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,		       \
		      &spi_nrfx_driver_api)

#ifdef CONFIG_SPI_0_NRF_SPI
SPI_NRFX_SPI_DEVICE(0);
#endif

#ifdef CONFIG_SPI_1_NRF_SPI
SPI_NRFX_SPI_DEVICE(1);
#endif

#ifdef CONFIG_SPI_2_NRF_SPI
SPI_NRFX_SPI_DEVICE(2);
#endif
