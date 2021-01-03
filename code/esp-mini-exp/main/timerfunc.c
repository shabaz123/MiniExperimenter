
// timer functions
// shabaz rev 1 December 2020

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <time.h>
#include <sys/time.h>

#include "esp_system.h"
#include "esp_console.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <driver/adc.h>
#include "esp_types.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "miniexp.h"
#include "timerfunc.h"
#include "esp_timer.h"

esp_timer_handle_t sample_timer;
extern int8_t sample_method;
extern xQueueHandle timer_queue;

static char sample_timer_active=0;


uint16_t get_year(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2020 - 1900)) {
        //printf("Time is not currectly set\r\n");
        return(0);
    }
    return(timeinfo.tm_year + 1900);
}

void sample_timer_callback(void* arg)
{

    timer_event_t evt;
    evt.event = 0;
    evt.countval = 0;

    if (sample_method & 0x01)
        evt.meas[0] = get_sample(0);
    else
        evt.meas[0] = 0;

    if (sample_method & 0x02)
        evt.meas[1] = get_sample(1);
    else
        evt.meas[1] = 0;

    if (sample_method & 0x04)
        evt.meas[2] = get_sample(2);
    else
        evt.meas[2] = 0;

    xQueueSendFromISR(timer_queue, &evt, NULL); // probably should use a non-ISR send function
}

void sample_timer_init(void)
{
    const esp_timer_create_args_t sample_timer_args = {
        .callback = &sample_timer_callback,
        .name = "sample"
    };
    ESP_ERROR_CHECK(esp_timer_create(&sample_timer_args, &sample_timer));
}

void sample_timer_start(uint64_t usec) {
    ESP_ERROR_CHECK(esp_timer_start_periodic(sample_timer, usec));
    sample_timer_active=1;
}

void sample_timer_stop(void) {
    if (sample_timer_active) {
        ESP_ERROR_CHECK(esp_timer_stop(sample_timer));
        sample_timer_active=0;
    }
}


#ifdef JUNK
#define TIMER_DIVIDER         800  //  Hardware timer clock divider, 800 means 80M/800 = 100kHz rate
#define TIMER_SCALE (TIMER_BASE_CLK / (TIMER_DIVIDER*1000000)) // when multiplied by a value in usec, we will get the desired number of ticks

extern xQueueHandle timer_queue;

extern int8_t sample_method;


// timer group 0 has two timers, timer 0 and timer 1. This is the interrupt for both timer 0 and timer 1
void IRAM_ATTR timer_group0_isr(void *para)
{
    timer_spinlock_take(TIMER_GROUP_0);
    int timer_idx = (int) para;
    double value=0;

    uint32_t timer_intr = timer_group_get_intr_status_in_isr(TIMER_GROUP_0); // interrupt status
    uint64_t timer_counter_value = timer_group_get_counter_value_in_isr(TIMER_GROUP_0, timer_idx); // counter value

    /* Prepare basic event data
       that will be then sent back to the main program task */
    timer_event_t evt;
    evt.timernum = timer_idx;
    evt.event = 0;
    evt.countval = timer_counter_value;

    /* Clear the interrupt
       and update the alarm time for the timer so that it is reloaded */
    if (timer_intr & TIMER_INTR_T0) {
        // timer 0 interrupt occurred
        switch(sample_method) {
            case 0:
                value=get_sample(0);
                break;
            default:
                break;
        }
        timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0); // reload
    } else if (timer_intr & TIMER_INTR_T1) {
        // timer 1 interrupt occurred
        // Don't reload. We are not using timer 1
        //timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
        //timer_counter_value += (uint64_t) (TIMER_INTERVAL0_SEC * TIMER_SCALE);
        //timer_group_set_alarm_value_in_isr(TIMER_GROUP_0, timer_idx, timer_counter_value);
    } else {
        // should not occur
    }

    evt.meas[0]=value;
    /* After the alarm has been triggered
      we need enable it again, so it is triggered the next time */
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, timer_idx);

    /* Now just send the event data back to the main program task */
    xQueueSendFromISR(timer_queue, &evt, NULL);
    timer_spinlock_give(TIMER_GROUP_0);
}

// set up timer 0 or 1 in timer group 0
void setup_tg0_timer(int timer_idx, bool auto_reload, double timer_interval_usec)
{
    /* Select and initialize basic parameters of the timer */
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = auto_reload,
    }; // default clock source is APB

    printf("about to call timer_init\r\n");
    timer_init(TIMER_GROUP_0, timer_idx, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    printf("about to call timer_set_counter_value\r\n");
    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000ULL);

    /* Configure the alarm value and the interrupt on alarm. */
    printf("about to call timer_set_alarm_value\r\n");
    timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_usec * TIMER_SCALE);
    printf("about to call timer_enable_intr\r\n");
    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    printf("about to call timer_isr_register\r\n");
    timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr,
                       (void *) timer_idx, ESP_INTR_FLAG_IRAM, NULL);

    printf("about to call timer_start\r\n");
    timer_start(TIMER_GROUP_0, timer_idx);
     printf("done timer_start\r\n");
}

#endif
