#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

static uint8_t rx_data[8];

void main(void) {
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));

    if (!device_is_ready(spi_dev)) {
        return;
    }

    // 2. Setup the SPI config with extra braces for safety
    struct spi_config slave_cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = {0}, 
    };

    // 3. Define the buffer structures ONCE outside the loop
    struct spi_buf rx_buf = {
        .buf = rx_data,
        .len = sizeof(rx_data)
    };
    struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1
    };

    printk("Slave Ready. Waiting for master...\n");

    while (1) {
        // Clear buffer before receive
        memset(rx_data, 0, sizeof(rx_data));

        // Use the pre-allocated rx_bufs set
        int err = spi_read(spi_dev, &slave_cfg, &rx_bufs);

        if (err == 0) {
            uint64_t ts = 0;
            for (int i = 0; i < 8; i++) {
                ts |= ((uint64_t)rx_data[i] << (56 - (i * 8)));
            }
            printk("Timestamp: %llu ms\n", ts);
        } else {
            // If Error 8 persists, it's a pointer validation issue in the driver
            printk("Error: %d\n", err);
            k_msleep(1000);
        }
    }
}