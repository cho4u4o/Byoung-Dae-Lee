#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#define NUM_LEDS 4
#define NUM_SWITCHES 4

static int led_pins[NUM_LEDS] = {23, 24, 25, 1};
static int switch_pins[NUM_SWITCHES] = {4, 17, 27, 22};
static int irq_numbers[NUM_SWITCHES];

// 상태를 나타내는 구조체 추가
struct led_state {
    volatile int mode;
    volatile int individual_direction;
    struct task_struct *thread;
    bool thread_running;
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

// LED 스레드 함수 개선
static int led_thread_function(void *data) {
    while (!kthread_should_stop()) {
        switch (led_control.mode) {
            case 0: // 전체 모드
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
                if (led_control.individual_direction == 0) {
                    for (int i = 0; i < NUM_LEDS; i++) {
                        if (led_control.mode != 1) break;
                        set_led(i, 1);
                        printk(KERN_INFO "LED[%d] ON\n", i);
                        msleep(2000);
                        reset_leds();
                    }
                } else {
                    for (int i = NUM_LEDS - 1; i >= 0; i--) {
                        if (led_control.mode != 1) break;
                        set_led(i, 1);
                        printk(KERN_INFO "LED[%d] ON\n", i);
                        msleep(2000);
                        reset_leds();
                    }
                }
                led_control.individual_direction = !led_control.individual_direction;
                printk(KERN_INFO "Direction reversed: %s\n",
                       led_control.individual_direction ? "Right-to-Left" : "Left-to-Right");
                break;

            default:
                msleep(100);
                break;
        }
    }
    
    led_control.thread_running = false;
    return 0;
}

static irqreturn_t switch_handler(int irq, void *dev_id) {
    int switch_id = (int)(long)dev_id;
    printk(KERN_INFO "Switch %d pressed\n", switch_id);

    // 기존 스레드 안전하게 중지
    if (led_control.thread && led_control.thread_running) {
        kthread_stop(led_control.thread);
        led_control.thread = NULL;
    }

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
            {
                int led_index = 0;  // 첫 번째 LED 선택
                int current_state = gpio_get_value(led_pins[led_index]);
                set_led(led_index, !current_state);
                printk(KERN_INFO "Manual mode: LED[%d] toggled to %d\n", led_pins[led_index], !current_state);
            }
            break;

        case 3: // 리셋 모드
            reset_leds();
            printk(KERN_INFO "Reset mode activated: All LEDs turned OFF\n");
            led_control.mode = -1;
            led_control.individual_direction = 0;
            printk(KERN_INFO "Previous mode cleared, ready for new mode\n");
            break;
    }

    return IRQ_HANDLED;
}

// 나머지 코드는 이전과 동일
