#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <inttypes.h>

#define SPI1_NODE DT_NODELABEL(spi1)
#define SYNC_PIN_NODE DT_ALIAS(sync_out)

/* Macro kept for build-time interval control */
#ifndef MASTER_SYNC_INTERVAL_MS
#define MASTER_SYNC_INTERVAL_MS 60000 
#endif

static const struct device *spi_dev = DEVICE_DT_GET(SPI1_NODE);
static const struct gpio_dt_spec sync_out = GPIO_DT_SPEC_GET(SYNC_PIN_NODE, gpios);

static const struct gpio_dt_spec cs_gpios[] = {
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 3),
};

static inline uint64_t get_uptime_us(void) {
    return (k_cycle_get_64() * 1000000ULL) / sys_clock_hw_cycles_per_sec();
}

static uint8_t tx_data[8] __aligned(4);
static uint8_t rx_dummy[8] __aligned(4);

static int send_timestamp_to_slave(uint8_t slave_id, uint64_t timestamp_us)
{
    for (int i = 0; i < 8; i++) {
        tx_data[i] = (uint8_t)(timestamp_us >> (56 - (i * 8)));
    }

    struct spi_buf tx_buf = { .buf = tx_data,  .len = 8 };
    struct spi_buf rx_buf = { .buf = rx_dummy, .len = 8 };
    struct spi_buf_set txs = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rxs = { .buffers = &rx_buf, .count = 1 };

    struct spi_cs_control cs_ctrl = {
        .gpio  = cs_gpios[slave_id],
        .delay = 0, 
    };

    struct spi_config cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .cs = cs_ctrl,
    };

    return spi_transceive(spi_dev, &cfg, &txs, &rxs);
}

int main(void)
{
    if (!device_is_ready(spi_dev) || !gpio_is_ready_dt(&sync_out)) return 0;
    gpio_pin_configure_dt(&sync_out, GPIO_OUTPUT_INACTIVE);

    for (int i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
        gpio_is_ready_dt(&cs_gpios[i]);
    }

    printk("Worker Broadcast Online. Interval: %d ms\n", MASTER_SYNC_INTERVAL_MS);

    while (1) {
        /* 1. Global Pulse: Triggers ISR on all slaves simultaneously */
        gpio_pin_set_dt(&sync_out, 1);
        uint64_t now_us = get_uptime_us(); 
        gpio_pin_set_dt(&sync_out, 0);

        printk("Global Sync Pulse sent: %" PRIu64 " us\n", now_us);

        /* 2. Sequential Data Delivery */
        for (uint8_t i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
            if (send_timestamp_to_slave(i, now_us) == 0) {
                printk("Sync delivered to Child %d: %" PRIu64 " us\n", i, now_us);
            }
            k_msleep(10); 
        }

        k_msleep(MASTER_SYNC_INTERVAL_MS);
    }
    return 0;
}