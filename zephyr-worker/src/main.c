#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

int main(void)
{
	while (1)
	{
		printk("Hello, Zephyr!\n");
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
