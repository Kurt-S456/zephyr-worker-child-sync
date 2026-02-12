#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <string.h>
#include <inttypes.h>

#ifndef APP_CHILD_ID
#error "APP_CHILD_ID not defined"
#endif

#define CHILD_ID APP_CHILD_ID
#define SPI_DEV_NODE DT_NODELABEL(spi1)

static uint8_t rx_data[8] __aligned(4);
static uint8_t tx_dummy[8] __aligned(4);

static struct spi_buf rx_buf = { .buf = rx_data, .len = sizeof(rx_data) };
static struct spi_buf tx_buf = { .buf = tx_dummy, .len = sizeof(tx_dummy) };
static struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
static struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

static const struct spi_config slave_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
};

/* --- MIKROSEKUNDEN LOGIK --- */

// Maximale Abweichung für Validierung: 10 Sekunden in Mikrosekunden
#ifndef MAX_ACCEPTABLE_OFFSET_US
#define MAX_ACCEPTABLE_OFFSET_US 10000000ULL
#endif

static int64_t clock_offset_us = 0;

static inline uint64_t get_uptime_us(void) {
    return (k_cycle_get_64() * 1000000ULL) / sys_clock_hw_cycles_per_sec();
}

static inline uint64_t get_synced_uptime_us(void) {
    return (uint64_t)((int64_t)get_uptime_us() + clock_offset_us);
}

int main(void)
{
    const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
    if (!device_is_ready(spi_dev)) return -1;

    printk("SPI CHILD %d ready (Precision: Microseconds)\n", CHILD_ID);

    int sync_count = 0;
    while (sync_count < 1000) {
        memset(rx_data, 0, 8);
        
        // Blockiert bis Master sendet
        int err = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);

        if (err >= 0) {
            uint64_t local_now_us = (sync_count == 0) ? get_uptime_us() : get_synced_uptime_us();
            
            // Master Timestamp aus SPI-Bytes extrahieren (us)
            uint64_t master_ts_us = 0;
            for (int i = 0; i < 8; i++) {
                master_ts_us = (master_ts_us << 8) | rx_data[i];
            }
            
            int64_t diff_us = (int64_t)master_ts_us - (int64_t)local_now_us;

            // Validierung
            uint64_t abs_diff = (diff_us < 0) ? (uint64_t)(-diff_us) : (uint64_t)diff_us;
            if (abs_diff < MAX_ACCEPTABLE_OFFSET_US || sync_count == 0) {
                clock_offset_us += diff_us; // Kumulativer Offset
            }

            sync_count++;

            double offset_ms = (double)diff_us / 1000.0;
            int32_t ms_int = (int32_t)offset_ms;
            uint32_t ms_frac = (uint32_t)((offset_ms - ms_int) * 1000000.0);
            if (offset_ms < 0 && ms_int == 0) {
                printk("CHILD %d clock offset: -%d.%06u ms\n", CHILD_ID, ms_int, ms_frac);
            } else {
                printk("CHILD %d clock offset: %d.%06u ms\n", CHILD_ID, ms_int, ms_frac);
            }

            uint64_t final_us = get_synced_uptime_us();
            printk("CHILD %d Synced Time: %" PRIu64 " us\n", CHILD_ID, final_us);

        } else {
            k_msleep(10);
        }
    }
    return 0;
}