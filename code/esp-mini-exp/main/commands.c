// commands for serial console
// rev 1: shabaz December 2020

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
#include "argtable3/argtable3.h"
#include "nvs.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <driver/adc.h>

#include "nvs_flash.h"
#include "timerfunc.h"

#define STORAGE_NAMESPACE "storage"

// function prototypes
esp_err_t get_nvs_str(const char* name, char* buf);
esp_err_t save_nvs_str(const char* name, char* buf, size_t maxsize);

// ********** console commands ****************

// ***** wifi *****
// example: wifi myssid mypassword

int get_wifi_details(char* ssid, char* wifipw)
{
    get_nvs_str("ssid", ssid);
    get_nvs_str("wifipw", wifipw);

    return(0);
}

esp_err_t save_wifi_details(char* ssid, char* wifipw)
{
    save_nvs_str("ssid", ssid, 32);
    save_nvs_str("wifipw", wifipw, 64);

    return ESP_OK;
}

static struct {
    //struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_args;

static int wifi_details(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &wifi_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_args.end, argv[0]);
        return 1;
    }
    if (strcmp(wifi_args.ssid->sval[0], "status")==0) {
        wifi_ap_record_t apinfo;
        esp_err_t ret;
        ret=esp_wifi_sta_get_ap_info(&apinfo);
        if (ret==ESP_OK) {
            printf("WiFi connected, rssi=%d\r\n", apinfo.rssi);
            uint16_t y = get_year();
            if (y==0) {
                printf("Wait for time to be set\r\n");
            } else {
                printf("Year is %d\r\n", y);
            }
        } else {
            printf("WiFi not connected\r\n");
        }
    } else {
        save_wifi_details((char*) wifi_args.ssid->sval[0], (char*) wifi_args.password->sval[0]);
        printf("Reboot for new WiFi credentials to take effect\r\n");
    }
    //bool connected = wifi_join(join_args.ssid->sval[0],
    //                           join_args.password->sval[0],
    //                           join_args.timeout->ival[0]);

    return 0;
}

void register_wifi(void)
{
    //wifi_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    wifi_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    wifi_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    wifi_args.end = arg_end(2);

    const esp_console_cmd_t wifi_cmd = {
        .command = "wifi",
        .help = "WiFi details",
        .hint = NULL,
        .func = &wifi_details,
        .argtable = &wifi_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&wifi_cmd) );
}

// ***** iot *****
// example: iot mydeviceid myidscope mysaskey

int get_iot_details(char* iotdev, char* iotscope, char* iotkey)
{
    get_nvs_str("iotdev", iotdev);
    get_nvs_str("iotscope", iotscope);
    get_nvs_str("iotkey", iotkey);

    return(0);
}

esp_err_t save_iot_details(char* iotdev, char* iotscope, char* iotkey)
{
    save_nvs_str("iotdev", iotdev, 32);
    save_nvs_str("iotscope", iotscope, 16);
    save_nvs_str("iotkey", iotkey, 64);

    return ESP_OK;
}

static struct {
    struct arg_str *device;
    struct arg_str *scope;
    struct arg_str *key;
    struct arg_end *end;
} iot_args;

static int iot_details(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &iot_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, iot_args.end, argv[0]);
        return 1;
    }

    save_iot_details(   (char*) iot_args.device->sval[0], 
                        (char*) iot_args.scope->sval[0],   
                        (char*) iot_args.key->sval[0] );
    
    printf("Reboot for new IoT credentials to take effect\r\n");

    return 0;
}

void register_iot_cmd(void)
{
    iot_args.device = arg_str1(NULL, NULL, "<device>", "device ID");
    iot_args.scope = arg_str0(NULL, NULL, "<scope>", "ID scope");
    iot_args.key = arg_str0(NULL, NULL, "<key>", "SAS key");
    iot_args.end = arg_end(2);

    const esp_console_cmd_t iot_cmd = {
        .command = "iot",
        .help = "IoT Device details",
        .hint = NULL,
        .func = &iot_details,
        .argtable = &iot_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&iot_cmd) );
}

// ************ initialize console ********************
void initialize_console(void)
{
    fflush(stdout);
    fsync(fileno(stdout));
    setvbuf(stdin, NULL, _IONBF, 0);
    //esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    //esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .source_clk = UART_SCLK_REF_TICK,
    };
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config) );
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_console_config_t console_config = {
            .max_cmdline_args = 8,
            .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
            .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(10);
    //linenoiseAllowEmpty(false);

}

//********* get a string from NVS **************
esp_err_t get_nvs_str(const char* name, char* buf)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    size_t required_size;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    // read from NVS the stored string
    required_size = 0; // first set to zero to query if already stored in NVS
    err = nvs_get_str(my_handle, name, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err; // unexpected error
    // required_size will now be greater than 0 if currently stored in NVS
    if (required_size==0) {
        buf[0]='\0'; // no SSID stored
    } else {
        err = nvs_get_str(my_handle, name, buf, &required_size);
    }

    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

// save a string to NVS
esp_err_t save_nvs_str(const char* name, char* buf, size_t maxsize)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    size_t required_size;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    // save to NVS the content
    required_size = 0; // first set to zero to query if already stored in NVS
    err = nvs_get_str(my_handle, name, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err; // unexpected error
    // required_size will now be greater than 0 if currently stored in NVS
    required_size = maxsize;
    err = nvs_set_str(my_handle, name, buf);

    // commit and close
    err = nvs_commit(my_handle);
    if (err != ESP_OK) return err;
    nvs_close(my_handle);
    return ESP_OK;
}

