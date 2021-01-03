/**********************************************************
 * Mine Explorer
 * rev 1 - shabaz - December 2020 - original code for ThunderBoard Sense 2
 * rev 1.1 - shabaz - December 2020 - extended for ESP32 and new features added
 * 
 * This code contains the main Mini Explorer processor
 * 
 * ********************************************************/


//#define MBED

#ifndef MBED
extern "C" {
#endif

#ifdef MBED
#include "mbed.h"
#include "Si1133.h"
#else
#include "miniexp.h"
#include <string.h>
#include "driver/uart.h"
#include <driver/adc.h>
#include "driver/timer.h"
#include "esp_timer.h"
#include "timerfunc.h"
#include "esp_wifi.h"
#endif


//defines
#define LED_PIN         LED0
#define TOGGLE_RATE     (0.5f)
#define BUFF_LENGTH     5
#define CASIO_START_INDICATOR 0x15
#define CODEA_OK 0x13
#define CODEB_OK 0x06
#define CODEA_RETRY 0x05
#define CODEB_RETRY 0x05
#define CODEA_ERROR 0x22
#define CODEB_ERROR 0x22
#define DIR_CASIO_SEND 'N'
#define DIR_CASIO_RECV 'R'
#define TYPE_ASCII 'A'
#define TYPE_HEX 'H'
#define START_HEADER_ERROR -10
#define COMM_IDLE 0
#define COMM_WAITING_INSTRUCTION 1
#define COMM_WAITING_DATA 2
#define COMM_WAITING_RX_HEADER_ACK 3
#define COMM_WAITING_RX_PACKET_ACK 4
#define COMM_WAITING_PERFORM_ROLESWAP 5
#define PROC_NULL 0
#define PROC_SEND38K 1
#define PROC_RECV38K 2
#define BRIEF 1
#define HL_IDLE 0
#define HL_SENDING 1
#define HL_ME_GETSAMPLE1 2
#define HL_ME_GETSAMPLE2 3
#define HL_ME_GETSAMPLE3 4
#define HL_STATUS_CHECK 5
#define HL_ME_STATUS 6
#define TOK_MAX 6
#define CHAN_MAX 3
#define TRIG_MODE_NRT 0
#define TRIG_MODE_RT 1
#define TOK_TYPE_INT 0
#define TOK_TYPE_FLOAT 1
#define ENV_ENA_PIN PF9
#define TIMER_SAMP_CHAN_NONE 0
#define TIMER_MASK_CHAN0 0x01
#define TIMER_MASK_CHAN1 0x02
#define TIMER_MASK_CHAN2 0x04


#ifndef USB_PRINT
#ifdef MBED
#define USB_PRINT usb_serial.printf
#else
#define USB_PRINT printf
#endif
#endif



#define SYS_IDLE 0
#define SYS_INIT 1


// typedef
typedef struct casio_cmd_s {
    char direction;
    char type;
    char form;
    uint16_t line;
    uint32_t offset;
    uint16_t psize;
    char area;
    char csum;
    uint16_t datapacksize; // packet size including start and checksum
    char direction2;
    char type2;
    char form2;
    char csum2;
    char command;
} casio_cmd_t;

typedef struct cmd_tok_s {
    int tokint;
    double tokfloat;
    char toktype;
} cmd_tok_t;

typedef struct chan_setup_s {
    char operation;
} chan_setup_t;

typedef struct samp_trig_setup_s {
    uint32_t period_usec;
    unsigned int numsamp;
    char mode;
} samp_trig_setup_t;

// globals
#ifdef MBED
Serial usb_serial(USBTX, USBRX);
Serial casio_serial(PC11, PC10, NULL, 38400);
Si1133* light_sensor;
#endif
casio_cmd_t casio_cmd;
char procedure=PROC_NULL;
chan_setup_t chan_setup[CHAN_MAX];
samp_trig_setup_t samp_trig_setup;
#ifdef MBED
DigitalOut env_en(ENV_ENA_PIN, 1);
LowPowerTicker      blinker;
DigitalOut          LED(LED_PIN);
#else
extern QueueHandle_t iotq;
extern xQueueHandle timer_queue;
char iot_connection_ok=0; // this gets set to 1 when the IoT connection is successful
#endif


bool                blinking = false;

#ifdef MBED
event_callback_t    serialEventCb;
event_callback_t    casioEventCb;
#endif

uint8_t             rx_buf[BUFF_LENGTH + 1];
uint8_t             casio_rx_buf[COMM_BUFF_LENGTH + 1];
uint8_t             casio_tx_buf[1024];
char comm_state = COMM_IDLE;
char sys_state = SYS_IDLE;
char hl_state = HL_IDLE;
uint16_t sampnum = 0;
int8_t sample_method = TIMER_SAMP_CHAN_NONE;

double value=-10.0;

// functions

void
init_miniexp(void)
{
    int i;
    for (i=0; i<CHAN_MAX; i++) {
        chan_setup[i].operation = 0; // clear all channels
    }
}

char
count_active_chan(void) {
    char tot=0;
    int i;
    for (i=0; i<CHAN_MAX; i++) {
        if (chan_setup[i].operation != 0) {
            tot++;
            sample_method |= (0x01<<i);
        } else {
            sample_method &= ~(0x01<<i);
        }
    }
    return(tot);
}

double
get_sample(int chan)
{
    double sampval=0.0;
#ifdef MBED
    float light, uv;
    light_sensor->get_light_and_uv(&light, &uv);
    light=light/1000.0;
    sampval=(double)light;
#else
    // for ESP32, channel numbering:
    // chan 0 (Casio CHAN1) is ESP32 ADC1_CHANNEL_6 (IO34)
    // chan 1 (Casio CHAN2) is ESP32 ADC1_CHANNEL_7 (IO35)
    // chan 1 (Casio CHAN3) is ESP32 ADC1_CHANNEL_5 (IO33)
    switch(chan) {
        case 0:
            sampval = (double)adc1_get_raw(ADC1_CHANNEL_6);
            break;
        case 1:
            sampval = (double)adc1_get_raw(ADC1_CHANNEL_7);
            break;
        case 2:
            sampval = (double)adc1_get_raw(ADC1_CHANNEL_5);
            break;
        default:
            printf("ERROR - unexpected channel in get_sample!\r\n");
            break;
    }
    sampval=sampval/1241.0;
#endif
    return(sampval);
}


uint16_t rescale(double v) {
    double value;
    double scaled;
    uint16_t scaled_u16;
    value=v;
    if (value>10.0) value=10.0;
    if (value<-10.0) value = -10.0;
    // convert value to a 12-bit number
    scaled=10.92+value;
    scaled=scaled*4096;
    scaled=scaled/21.555;
    scaled_u16=(uint16_t)scaled;
    scaled_u16=scaled_u16&0x0fff;
    return(scaled_u16);
}


// com_seek_char searches the casio comms rx buffer for a particular
// character v. Returns the position of v. Returns -1 if it was not found.
int
comm_seek_char(char v)
{
    int i;
    char found=0;
    for (i=0; i<COMM_BUFF_LENGTH; i++) {
        if(casio_rx_buf[i] == v){
            found=1;
            break;
        }
    }
    if (found)
        return (i);
    else
        return(-1);
}

void
casio_send_response(char r)
{
#ifdef MBED
    casio_serial.write((const uint8_t *)&r, 1, NULL);
#else
    uart_write_bytes(CASIO_UART_NUM, (const char*) &r, 1);
#endif
}

void
casio_send_buf(uint8_t* buf, uint16_t len)
{
#ifdef MBED
    casio_serial.write(buf, len, NULL);
#else
    uart_write_bytes(CASIO_UART_NUM, (const char*) buf, len);
#endif
}

// hex_print: used to print the entire passed buffer of length len
// if brief is set to 1 then it will only print the hex values
// otherwise it will also print the ascii text for the buffer too.
// Output example: 3a,31,32,2c,31,40  text: ':12,1@'
// where 0x3a is the start byte, and 0x40 is the checksum
// 
void
hex_print(uint8_t* buf, char len, char brief=0)
{
    int i;
    char printable=0;
    if (len<1) return;
    USB_PRINT("%02x", buf[0]);
    for (i=1; i<len; i++) {
        USB_PRINT(",%02x", buf[i]);
    }
    
    if (brief) return;
    
    USB_PRINT("  text: '");
    for (i=0; i<len; i++) {
        printable=1;
        if ((buf[i]<32) || (buf[i]>126))
            printable=0;
        if (printable)
            USB_PRINT("%c", buf[i]);
        else
            USB_PRINT(".");
    }
}

void
asc_print(uint8_t* buf, char len)
{
    int i;
    char printable=0;
    if (len<1) return;

    for (i=0; i<len; i++) {
        printable=1;
        if ((buf[i]<32) || (buf[i]>126))
            printable=0;
        if (printable)
            USB_PRINT("%c", buf[i]);
        else
            USB_PRINT(".");
    }
}

void get_tokens(uint8_t* buf, uint16_t len, cmd_tok_t* tok_arr, char* total)
{
    char* token;
    const char s[2]=",";
    char tot=0;
    buf[len]='\0'; // bit naughty, writing outside the length specified. We don't need the checksum so it will get overwritten
    
    token=strtok((char*)buf, s);
    
    while(token != NULL) {
        sscanf(token, "%d", &(tok_arr[(unsigned char)tot].tokint));
        tok_arr[(unsigned char)tot].toktype=TOK_TYPE_INT;
        if (strstr(token, ".")!=NULL) {
            sscanf(token, "%lf", &(tok_arr[(unsigned char)tot].tokfloat));
            tok_arr[(unsigned char)tot].toktype=TOK_TYPE_FLOAT;
        }
        tot++;
        if (tot>=TOK_MAX) {
            if(DEVELOPER) USB_PRINT("Reached TOK_MAX number of allowed tokens\r\n");
            break;
        }
        token = strtok(NULL, s); // get next token
    }
    if(VERBOSE) USB_PRINT("num tokens found: %d\r\n", tot);
    *total=tot;
}

void
instruction_print(char do_pingpong=0)
{
    char dirprint=0;
    char typeprint=0;
    char formprint=0;
    if (casio_cmd.direction2!=0) {
        dirprint = DIR_CASIO_RECV;
    } else {
        dirprint = DIR_CASIO_SEND;
    }
    if(!do_pingpong)
        USB_PRINT("instruction: {\r\n");
    switch(dirprint) {
        case DIR_CASIO_SEND:
            if (do_pingpong)
                USB_PRINT("  |---N");
            else
                USB_PRINT("  direction : send,\r\n");
            break;
        case DIR_CASIO_RECV:
            if (do_pingpong)
                USB_PRINT("  |---------------R");
            else
                USB_PRINT("  direction : recv,\r\n");
            break;
        default:
            if (do_pingpong)
                USB_PRINT("  |?---X");
            else
                USB_PRINT("  direction : unknown '%c',\r\n", casio_cmd.direction);
            break;
    }
    if (dirprint==DIR_CASIO_RECV)
        typeprint=casio_cmd.type2;
    else
        typeprint=casio_cmd.type;
    switch(typeprint) {
        case 'A':
            if (do_pingpong)
                USB_PRINT("A");
            else
                USB_PRINT("  type : ascii,\r\n");
            break;
        case 'H':
            if (do_pingpong)
                USB_PRINT("H");
            else
                USB_PRINT("  type : hex,\r\n");
            break;
        default:
            if (do_pingpong)
                USB_PRINT("X");
            else
                USB_PRINT("  type : unknown '%c',\r\n", typeprint);
            break;
    }
    if (dirprint==DIR_CASIO_RECV)
        formprint=casio_cmd.form2;
    else
        formprint=casio_cmd.form;
    switch(formprint) {
        case 'V':
            if (do_pingpong)
                USB_PRINT("V");
            else
                USB_PRINT("  form : variable,\r\n");
            break;
        case 'L':
            if (do_pingpong)
                USB_PRINT("L");
            else
                USB_PRINT("  form : list,\r\n");
            break;
        default:
            if (do_pingpong)
                USB_PRINT("X");
            else
                USB_PRINT("  form : unknown '%c',\r\n", formprint);
            break;
    }
    if (dirprint==DIR_CASIO_SEND) {
        // these fields only make sense for send from casio
        if (do_pingpong) {
#ifdef MBED
            USB_PRINT(",L=%u,O=%lu,P=%u,", casio_cmd.line, casio_cmd.offset, casio_cmd.psize);
#else
            USB_PRINT(",L=%u,O=%u,P=%u,", casio_cmd.line, casio_cmd.offset, casio_cmd.psize);
#endif
        } else {
            USB_PRINT("  line : %u,\r\n", casio_cmd.line);
#ifdef MBED
            USB_PRINT("  offset : %lu,\r\n", casio_cmd.offset);
#else
            USB_PRINT("  offset : %u,\r\n", casio_cmd.offset);
#endif
            USB_PRINT("  packet_size : %u,\r\n", casio_cmd.psize);
        }
        if (do_pingpong) {
            // tidy the length for pingpong properly later
            if (casio_cmd.psize>9)
                USB_PRINT("%c---------->|\r\n", casio_cmd.area);
            else
                USB_PRINT("%c----------->|\r\n", casio_cmd.area);
        } else {
            switch(casio_cmd.area) {
                case 'A':
                    USB_PRINT("  area : all,\r\n");
                    break;
                case 'S':
                    USB_PRINT("  area : start,\r\n");
                    break;
                case 'M':
                    USB_PRINT("  area : middle,\r\n");
                    break;
                case 'E':
                    USB_PRINT("  area : end,\r\n");
                    break;
                default:
                    USB_PRINT("  area : unknown '%c',\r\n", casio_cmd.form);
                    break;
            }
        }
    } else {
        // DIR_CASIO_RECV
        if (do_pingpong) USB_PRINT("------------->|\r\n");
    }
    if (!do_pingpong) {
        USB_PRINT("  checksum : 0x%02x\r\n", casio_cmd.csum);
        USB_PRINT("}\r\n");
    }
}

void
print_hlpp_r38(uint8_t* buf, uint16_t len, char f)
{
    int8_t plen;
    int i;
    char padding[24];
    
    USB_PRINT("  |<--R38K: ");
    if (f=='A') { // ASCII
        if (len<20) {
            plen=(int8_t)len;
            asc_print(buf, plen);
        } else {
            plen=20;
            asc_print(buf, plen-3);
            USB_PRINT("etc");
        }     
        for (i=0; i<(20-plen); i++) {
            padding[i]='-';
        }
        padding[20-plen]='\0';
        USB_PRINT(padding);
        USB_PRINT("---|\r\n");
    } else if (f=='H') { // Hex
        USB_PRINT("0x");
        if (len<6) {
            plen=(int8_t)len;
            hex_print(buf, plen, BRIEF);
        } else {
            plen=6;
            hex_print(buf, plen-1, BRIEF);
            USB_PRINT("..");
        }     
        for (i=0; i<(17-plen); i++) {
            padding[i]='-';
        }
        padding[17-plen]='\0';
        USB_PRINT(padding);
        USB_PRINT("-|\r\n");    
    } else {
        // unknown format
        USB_PRINT("unknown format!--------|\r\n");
    }
}

// convert a floating point value into ascii text 
// this function always returns 6 characters such as "1.2345" but no end of string!!
void float2ascii(double v, uint8_t* buf)
{
    int idx=0;
    int i;
    int l;
    char found=0;
    uint8_t x;
    uint8_t tbuf[10];
    snprintf((char*)tbuf, 9, "%lf", v);
    tbuf[6]='\0';
    //printf("input is  '%s'\n", tbuf);
    for (i=0; i<10;i++) {
        found=0;
        x=tbuf[i];
        if (x=='-') {
            found=1;
        } else if (x=='.') {
            found=1;
        } else if ((x>='0') && (x<='9')) {
            found=1;
        } else if (x=='\0') {
            found=2;
        }
        switch(found) {
            case 0:
                // perhaps space?
                break;
            case 1: // found valid char
                buf[idx]=x;
                idx++;
                if (idx>=6) {
                    //buf[6]='\0';
                    i=10;
                }
                break;
            case 2: // found end of string
                l=strlen((char*)buf);
                if (l<6) {
                    // zero pad from the beginning
                    for (i=5; i>=0; i--) {
                        l--;
                        if (l>=0) {
                            if(buf[i-(5-l)]=='-') {
                                found=3;
                                buf[i]='0';
                            } else {
                                buf[i]=buf[i-(5-l)];
                            }
                        } else {
                            buf[i]='0';
                        }
                    }
                } else {
                    //buf[6]='\0';
                }
                if (found==3) {
                    buf[0]='-';
                }
                //buf[6]='\0';
                break;
            default:
                // should not occur
                break;
        }

    }
    //printf("output is '%s'\n", buf);
}

void clear_buf(uint8_t* buf, char len)
{
    int i;
    for (i=0; i<len; i++) {
        buf[i]=0;
    }
}

int8_t
calc_checksum(uint8_t* buf, int len, char* calc_result)
{
    int i;
    char tot=0;
    if(len<3) {
        printf("error, buffer too small for calculating checksum!\r\n");
        return(-1);
    }
    // loop through buffer except the start and end bytes
    for (i=1; i<(len-1); i++) {
        tot=tot+buf[i];
    }
    *calc_result = (0xff - tot)+1;
    return(0);
}


int8_t
decode_instruction(uint8_t* buf)
{
    char csum;
    // sanity check
    if (buf[0]!=':') {
        USB_PRINT("error, start_header is not ':'!\r\n");
        return(START_HEADER_ERROR);
    }
    if ((buf[1]!='N') && (buf[1]!='R')) {
        USB_PRINT("error, direction is not 'N' or 'R'!\r\n");
        return(-1);
    }
    calc_checksum(buf, 15, &csum);
    if (csum!=buf[14]) {
        USB_PRINT("error, I compute checksum should be %02x\r\n", csum);
        return(-1);
    }
    
    if (buf[1]==DIR_CASIO_SEND) {
        casio_cmd.direction=buf[1];
        casio_cmd.type=buf[2];
        casio_cmd.form=buf[3];
        casio_cmd.line=(uint16_t)buf[4];
        casio_cmd.line=(casio_cmd.line<<8) | ((uint16_t)buf[5]);
        casio_cmd.offset=((uint32_t)buf[6])<<24;
        casio_cmd.offset=casio_cmd.offset | (((uint32_t)buf[7])<<16);
        casio_cmd.offset=casio_cmd.offset | (((uint32_t)buf[8])<<8);
        casio_cmd.offset=casio_cmd.offset | ((uint32_t)buf[9]);
        casio_cmd.psize=(uint16_t)buf[10];
        casio_cmd.psize=(casio_cmd.psize<<8) | ((uint16_t)buf[11]);
        casio_cmd.area=buf[13];
        casio_cmd.csum=buf[14];
        casio_cmd.direction2=0;
    } else { // DIR_CASIO_RECV
        casio_cmd.direction2=buf[1];
        casio_cmd.type2=buf[2];
        casio_cmd.form2=buf[3];
        casio_cmd.csum2=buf[14];
    }
    
    if(VERBOSE) USB_PRINT("instruction decoded\r\n");
    return(0);
}


// callbacks
void blink(void) {
#ifdef MBED
    LED = !LED;
#endif
}


void casio_uart_processor(int events) {
    int8_t res;
    int txbytes_total=0;
    uint16_t scaled_u16;
    int8_t plen;
    int i;
    char padding[24];
    char doerror=0;
    char numtok=0;
    //int16_t tok_arr[TOK_MAX];
    cmd_tok_t tok_arr[TOK_MAX];
    double sample;
    char iot_text[64]={0};
    
    
    
    switch(comm_state) {
        case COMM_IDLE:
            procedure=PROC_NULL;
            if (comm_seek_char(CASIO_START_INDICATOR) >= 0) {
                // casio has sent a start indicator. We should send CODEA_OK
                // but first prepare for the 15 byte response
                comm_state=COMM_WAITING_INSTRUCTION;
                clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                if(PINGPONG) USB_PRINT("  |                                |\r\n");
                if(PINGPONG) USB_PRINT("  |                          **COMM_IDLE**\r\n");
                if(PINGPONG) USB_PRINT("  |------0x15-CASIO-START-IND----->|\r\n");     
                if(PINGPONG) USB_PRINT("  |<-----------CODEA_OK------------|\r\n");
                if(DEVELOPER) USB_PRINT("wait instruct:\r\n");
#ifdef MBED
                casio_serial.read(casio_rx_buf, 15, casio_uart_processor);
#endif
                // send CODEA_OK
                casio_send_response(CODEA_OK);
            } else {
                // we didn't receive a start indication from casio.
                if(DEVELOPER) USB_PRINT("received junk, ignoring:\r\n");
                if(DEVELOPER) { hex_print(casio_rx_buf, 15); USB_PRINT("'\r\n"); }
                if(DEVELOPER) USB_PRINT("waiting start indicator\r\n");
                if(PINGPONG) {
                    USB_PRINT("  |                                |\r\n");
                    USB_PRINT("  |                          **COMM_IDLE**\r\n");
                    USB_PRINT("  |-JUNK-");
                    hex_print(casio_rx_buf, 3);
                    USB_PRINT("etc-->|\r\n");
                }
                clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
#ifdef MBED
                casio_serial.read(casio_rx_buf, 15, casio_uart_processor, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
#endif
            }
            break;
        case COMM_WAITING_INSTRUCTION:
            if (DEVELOPER) USB_PRINT("recvd instruct:\r\n");
            if (DEVELOPER) { hex_print(casio_rx_buf, 15); USB_PRINT("'\r\n"); }
            if(PINGPONG) USB_PRINT("  |                    COMM_WAITING_INSTRUCTION\r\n");
            res=decode_instruction(casio_rx_buf);
            if (res==START_HEADER_ERROR) {
                if(DEVELOPER) USB_PRINT("revert to waiting for start indicator\r\n");
                if(PINGPONG) USB_PRINT("  |------[START HEADER ERROR]----->|\r\n");
                comm_state=COMM_IDLE;
                clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
#ifdef MBED
                casio_serial.read(casio_rx_buf, 15, casio_uart_processor, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
#endif
            } else {
                if (casio_rx_buf[1]==DIR_CASIO_SEND)
                    procedure=PROC_SEND38K;
                else
                    procedure=PROC_RECV38K;
                if (VERBOSE) instruction_print();
                if(PINGPONG) instruction_print(1);
                if (casio_rx_buf[1]==DIR_CASIO_SEND) {
                    // casio will now send data, when we issue CODEB_OK
                    if(PINGPONG) USB_PRINT("  |<-----------CODEB_OK------------|\r\n");
                    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                    if (casio_cmd.type==TYPE_ASCII) {
                        casio_cmd.datapacksize=casio_cmd.psize+2; 
                    } else {
                        USB_PRINT("error, cannot handle type '%c'\r\n", casio_cmd.type);
                        casio_cmd.datapacksize=casio_cmd.psize+2; 
                    }
                    if (casio_cmd.datapacksize>COMM_BUFF_LENGTH) {
                        USB_PRINT("error, length %u is larger than buffer size!\r\n", casio_cmd.datapacksize);
                        casio_cmd.datapacksize=COMM_BUFF_LENGTH;
                    }
#ifdef MBED
                    casio_serial.read(casio_rx_buf, casio_cmd.datapacksize, casio_uart_processor);
#endif
                    comm_state = COMM_WAITING_DATA;
                    casio_send_response(CODEB_OK);
                } else { // DIR_CASIO_RECV
                    // we are now expected to receive CODEB after sending a header
#ifdef MBED
                    casio_serial.read(casio_rx_buf, 1, casio_uart_processor, SERIAL_EVENT_RX_ALL, CODEB_OK);
#endif
                    comm_state=COMM_WAITING_RX_HEADER_ACK;
                    switch(hl_state) {
                        case HL_ME_STATUS:
                            if (VERBOSE) USB_PRINT("building header for MiniExp status response\r\n");
                            casio_tx_buf[0]=':';
                            casio_tx_buf[1]='N';
                            casio_tx_buf[2]='A';
                            casio_tx_buf[3]='V';
                            casio_tx_buf[4]=0;
                            casio_tx_buf[5]=1;
                            casio_tx_buf[6]=0;
                            casio_tx_buf[7]=0;
                            casio_tx_buf[8]=0;
                            casio_tx_buf[9]=1;
                            casio_tx_buf[10]=0;
                            casio_tx_buf[11]=1; // Lets use 1 character for the status
                            casio_tx_buf[12]=0xff;
                            casio_tx_buf[13]='A';
                            calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
                            if(DEVELOPER) USB_PRINT("sending MiniExp status header, waiting for CODEB_OK\r\n");
                            casio_send_buf(casio_tx_buf, 15);
                            break;
                        case HL_ME_GETSAMPLE1:
                        case HL_ME_GETSAMPLE2:
                        case HL_ME_GETSAMPLE3:
                            if (VERBOSE) USB_PRINT("building header for value response\r\n");
                            casio_tx_buf[0]=':';
                            casio_tx_buf[1]='N';
                            casio_tx_buf[2]='A';
                            casio_tx_buf[3]='V';
                            casio_tx_buf[4]=0;
                            casio_tx_buf[5]=1;
                            casio_tx_buf[6]=0;
                            casio_tx_buf[7]=0;
                            casio_tx_buf[8]=0;
                            casio_tx_buf[9]=1;
                            casio_tx_buf[10]=0;
                            casio_tx_buf[11]=6; // hard-coded for now, always 6 characters.
                            casio_tx_buf[12]=0xff;
                            casio_tx_buf[13]='A';
                            calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
                            if(DEVELOPER) USB_PRINT("sending value header, waiting for CODEB_OK\r\n");
                            casio_send_buf(casio_tx_buf, 15);
                            break;
                        default:
                            if (casio_cmd.form2=='L') { // think we can now send header for voltage packets?
                                // a list is being requested. It could be a measurement request, or a status check request list
                                switch(hl_state) {
                                    case HL_STATUS_CHECK: // prepare to send header for status check "1,0,999,1" (9 bytes) to indicate status OK and channel 1 active
                                        if (VERBOSE) USB_PRINT("building header for list response for status request\r\n");
                                        casio_tx_buf[0]=':';
                                        casio_tx_buf[1]='N';
                                        casio_tx_buf[2]='A';
                                        casio_tx_buf[3]='L';
                                        casio_tx_buf[4]=0;
                                        casio_tx_buf[5]=1;
                                        casio_tx_buf[6]=0;
                                        casio_tx_buf[7]=0;
                                        casio_tx_buf[8]=0;
                                        casio_tx_buf[9]=1;
                                        casio_tx_buf[10]=0;
                                        casio_tx_buf[11]=9; // 9 bytes hard coded for now
                                        if(PINGPONG) USB_PRINT("  |<---NAL,L=1,O=1,P=9,A-----------|\r\n");
                                        
                                        casio_tx_buf[12]=0xff;
                                        casio_tx_buf[13]='A';
                                        break;
                                    default: // send header for voltage packets
                                        casio_cmd.command=99; // magic code for now for voltage measurement request
                                        if (VERBOSE) USB_PRINT("building header for list response for measurement\r\n");
                                        casio_tx_buf[0]=':';
                                        casio_tx_buf[1]='N';
                                        if (casio_cmd.type2=='A') {
                                            casio_tx_buf[2]='A';
                                        } else {
                                            casio_tx_buf[2]='H'; //H
                                        }
                                        casio_tx_buf[3]='L';
                                        casio_tx_buf[4]=0;
                                        char totchan=count_active_chan();
                                        if (casio_cmd.type2=='A') {
                                            casio_tx_buf[5]=totchan; // Line field seems to be number of values in the list
                                        }
                                        casio_tx_buf[6]=0;
                                        casio_tx_buf[7]=0;
                                        casio_tx_buf[8]=0;
                                        casio_tx_buf[9]=1;
                                        casio_tx_buf[10]=0;
                                        if (casio_cmd.type2=='A') {
                                            //char totchan=count_active_chan();
                                            char asc_len=totchan*6; // 6 characters per ascii voltage value for now.
                                            asc_len=asc_len+(totchan-1); // add 1 byte for each comma between voltage values
                                            casio_tx_buf[11]=asc_len;
                                            if(DEVELOPER) USB_PRINT("asc_len set to %d\r\n", asc_len);
                                            if(PINGPONG) USB_PRINT("  |<---NAL,L=1,O=1,P=N,A-----------|\r\n");
                                        } else if (samp_trig_setup.mode==TRIG_MODE_NRT) { // non-real-time chunk, used for faster sampling. Doesn't work.
                                            unsigned int nn;
                                            nn = samp_trig_setup.numsamp;
                                            if (nn>512) { // don't know yet how to extend beyond one packet : (
                                                if(DEVELOPER) USB_PRINT("samples greater than 512 may not currently work..\r\n");
                                            } 
                                            casio_tx_buf[4] = (nn>>8) & 0x00ff;  // Line
                                            casio_tx_buf[5] = nn & 0x00ff;       // Line
                                            if(DEVELOPER) USB_PRINT("buf[4]=0x%02x, buf[5]=0x%02x\r\n", casio_tx_buf[4], casio_tx_buf[5]);
                                            //casio_tx_buf[5]=100; // 100 lines?
                                            nn=nn*2; // 2 bytes per sample
                                            casio_tx_buf[10] = (nn>>8) & 0x00ff;   // Packet size
                                            casio_tx_buf[11] = nn & 0x00ff;        // Packet size
                                            if(DEVELOPER) USB_PRINT("buf[10]=0x%02x, buf[11]=0x%02x\r\n", casio_tx_buf[10], casio_tx_buf[11]);
                                            if (sampnum>=512) { // this doesn't work currently
                                                nn=samp_trig_setup.numsamp;
                                                //nn=nn>>9; // divide by 512
                                                casio_tx_buf[8]=(nn>>8) & 0x00ff; // Offset
                                                casio_tx_buf[9]=nn & 0x00ff;      // Offset
                                            }
                                            if(PINGPONG) USB_PRINT("  |<---NHL,L=NN,O=1,P=NN,A---------|\r\n");
                                        } else { // real-time mode (slower sampling, data sent one sample at a time)
                                            char totchan=count_active_chan();
                                            casio_tx_buf[11]=2*totchan; // 2 bytes for hex value
                                            if(PINGPONG) USB_PRINT("  |<---NHL,L=1,O=1,P=2,A-----------|\r\n");
                                        }
                                        casio_tx_buf[12]=0xff;
                                        if (sampnum==0) {
                                            casio_tx_buf[13]='A';// A   // anything else seems to generate an error : (
                                        } else if (sampnum>=512) { // don't know what to do here
                                            casio_tx_buf[13]='M';//E
                                        } else {
                                            casio_tx_buf[13]='M';//M
                                        }
                                        break;
                                }

                                calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
                                if(DEVELOPER) USB_PRINT("sending list header\r\n");
                                casio_send_buf(casio_tx_buf, 15);
                            }
                            
                            if ((casio_cmd.command=='7') && (casio_cmd.form2!='L')) {
                                if(DEVELOPER) USB_PRINT("building header for status check (command 7) response\r\n");
                                if (casio_cmd.line==1) {
                                    if(DEVELOPER) USB_PRINT("building header for line 1\r\n");
                                    casio_tx_buf[0]=':';
                                    casio_tx_buf[1]='N';
                                    casio_tx_buf[2]='A';
                                    casio_tx_buf[3]='L';
                                    casio_tx_buf[4]=0;
                                    casio_tx_buf[5]=1;
                                    casio_tx_buf[6]=0;
                                    casio_tx_buf[7]=0;
                                    casio_tx_buf[8]=0;
                                    casio_tx_buf[9]=1;
                                    casio_tx_buf[10]=0;
                                    casio_tx_buf[11]=1;
                                    casio_tx_buf[12]=0xff;
                                    casio_tx_buf[13]='A';
                                    calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
                                    if(DEVELOPER) USB_PRINT("sending header, waiting for CODEB_OK\r\n");
                                    if(PINGPONG) USB_PRINT("  |<---NAL,L=1,O=1,P=1,A-----------|\r\n");
                                    casio_send_buf(casio_tx_buf, 15);
                                }
                            }

                            break;
                    }
                    

                    
                    switch(sys_state) {
                        case SYS_IDLE:
                            sys_state = SYS_INIT;
                            break;
                        default:
                            if(VERBOSE) USB_PRINT("unrecognized sys_state %d\r\n", sys_state);
                            break;
                    }   
                    
                }
                
            }
            break;
        case COMM_WAITING_DATA:
            if(DEVELOPER) USB_PRINT("recvd data pak:\r\n");
            if(DEVELOPER) { hex_print(casio_rx_buf, casio_cmd.datapacksize); USB_PRINT("'\r\n"); }
            if(PINGPONG) USB_PRINT("  |                       COMM_WAITING_DATA\r\n");
            if(HLPP) {
                if (procedure==PROC_SEND38K) {
                    USB_PRINT("  |---S38K: ");
                    if (casio_cmd.type==TYPE_ASCII) {
                        plen=(int8_t)casio_cmd.datapacksize-2;
                        if(plen<20) {
                            asc_print(casio_rx_buf+1, plen);
                        } else {
                            plen=20;
                            asc_print(casio_rx_buf+1, plen-3);
                            USB_PRINT("etc");
                        }
                        for (i=0; i<(20-plen); i++) {
                            padding[i]='-';
                        }
                        padding[20-plen]='\0';
                        USB_PRINT(padding);
                        USB_PRINT("-->|\r\n");
                    } else if (casio_cmd.type==TYPE_HEX) {
                        // not fully tested for now
                        plen=(int8_t)casio_cmd.datapacksize-2;
                        if(plen<6) {
                            hex_print(casio_rx_buf+1, plen);
                        } else {
                            plen=5;
                            hex_print(casio_rx_buf+1, plen);
                            USB_PRINT("..");
                        }
                        for (i=0; i<(20-plen); i++) {
                            padding[i]='-';
                        }
                        padding[20-plen]='\0';
                        USB_PRINT(padding);
                        USB_PRINT("-->|\r\n");
                    } else {
                        // unexpected format
                        USB_PRINT("unknown format!------->|\r\n");
                    }
                } else {
                    // we don't expect any other procedure if we're in state COM_WAITING_DATA
                }
            }
            // is the first byte ':'? If not, then reject with a CODEB_ERROR for now
            if (casio_rx_buf[0]!=':') {
                if(DEVELOPER) USB_PRINT("error, invalid data, send CODEB_ERROR\r\n");
                doerror=1;
            }
            
            get_tokens(&casio_rx_buf[1], casio_cmd.datapacksize-2, tok_arr, &numtok);
            // handle the commands!
            switch(tok_arr[0].tokint) {
                case 2001: // MiniExperimenter command
                    if (numtok>=3) {
                        switch (tok_arr[1].tokint)
                        {
                            case 0: // provide a status
                                hl_state=HL_ME_STATUS;
                                if(DEVELOPER) USB_PRINT("will send MiniExp status to casio on next Receive38K\r\n");
                                break;
                            case 1: // get sample
                            case 2:
                            case 3:
                                // on Receive38K, send the calculator a sample
                                hl_state=HL_ME_GETSAMPLE1 + tok_arr[1].tokint - 1;
                                if(DEVELOPER) USB_PRINT("will send sample to casio on next Receive38K\r\n");
                                break;
                            case 21: // send something to cloud
                            case 22:
                            case 23:
                                int pos;
                                // now we need to build a message in the format: {"chX": 1.2345} where X is 1,2 or 3. The value can be float or int. 
                                // there seems to be a limit of 32 bytes for the IoT message somewhere. 
                                if (tok_arr[2].toktype==TOK_TYPE_FLOAT) {
                                    pos = snprintf(iot_text, sizeof(iot_text) - 1, "{\"ch%d\": %lf}", tok_arr[1].tokint-20, tok_arr[2].tokfloat);
                                } else {
                                    pos = snprintf(iot_text, sizeof(iot_text) - 1, "{\"ch%d\": %d}", tok_arr[1].tokint-20, tok_arr[2].tokint);
                                }
                                iot_text[pos] = 0; 
#ifdef MBED
                                // not supported currently
#else
#ifdef WITH_IOT
                                if(DEVELOPER) USB_PRINT("adding to IOT queue\r\n");
                                xQueueSend(iotq, iot_text, 50 / portTICK_PERIOD_MS);
#endif
#endif
                            default:
                                break;
                        }
                    }
                    break;
                
                case 0: // don't know what this is. Lets use it to clear the channel list.
                    if(DEVELOPER)USB_PRINT("received command 0\r\n");
                    for (i=0; i<CHAN_MAX; i++) {
                        chan_setup[i].operation=0;
                    }
                    sampnum=0;
                    break;
                case 1: // channel setup command
                    if(DEVELOPER)USB_PRINT("received channel setup data\r\n");
                    if(PINGPONG) USB_PRINT("  |--------1-CHAN_SETUP----------->|\r\n");
                    if (numtok>=3) {
                        samp_trig_setup.mode=TRIG_MODE_NRT; // we reset to this as a default if no command 12 arrives later
                        if (tok_arr[1].tokint<=CHAN_MAX) {
                            chan_setup[(tok_arr[1].tokint)-1].operation = tok_arr[2].tokint;
                            if(DEVELOPER)USB_PRINT("CH%d set to type %d\r\n", tok_arr[1].tokint, tok_arr[2].tokint);
                            if (tok_arr[2].tokint!=2) {
                                if(DEVELOPER)USB_PRINT("error, unsupported chan type\r\n");
                            }
                        }
                    } else {
                        // wrong amount of tokens!
                    }
                    break;
                case 3: // sample rate and num samples
                    if(DEVELOPER)USB_PRINT("received sampling rate\r\n");
                    if(PINGPONG) USB_PRINT("  |--------3-SAMPLERATE----------->|\r\n");
                    if (numtok>=3) {
                        if (tok_arr[1].toktype==TOK_TYPE_INT) {
                            samp_trig_setup.period_usec=((uint32_t)(tok_arr[1].tokint))*1E6;
                        } else {
                            samp_trig_setup.period_usec=(uint32_t)(tok_arr[1].tokfloat*1000000.0);
                        }
                        if(DEVELOPER)USB_PRINT("sample rate: %u usec\r\n", samp_trig_setup.period_usec);
                        if (tok_arr[2].tokint==-1) {
                            samp_trig_setup.mode=TRIG_MODE_RT;
                            samp_trig_setup.numsamp=0; // sampled with each data request
                            if(DEVELOPER)USB_PRINT("num samples: per data request\r\n");
                        } else {
                            samp_trig_setup.numsamp=(unsigned int)tok_arr[2].tokint;
                            if(DEVELOPER)USB_PRINT("num samples: %u\r\n", samp_trig_setup.numsamp);
                        }
                        
                    } else {
                        // wrong amount of tokens!   
                    }
                    break;
                case 7: // status check command 7
                    casio_cmd.command='7';
                    if(DEVELOPER) USB_PRINT("received status check command '7'\r\n");
                    if(PINGPONG) USB_PRINT("  |--------7-STATUS_CHECK--------->|\r\n");
                    hl_state=HL_STATUS_CHECK;
                    break;
                case 8: // trigger command to start sampling
                    // enable the timer (it may be already enabled. But this resets it too)
                    //setup_tg0_timer(TIMER_0, 1, 1000000);
                    count_active_chan(); // this updates sample_method bitmask
                    if(DEVELOPER) USB_PRINT("triggered, sample_method is 0x%02x\r\n", sample_method);
                    if (samp_trig_setup.period_usec>=200000) { // >= 0.2 sec
                        sample_timer_start(samp_trig_setup.period_usec);
                    } else {
                        // todo: figure out bulk (non-real-time) sampling
                        //sample_timer_start(200000);
                    }
                    sampnum=0;
                    hl_state=HL_SENDING;
                    if(DEVELOPER)USB_PRINT("entering state HL_SENDING\r\n");
                    if(PINGPONG) USB_PRINT("  |--------8-TRIGGER-------------->|\r\n");
                    break;
                case 12: // real time mode
                    if (numtok==2) {
                        if (tok_arr[1].tokint==1) {
                            samp_trig_setup.mode=TRIG_MODE_RT; // real-time mode, single result
                            if(DEVELOPER)USB_PRINT("entering realtime mode\r\n");
                            if(PINGPONG) USB_PRINT("  |--------12-REALTIME------------>|\r\n");
                        } else {
                            samp_trig_setup.mode=TRIG_MODE_NRT; // non-real-time, batched. can't get this to work..
                            if(DEVELOPER)USB_PRINT("entering unusable non-realtime mode (batch)\r\n");
                            if(PINGPONG) USB_PRINT("  |--------12-NONREALTIME--------->|\r\n");
                        }
                    } else {
                        // wrong number of tokens! Should not occur.
                    }
                    break;
                default:
                    if(PINGPONG) {
                        USB_PRINT("  |--[PAK ");
                        plen=(int8_t)casio_cmd.datapacksize-2;
                        if(plen<20) {
                            asc_print(casio_rx_buf+1, plen);
                        } else {
                            plen=20;
                            asc_print(casio_rx_buf+1, plen-3);
                            USB_PRINT("etc");
                        }
                        USB_PRINT("]");
                        for (i=0; i<(20-plen); i++) {
                            padding[i]='-';
                        }
                        padding[20-plen]='\0';
                        USB_PRINT(padding);
                        USB_PRINT("--->|\r\n");
                    }
                    break;
            }
            
            // we can now send CODEB_OK and go back to idle state
            if(PINGPONG) {
                if (doerror) {
                    USB_PRINT("  |<---------CODEB_ERROR-----------|\r\n");
                } else {
                    USB_PRINT("  |<-----------CODEB_OK------------|\r\n");
                }
            }
            comm_state=COMM_IDLE;
            clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
#ifdef MBED
            casio_serial.read(casio_rx_buf, 15, casio_uart_processor, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
#endif
            if (doerror) {
                casio_send_response(CODEB_ERROR);
            } else {
                casio_send_response(CODEB_OK);
            }
            break;
        case COMM_WAITING_RX_HEADER_ACK:
            if(PINGPONG) USB_PRINT("  |                  COMM_WAITING_RX_HEADER_ACK\r\n");
            switch(casio_rx_buf[0]) {
                case CODEB_OK:
                    if(DEVELOPER) USB_PRINT("rcvd CODEB_OK\r\n");
                    if(PINGPONG) USB_PRINT("  |------------CODEB_OK----------->|\r\n");
                    // we are now expected to receive a CODEB after sending a packet
#ifdef MBED
                    casio_serial.read(casio_rx_buf, 1, casio_uart_processor, SERIAL_EVENT_RX_ALL, CODEB_OK);
#endif
                    char av;
                    switch (hl_state) {
                        case HL_ME_STATUS:
                            if(DEVELOPER) USB_PRINT("HL_ME_STATUS: sending ME status to Casio\r\n");
                            if(PINGPONG) USB_PRINT("  |<------[MINIEXP STATUS]---------|\r\n");
                            av='0';
                            wifi_ap_record_t apinfo;
                            if (esp_wifi_sta_get_ap_info(&apinfo)==ESP_OK) {
                                av ='1'; // WiFi is connected
                                if (get_year()>=2021) {
                                    av ='2'; // NTP is working
                                    if (iot_connection_ok==1) {
                                        av ='3'; // IoT connection is ok
                                    }
                                }
                            }
                            casio_tx_buf[0]=':';
                            casio_tx_buf[1]=av;
                            txbytes_total=3;
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            hl_state=HL_IDLE;
                            comm_state=COMM_WAITING_RX_PACKET_ACK;
                            casio_send_buf(casio_tx_buf, txbytes_total);
                            break;
                        case HL_ME_GETSAMPLE1:
                        case HL_ME_GETSAMPLE2:
                        case HL_ME_GETSAMPLE3:
                            if(DEVELOPER) USB_PRINT("HL_ME_GETSAMPLE: sending sample to Casio\r\n");
                            if(PINGPONG) USB_PRINT("  |<-----[MEASUREMENT ASCII]-------|\r\n");
                            casio_tx_buf[0]=':';
                            sample=get_sample(hl_state - HL_ME_GETSAMPLE1); // get measurement for channel 0, 1 or 2
                            float2ascii(sample, &casio_tx_buf[1]); // populate 6 bytes with the ASCII representation
                            txbytes_total=6+2; // 6 bytes from float2ascii, and two bytes for ':' and the checksum
                            if (HLPP) print_hlpp_r38(&casio_tx_buf[1], txbytes_total-2, 'A');
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            hl_state=HL_IDLE;
                            comm_state=COMM_WAITING_RX_PACKET_ACK;
                            casio_send_buf(casio_tx_buf, txbytes_total);
                            break;
                        case HL_STATUS_CHECK:
                            if(PINGPONG) USB_PRINT("  |<-------1-STATUS_READY----------|\r\n");
                            if (HLPP) USB_PRINT("  |<--R38K: 1----------------------|\r\n");
                            if (casio_cmd.form2=='L') { // list expected
                                if(DEVELOPER) USB_PRINT("used list for status check (command 7) response\r\n");
                                casio_tx_buf[0]=':';
                                casio_tx_buf[1]='1';
                                casio_tx_buf[2]=',';
                                casio_tx_buf[3]='0';
                                casio_tx_buf[4]=',';
                                casio_tx_buf[5]='9';
                                casio_tx_buf[6]='9';
                                casio_tx_buf[7]='9';
                                casio_tx_buf[8]=',';
                                casio_tx_buf[9]='1';
                                txbytes_total=11; // 9+2
                            } else { //value expected
                                if(DEVELOPER) USB_PRINT("used variable for status check (command 7) response\r\n");
                                casio_tx_buf[0]=':';
                                casio_tx_buf[1]='1';
                                txbytes_total=3; // 1+2
                            }
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            if(DEVELOPER) USB_PRINT("sending packet, waiting for CODEB_OK\r\n");
                            hl_state=HL_IDLE;
                            comm_state=COMM_WAITING_RX_PACKET_ACK;
                            casio_send_buf(casio_tx_buf, txbytes_total);
                            break;
                        default:
                            break;
                    }
                    if (casio_cmd.command=='7') {
                        if(DEVELOPER) USB_PRINT("building packet for status check (command 7) response\r\n");
                        if (casio_cmd.line==1) {
                            if(DEVELOPER) USB_PRINT("building packet for line 1\r\n");
                            if(PINGPONG) USB_PRINT("  |<-------1-STATUS_READY----------|\r\n");
                            if (HLPP) USB_PRINT("  |<--R38K: 1----------------------|\r\n");
                            casio_tx_buf[0]=':';
                            casio_tx_buf[1]='1';
                            txbytes_total=3;
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            if(DEVELOPER) USB_PRINT("sending packet, waiting for CODEB_OK\r\n");
                            comm_state=COMM_WAITING_RX_PACKET_ACK;
                            casio_send_buf(casio_tx_buf, txbytes_total);
                        }
                    }
                    if (casio_cmd.command==99) {
                        if (VERBOSE) USB_PRINT("building packet with voltage value response\r\n");
                        //if (1/*casio_cmd.line==1*/) {
                        if (casio_cmd.type2=='A') {
                            if (DEVELOPER) USB_PRINT("sending meas pak\r\n");
                            if(PINGPONG) USB_PRINT("  |<-----[MEASUREMENT ASCII]-------|\r\n");
                            casio_tx_buf[0]=':';


                            int bytenum=1;
                            txbytes_total=2; // header and checksum
                            for (i=0; i<CHAN_MAX; i++) {
                                if (chan_setup[i].operation!=0) {
                                    if (DEVELOPER) USB_PRINT("chan %d enabled\r\n", i);
                                    if (bytenum!=1) {
                                        // there is more than one channel result! add a comma
                                        casio_tx_buf[bytenum]=',';
                                        bytenum++;
                                        txbytes_total++;
                                    }
                                    sample = get_sample(i);
                                    float2ascii(sample, &casio_tx_buf[bytenum]); // populate 6 bytes with the ASCII representation
                                    txbytes_total=txbytes_total+6;
                                    bytenum=bytenum+6;
                                }
                            }

                            casio_tx_buf[txbytes_total-1]='\0'; // just to print it out. It gets overwritten with checksum later
                            if (DEVELOPER) USB_PRINT("sending values '%s'\r\n", &casio_tx_buf[1]);


                            //sample=get_sample(0);
                            //float2ascii(sample, &casio_tx_buf[1]); // populate 6 bytes with the ASCII representation
                            //casio_tx_buf[1]='3';
                            //casio_tx_buf[2]='.';
                            //casio_tx_buf[3]='6';
                            //txbytes_total=6+2; // 6 bytes from float2ascii, and two bytes for ':' and the checksum
                            if (HLPP) print_hlpp_r38(&casio_tx_buf[1], txbytes_total-2, 'A');
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                        } else if (casio_cmd.type2=='H') { // is this hex format?
                            if (VERBOSE) USB_PRINT("building hex packet for line 1\r\n");
                            if(PINGPONG) USB_PRINT("  |<------[MEASUREMENT HEX]--------|\r\n");
                            casio_tx_buf[0]=':';

                            timer_event_t evt;
                            xQueueReceive(timer_queue, &evt, (samp_trig_setup.period_usec/512)/portTICK_PERIOD_MS /*portMAX_DELAY*/);
                            if (evt.event==0) {
                                //value = evt.meas[0];
                            } else {
                                if(DEVELOPER)USB_PRINT("***TIMER FAIL!***\r\n");
                                //value=get_sample(0);
                            }

                            //scaled_u16=rescale(value);

                            //if (value>10.0) value=10.0;
                            //if (value<-10.0) value = -10.0;
                            // convert value to a 12-bit number
                            //scaled=10.92+value;
                            //scaled=scaled*4096;
                            //scaled=scaled/21.555;
                            //scaled_u16=(uint16_t)scaled;
                            //scaled_u16=scaled_u16&0x0fff;
                            
                            //value=value+0.1;
                            //if (value>10.0) value=-10;
                            
                            int bytenum=1;
                            txbytes_total=2; // header and checksum
                            for (i=0; i<CHAN_MAX; i++) {
                                if (chan_setup[i].operation!=0) {
                                    value = evt.meas[i];
                                    scaled_u16=rescale(value);
                                    casio_tx_buf[bytenum]=(uint8_t)(scaled_u16 & 0x00ff);
                                    bytenum++;
                                    casio_tx_buf[bytenum]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                    bytenum++;
                                    txbytes_total=txbytes_total+2;
                                }
                            }
                            //casio_tx_buf[1]=(uint8_t)(scaled_u16 & 0x00ff);
                            //casio_tx_buf[2]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                            //txbytes_total=4;
                            //if (count_active_chan()>1) {
                            //    casio_tx_buf[3]=(uint8_t)(scaled_u16 & 0x00ff);
                            //    casio_tx_buf[4]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                            //    txbytes_total=6;
                            //}

                            

                            char fin=0;
                            if ((hl_state==HL_SENDING) && (samp_trig_setup.mode==TRIG_MODE_NRT)){
                                if (sampnum==samp_trig_setup.numsamp) {
                                    // we have completed the bulk transfer of data.
                                    fin=1;
                                    casio_tx_buf[0]=':';
                                    casio_tx_buf[1]='E';
                                    casio_tx_buf[2]='N';
                                    casio_tx_buf[3]='D';
                                    unsigned int ns = samp_trig_setup.numsamp;
                                    for (i=3; i<(ns*3); i++) {
                                        casio_tx_buf[i]=0xff;
                                    }
                                    txbytes_total=(ns*3)+2;
                                    if(DEVELOPER)USB_PRINT("Sending :END\r\n");
                                }
                            }

                            sampnum=sampnum+1;
                            
                            // non-real-time mode is used for fast sample rates. Doesn't work : (
                            if ((hl_state==HL_SENDING) && (samp_trig_setup.mode==TRIG_MODE_NRT) && (fin==0)){
                                int padding=0;
                                int mult=2+padding;
                                unsigned int ns = samp_trig_setup.numsamp;
                                for (i=0; i<ns; i++) {
                                    value = get_sample(0);
                                    scaled_u16=rescale(value);
                                    casio_tx_buf[(i*mult)+1]=(uint8_t)(scaled_u16 & 0x00ff);
                                    casio_tx_buf[(i*mult)+2]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                    if(padding>0) {
                                        casio_tx_buf[(i*mult)+3]=',';
                                    }
                                    if(padding>1) {
                                        casio_tx_buf[(i*mult)+4]=0x00;
                                    }
                                    if(padding>2) {
                                        casio_tx_buf[(i*mult)+5]=0x00;
                                    }
                                }
                                txbytes_total=(ns*mult)+2;   //(200*2)+2;
                                sampnum=sampnum+ns-1;
                            }                           
                            
                            if (HLPP) print_hlpp_r38(&casio_tx_buf[1], txbytes_total-2, 'H');
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            //sampnum=sampnum+100;
                        } else {
                            USB_PRINT("error, unrecognizable type '%c'!\r\n", casio_cmd.type2);
                        }
                        //sampnum++;
                        if(DEVELOPER)USB_PRINT("sending sample %u\r\n", sampnum);
                        if(VERBOSE)USB_PRINT("sending voltage sample %u, txbytes_total %d waiting for CODEB_OK\r\n", sampnum, txbytes_total);
                        if ((hl_state==HL_SENDING) && (samp_trig_setup.mode==TRIG_MODE_RT)){
                            if (sampnum>=samp_trig_setup.numsamp) {
                                sample_method=TIMER_SAMP_CHAN_NONE;
                                sample_timer_stop();
                            }
                        }
                        if ((hl_state==HL_SENDING) && (samp_trig_setup.mode==TRIG_MODE_NRT)){
                            if (sampnum>=samp_trig_setup.numsamp) {
                                hl_state=HL_IDLE;
                                sample_method=TIMER_SAMP_CHAN_NONE;
                                //comm_state=COMM_WAITING_RX_PACKET_ACK;
                                comm_state=COMM_WAITING_PERFORM_ROLESWAP;
                                if(DEVELOPER)USB_PRINT("Entering state HL_IDLE\r\n");
                            } else {
                                // we will stay in this state if we are sending multiple data packets
                                if(DEVELOPER)USB_PRINT("then ready to send next data packet\r\n");
                                
                            }
                            
                        } else {
                            comm_state=COMM_WAITING_RX_PACKET_ACK;
                        }
                        casio_send_buf(casio_tx_buf, txbytes_total /*15*/);
                        //}
                    }
                    break;
                case CODEB_RETRY:
                    if(DEVELOPER) USB_PRINT("received CODEB_RETRY\r\n");
                    if(PINGPONG) USB_PRINT("  |----------CODEB_RETRY---------->|\r\n");
                    break;
                case CODEB_ERROR:
                    if(DEVELOPER) USB_PRINT("received CODEB_ERROR\r\n");
                    if(PINGPONG) USB_PRINT("  |----------CODEB_ERROR---------->|\r\n");
                    break;
                case CASIO_START_INDICATOR:
                    // we don't expect this, but it seems to occur occasionally. We should try to recover.
                    // I think this occurs when status check variable request occurs
                    // do whatever would occur if we'd been in state COMM_IDLE.
                    if(DEVELOPER) USB_PRINT("COMM_WAITING_RX_HEADER_ACK received unexpected start indication\r\n");
                    procedure=PROC_NULL;

                    // casio has sent a start indicator. We should send CODEA_OK
                    // but first prepare for the 15 byte response
                    comm_state=COMM_WAITING_INSTRUCTION;
                    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                    if(PINGPONG) USB_PRINT("  |                                |\r\n");
                    if(PINGPONG) USB_PRINT("  |                          **COMM_IDLE**\r\n");
                    if(PINGPONG) USB_PRINT("  |------0x15-CASIO-START-IND----->|\r\n");     
                    if(PINGPONG) USB_PRINT("  |<-----------CODEA_OK------------|\r\n");
                    if(DEVELOPER) USB_PRINT("wait instruct:\r\n");
#ifdef MBED
                    casio_serial.read(casio_rx_buf, 15, casio_uart_processor);
#endif
                    // send CODEA_OK
                    casio_send_response(CODEA_OK);
                    break;
                default:
                    if(DEVELOPER) USB_PRINT("COMM_WAITING_RX_HEADER_ACK received unexpected value '%u', expected CODEB\r\n", casio_rx_buf[0]);
                    if(PINGPONG) USB_PRINT("  |---------CODEB_UNKNOWN!-------->|\r\n");
                    break;
            }
            break;
        case COMM_WAITING_RX_PACKET_ACK:
            if(PINGPONG) USB_PRINT("  |                COMM_WAITING_RX_PACKET_ACK\r\n");
            switch(casio_rx_buf[0]) {
                case CODEB_OK:
                    if(DEVELOPER) USB_PRINT("rcvd CODEB_OK. Fin\r\n");
                    if(PINGPONG) USB_PRINT("  |------------CODEB_OK----------->|\r\n");
                    casio_cmd.direction=0;
                    casio_cmd.direction2=0;
#ifdef MBED
                    casio_serial.read(casio_rx_buf, 15, casio_uart_processor, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
#endif
                    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                    comm_state=COMM_IDLE;
                    break;
                case CODEB_RETRY:
                    if(DEVELOPER) USB_PRINT("received CODEB_RETRY\r\n");
                    if(PINGPONG) USB_PRINT("  |----------CODEB_RETRY---------->|\r\n");
                    break;
                case CODEB_ERROR:
                    if(DEVELOPER) USB_PRINT("received CODEB_ERROR\r\n");
                    if(PINGPONG) USB_PRINT("  |----------CODEB_ERROR---------->|\r\n");
                    break;
                case CASIO_START_INDICATOR:
                    // we don't expect this, but it seems to occur occasionally. We should try to recover.
                    // I think this occurs when status check variable request occurs
                    // do whatever would occur if we'd been in state COMM_IDLE.
                    if(DEVELOPER) USB_PRINT("COMM_WAITING_RX_PACKET_ACK received unexpected start indication\r\n");
                    procedure=PROC_NULL;

                    // casio has sent a start indicator. We should send CODEA_OK
                    // but first prepare for the 15 byte response
                    comm_state=COMM_WAITING_INSTRUCTION;
                    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                    if(PINGPONG) USB_PRINT("  |                                |\r\n");
                    if(PINGPONG) USB_PRINT("  |                          **COMM_IDLE**\r\n");
                    if(PINGPONG) USB_PRINT("  |------0x15-CASIO-START-IND----->|\r\n");     
                    if(PINGPONG) USB_PRINT("  |<-----------CODEA_OK------------|\r\n");
                    if(DEVELOPER) USB_PRINT("wait instruct:\r\n");
#ifdef MBED
                    casio_serial.read(casio_rx_buf, 15, casio_uart_processor);
#endif
                    // send CODEA_OK
                    casio_send_response(CODEA_OK);
                    break;
                default:
                    if(DEVELOPER) USB_PRINT("COMM_WAITING_RX_PACKET_ACK received unexpected value '%u', expected CODEB\r\n", casio_rx_buf[0]);
                    if(PINGPONG) USB_PRINT("  |---------CODEB_UNKNOWN!-------->|\r\n");
                    break;
            }
            break;
        case COMM_WAITING_PERFORM_ROLESWAP:
            casio_cmd.direction=0;
            casio_cmd.direction2=0;
#ifdef MBED
            casio_serial.read(casio_rx_buf, 15, casio_uart_processor, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
#endif
            clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
            comm_state=COMM_IDLE;
            casio_tx_buf[0]=':';
            casio_tx_buf[0]='R';
            casio_tx_buf[0]='A';
            casio_tx_buf[0]='L';
            casio_tx_buf[4]=0xff;
            casio_tx_buf[5]=0xff;
            casio_tx_buf[6]=0xff;
            casio_tx_buf[7]=0xff;
            casio_tx_buf[8]=0xff;
            casio_tx_buf[9]=0xff;
            casio_tx_buf[10]=0xff;
            casio_tx_buf[11]=0xff;
            casio_tx_buf[12]=0xff;
            casio_tx_buf[13]=0xff;
            if(DEVELOPER)USB_PRINT("Sending Roleswap\r\n");
            calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
            casio_send_buf(casio_tx_buf, 15);   
            break;
        default:
            if(DEVELOPER) USB_PRINT("unexpected comm state. Going to COMM_IDLE\r\n");
            if(PINGPONG) USB_PRINT("  |                          COMM_UNKNOWN!\r\n");
            comm_state=COMM_IDLE;
            clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
#ifdef MBED
            casio_serial.read(casio_rx_buf, 15, casio_uart_processor, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
#endif
            break;
    }

}

#ifndef MBED
} // extern "C"
#endif
