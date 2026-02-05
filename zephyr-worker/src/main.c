#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#define SPI1_NODE DT_NODELABEL(spi1)

/* Global SPI device handle */
const struct device *spi_dev = DEVICE_DT_GET(SPI1_NODE);

/* * We define the CS specs separately to ensure the GPIO 
 * structures are fully initialized by the compiler.
 */
static const struct gpio_dt_spec cs_gpios[] = {
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(SPI1_NODE, cs_gpios, 3),
};

void send_to_slave(uint8_t slave_id, uint8_t data_byte) {
    if (slave_id >= ARRAY_SIZE(cs_gpios)) return;

    /* 1. Initialize the config struct to zero */
    struct spi_config cfg = {0};
    
    /* 2. Set the SPI parameters */
    cfg.frequency = 1000000;
    cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
    cfg.slave = 0;

    /* 3. Assign the CS struct DIRECTLY (no ampersand &) */
    /* In your version of Zephyr, cfg.cs is a struct, not a pointer. */
    cfg.cs.gpio = cs_gpios[slave_id];
    cfg.cs.delay = 0;

    uint8_t tx_data[] = { data_byte };
    struct spi_buf tx_buf = { .buf = tx_data, .len = sizeof(tx_data) };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

    int err = spi_write(spi_dev, &cfg, &tx_bufs);
    if (err) {
        printk("SPI error: %d\n", err);
    }
}

int main(void) {
    if (!device_is_ready(spi_dev)) {
        printk("SPI device not ready\n");
        return 0;
    }

    /* Important: The SPI driver handles the GPIO initialization, 
       but we verify the ports just in case */
    for (int i = 0; i < ARRAY_SIZE(cs_gpios); i++) {
        if (!gpio_is_ready_dt(&cs_gpios[i])) {
            printk("GPIO for CS %d not ready\n", i);
            return 0;
        }
    }

    printk("Multi-slave SPI system online\n");

    while (1) {
        for (uint8_t i = 0; i < 4; i++) {
            printk("Talking to Slave %d...\n", i);
            send_to_slave(i, 0xAB + i);
            k_msleep(1000);
        }
    }
    return 0;
}