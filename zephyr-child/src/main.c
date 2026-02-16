#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#define CHILD_ID APP_CHILD_ID
#define SPI_DEV_NODE DT_NODELABEL(spi1)
#define SYNC_PIN_NODE DT_ALIAS(sw0) 
#define SYNC_MAGIC 0xACE1ACE1

/* --- Data Structure for Reporting (20 Bytes) --- */
struct slave_report {
    uint32_t magic;
    uint32_t id;
    int64_t  offset_us;
    uint64_t synced_time_us;
} __attribute__((packed));

static const struct gpio_dt_spec sync_pin = GPIO_DT_SPEC_GET(SYNC_PIN_NODE, gpios);
static struct gpio_callback sync_cb_data;
static struct k_sem sync_sem;

/* Time latched during hardware interrupt */
static volatile uint64_t latched_local_us = 0;
static int64_t clock_offset_us = 0;

/* Returns raw hardware uptime in microseconds */
static inline uint64_t get_uptime_us(void) {
    return (k_cycle_get_64() * 1000000ULL) / sys_clock_hw_cycles_per_sec();
}

/* Returns uptime adjusted by the calculated clock offset */
static inline uint64_t get_synced_uptime_us(void) {
    return (uint64_t)((int64_t)get_uptime_us() + clock_offset_us);
}

/* ISR: Executed immediately on hardware signal edge */
void sync_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    latched_local_us = get_uptime_us();
    k_sem_give(&sync_sem);
}

/* SPI Buffers updated for Full Duplex Reporting */
static uint8_t rx_data[sizeof(struct slave_report)] __aligned(4);
static struct slave_report tx_report __aligned(4);

static struct spi_buf rx_buf = { .buf = rx_data, .len = sizeof(struct slave_report) };
static struct spi_buf tx_buf = { .buf = &tx_report, .len = sizeof(struct slave_report) };

static struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
static struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

static const struct spi_config slave_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | 
                 SPI_TRANSFER_MSB,
};

int main(void) {
    const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
    
    /* Initialize Semaphore and GPIO Interrupt */
    k_sem_init(&sync_sem, 0, 1);
    gpio_pin_configure_dt(&sync_pin, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_pin_interrupt_configure_dt(&sync_pin, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&sync_cb_data, sync_callback, BIT(sync_pin.pin));
    gpio_add_callback(sync_pin.port, &sync_cb_data);

    /* Initialize the report with dummy data for the first cycle */
   
    int sync_count = 0;
    while (1) {
        /* 1. Wait for the hardware pulse */
        k_sem_take(&sync_sem, K_FOREVER);
        printk("Sync trigger received...\n");
        /* 2. IMMEDIATELY enter SPI mode. 
            Do not print or calculate anything here. */
        int ret = spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set);

        if (ret == 0) {
            /* 3. Extract the timestamp received FROM the Master */
            uint64_t master_ts_us = 0;
            for (int i = 0; i < 8; i++) {
                master_ts_us = (master_ts_us << 8) | rx_data[i];
            }

            /* 4. Do the sync math */
            clock_offset_us = (int64_t)master_ts_us - (int64_t)latched_local_us;
            uint64_t local_ref_us = (uint64_t)((int64_t)latched_local_us + clock_offset_us);
            int64_t diff_us = (int64_t)master_ts_us - (int64_t)local_ref_us;

            /* 5. Prepare the data for the NEXT sync cycle */
            tx_report.id = CHILD_ID;
            tx_report.offset_us = diff_us;
            tx_report.synced_time_us = local_ref_us;

            /* 6. Print only once in a while to prevent err=64 */
            if (sync_count % 10 == 0) {
                printk("S%d synced\n", CHILD_ID);
            }
            sync_count++;
        }
    }   
    return 0;
}