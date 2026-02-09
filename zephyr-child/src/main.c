#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <string.h>
#include <inttypes.h>

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

/* Build-time controls:
 * - Define ENABLE_OFFSET_VALIDATION=0 to disable rejecting large jumps.
 * - Define MAX_ACCEPTABLE_OFFSET_MS to override the default threshold (10s).
 * Example: add -DENABLE_OFFSET_VALIDATION=1 -DMAX_ACCEPTABLE_OFFSET_MS=5000
 */
#ifndef ENABLE_OFFSET_VALIDATION
#define ENABLE_OFFSET_VALIDATION 1
#endif

#ifndef MAX_ACCEPTABLE_OFFSET_MS
#define MAX_ACCEPTABLE_OFFSET_MS 10000 /* 10 seconds */
#endif


/* Signed offset (ms) to add to local `k_uptime_get()` to align with master time. */
static int64_t clock_offset_ms = 0;

static inline uint64_t get_synced_uptime_ms(void)
{
    return (uint64_t)((int64_t)k_uptime_get() + clock_offset_ms);
}

int main(void)
{
    const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
    if (!device_is_ready(spi_dev)) {
        printk("SPI1 device not ready\n");
        return -1;
    }

   	printk("SPI CHILD %d ready\n", CHILD_ID);

    int64_t offset = 0;  /* ms (signed diff) */


    while (1) {
        memset(rx_data, 0, sizeof(rx_data));
        memset(tx_dummy, 0, sizeof(tx_dummy));

        /* Blocks until the master clocks a transaction (and NSS frames it) */
        int err = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);

        if (err >= 0) {
            printk("---SPI transaction complete---\n");
            /* read true local uptime once */
            uint64_t local = k_uptime_get();
            printk("CHILD %d local timestamp: %" PRIu64 " ms\n", CHILD_ID, local);
            /* Some drivers return 0, some return number of frames/bytes.
            Treat any non-negative as success. */
            uint64_t worker_ts = 0;
            for (int i = 0; i < 8; i++) {
                worker_ts = (worker_ts << 8) | rx_data[i];
            }
            
            /* compute signed offset (master_ts - local_ts) */
            offset = (int64_t)worker_ts - (int64_t)local;

            /* validate jump size before applying (can be disabled at build time) */
#if ENABLE_OFFSET_VALIDATION
            {
                uint64_t abs_offset = (offset < 0) ? (uint64_t)(-offset) : (uint64_t)offset;
                if (abs_offset > (uint64_t)MAX_ACCEPTABLE_OFFSET_MS) {
                    printk("CHILD %d clock offset jump too large: %" PRId64 " ms (rejecting)\n", CHILD_ID, offset);
                } else {
                    clock_offset_ms = offset;
                }
            }
#else
            clock_offset_ms = offset;
#endif

            int64_t synced_ts = get_synced_uptime_ms();
            printk("CHILD %d RX: %02x %02x %02x %02x %02x %02x %02x %02x -> %" PRIu64 " ms (%" PRIu64 " s)\n",
                CHILD_ID,
                rx_data[0], rx_data[1], rx_data[2], rx_data[3],
                rx_data[4], rx_data[5], rx_data[6], rx_data[7],
                worker_ts, worker_ts/1000);
            printk("CHILD %d clock offset: %" PRId64 " ms\n", CHILD_ID, clock_offset_ms);

         
            printk("CHILD %d synced timestamp: %" PRIu64 " ms (%" PRIu64 " s)\n", CHILD_ID, synced_ts, synced_ts/1000);
            

		} else {
			printk("SPI error: %d\n", err);  /* negative errno */
			k_msleep(10);
		}

    }
    return 0;
}
