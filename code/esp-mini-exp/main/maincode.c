/**********************************
 * maincode.c
 * 
 * The code here starts up the processes,
 * initializes the console and WiFI,
 * and handles byte-level UART reception
 * (UART2, GPIO16 and 17) for onward
 * forwarding to the miniexp code
 * 
 * rev 0 - example code for azure - Microsoft
 * rev 1 - initial mod for miniexp - shabaz December 2020
 * 
 **********************************/


#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "linenoise/linenoise.h"
#include "nvs.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <driver/adc.h>
#include "commands.h"

#include "nvs_flash.h"
#include "miniexp.h"
#ifdef WITH_IOT
#include "azure-iot-central.h"
#endif

#include "timerfunc.h"
#include "esp_timer.h"



#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

// UART for connection to Casio
#define PATTERN_CHR_NUM    (1)
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
#define MIN_PATTERN_INTERVAL (9)
#define MIN_POST_IDLE (0)
#define MIN_PRE_IDLE (0)
// might need to increase RX_TIMEOUT in future
#define RX_TIMEOUT 2

#ifdef CONFIG_ESP_CONSOLE_USB_CDC
#error This example is incompatible with USB CDC console. Please try "console_usb" example instead.
#endif // CONFIG_ESP_CONSOLE_USB_CDC

const char* prompt = "> ";
static QueueHandle_t uart0_queue;

esp_timer_handle_t sample_timer;
xQueueHandle timer_queue;

extern uint8_t casio_rx_buf[];

static char do_append=0;
static uint8_t appendbuf[64];
static int appendpos=0;

QueueHandle_t iotq;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

#ifndef BIT0
#define BIT0 (0x1 << 0)
#endif
/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static const char *TAG = "me"; // logging tag

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP platform WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

int esp_platform_init(void);
static void initialise_wifi(char* ssid, char* wifipw)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    if ((ssid[0]!='\0') && (wifipw[0]!='\0')) {
        strncpy((char*)wifi_config.sta.ssid, ssid, 32);
        wifi_config.sta.ssid[31]='\0';
        strncpy((char*)wifi_config.sta.password, wifipw, 64);
        wifi_config.sta.password[63]='\0';

    }
    ESP_LOGI(TAG, "Setting WiFi configuration SSID [%c...]", wifi_config.sta.ssid[0]);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    bzero(wifi_config.sta.ssid, 32);
    bzero(wifi_config.sta.password, 64);
    ESP_ERROR_CHECK( esp_wifi_start() );
}

