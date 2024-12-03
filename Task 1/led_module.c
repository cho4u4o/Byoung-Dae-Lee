#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>

#define NUM_LEDS 4
#define NUM_SWITCHES 4

// 실제 Raspberry Pi GPIO 핀 매핑 (검증 필요)
static int led_pins[NUM_LEDS] = {23, 24, 25, 1};
static int switch_pins[NUM_SWITCHES] = {4, 17, 27, 22};
static int irq_numbers[NUM_SWITCHES];

struct led_state {
    volatile int mode;
    volatile int individual_direction;
    struct task_struct *thread;
    bool thread_running;
    struct mutex lock;  // 뮤텍스 추가
};

static struct led_state led_control = {
    .mode = -1,
    .individual_direction = 0,
    .thread = NULL,
    .thread_running = false
};

static void reset_leds(void) {
    for (int i = 0; i < NUM_LEDS; i++) {
        gpio_set_value(led_pins[i], 0);
    }
}

static void set_led(int led_idx, int value) {
    if (led_idx >= 0 && led_idx < NUM_LEDS) {
        gpio_set_value(led_pins[led_idx], value);
    }
}

static int led_thread_function(void *data) {
    while (!kthread_should_stop()) {
        mutex_lock(&led_control.lock);
        int current_mode = led_control.mode;
        mutex_unlock(&led_control.lock);

        switch (current_mode) {
            case 0: // 전체 모드
                reset_leds();
                for (int i = 0; i < NUM_LEDS; i++) {
                    set_led(i, 1);
                }
                printk(KERN_INFO "All LEDs ON\n");
                msleep(2000);
                reset_leds();
                printk(KERN_INFO "All LEDs OFF\n");
                msleep(2000);
                break;

            case 1: // 개별 모드
                reset_leds();
                mutex_lock(&led_control.lock);
                int direction = led_control.individual_direction;
                mutex_unlock(&led_control.lock);

                if (direction == 0) {
                    for (int i = 0; i < NUM_LEDS; i++) {
                        mutex_lock(&led_control.lock);
                        if (led_control.mode != 1) {
                            mutex_unlock(&led_control.lock);
                            break;
                        }
                        mutex_unlock(&led_control.lock);

                        set_led(i, 1);
                        printk(KERN_INFO "LED[%d] ON\n", i);
                        msleep(2000);
                        reset_leds();
                    }
                } else {
                    for (int i = NUM_LEDS - 1; i >= 0; i--) {
                        mutex_lock(&led_control.lock);
                        if (led_control.mode != 1) {
                            mutex_unlock(&led_control.lock);
                            break;
                        }
                        mutex_unlock(&led_control.lock);

                        set_led(i, 1);
                        printk(KERN_INFO "LED[%d] ON\n", i);
                        msleep(2000);
                        reset_leds();
                    }
                }

                mutex_lock(&led_control.lock);
                led_control.individual_direction = !led_control.individual_direction;
                mutex_unlock(&led_control.lock);
                break;

            default:
                msleep(100);
                break;
        }
    }
    
    mutex_lock(&led_control.lock);
    led_control.thread_running = false;
    mutex_unlock(&led_control.lock);
    return 0;
}

static irqreturn_t switch_handler(int irq, void *dev_id) {
    int switch_id = (int)(long)dev_id;
    printk(KERN_INFO "Switch %d pressed\n", switch_id);

    mutex_lock(&led_control.lock);
    
    // 기존 스레드 안전하게 중지
    if (led_control.thread && led_control.thread_running) {
        kthread_stop(led_control.thread);
        led_control.thread = NULL;
    }

    switch (led_control.mode) {
        case 2: // 수동 모드
            if (switch_id < 3) {
                reset_leds();
                set_led(switch_id, 1);
                printk(KERN_INFO "Manual mode: LED[%d] ON\n", switch_id);
                break;
            }

        case 3: // 리셋 모드
            reset_leds();
            led_control.mode = -1;
            led_control.individual_direction = 0;
            printk(KERN_INFO "Reset mode: All LEDs OFF, Ready for new mode\n");
            break;

        default: // 모드 선택
            led_control.mode = switch_id;

            switch (switch_id) {
                case 0: // 전체 모드
                case 1: // 개별 모드
                    led_control.thread = kthread_run(led_thread_function, NULL, "led_control_thread");
                    if (IS_ERR(led_control.thread)) {
                        printk(KERN_ERR "Failed to create LED control thread\n");
                        led_control.thread = NULL;
                    } else {
                        led_control.thread_running = true;
                    }
                    break;

                case 2: // 수동 모드
                    reset_leds();
                    printk(KERN_INFO "Manual mode: Select LED (0/1/2)\n");
                    break;

                case 3: // 리셋 모드
                    reset_leds();
                    led_control.mode = -1;
                    printk(KERN_INFO "Reset mode: All LEDs OFF\n");
                    break;
            }
            break;
    }
    
    mutex_unlock(&led_control.lock);
    return IRQ_HANDLED;
}

static int __init led_module_init(void) {
    int ret, i;

    mutex_init(&led_control.lock);  // 뮤텍스 초기화

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

        // IRQF_TRIGGER_FALLING 또는 IRQF_TRIGGER_BOTH로 변경
        ret = request_irq(irq_numbers[i], switch_handler, IRQF_TRIGGER_FALLING, "switch_irq", (void *)(long)i);
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

    mutex_lock(&led_control.lock);
    if (led_control.thread) {
        kthread_stop(led_control.thread);
    }
    mutex_unlock(&led_control.lock);

    for (i = 0; i < NUM_LEDS; i++) {
        gpio_set_value(led_pins[i], 0);
        gpio_free(led_pins[i]);
    }

    for (i = 0; i < NUM_SWITCHES; i++) {
        free_irq(irq_numbers[i], (void *)(long)i);
        gpio_free(switch_pins[i]);
    }

    mutex_destroy(&led_control.lock);

    printk(KERN_INFO "LED Module Exit Complete\n");
}

module_init(led_module_init);
module_exit(led_module_exit);

MODULE_LICENSE("GPL");
