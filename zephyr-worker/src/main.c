#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#define EXPECTED_MAGIC 0xACE1ACE1

#define SPI1_NODE DT_NODELABEL(spi1)
#define SYNC_PIN_NODE DT_ALIAS(sync_out)
#define NUM_SLAVES 4

#ifndef MASTER_SYNC_INTERVAL_MS
#define MASTER_SYNC_INTERVAL_MS 60000 
#endif

struct slave_report {
    uint32_t magic;
    uint32_t id;
    int64_t  offset_us;
    uint64_t synced_time_us;
} __attribute__((packed));

static const struct device *spi_dev = DEVICE_DT_GET(SPI1_NODE);
static const struct gpio_dt_spec sync_out = GPIO_DT_SPEC_GET(SYNC_PIN_NODE, gpios);

/* Buffer to store the batch of reports from one sync cycle */
static struct slave_report batch_results[NUM_SLAVES];
static uint64_t last_sync_timestamp = 0;

static const struct gpio_dt_spec cs_gpios[] = {
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 3),
};

static inline uint64_t get_uptime_us(void) {
    return (k_cycle_get_64() * 1000000ULL) / sys_clock_hw_cycles_per_sec();
}

/* Helper: print a small buffer as contiguous hex (no spaces) */
static void dump_hex(const void *buf, size_t len)
{
    const uint8_t *b = buf;
    char out[128];
    int pos = 0;
    for (size_t i = 0; i < len && pos < (int)sizeof(out) - 3; i++) {
        pos += snprintf(&out[pos], sizeof(out) - pos, "%02X", b[i]);
    }
    out[pos] = '\0';
    printk("RXHEX,%s\n", out);
}

/**
 * Perform the SPI transaction and store the result in the batch buffer
 */
static int collect_single_report(uint8_t slave_id, uint64_t timestamp_us)
{
    uint8_t tx_full[sizeof(struct slave_report)] = {0};

    /* Encode timestamp */
    for (int i = 0; i < 8; i++) {
        tx_full[i] = (uint8_t)(timestamp_us >> (56 - (i * 8)));
    }

    struct spi_buf tx_buf = { .buf = tx_full,  .len = sizeof(tx_full) };
    struct spi_buf rx_buf = { .buf = &batch_results[slave_id], .len = sizeof(struct slave_report) };
    
    struct spi_buf_set txs = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rxs = { .buffers = &rx_buf, .count = 1 };

    struct spi_cs_control cs_ctrl = {
        .gpio  = cs_gpios[slave_id],
        .delay = 0, 
    };

    struct spi_config cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | 
                 SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
        .cs = cs_ctrl,
    };

    return spi_transceive(spi_dev, &cfg, &txs, &rxs);
}

int main(void)
{
    if (!device_is_ready(spi_dev) || !gpio_is_ready_dt(&sync_out)) return 0;
    gpio_pin_configure_dt(&sync_out, GPIO_OUTPUT_INACTIVE);

    for (int i = 0; i < NUM_SLAVES; i++) {
        gpio_is_ready_dt(&cs_gpios[i]);
    }

    printk("Master Buffered Logger Online. CSV Header Ready.\n");
    printk("CSV: master_ts,child_id,sync_cnt,child_ref,offset_us\n");

    while (1) {
        /* PHASE 1: HIGH PRECISION PULSE */
        gpio_pin_set_dt(&sync_out, 1);
        k_busy_wait(100);
        last_sync_timestamp = get_uptime_us(); 
        gpio_pin_set_dt(&sync_out, 0);
        /* Give Slaves time to wake up and call spi_transceive() */
        k_msleep(100);
        /* PHASE 2: RAPID DATA COLLECTION (No Printing!) */
        for (uint8_t i = 0; i < NUM_SLAVES; i++) {
            printk("Attempting Slave %d...\n", i);
            int ret = collect_single_report(i, last_sync_timestamp);
            if (ret < 0) {
                printk("SPI Error Slave %d: %d\n", i, ret);
            } 
            // Minimal delay just for slave logic to prep, but no UART here
            k_busy_wait(500); 
        }

        /* PHASE 3: RELAXED LOGGING (During Sleep) */
        for (uint8_t i = 0; i < NUM_SLAVES; i++) {
            if (batch_results[i].magic == EXPECTED_MAGIC) {
                printk("DATA,%" PRIu64 ",%u,%" PRIu64 ",%" PRId64 "\n", 
                    last_sync_timestamp,
                    batch_results[i].id,
                    batch_results[i].synced_time_us,
                    batch_results[i].offset_us);
            } else {
                /* This catches bit-shifts or bad wiring */
                printk("ERROR,%" PRIu64 ",%u,BAD_MAGIC(0x%08X),0,0\n", 
                    last_sync_timestamp, 
                    i, 
                    batch_results[i].magic);
                /* Print expected vs actual and the parsed fields for diagnosis */
                printk("DETAILS,expected=0x%08X,actual=0x%08X,id=%u,offset=%" PRId64 ",synced=%" PRIu64 "\n",
                    EXPECTED_MAGIC,
                    batch_results[i].magic,
                    batch_results[i].id,
                    batch_results[i].offset_us,
                    batch_results[i].synced_time_us);
                /* Hex dump the raw received structure bytes */
                dump_hex(&batch_results[i], sizeof(batch_results[i]));
            }
        }
        k_msleep(MASTER_SYNC_INTERVAL_MS);
    }
    return 0;
}