#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

#define SPI_DEV_LABEL DT_LABEL(DT_NODELABEL(spi1))

void main(void)
{
	const struct device *spi_dev = device_get_binding(SPI_DEV_LABEL);
	if (!spi_dev) {
		printk("Child: SPI device %s not found\n", SPI_DEV_LABEL);
		return;
	}

	/* Configure SPI slave parameters (master provides clock/CS). */
	struct spi_config spi_cfg = {0};
	spi_cfg.frequency = 1000000U;
	spi_cfg.operation = SPI_OP_MODE_SLAVE | SPI_TRANSFER_MSB | SPI_WORD_SET(8);
	spi_cfg.slave = 0;

	printk("Child: SPI-slave message printer started\n");

	while (1) {
		uint8_t tx_buf[64];
		uint8_t rx_buf[64];
		memset(tx_buf, 0, sizeof(tx_buf));
		memset(rx_buf, 0, sizeof(rx_buf));

		struct spi_buf tx_spi_buf = { .buf = tx_buf, .len = sizeof(tx_buf) };
		struct spi_buf rx_spi_buf = { .buf = rx_buf, .len = sizeof(rx_buf) };
		const struct spi_buf_set tx_set = { .buffers = &tx_spi_buf, .count = 1 };
		const struct spi_buf_set rx_set = { .buffers = &rx_spi_buf, .count = 1 };

		int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
		uint32_t t = k_cycle_get_32();
		if (ret) {
			printk("spi_transceive failed: %d\n", ret);
			k_msleep(100);
			continue;
		}

		/* Print timestamp and raw bytes (hex) */
		printk("Master message @%u: ", t);
		for (size_t i = 0; i < sizeof(rx_buf); ++i) {
			printk("%02x ", rx_buf[i]);
		}
		printk("\n");

		/* Also print ASCII where printable */
		printk("Ascii: ");
		for (size_t i = 0; i < sizeof(rx_buf); ++i) {
			uint8_t c = rx_buf[i];
			if (c >= 32 && c <= 126) {
				printk("%c", c);
			} else {
				printk(".");
			}
		}
		printk("\n");

		/* Small delay to avoid flooding console if master clocks continuously */
		k_msleep(10);
	}
}
