#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#define NUM_LEDS 4
#define NUM_SWITCHES 4

static int led_pins[NUM_LEDS] = {23, 24, 25, 1};
static int switch_pins[NUM_SWITCHES] = {4, 17, 27, 22};
static int irq_numbers[NUM_SWITCHES];

volatile int mode = 0;
volatile int individual_direction = 0;

static void reset_leds(void);
static void set_led(int led_idx, int value);
static irqreturn_t switch_handler(int irq, void *dev_id);

static int __init led_module_init(void) {
    int ret, i;

    printk(KERN_INFO "LED Module Init\n");

    for (i = 0; i < NUM_LEDS; i++) {
        ret = gpio_request(led_pins[i], "LED");
        if (ret) {
            printk(KERN_ERR "Failed to request GPIO %d for LED\n", led_pins[i]);
            return ret;
        }
        gpio_direction_output(led_pins[i], 0);
    }

    for (i = 0; i < NUM_SWITCHES; i++) {
        ret = gpio_request(switch_pins[i], "Switch");
        if (ret) {
            printk(KERN_ERR "Failed to request GPIO %d for Switch\n", switch_pins[i]);
            return ret;
        }
        gpio_direction_input(switch_pins[i]);

        irq_numbers[i] = gpio_to_irq(switch_pins[i]);
        if (irq_numbers[i] < 0) {
            printk(KERN_ERR "Failed to get IRQ for GPIO %d\n", switch_pins[i]);
            return irq_numbers[i];
        }

        ret = request_irq(irq_numbers[i], switch_handler, IRQF_TRIGGER_RISING, "switch_irq", (void *)(long)i);
        if (ret) {
            printk(KERN_ERR "Failed to request IRQ %d for GPIO %d\n", irq_numbers[i], switch_pins[i]);
            return ret;
        }
    }

    printk(KERN_INFO "LED Module Init Complete\n");
    return 0;
}

static void __exit led_module_exit(void) {
    int i;

    printk(KERN_INFO "LED Module Exit\n");

    for (i = 0; i < NUM_LEDS; i++) {
        gpio_set_value(led_pins[i], 0);
        gpio_free(led_pins[i]);
    }

    for (i = 0; i < NUM_SWITCHES; i++) {
        free_irq(irq_numbers[i], (void *)(long)i);
        gpio_free(switch_pins[i]);
    }

    printk(KERN_INFO "LED Module Exit Complete\n");
}

static void reset_leds(void) {
    int i;
    for (i = 0; i < NUM_LEDS; i++) {
        gpio_set_value(led_pins[i], 0);
    }
}

static void set_led(int led_idx, int value) {
    if (led_idx >= 0 && led_idx < NUM_LEDS) {
        gpio_set_value(led_pins[led_idx], value);
    }
}

static irqreturn_t switch_handler(int irq, void *dev_id) {
    int switch_id = (int)(long)dev_id;
    int i;

    printk(KERN_INFO "Switch %d pressed\n", switch_id);

    mode = switch_id;

    switch (mode) {
        case 0:
            reset_leds();
            while (mode == 0) {
                for (i = 0; i < NUM_LEDS; i++) {
                    set_led(i, 1);
                }
                printk(KERN_INFO "All LEDs ON\n");
                msleep(2000);

                reset_leds();
                printk(KERN_INFO "All LEDs OFF\n");
                msleep(2000);
            }
            break;

        case 1:
            reset_leds();
            if (individual_direction == 0) {
                for (i = 0; i < NUM_LEDS; i++) {
                    set_led(i, 1);
                    printk(KERN_INFO "LED[%d] ON\n", i);
                    msleep(2000);
                    reset_leds();
                }
            } else {
                for (i = NUM_LEDS - 1; i >= 0; i--) {
                    set_led(i, 1);
                    printk(KERN_INFO "LED[%d] ON\n", i);
                    msleep(2000);
                    reset_leds();
                }
            }
            individual_direction = !individual_direction;
            printk(KERN_INFO "Direction reversed: %s\n",
                   individual_direction ? "Right-to-Left" : "Left-to-Right");
            break;

        case 2:
            if (switch_id >= 0 && switch_id < 3) {
                int led_index = switch_id;
                int current_state = gpio_get_value(led_pins[led_index]);
                set_led(led_index, !current_state);
                printk(KERN_INFO "Manual mode: LED[%d] toggled to %d\n", led_pins[led_index], !current_state);
            } else {
                printk(KERN_WARNING "Manual mode: Invalid switch_id %d\n", switch_id);
            }
            break;

        case 3:
            reset_leds();
            printk(KERN_INFO "Reset mode activated: All LEDs turned OFF\n");
            mode = -1;
            individual_direction = 0;
            printk(KERN_INFO "Previous mode cleared, ready for new mode\n");
            break;
    }

    return IRQ_HANDLED;
}

module_init(led_module_init);
module_exit(led_module_exit);

MODULE_LICENSE("GPL");