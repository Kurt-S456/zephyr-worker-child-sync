#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>

#define CS_NODE DT_ALIAS(cs1)
#define LED_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(CS_NODE, okay)
#error "Unsupported board: cs1 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(LED_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec cs1 = GPIO_DT_SPEC_GET(CS_NODE, gpios);
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED_NODE, gpios);

// GPIO callback structure
static struct gpio_callback cs_callback;

struct k_work child_work;

atomic_t pin_state_flag;

void led_work_handler(struct k_work *work)
{
    // Read the physical state of the pin
   int state = (int)atomic_get(&pin_state_flag);
    
    gpio_pin_set_dt(&led0, state);
    printk("Work Queue processed state: %d\n", state);
}

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

void configure_led_pin(void)
{
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	if (!device_is_ready(led0.port)) {
		printk("Error: GPIO device not ready\n");
		return;
	}
}

void cs_isr_handler(const struct device *dev, 
					struct gpio_callback *cb, 
					uint32_t pins)
{
	int val = gpio_pin_get_dt(&cs1);

	atomic_set(&pin_state_flag, (atomic_val_t)val);
    
    k_work_submit(&child_work);
	
}

int main(void)
{
	configure_cs_pin();
	configure_led_pin();

	// Initialize work item
	k_work_init(&child_work, led_work_handler);

    int ret = gpio_pin_interrupt_configure_dt(&cs1, GPIO_INT_EDGE_BOTH);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt\n", ret);
        return -1;
    }

    // 2. Initialize and add callback
    gpio_init_callback(&cs_callback, cs_isr_handler, BIT(cs1.pin));
    gpio_add_callback(cs1.port, &cs_callback);

    printk("CS pin interrupt armed\n");

    while (1) {
        k_sleep(K_FOREVER);
    }
	return 0;
}
