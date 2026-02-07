#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <stdbool.h>

#define SPI1_NODE DT_NODELABEL(spi1)

static const struct device *spi_dev = DEVICE_DT_GET(SPI1_NODE);

static const struct gpio_dt_spec cs_gpios[] = {
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 3),
};

#ifndef WORKER_SLAVE_COUNT
#define WORKER_SLAVE_COUNT ARRAY_SIZE(cs_gpios)
#endif

/* Make buffers static + aligned (safe for DMA/ISR paths) */
static uint8_t tx_data[8] __aligned(4);
static uint8_t rx_data[8] __aligned(4);
static uint8_t tx_dummy[8] __aligned(4);

/* ACK value expected from child */
#define CHILD_ACK 0xAC

static int sync_with_slave(uint8_t slave_id)
{
    if (slave_id >= ARRAY_SIZE(cs_gpios)) {
        return -EINVAL;
    }

    /* CS control for this slave */
    struct spi_cs_control cs_ctrl = {
        .gpio  = cs_gpios[slave_id],
        .delay = 2,
    };

    struct spi_config cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = cs_ctrl,
    };

    /* New sequence:
     * 1) Master sends initial timestamp (master_ts1)
     * 2) Master reads child's timestamp (child_ts)
     * 3) Master sends final timestamp (master_ts2)
     * 4) Master reads ACK from child
     */

    /* Phase 1: send initial master timestamp */
    uint64_t master_ts1 = (uint64_t)k_uptime_get();
    for (int i = 0; i < 8; i++) {
        tx_data[i] = (uint8_t)(master_ts1 >> (56 - (i * 8)));
    }
    printk("MASTER phase1 ts=%llu bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           master_ts1,
           tx_data[0], tx_data[1], tx_data[2], tx_data[3],
           tx_data[4], tx_data[5], tx_data[6], tx_data[7]);
    memset(rx_data, 0, sizeof(rx_data));

    struct spi_buf tx_buf1 = { .buf = tx_data, .len = 8 };
    struct spi_buf rx_buf1 = { .buf = rx_data, .len = 8 };
    struct spi_buf_set txs1 = { .buffers = &tx_buf1, .count = 1 };
    struct spi_buf_set rxs1 = { .buffers = &rx_buf1, .count = 1 };

    int ret = spi_transceive(spi_dev, &cfg, &txs1, &rxs1);
    if (ret < 0) {
        return ret;
    }

    /* Small gap so the slave can prepare its response */
    k_msleep(1);

    /* Phase 2: read child's timestamp */
    memset(tx_dummy, 0, sizeof(tx_dummy));
    memset(rx_data, 0, sizeof(rx_data));

    struct spi_buf tx_buf2 = { .buf = tx_dummy, .len = 8 };
    struct spi_buf rx_buf2 = { .buf = rx_data,  .len = 8 };
    struct spi_buf_set txs2 = { .buffers = &tx_buf2, .count = 1 };
    struct spi_buf_set rxs2 = { .buffers = &rx_buf2, .count = 1 };

    ret = spi_transceive(spi_dev, &cfg, &txs2, &rxs2);
    int64_t arrival_master_recv = k_uptime_get();
    if (ret < 0) {
        return ret;
    }

    uint64_t child_ts = 0;
    for (int i = 0; i < 8; i++) {
        child_ts = (child_ts << 8) | rx_data[i];
    }

    printk("Slave %d sent TS: %llu ms; master arrival: %lld ms\n",
           slave_id, child_ts, arrival_master_recv);

    /* Phase 3: send final master timestamp */
    uint64_t master_ts2 = (uint64_t)k_uptime_get();
    for (int i = 0; i < 8; i++) {
        tx_data[i] = (uint8_t)(master_ts2 >> (56 - (i * 8)));
    }
    printk("MASTER phase3 ts=%llu bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           master_ts2,
           tx_data[0], tx_data[1], tx_data[2], tx_data[3],
           tx_data[4], tx_data[5], tx_data[6], tx_data[7]);
    memset(rx_data, 0, sizeof(rx_data));

    struct spi_buf tx_buf3 = { .buf = tx_data, .len = 8 };
    struct spi_buf rx_buf3 = { .buf = rx_data, .len = 8 };
    struct spi_buf_set txs3 = { .buffers = &tx_buf3, .count = 1 };
    struct spi_buf_set rxs3 = { .buffers = &rx_buf3, .count = 1 };

    ret = spi_transceive(spi_dev, &cfg, &txs3, &rxs3);
    if (ret < 0) {
        return ret;
    }

    /* Phase 4: read ACK from child */
    memset(tx_dummy, 0, sizeof(tx_dummy));
    memset(rx_data, 0, sizeof(rx_data));

    struct spi_buf tx_buf4 = { .buf = tx_dummy, .len = 8 };
    struct spi_buf rx_buf4 = { .buf = rx_data,  .len = 8 };
    struct spi_buf_set txs4 = { .buffers = &tx_buf4, .count = 1 };
    struct spi_buf_set rxs4 = { .buffers = &rx_buf4, .count = 1 };

    ret = spi_transceive(spi_dev, &cfg, &txs4, &rxs4);
    if (ret < 0) {
        return ret;
    }

    if (rx_data[0] == CHILD_ACK) {
        printk("Slave %d ACK received\n", slave_id);
    } else {
        printk("Slave %d ACK missing (got %02x)\n", slave_id, rx_data[0]);
    }

    return 0;
}

