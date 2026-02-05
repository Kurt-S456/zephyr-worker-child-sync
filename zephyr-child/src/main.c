#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// SPI Config for Slave
static const struct spi_config spi_cfg = {
    .operation = SPI_WORD_SET(8) | SPI_OP_MODE_SLAVE | SPI_TRANSFER_MSB,
    .frequency = 1000000,
    .slave = 0,
};

int main(void) {
    uint8_t rx_byte;
    struct spi_buf rx_buf = { .buf = &rx_byte, .len = 1 };
    struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1 };

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if(device_is_ready(spi_dev) == false) {
		printk("SPI device not ready\n");
		return -1;
	}

	if(device_is_ready(led.port) == false) {
		printk("LED device not ready\n");
		return -1;
	}

	printk("SPI Slave Example Started\n");

    while (1) {
		gpio_pin_set_dt(&led, 0);
        spi_read(spi_dev, &spi_cfg, &rx_bufs);
		gpio_pin_set_dt(&led, 1);
        printk("Slave Received: 0x%02X\n", rx_byte);
		k_sleep(K_MSEC(500));
    }
}