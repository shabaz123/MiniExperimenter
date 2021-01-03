

#ifndef _TIMERFUNC_HEADER_FILE_H
#define _TIMERFUNC_HEADER_FILE_H

#ifdef __cplusplus
extern "C" {
#endif


typedef struct timer_event_s {
    int timernum;
    int event;
    uint64_t countval;
    double meas[3]; // 3 is CHAN_MAX
} timer_event_t;

// timer functions

void sample_timer_init(void);
void sample_timer_start(uint64_t usec);
void sample_timer_stop(void);

// general time functions
uint16_t get_year(void); // useful for seeing if NTP has worked.

// setup timer group 0, timer 0 or timer 1
//void setup_tg0_timer(int timer_idx, bool auto_reload, double timer_interval_usec);




#ifdef __cplusplus
}
#endif

#endif /* _TIMERFUNC_HEADER_FILE_H */