static int send_timestamp_to_slave(uint8_t slave_id, uint64_t ts)
{
    (void)ts;
    return sync_with_slave(slave_id);
}

/* Probe whether a slave is present on a given CS by issuing a dummy
 * transceive and checking whether MISO returns any non-zero data.
 */
static bool probe_slave(uint8_t slave_id)
{
    if (slave_id >= ARRAY_SIZE(cs_gpios)) {
        return false;
    }

    struct spi_cs_control cs_ctrl = {
        .gpio  = cs_gpios[slave_id],
        .delay = 2,
    };

    struct spi_config cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = cs_ctrl,
    };

    uint8_t probe_tx[8] __aligned(4) = {0};
    uint8_t probe_rx[8] __aligned(4) = {0};

    struct spi_buf txb = { .buf = probe_tx, .len = 8 };
    struct spi_buf rxb = { .buf = probe_rx, .len = 8 };
    struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
    struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };

    int ret = spi_transceive(spi_dev, &cfg, &txs, &rxs);
    if (ret < 0) {
        return false;
    }

    for (int i = 0; i < 8; i++) {
        if (probe_rx[i] != 0) {
            return true;
        }
    }
    return false;
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

            /* Probe which CS lines have attached slaves (and re-probe periodically). */
            static bool slave_present[ARRAY_SIZE(cs_gpios)];
            for (uint8_t i = 0; i < WORKER_SLAVE_COUNT; i++) {
                slave_present[i] = probe_slave(i);
                printk("CS %d present=%d\n", i, (int)slave_present[i]);
            }

            printk("SPI worker online\n");

            uint32_t loop_count = 0;
            while (1) {
                int64_t now = k_uptime_get();

                for (uint8_t i = 0; i < WORKER_SLAVE_COUNT; i++) {
                    if (!slave_present[i]) {
                        continue;
                    }
                    int err = send_timestamp_to_slave(i, (uint64_t)now);
                    if (err) {
                        printk("Slave %d: send failed: %d\n", i, err);
                    }
                    /* Give slaves time to re-arm (important for Zephyr SPI slave) */
                    k_msleep(5);
                }

                /* Re-probe absent CS lines every 60 loops (~60 seconds by default) */
                loop_count++;
                if ((loop_count % 60) == 0) {
                    for (uint8_t i = 0; i < WORKER_SLAVE_COUNT; i++) {
                        if (!slave_present[i]) {
                            slave_present[i] = probe_slave(i);
                            if (slave_present[i]) {
                                printk("Detected slave on CS %d\n", i);
                            }
                        }
                    }
                }

                k_msleep(1000);
            }
    }
    return 0;
}
