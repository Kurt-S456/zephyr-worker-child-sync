#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#define CS1_NODE DT_ALIAS(cs1)

#if !DT_NODE_HAS_STATUS(CS1_NODE, okay)
#error "Unsupported board: cs1 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec cs1 = GPIO_DT_SPEC_GET(CS1_NODE, gpios);

void configure_cs_pin(void)
{
	if (!device_is_ready(cs1.port)) {
		printk("Error: GPIO device not ready\n");
		return;
	}

	int ret = gpio_pin_configure_dt(&cs1, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) {
		printk("Error %d: failed to configure CS pin\n", ret);
		return;
	}
}


int main(void)
{

	configure_cs_pin();

	int value = 0;
	while (1)
	{
		gpio_pin_set_dt(&cs1, value);
		printk("CS1 set to %d\n", value);
		value = !value;
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
