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

/* --- Data Structure for Reporting (20 Bytes) --- */
struct slave_report {
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
static uint8_t rx_data[8] __aligned(4);
static struct slave_report tx_report __aligned(4); // We send this back to Master

static struct spi_buf rx_buf = { .buf = rx_data, .len = 8 };
/* TX buffer length is now 20 bytes to send the full report */
static struct spi_buf tx_buf = { .buf = &tx_report, .len = sizeof(struct slave_report) };

static struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
static struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

static const struct spi_config slave_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
};

int main(void) {
    const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
    
    /* Initialize Semaphore and GPIO Interrupt */
    k_sem_init(&sync_sem, 0, 1);
    gpio_pin_configure_dt(&sync_pin, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&sync_pin, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&sync_cb_data, sync_callback, BIT(sync_pin.pin));
    gpio_add_callback(sync_pin.port, &sync_cb_data);

    /* Initialize the report with dummy data for the first cycle */
    tx_report.id = CHILD_ID;
    tx_report.offset_us = 0;
    tx_report.synced_time_us = 0;

    int sync_count = 0;
    while (sync_count < 1000) {
        /* Block until Master sends hardware trigger pulse */
        k_sem_take(&sync_sem, K_FOREVER);
        
        /* Receive Master Timestamp via SPI AND Send Previous Report simultaneously */
        if (spi_transceive(spi_dev, &slave_cfg, &tx_set, &rx_set) >= 0) {
            
            /* --- TIME CRITICAL PART DONE --- */
            
            uint64_t master_ts_us = 0;
            for (int i = 0; i < 8; i++) master_ts_us = (master_ts_us << 8) | rx_data[i];

            /* Calculate reference time at the exact moment of the interrupt */
            uint64_t local_ref_us = (sync_count == 0) ? 
                                    latched_local_us : 
                                    (uint64_t)((int64_t)latched_local_us + clock_offset_us);
            
            int64_t diff_us = (int64_t)master_ts_us - (int64_t)local_ref_us;

            /* TWO METHODS OF SYNCHRONIZATION */
            #ifdef USE_CUMULATIVE_SYNC
                /* Method A: Cumulative (Drift Compensation) 
                   Adds error to current offset for gradual convergence */
                clock_offset_us += diff_us;
            #else
                /* Method B: Hard Step (Absolute Set) 
                   Resets offset based on the latest hardware latch */
                clock_offset_us = (int64_t)master_ts_us - (int64_t)latched_local_us;
            #endif

            /* PREPARE REPORT FOR NEXT CYCLE */
            /* Instead of printk, we fill the struct. The hardware will send this
               automatically the next time the Master toggles our Chip Select. */
            tx_report.id = CHILD_ID;
            tx_report.offset_us = diff_us;
            tx_report.synced_time_us = local_ref_us;

            sync_count++;
        }
    }
    return 0;
}