// task to handle UART events for Casio connection
static void uart_event_task(void *pvParameters)
{
    int i,j;
    uart_event_t event;
    size_t buffered_size;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart0_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
            if (VERBOSE) {ESP_LOGI(TAG, "uart[%d] event:", CASIO_UART_NUM);}
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    if (VERBOSE) {ESP_LOGI(TAG, "[UART DATA]: %d", event.size);}
                    uart_read_bytes(CASIO_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    if (VERBOSE) {ESP_LOGI(TAG, "size %d, first bytes are 0x%02x, %02x, %02x, %02x", event.size, dtmp[0], dtmp[1], dtmp[2], dtmp[3]);}
                    if (VERBOSE) {ESP_LOGI(TAG, "[DATA EVT]:");}
                    if ((do_append==0) && (event.size<15) && (event.size>2)) {
                        if ((dtmp[0]==':') && ((dtmp[1]=='N') || (dtmp[1]=='R'))) {
                            do_append=1;
                            appendpos=event.size;
                            for (i=0; i<event.size; i++) {
                                appendbuf[i]=dtmp[i];
                            }
                        } else {
                            for (j=0; j<event.size; j++) {
                                casio_rx_buf[j]=dtmp[j];
                            }
                        }
                    } else if (do_append==1) {
                        for (i=0; i<event.size; i++) {
                            appendbuf[appendpos]=dtmp[i];
                            appendpos++;
                            if (appendpos>=15) {
                                do_append=0;
                                appendpos=0;
                                for (j=0; j<15; j++) {
                                    casio_rx_buf[j]=appendbuf[j];
                                }
                                break;
                            }
                        }
                    } else {
                        for (i=0; i<event.size; i++) {
                            if (i>=COMM_BUFF_LENGTH)
                                break;
                            else
                                casio_rx_buf[i]=dtmp[i];
                        }
                    }

                    if (do_append==0) {
                        casio_uart_processor(1);
                    }
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(CASIO_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(CASIO_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(CASIO_UART_NUM, &buffered_size);
                    int pos = uart_pattern_pop_pos(CASIO_UART_NUM);
                    ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                    if (pos == -1) {
                        // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                        // record the position. We should set a larger queue size.
                        // As an example, we directly flush the rx buffer here.
                        uart_flush_input(CASIO_UART_NUM);
                    } else {
                        uart_read_bytes(CASIO_UART_NUM, dtmp, pos, 100 / portTICK_PERIOD_MS);
                        uint8_t pat[PATTERN_CHR_NUM + 1];
                        memset(pat, 0, sizeof(pat));
                        uart_read_bytes(CASIO_UART_NUM, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                        ESP_LOGI(TAG, "read data: %s", dtmp);
                        ESP_LOGI(TAG, "read pat : %s", pat);
                    }
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

// task to handle interaction with Azure IoT Central
void azure_task(void *pvParameter)
{
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP success!");

    esp_platform_init();
#ifdef WITH_IOT
    iotc_main();
#endif

    vTaskDelete(NULL);
}

void app_main()
{
    esp_err_t ret;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    initialize_console();

    init_miniexp();
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));
    sample_timer_init();

    // register console commands
    register_wifi();
    register_iot_cmd();

    // get wifi credentials and initialize wifi
    char* ssid = malloc(32);
    char* wifipw = malloc(64);
    get_wifi_details(ssid, wifipw);
#ifdef WITH_WLAN
    initialise_wifi(ssid, wifipw);
#endif
    bzero(ssid, 32);
    bzero(wifipw, 64);
    free(ssid);
    free(wifipw);

    // test
    //casio_uart_processor(10);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6,ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_5,ADC_ATTEN_DB_11);

    iotq = xQueueCreate( 5, 32); // a queue of up to 5 items, of 32 bytes each

    // UART for Casio
    uart_config_t uart_config = {
        .baud_rate = 38400,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(CASIO_UART_NUM, &uart_config);
    ret=uart_set_pin(CASIO_UART_NUM, GPIO_NUM_17 /*Tx*/, GPIO_NUM_16 /*Rx*/, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret==ESP_OK) {
        printf("uart_set_pin OK\r\n");
    }
    uart_driver_install(CASIO_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);
    //uart_enable_pattern_det_intr(CASIO_UART_NUM, 0x15, PATTERN_CHR_NUM, 10000, 10, 10)
    uart_set_rx_timeout(CASIO_UART_NUM, RX_TIMEOUT);
    //uart_enable_pattern_det_baud_intr(CASIO_UART_NUM, 0x15, PATTERN_CHR_NUM, MIN_PATTERN_INTERVAL, MIN_POST_IDLE, MIN_PRE_IDLE);
    uart_pattern_queue_reset(CASIO_UART_NUM, 20);
    xTaskCreate(uart_event_task, "uart_event_task", 1024*10, NULL, 12, NULL);

#ifdef WITH_IOT
    if ( xTaskCreate(&azure_task, "azure_task", 1024 * 10, NULL, 5, NULL) != pdPASS ) {
        printf("create azure task failed\r\n");
    }
#endif

    printf("Hello from Mini Explorer\r\n");
    while(1) {
        char* line = linenoise(prompt);
        //if (line == NULL) break; // might want to comment this out and stay in console
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);

    }
    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();

}
