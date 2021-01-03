#ifndef __MINIEXP_HEADER_FILE__
#define __MINIEXP_HEADER_FILE__

#ifdef __cplusplus
extern "C" {
#endif

// uncomment to enable IOT capability:
#define WITH_WLAN
//#define WITH_IOT

// debug settings, set these to 0 or 1
#define VERBOSE 0
#define PINGPONG 0
#define DEVELOPER 1
#define HLPP 0



// ESP32
#define CASIO_UART_NUM UART_NUM_2

#define COMM_BUFF_LENGTH 30

void init_miniexp(void);
void casio_uart_processor(int events);
double get_sample(int chan);


#ifdef __cplusplus
}
#endif

#endif // __MINIEXP_HEADER_FILE__
