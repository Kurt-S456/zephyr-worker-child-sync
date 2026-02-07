#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <string.h>

#ifndef APP_CHILD_ID
#error "APP_CHILD_ID not defined (use -DAPP_CHILD_ID=x)"
#endif

#define CHILD_ID APP_CHILD_ID

#define SPI_DEV_NODE DT_NODELABEL(spi1)

/* DMA/IRQ-safe static buffers (also helps on non-DMA) */
static uint8_t rx_data[8] __aligned(4);
static uint8_t tx_dummy[8] __aligned(4);

static struct spi_buf rx_buf = {
    .buf = rx_data,
    .len = sizeof(rx_data),
};

static struct spi_buf tx_buf = {
    .buf = tx_dummy,
    .len = sizeof(tx_dummy),
};

static struct spi_buf_set rx_set = {
    .buffers = &rx_buf,
    .count = 1,
};

static struct spi_buf_set tx_set = {
    .buffers = &tx_buf,
    .count = 1,
};

/* IMPORTANT:
 * - STM32 driver validates frequency even in slave mode -> must be non-zero.
 * - Mode defaults to CPOL=0/CPHA=0 (Mode 0). Add SPI_MODE_CPOL / SPI_MODE_CPHA if your master differs.
 */
static const struct spi_config slave_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
};

void main(void)
{
    const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);

    if (!device_is_ready(spi_dev)) {
        printk("SPI1 device not ready\n");
        return;
    }

   	printk("SPI CHILD %d ready\n", CHILD_ID);


    while (1) {
        memset(rx_data, 0, sizeof(rx_data));
        memset(tx_dummy, 0, sizeof(tx_dummy));

        /* Blocks until the master clocks a transaction (and NSS frames it) */
        int err = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);

        if (err >= 0) {
    /* Some drivers return 0, some return number of frames/bytes.
       Treat any non-negative as success. */
		uint64_t ts = 0;
		for (int i = 0; i < 8; i++) {
			ts = (ts << 8) | rx_data[i];
		}
		printk("CHILD %d RX: %02x %02x %02x %02x %02x %02x %02x %02x -> %llu ms\n",
			CHILD_ID,
			rx_data[0], rx_data[1], rx_data[2], rx_data[3],
			rx_data[4], rx_data[5], rx_data[6], rx_data[7],
			ts);
		} else {
			printk("SPI error: %d\n", err);  /* negative errno */
			k_msleep(10);
		}

    }
}
