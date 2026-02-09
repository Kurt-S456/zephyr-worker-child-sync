#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

#define SPI1_NODE DT_NODELABEL(spi1)

static const struct device *spi_dev = DEVICE_DT_GET(SPI1_NODE);

static const struct gpio_dt_spec cs_gpios[] = {
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 3),
};

#/***********************************************************
# * MASTER_SYNC_INTERVAL_MS
# *
# * Can be overridden at build time. Example (PlatformIO):
# *   add to the env in platformio.ini:
# *     build_flags = -DMASTER_SYNC_INTERVAL_MS=30000
# * Or pass a compiler flag directly: -DMASTER_SYNC_INTERVAL_MS=60000
# */
#ifndef MASTER_SYNC_INTERVAL_MS
#define MASTER_SYNC_INTERVAL_MS 60000 /* 60 seconds */
#endif

static uint8_t tx_data[8] __aligned(4);
static uint8_t rx_dummy[8] __aligned(4);

static int send_timestamp_to_slave(uint8_t slave_id, uint64_t timestamp)
{
    if (slave_id >= ARRAY_SIZE(cs_gpios)) {
        return -EINVAL;
    }

    for (int i = 0; i < 8; i++) {
        tx_data[i] = (uint8_t)(timestamp >> (56 - (i * 8)));
    }
    memset(rx_dummy, 0, sizeof(rx_dummy));

    struct spi_buf tx_buf = { .buf = tx_data,  .len = 8 };
    struct spi_buf rx_buf = { .buf = rx_dummy, .len = 8 };
    struct spi_buf_set txs = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rxs = { .buffers = &rx_buf, .count = 1 };

    struct spi_cs_control cs_ctrl = {
        .gpio  = cs_gpios[slave_id],
        .delay = 2, /* microseconds */
    };

    struct spi_config cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = cs_ctrl,
    };

    return spi_transceive(spi_dev, &cfg, &txs, &rxs);
}

int main(void)
{
    if (!device_is_ready(spi_dev)) {
        printk("SPI device not ready\n");
        return 0;
    }

    for (int i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
        if (!gpio_is_ready_dt(&cs_gpios[i])) {
            printk("GPIO for CS %d not ready\n", i);
            return 0;
        }
    }

    printk("SPI worker online\n");

    while (1) {
        int64_t now = k_uptime_get();

        for (uint8_t i = 0; i < 4; i++) {
            int err = send_timestamp_to_slave(i, (uint64_t)now);
            if (err) {
                printk("Slave %d: send failed: %d\n", i, err);
            }
            /* Give slaves time to re-arm (important for Zephyr SPI slave) */
            k_msleep(100);
        }

        k_msleep(MASTER_SYNC_INTERVAL_MS);
    }
    return 0;
}
