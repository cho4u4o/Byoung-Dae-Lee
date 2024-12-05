#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/mutex.h>

#define NUM_LEDS 4
#define NUM_SWITCHES 4
#define DEBOUNCE_DELAY (HZ / 5)  // 200ms 디바운스 시간

static int led_pins[NUM_LEDS] = {23, 24, 25, 1};
static int switch_pins[NUM_SWITCHES] = {4, 17, 27, 22};
static int irq_numbers[NUM_SWITCHES];
static bool led_states[NUM_LEDS] = {false, false, false, false};
static unsigned long last_switch_time[NUM_SWITCHES] = {0, 0, 0, 0};

static int current_mode = -1; 
static int direction = 0;     
static int current_led = 0;
static struct timer_list led_timer;
static struct mutex mode_lock;

static void reset_leds(void) {
    int i;
    for (i = 0; i < NUM_LEDS; i++) {
        gpio_set_value(led_pins[i], 0);
        led_states[i] = false;
    }
    current_mode = -1;
}

static void set_led(int led_idx, int value) {
    if (led_idx >= 0 && led_idx < NUM_LEDS) {
        gpio_set_value(led_pins[led_idx], value);
        led_states[led_idx] = value;
    }
}

static void led_timer_callback(struct timer_list *timer) {
    if (current_mode == -1) return;  // 모드가 없으면 타이머 동작 중지

    mutex_lock(&mode_lock);

    switch (current_mode) {
        case 0: // 전체 모드
            if (!led_states[0]) { 
                int i;
                for (i = 0; i < NUM_LEDS; i++) {
                    set_led(i, 1);
                }
            } else { 
                reset_leds();
            }
            break;

        case 1: // 개별 모드
            reset_leds();
            set_led(current_led, 1);

            if (direction == 0) { // 왼→오
                current_led = (current_led + 1) % NUM_LEDS;
            } else { // 오→왼
                current_led = (current_led - 1 + NUM_LEDS) % NUM_LEDS;
            }
            break;

        default:
            break;
    }

    mutex_unlock(&mode_lock);

    // 타이머 재설정 최적화
    if (current_mode != -1) {
        mod_timer(&led_timer, jiffies + HZ * 2);
    }
}

static irqreturn_t switch_handler(int irq, void *dev_id) {
    int switch_id = (int)(long)dev_id;
    unsigned long current_time = jiffies;

    // 디바운스 처리
    if (current_time - last_switch_time[switch_id] < DEBOUNCE_DELAY) {
        return IRQ_HANDLED;
    }
    last_switch_time[switch_id] = current_time;

    mutex_lock(&mode_lock);

    switch (switch_id) {
        case 0: // 전체 모드
        case 1: // 개별 모드
            current_mode = switch_id;
            current_led = (switch_id == 1) ? 0 : -1;
            
            // 타이머 재시작
            del_timer(&led_timer);
            timer_setup(&led_timer, led_timer_callback, 0);
            mod_timer(&led_timer, jiffies + HZ * 2);
            break;

        case 2: // 수동 모드
            current_mode = 2;
            if (led_states[switch_id]) {
                set_led(switch_id, 0);
            } else {
                set_led(switch_id, 1);
            }
            break;

        case 3: // 리셋 모드
            reset_leds();
            del_timer(&led_timer);
            break;
    }

    mutex_unlock(&mode_lock);

    return IRQ_HANDLED;
}

static int __init led_module_init(void) {
    int ret, i;
    unsigned long flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;

    mutex_init(&mode_lock);

    // LED 핀 초기화 with comprehensive error handling
    for (i = 0; i < NUM_LEDS; i++) {
        ret = gpio_request(led_pins[i], "LED");
        if (ret) {
            printk(KERN_ERR "Failed to request LED GPIO %d\n", led_pins[i]);
            goto led_init_error;
        }
        gpio_direction_output(led_pins[i], 0);
    }

    // 스위치 핀 초기화 및 IRQ 설정 with comprehensive error handling
    for (i = 0; i < NUM_SWITCHES; i++) {
        ret = gpio_request(switch_pins[i], "Switch");
        if (ret) {
            printk(KERN_ERR "Failed to request Switch GPIO %d\n", switch_pins[i]);
            goto switch_init_error;
        }
        gpio_direction_input(switch_pins[i]);
        
        irq_numbers[i] = gpio_to_irq(switch_pins[i]);
        if (irq_numbers[i] < 0) {
            printk(KERN_ERR "Failed to get IRQ for GPIO %d\n", switch_pins[i]);
            ret = irq_numbers[i];
            goto switch_init_error;
        }
        
        ret = request_irq(irq_numbers[i], switch_handler, flags, "switch_handler", (void *)(long)i);
        if (ret) {
            printk(KERN_ERR "Failed to request IRQ for GPIO %d\n", switch_pins[i]);
            goto switch_init_error;
        }
    }

    // 타이머 초기화
    timer_setup(&led_timer, led_timer_callback, 0);

    printk(KERN_INFO "LED Control Module Initialized Successfully\n");
    return 0;

// 오류 발생 시 정리를 위한 레이블
switch_init_error:
    // 스위치 IRQ와 GPIO 해제
    while (i--) {
        free_irq(irq_numbers[i], (void *)(long)i);
        gpio_free(switch_pins[i]);
    }
    
    // LED GPIO 해제
    for (i = 0; i < NUM_LEDS; i++) {
        gpio_free(led_pins[i]);
    }
    return ret;

led_init_error:
    // LED GPIO 해제
    while (i--) {
        gpio_free(led_pins[i]);
    }
    return ret;
}

static void __exit led_module_exit(void) {
    int i;

    // 타이머 제거
    del_timer_sync(&led_timer);

    // IRQ와 GPIO 안전하게 해제
    for (i = 0; i < NUM_SWITCHES; i++) {
        free_irq(irq_numbers[i], (void *)(long)i);
        gpio_free(switch_pins[i]);
    }

    for (i = 0; i < NUM_LEDS; i++) {
        gpio_set_value(led_pins[i], 0);
        gpio_free(led_pins[i]);
    }

    mutex_destroy(&mode_lock);

    printk(KERN_INFO "LED Control Module Safely Exited\n");
}

module_init(led_module_init);
module_exit(led_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Enhanced LED Control Module with Error Handling and Debounce");