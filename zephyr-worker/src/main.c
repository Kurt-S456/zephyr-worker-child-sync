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

void send_timestamp_to_slave(uint8_t slave_id, uint64_t timestamp) {
    if (slave_id >= ARRAY_SIZE(cs_gpios)) return;

    uint8_t tx_data[8];
    for (int i = 0; i < 8; i++) {
        tx_data[i] = (uint8_t)(timestamp >> (56 - (i * 8)));
    }

    struct spi_buf tx_buf = { .buf = tx_data, .len = sizeof(tx_data) };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

    /* 1. Fully initialize the CS control struct from the GPIO spec */
    struct spi_cs_control cs_ctrl = {
        .gpio = cs_gpios[slave_id],
        .delay = 0,
    };

    /* 2. Configure SPI with the pointer to the CS control */
    struct spi_config cfg = {
        .frequency = 1000000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
        .cs = cs_ctrl, // Pass the struct
    };

    /* This call now handles the CS pin automatically for all 8 bytes */
    int err = spi_write(spi_dev, &cfg, &tx_bufs);
    
    if (err) {
        printk("SPI Error: %d\n", err);
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
			int64_t now = k_uptime_get();
            printk("Slave %d: Sending timestamp %lld ms\n", i, now);
            send_timestamp_to_slave(i, now);
            k_msleep(100); // Small gap between slaves
        }

        k_msleep(1000); // Send once per second
    }
    return 0;
}