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

static int send_timestamp_to_slave(uint8_t slave_id, uint64_t timestamp_us, uint8_t *out_rx)
{
    for (int i = 0; i < 8; i++) {
        tx_data[i] = (uint8_t)(timestamp_us >> (56 - (i * 8)));
    }

    struct spi_buf tx_buf = { .buf = tx_data,  .len = 8 };
    struct spi_buf rx_buf = { .buf = out_rx ? out_rx : rx_dummy, .len = 8 };
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


    int ret = spi_transceive(spi_dev, &cfg, &txs, &rxs);

    /* Debug: print immediate RX buffer and return code */
    uint8_t *rxp = out_rx ? out_rx : rx_dummy;
    printk("Master <- Child %d ret=%d RX:", slave_id, ret);
    for (int i = 0; i < 8; i++) {
        printk(" %02x", rxp[i]);
         k_msleep(5);
    } 
    printk("\n");

    return ret;
}

int main(void)
{
    if (!device_is_ready(spi_dev) || !gpio_is_ready_dt(&sync_out)) return 0;
    gpio_pin_configure_dt(&sync_out, GPIO_OUTPUT_INACTIVE);

    for (int i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
        bool ready = gpio_is_ready_dt(&cs_gpios[i]);
        printk("CS[%d] gpio ready: %d (port=%p pin=%d)\n", i, ready, cs_gpios[i].port, cs_gpios[i].pin);
    }

    printk("Worker Broadcast Online. Interval: %d ms\n", MASTER_SYNC_INTERVAL_MS);

    while (1) {
        /* 1. Global Pulse: Triggers ISR on all slaves simultaneously */
        gpio_pin_set_dt(&sync_out, 1);
        uint64_t now_us = get_uptime_us(); 
        gpio_pin_set_dt(&sync_out, 0);
        k_msleep(100);
        /* 2. Sequential Data Delivery: collect each child's response into an array */
        static uint8_t collected_rx[ARRAY_SIZE(cs_gpios)][8] __aligned(4);
        static int results[ARRAY_SIZE(cs_gpios)];


        for (uint8_t i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
            results[i] = send_timestamp_to_slave(i, now_us, collected_rx[i]);
        }

        /* 3. In-between-interval printing: print pulse + all received buffers here */
        printk("Global Sync Pulse sent: %" PRIu64 " us\n", now_us);
        for (uint8_t i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
            if (results[i] == 0) {
                printk("Recv from Child %d:", i);
                k_msleep(5);
                for (int j = 0; j < 8; j++) {
                    printk(" %02x", collected_rx[i][j]);
                    k_msleep(5);
                }
                printk("\n");
            } else {
                printk("Child %d: transfer failed (ret=%d)\n", i, results[i]);
            }
        }

        k_msleep(MASTER_SYNC_INTERVAL_MS);
    }
    return 0;
}