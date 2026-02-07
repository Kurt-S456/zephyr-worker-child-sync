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
static uint8_t tx_data[8] __aligned(4);
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


    /* local offset to allow logical clock adjustments (ms) */
    int64_t local_offset_ms = 0;

    while (1) {
        /* Sequence expected from master:
         * 1) Master sends initial timestamp (we must be ready to receive)
         * 2) We send our child timestamp (master will read it)
         * 3) Master sends final timestamp (we receive and adjust)
         * 4) We send ACK
         */

        /* Phase 1: receive initial master timestamp */
        memset(rx_data, 0, sizeof(rx_data));
        memset(tx_dummy, 0, sizeof(tx_dummy));

        struct spi_buf tx_buf1 = { .buf = tx_dummy, .len = 8 };
        struct spi_buf rx_buf1 = { .buf = rx_data,  .len = 8 };
        struct spi_buf_set txs1 = { .buffers = &tx_buf1, .count = 1 };
        struct spi_buf_set rxs1 = { .buffers = &rx_buf1, .count = 1 };

        int err = spi_transceive(spi_dev, &slave_cfg, &txs1, &rxs1);
        if (err < 0) {
            printk("SPI RX initial master TS error: %d\n", err);
            k_msleep(10);
            continue;
        }

        int64_t arrival_initial = k_uptime_get() + local_offset_ms;
        uint64_t master_ts1 = 0;
        for (int i = 0; i < 8; i++) {
            master_ts1 = (master_ts1 << 8) | rx_data[i];
        }
        printk("CHILD %d: got initial master TS %llu ms; arrival %lld ms\n",
               CHILD_ID, master_ts1, arrival_initial);

        /* Phase 2: send our child timestamp (master will read this) */
        uint64_t child_ts = (uint64_t)k_uptime_get() + (uint64_t)local_offset_ms;
        for (int i = 0; i < 8; i++) {
            tx_data[i] = (uint8_t)(child_ts >> (56 - (i * 8)));
        }
        memset(rx_data, 0, sizeof(rx_data));

         /* use global buffers (stable memory) for slave TX/RX */
         tx_buf.buf = tx_data;
         tx_buf.len = 8;
         rx_buf.buf = rx_data;
         rx_buf.len = 8;

         printk("CHILD %d phase2 send TS=%llu bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
             CHILD_ID, child_ts,
             tx_data[0], tx_data[1], tx_data[2], tx_data[3],
             tx_data[4], tx_data[5], tx_data[6], tx_data[7]);

         err = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);
         printk("CHILD %d phase2 transceive ret=%d\n", CHILD_ID, err);
        if (err < 0) {
            printk("SPI send child TS error: %d\n", err);
            k_msleep(10);
            continue;
        }

        /* Phase 3: receive final master timestamp (sync message) */
        memset(rx_data, 0, sizeof(rx_data));
        memset(tx_dummy, 0, sizeof(tx_dummy));

         /* prepare global buffers for RX of final master timestamp */
         tx_buf.buf = tx_dummy;
         tx_buf.len = 8;
         rx_buf.buf = rx_data;
         rx_buf.len = 8;

         err = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);
         printk("CHILD %d phase3 transceive ret=%d rx: %02x %02x %02x %02x %02x %02x %02x %02x\n",
             CHILD_ID, err,
             rx_data[0], rx_data[1], rx_data[2], rx_data[3],
             rx_data[4], rx_data[5], rx_data[6], rx_data[7]);
        if (err < 0) {
            printk("SPI RX final master TS error: %d\n", err);
            k_msleep(10);
            continue;
        }

        int64_t arrival_final = k_uptime_get() + local_offset_ms;
        uint64_t master_ts2 = 0;
        for (int i = 0; i < 8; i++) {
            master_ts2 = (master_ts2 << 8) | rx_data[i];
        }

        int64_t new_offset = (int64_t)master_ts2 - arrival_final;
        local_offset_ms += new_offset;

        printk("CHILD %d: received final master TS %llu ms; arrival %lld ms; adjust offset by %lld -> offset %lld\n",
               CHILD_ID, master_ts2, arrival_final, new_offset, local_offset_ms);

        /* Phase 4: send ACK to master */
        memset(tx_data, 0, sizeof(tx_data));
        tx_data[0] = 0xAC; /* ACK */
        memset(rx_data, 0, sizeof(rx_data));

         tx_buf.buf = tx_data;
         tx_buf.len = 8;
         rx_buf.buf = rx_data;
         rx_buf.len = 8;

         printk("CHILD %d phase4 send ACK bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
             CHILD_ID,
             tx_data[0], tx_data[1], tx_data[2], tx_data[3],
             tx_data[4], tx_data[5], tx_data[6], tx_data[7]);

         err = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);
         printk("CHILD %d phase4 transceive ret=%d\n", CHILD_ID, err);
        if (err < 0) {
            printk("SPI ACK send error: %d\n", err);
            k_msleep(10);
            continue;
        }

        /* Small pause before next sync round */
        k_msleep(1);
    }
}
