#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define SPI1_NODE DT_NODELABEL(spi1)
#define SPI_BUS_FREQ 1000000

static const struct device *spi_dev(void)
{
	const struct device *dev = DEVICE_DT_GET(SPI1_NODE);
	if (!device_is_ready(dev)) {
		LOG_ERR("SPI device not ready");
		return NULL;
	}
	return dev;
}

/*
*/
static int spi_exchange(const struct device *spi, const uint8_t *tx, size_t tx_len,
						uint8_t *rx, size_t rx_len, const struct spi_config *cfg)
{
	struct spi_buf tx_buf = {
		.buf = (void *)tx,
		.len = tx_len,
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1,
	};

	struct spi_buf rx_buf = {
		.buf = rx,
		.len = rx_len,
	};
	const struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1,
	};

	int ret = spi_transceive(spi, cfg, &tx_set, &rx_set);
	if (ret) {
		LOG_ERR("spi_transceive failed: %d", ret);
	}
	return ret;
}

void main(void)
{
	LOG_INF("Starting SPI master example");

	const struct device *spi = spi_dev();
	if (!spi) {
		return;
	}

	const struct spi_config cfg = {
		.frequency = SPI_BUS_FREQ,
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
		.slave = 0,
		.cs = NULL,
	};

	uint8_t tx_buf[2];
	uint8_t rx_buf[8];

	while (1) {
		tx_buf[0] = 0xA5; /* simple command */
		tx_buf[1] = 0x01; /* payload */
		memset(rx_buf, 0, sizeof(rx_buf));

		LOG_DBG("Sending to child node: 0x%02X 0x%02X", tx_buf[0], tx_buf[1]);

		int attempts = 3;
		int ret = -EIO;
		for (int i = 0; i < attempts; i++) {
			ret = spi_exchange(spi, tx_buf, sizeof(tx_buf), rx_buf, sizeof(rx_buf), &cfg);
			if (ret == 0) {
				LOG_INF("Received %d bytes from child", (int)sizeof(rx_buf));
				LOG_HEXDUMP_INF(rx_buf, sizeof(rx_buf), "child reply");
				break;
			}
			LOG_WRN("SPI attempt %d failed", i + 1);
			k_msleep(100);
		}

		if (ret) {
			LOG_ERR("All SPI attempts failed");
		}

		k_msleep(1000);
	}
}
