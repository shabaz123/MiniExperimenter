/***************************************************************************//**
 * Mini Experimenter
 * for Thunderboard Sense 2, connects to Casio fx-CG50
 * rev 0.1 Dec 2020, shabaz
 * not for sale, free for all educational purposes for kids, home and school use
 *
 ******************************************************************************/
 
#include "mbed.h"
#include "Si1133.h"

// defines
//#define TX_PIN          USBTX
//#define RX_PIN          USBRX
#define LED_PIN         LED0
#define TOGGLE_RATE     (0.5f)
#define BUFF_LENGTH     5
#define COMM_BUFF_LENGTH 30
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
#define TOK_MAX 6
#define CHAN_MAX 3
#define TRIG_MODE_NRT 0
#define TRIG_MODE_RT 1
#define TOK_TYPE_INT 0
#define TOK_TYPE_FLOAT 1
#define ENV_ENA_PIN PF9


// debug settings, set these to 0 or 1
#define VERBOSE 0
#define PINGPONG 0
#define DEVELOPER 1
#define HLPP 1

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
Serial usb_serial(USBTX, USBRX);
Serial casio_serial(PC11, PC10, NULL, 38400);
casio_cmd_t casio_cmd;
char procedure=PROC_NULL;
chan_setup_t chan_setup[CHAN_MAX];
samp_trig_setup_t samp_trig_setup;
DigitalOut env_en(ENV_ENA_PIN, 1);
Si1133* light_sensor;

LowPowerTicker      blinker;
bool                blinking = false;
event_callback_t    serialEventCb;
event_callback_t    casioEventCb;
DigitalOut          LED(LED_PIN);
uint8_t             rx_buf[BUFF_LENGTH + 1];
uint8_t             casio_rx_buf[COMM_BUFF_LENGTH + 1];
//uint8_t             casio_tx_buf[COMM_BUFF_LENGTH + 1];
uint8_t             casio_tx_buf[1024];
char comm_state = COMM_IDLE;
char sys_state = SYS_IDLE;
char hl_state = HL_IDLE;
uint16_t sampnum = 0;

double value=-10.0;

// functions

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
    casio_serial.write((const uint8_t *)&r, 1, NULL);
}

void
casio_send_buf(uint8_t* buf, uint8_t len)
{
    casio_serial.write(buf, len, NULL);
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
    usb_serial.printf("%02x", buf[0]);
    for (i=1; i<len; i++) {
        usb_serial.printf(",%02x", buf[i]);
    }
    
    if (brief) return;
    
    usb_serial.printf("  text: '");
    for (i=0; i<len; i++) {
        printable=1;
        if ((buf[i]<32) || (buf[i]>126))
            printable=0;
        if (printable)
            usb_serial.printf("%c", buf[i]);
        else
            usb_serial.printf(".");
    }
    //usb_serial.printf("'\r\n");
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
            usb_serial.printf("%c", buf[i]);
        else
            usb_serial.printf(".");
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
        sscanf(token, "%d", &(tok_arr[tot].tokint));
        tok_arr[tot].toktype=TOK_TYPE_INT;
        if (strstr(token, ".")!=NULL) {
            sscanf(token, "%lf", &(tok_arr[tot].tokfloat));
            tok_arr[tot].toktype=TOK_TYPE_FLOAT;
        }
        tot++;
        if (tot>=TOK_MAX) {
            if(DEVELOPER) usb_serial.printf("Reached TOK_MAX number of allowed tokens\r\n");
            break;
        }
        token = strtok(NULL, s); // get next token
    }
    if(DEVELOPER) usb_serial.printf("number of tokens found: %d\r\n", tot);
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
        usb_serial.printf("instruction: {\r\n");
    switch(dirprint) {
        case DIR_CASIO_SEND:
            if (do_pingpong)
                usb_serial.printf("  |---N");
            else
                usb_serial.printf("  direction : send,\r\n");
            break;
        case DIR_CASIO_RECV:
            if (do_pingpong)
                usb_serial.printf("  |---------------R");
            else
                usb_serial.printf("  direction : recv,\r\n");
            break;
        default:
            if (do_pingpong)
                usb_serial.printf("  |?---X");
            else
                usb_serial.printf("  direction : unknown '%c',\r\n", casio_cmd.direction);
            break;
    }
    if (dirprint==DIR_CASIO_RECV)
        typeprint=casio_cmd.type2;
    else
        typeprint=casio_cmd.type;
    switch(typeprint) {
        case 'A':
            if (do_pingpong)
                usb_serial.printf("A");
            else
                usb_serial.printf("  type : ascii,\r\n");
            break;
        case 'H':
            if (do_pingpong)
                usb_serial.printf("H");
            else
                usb_serial.printf("  type : hex,\r\n");
            break;
        default:
            if (do_pingpong)
                usb_serial.printf("X");
            else
                usb_serial.printf("  type : unknown '%c',\r\n", typeprint);
            break;
    }
    if (dirprint==DIR_CASIO_RECV)
        formprint=casio_cmd.form2;
    else
        formprint=casio_cmd.form;
    switch(formprint) {
        case 'V':
            if (do_pingpong)
                usb_serial.printf("V");
            else
                usb_serial.printf("  form : variable,\r\n");
            break;
        case 'L':
            if (do_pingpong)
                usb_serial.printf("L");
            else
                usb_serial.printf("  form : list,\r\n");
            break;
        default:
            if (do_pingpong)
                usb_serial.printf("X");
            else
                usb_serial.printf("  form : unknown '%c',\r\n", formprint);
            break;
    }
    if (dirprint==DIR_CASIO_SEND) {
        // these fields only make sense for send from casio
        if (do_pingpong) {
            usb_serial.printf(",L=%u,O=%lu,P=%u,", casio_cmd.line, casio_cmd.offset, casio_cmd.psize);
        } else {
            usb_serial.printf("  line : %u,\r\n", casio_cmd.line);
            usb_serial.printf("  offset : %lu,\r\n", casio_cmd.offset);
            usb_serial.printf("  packet_size : %u,\r\n", casio_cmd.psize);
        }
        if (do_pingpong) {
            // tidy the length for pingpong properly later
            if (casio_cmd.psize>9)
                usb_serial.printf("%c---------->|\r\n", casio_cmd.area);
            else
                usb_serial.printf("%c----------->|\r\n", casio_cmd.area);
        } else {
            switch(casio_cmd.area) {
                case 'A':
                    usb_serial.printf("  area : all,\r\n");
                    break;
                case 'S':
                    usb_serial.printf("  area : start,\r\n");
                    break;
                case 'M':
                    usb_serial.printf("  area : middle,\r\n");
                    break;
                case 'E':
                    usb_serial.printf("  area : end,\r\n");
                    break;
                default:
                    usb_serial.printf("  area : unknown '%c',\r\n", casio_cmd.form);
                    break;
            }
        }
    } else {
        // DIR_CASIO_RECV
        if (do_pingpong) usb_serial.printf("------------->|\r\n");
    }
    if (!do_pingpong) {
        usb_serial.printf("  checksum : 0x%02x\r\n", casio_cmd.csum);
        usb_serial.printf("}\r\n");
    }
}

void
print_hlpp_r38(uint8_t* buf, uint16_t len, char f)
{
    int8_t plen;
    int i;
    char padding[24];
    
    usb_serial.printf("  |<--R38K: ");
    if (f=='A') { // ASCII
        if (len<20) {
            plen=(int8_t)len;
            asc_print(buf, plen);
        } else {
            plen=20;
            asc_print(buf, plen-3);
            usb_serial.printf("etc");
        }     
        for (i=0; i<(20-plen); i++) {
            padding[i]='-';
        }
        padding[20-plen]='\0';
        usb_serial.printf(padding);
        usb_serial.printf("---|\r\n");
    } else if (f=='H') { // Hex
        usb_serial.printf("0x");
        if (len<6) {
            plen=(int8_t)len;
            hex_print(buf, plen, BRIEF);
        } else {
            plen=6;
            hex_print(buf, plen-1, BRIEF);
            usb_serial.printf("..");
        }     
        for (i=0; i<(17-plen); i++) {
            padding[i]='-';
        }
        padding[17-plen]='\0';
        usb_serial.printf(padding);
        usb_serial.printf("-|\r\n");    
    } else {
        // unknown format
        usb_serial.printf("unknown format!--------|\r\n");
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
    int8_t retval=0;
    char csum;
    // sanity check
    if (buf[0]!=':') {
        usb_serial.printf("error, start_header is not ':'!\r\n");
        return(START_HEADER_ERROR);
    }
    if ((buf[1]!='N') && (buf[1]!='R')) {
        usb_serial.printf("error, direction is not 'N' or 'R'!\r\n");
        return(-1);
    }
    calc_checksum(buf, 15, &csum);
    if (csum!=buf[14]) {
        usb_serial.printf("error, I compute checksum should be %02x\r\n", csum);
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
    
    if(DEVELOPER) usb_serial.printf("instruction decoded\r\n");
    return(0);
}


// callbacks
void blink(void) {
    LED = !LED;
}

void casio_callback(int events) {
    int8_t res;
    uint16_t n;
    int txbytes_total=0;
    double scaled;
    uint16_t scaled_u16;
    int8_t plen;
    int i;
    char padding[24];
    char doerror=0;
    char numtok=0;
    //int16_t tok_arr[TOK_MAX];
    cmd_tok_t tok_arr[TOK_MAX];
    float light, uv;
    
    
    
    switch(comm_state) {
        case COMM_IDLE:
            procedure=PROC_NULL;
            if (comm_seek_char(CASIO_START_INDICATOR) >= 0) {
                // casio has sent a start indicator. We should send CODEA_OK
                // but first prepare for the 15 byte response
                comm_state=COMM_WAITING_INSTRUCTION;
                clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                if(PINGPONG) usb_serial.printf("  |                                |\r\n");
                if(PINGPONG) usb_serial.printf("  |                          **COMM_IDLE**\r\n");
                if(PINGPONG) usb_serial.printf("  |------0x15-CASIO-START-IND----->|\r\n");     
                if(PINGPONG) usb_serial.printf("  |<-----------CODEA_OK------------|\r\n");
                if(DEVELOPER) usb_serial.printf("waiting instruction\r\n");
                casio_serial.read(casio_rx_buf, 15, casio_callback);
                // send CODEA_OK
                casio_send_response(CODEA_OK);
            } else {
                // we didn't receive a start indication from casio.
                if(DEVELOPER) usb_serial.printf("received junk, ignoring:\r\n");
                if(DEVELOPER) { hex_print(casio_rx_buf, 15); usb_serial.printf("'\r\n"); }
                if(DEVELOPER) usb_serial.printf("waiting start indicator\r\n");
                if(PINGPONG) {
                    usb_serial.printf("  |                                |\r\n");
                    usb_serial.printf("  |                          **COMM_IDLE**\r\n");
                    usb_serial.printf("  |-JUNK-");
                    hex_print(casio_rx_buf, 3);
                    usb_serial.printf("etc-->|\r\n");
                }
                clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
            }
            break;
        case COMM_WAITING_INSTRUCTION:
            if (DEVELOPER) usb_serial.printf("received instruction:\r\n");
            if (DEVELOPER) { hex_print(casio_rx_buf, 15); usb_serial.printf("'\r\n"); }
            if(PINGPONG) usb_serial.printf("  |                    COMM_WAITING_INSTRUCTION\r\n");
            res=decode_instruction(casio_rx_buf);
            if (res==START_HEADER_ERROR) {
                if(DEVELOPER) usb_serial.printf("revert to waiting for start indicator\r\n");
                if(PINGPONG) usb_serial.printf("  |------[START HEADER ERROR]----->|\r\n");
                comm_state=COMM_IDLE;
                clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
            } else {
                if (casio_rx_buf[1]==DIR_CASIO_SEND)
                    procedure=PROC_SEND38K;
                else
                    procedure=PROC_RECV38K;
                if (VERBOSE) instruction_print();
                if(PINGPONG) instruction_print(1);
                if (casio_rx_buf[1]==DIR_CASIO_SEND) {
                    // casio will now send data, when we issue CODEB_OK
                    if(PINGPONG) usb_serial.printf("  |<-----------CODEB_OK------------|\r\n");
                    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                    if (casio_cmd.type==TYPE_ASCII) {
                        casio_cmd.datapacksize=casio_cmd.psize+2; 
                    } else {
                        usb_serial.printf("error, cannot handle type '%c'\r\n", casio_cmd.type);
                        casio_cmd.datapacksize=casio_cmd.psize+2; 
                    }
                    if (n>COMM_BUFF_LENGTH) {
                        usb_serial.printf("error, length %u is larger than buffer size!\r\n", n);
                        n=COMM_BUFF_LENGTH;
                    }
                    casio_serial.read(casio_rx_buf, casio_cmd.datapacksize, casio_callback);
                    comm_state = COMM_WAITING_DATA;
                    casio_send_response(CODEB_OK);
                } else { // DIR_CASIO_RECV
                    // we are now expected to receive CODEB after sending a header
                    casio_serial.read(casio_rx_buf, 1, casio_callback, SERIAL_EVENT_RX_ALL, CODEB_OK);
                    comm_state=COMM_WAITING_RX_HEADER_ACK;
                    if (casio_cmd.form2=='L') { // think we can now send header for voltage packets?
                        casio_cmd.command=99; // magic code for now
                        if (VERBOSE) usb_serial.printf("building header for list response\r\n");
                        casio_tx_buf[0]=':';
                        casio_tx_buf[1]='N';
                        if (casio_cmd.type2=='A') {
                            casio_tx_buf[2]='A';
                        } else {
                            casio_tx_buf[2]='H'; //H
                        }
                        casio_tx_buf[3]='L';
                        casio_tx_buf[4]=0;
                        casio_tx_buf[5]=1;
                        casio_tx_buf[6]=0;
                        casio_tx_buf[7]=0;
                        casio_tx_buf[8]=0;
                        casio_tx_buf[9]=1;
                        casio_tx_buf[10]=0;
                        if (casio_cmd.type2=='A') {
                            casio_tx_buf[11]=6; // hard-coded for now, always 6 characters. Should be sufficient.
                            if(PINGPONG) usb_serial.printf("  |<---NAL,L=1,O=1,P=3,A-----------|\r\n");
                        } else if (samp_trig_setup.mode==TRIG_MODE_NRT) { // non-real-time chunk, used for faster sampling. Doesn't work.
                            casio_tx_buf[10]=0x01;
                            casio_tx_buf[11]=0x90; //400 bytes for 200 hex values.. this doesn't work anyway
                            if(PINGPONG) usb_serial.printf("  |<---NAL,L=1,O=1,P=2,A-----------|\r\n");
                        } else { // real-time mode (slower sampling, data sent one sample at a time)
                            casio_tx_buf[11]=2; // 2 bytes for hex value
                            if(PINGPONG) usb_serial.printf("  |<---NAL,L=1,O=1,P=2,A-----------|\r\n");
                        }
                        casio_tx_buf[12]=0xff;
                        if (sampnum==0) {
                            casio_tx_buf[13]='A';
                        } else if (sampnum==100) {
                            casio_tx_buf[13]='E';
                        } else {
                            casio_tx_buf[13]='M';
                        }
                        calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
                        if(DEVELOPER) usb_serial.printf("sending list header, waiting for CODEB_OK\r\n");
                        casio_send_buf(casio_tx_buf, 15);
                    }
                    
                    if (casio_cmd.command=='7') {
                        if(DEVELOPER) usb_serial.printf("building header for status check (command 7) response\r\n");
                        if (casio_cmd.line==1) {
                            if(DEVELOPER) usb_serial.printf("building header for line 1\r\n");
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
                            if(DEVELOPER) usb_serial.printf("sending header, waiting for CODEB_OK\r\n");
                            if(PINGPONG) usb_serial.printf("  |<---NAL,L=1,O=1,P=1,A-----------|\r\n");
                            casio_send_buf(casio_tx_buf, 15);
                        }
                    }
                    switch(sys_state) {
                        case SYS_IDLE:
                            sys_state = SYS_INIT;
                            break;
                        default:
                            if(DEVELOPER) usb_serial.printf("error, unrecognized sys_state %d\r\n", sys_state);
                            break;
                    }   
                    
                }
                
            }
            break;
        case COMM_WAITING_DATA:
            if(DEVELOPER) usb_serial.printf("received data packet:\r\n");
            if(DEVELOPER) { hex_print(casio_rx_buf, casio_cmd.datapacksize); usb_serial.printf("'\r\n"); }
            if(PINGPONG) usb_serial.printf("  |                       COMM_WAITING_DATA\r\n");
            if(HLPP) {
                if (procedure==PROC_SEND38K) {
                    usb_serial.printf("  |---S38K: ");
                    if (casio_cmd.type==TYPE_ASCII) {
                        plen=(int8_t)casio_cmd.datapacksize-2;
                        if(plen<20) {
                            asc_print(casio_rx_buf+1, plen);
                        } else {
                            plen=20;
                            asc_print(casio_rx_buf+1, plen-3);
                            usb_serial.printf("etc");
                        }
                        for (i=0; i<(20-plen); i++) {
                            padding[i]='-';
                        }
                        padding[20-plen]='\0';
                        usb_serial.printf(padding);
                        usb_serial.printf("-->|\r\n");
                    } else if (casio_cmd.type==TYPE_HEX) {
                        // not fully tested for now
                        plen=(int8_t)casio_cmd.datapacksize-2;
                        if(plen<6) {
                            hex_print(casio_rx_buf+1, plen);
                        } else {
                            plen=5;
                            hex_print(casio_rx_buf+1, plen);
                            usb_serial.printf("..");
                        }
                        for (i=0; i<(20-plen); i++) {
                            padding[i]='-';
                        }
                        padding[20-plen]='\0';
                        usb_serial.printf(padding);
                        usb_serial.printf("-->|\r\n");
                    } else {
                        // unexpected format
                        usb_serial.printf("unknown format!------->|\r\n");
                    }
                } else {
                    // we don't expect any other procedure if we're in state COM_WAITING_DATA
                }
            }
            // is the first byte ':'? If not, then reject with a CODEB_ERROR for now
            if (casio_rx_buf[0]!=':') {
                if(DEVELOPER) usb_serial.printf("error, invalid data, send CODEB_ERROR\r\n");
                doerror=1;
            }
            
            get_tokens(&casio_rx_buf[1], casio_cmd.datapacksize-2, tok_arr, &numtok);
            // handle the commands!
            switch(tok_arr[0].tokint) {
                case 1: // channel setup command
                    if(DEVELOPER)usb_serial.printf("received channel setup data\r\n");
                    if(PINGPONG) usb_serial.printf("  |--------1-CHAN_SETUP----------->|\r\n");
                    if (numtok>=3) {
                        if (tok_arr[1].tokint<=CHAN_MAX) {
                            chan_setup[tok_arr[1].tokint].operation = tok_arr[2].tokint;
                            if(DEVELOPER)usb_serial.printf("CH%d set to type %d\r\n", tok_arr[1].tokint, tok_arr[2].tokint);
                            if (tok_arr[2].tokint!=2) {
                                if(DEVELOPER)usb_serial.printf("error, unsupported chan type\r\n");
                            }
                        }
                    } else {
                        // wrong amount of tokens!
                    }
                    break;
                case 3: // sample rate and num samples
                    if(DEVELOPER)usb_serial.printf("received sampling rate\r\n");
                    if(PINGPONG) usb_serial.printf("  |--------3-SAMPLERATE----------->|\r\n");
                    if (numtok>=3) {
                        if (tok_arr[1].toktype==TOK_TYPE_INT) {
                            samp_trig_setup.period_usec=((uint32_t)(tok_arr[1].tokint))*1E6;
                        } else {
                            samp_trig_setup.period_usec=(uint32_t)(tok_arr[1].tokfloat*1000000.0);
                        }
                        if(DEVELOPER)usb_serial.printf("sample rate: %u usec\r\n", samp_trig_setup.period_usec);
                        if (tok_arr[2].tokint==-1) {
                            samp_trig_setup.mode=TRIG_MODE_RT;
                            samp_trig_setup.numsamp=0; // sampled with each data request
                            if(DEVELOPER)usb_serial.printf("num samples: per data request\r\n");
                        } else {
                            samp_trig_setup.numsamp=(unsigned int)tok_arr[2].tokint;
                            if(DEVELOPER)usb_serial.printf("num samples: %u\r\n", samp_trig_setup.numsamp);
                        }
                        
                    } else {
                        // wrong amount of tokens!   
                    }
                    break;
                case 7: // status check command 7
                    casio_cmd.command='7';
                    if(DEVELOPER) usb_serial.printf("received status check command '7'\r\n");
                    if(PINGPONG) usb_serial.printf("  |--------7-STATUS_CHECK--------->|\r\n");
                    break;
                case 8: // trigger command to start sampling
                    sampnum=0;
                    hl_state=HL_SENDING;
                    if(DEVELOPER)usb_serial.printf("entering state HL_SENDING\r\n");
                    if(PINGPONG) usb_serial.printf("  |--------8-TRIGGER-------------->|\r\n");
                    break;
                case 12: // real time mode
                    if (numtok==2) {
                        if (tok_arr[1].tokint==1) {
                            samp_trig_setup.mode=TRIG_MODE_RT; // real-time mode, single result
                            if(DEVELOPER)usb_serial.printf("entering realtime mode\r\n");
                            if(PINGPONG) usb_serial.printf("  |--------12-REALTIME------------>|\r\n");
                        } else {
                            samp_trig_setup.mode=TRIG_MODE_NRT; // non-real-time, batched. can't get this to work..
                            if(DEVELOPER)usb_serial.printf("entering unusable non-realtime mode (batch)\r\n");
                            if(PINGPONG) usb_serial.printf("  |--------12-NONREALTIME--------->|\r\n");
                        }
                    } else {
                        // wrong number of tokens! Should not occur.
                    }
                default:
                    if(PINGPONG) {
                        usb_serial.printf("  |--[PAK ");
                        plen=(int8_t)casio_cmd.datapacksize-2;
                        if(plen<20) {
                            asc_print(casio_rx_buf+1, plen);
                        } else {
                            plen=20;
                            asc_print(casio_rx_buf+1, plen-3);
                            usb_serial.printf("etc");
                        }
                        usb_serial.printf("]");
                        for (i=0; i<(20-plen); i++) {
                            padding[i]='-';
                        }
                        padding[20-plen]='\0';
                        usb_serial.printf(padding);
                        usb_serial.printf("--->|\r\n");
                    }
                    break;
            }
            
            // we can now send CODEB_OK and go back to idle state
            if(PINGPONG) {
                if (doerror) {
                    usb_serial.printf("  |<---------CODEB_ERROR-----------|\r\n");
                } else {
                    usb_serial.printf("  |<-----------CODEB_OK------------|\r\n");
                }
            }
            comm_state=COMM_IDLE;
            clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
            casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
            if (doerror) {
                casio_send_response(CODEB_ERROR);
            } else {
                casio_send_response(CODEB_OK);
            }
            break;
        case COMM_WAITING_RX_HEADER_ACK:
            if(PINGPONG) usb_serial.printf("  |                  COMM_WAITING_RX_HEADER_ACK\r\n");
            switch(casio_rx_buf[0]) {
                case CODEB_OK:
                    if(DEVELOPER) usb_serial.printf("received CODEB_OK\r\n");
                    if(PINGPONG) usb_serial.printf("  |------------CODEB_OK----------->|\r\n");
                    // we are now expected to receive a CODEB after sending a packet
                    casio_serial.read(casio_rx_buf, 1, casio_callback, SERIAL_EVENT_RX_ALL, CODEB_OK);
                    if (casio_cmd.command=='7') {
                        if(DEVELOPER) usb_serial.printf("building packet for status check (command 7) response\r\n");
                        if (casio_cmd.line==1) {
                            if(DEVELOPER) usb_serial.printf("building packet for line 1\r\n");
                            if(PINGPONG) usb_serial.printf("  |<-------1-STATUS_READY----------|\r\n");
                            if (HLPP) usb_serial.printf("  |<--R38K: 1----------------------|\r\n");
                            casio_tx_buf[0]=':';
                            casio_tx_buf[1]='1';
                            txbytes_total=3;
                            calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            if(DEVELOPER) usb_serial.printf("sending packet, waiting for CODEB_OK\r\n");
                            comm_state=COMM_WAITING_RX_PACKET_ACK;
                            casio_send_buf(casio_tx_buf, txbytes_total);
                        }
                    }
                    if (casio_cmd.command==99) {
                        if (VERBOSE) usb_serial.printf("building packet with voltage value response\r\n");
                        if (1/*casio_cmd.line==1*/) {
                            if (casio_cmd.type2=='A') {
                                if (VERBOSE) usb_serial.printf("building ascii packet for line 1\r\n");
                                if(PINGPONG) usb_serial.printf("  |<-----[MEASUREMENT ASCII]-------|\r\n");
                                casio_tx_buf[0]=':';
                                light_sensor->get_light_and_uv(&light, &uv);
                                float2ascii((double)light/1000.0, &casio_tx_buf[1]); // populate 6 bytes with the ASCII representation
                                //casio_tx_buf[1]='3';
                                //casio_tx_buf[2]='.';
                                //casio_tx_buf[3]='6';
                                txbytes_total=6+2; // 6 bytes from float2ascii, and two bytes for ':' and the checksum
                                if (HLPP) print_hlpp_r38(&casio_tx_buf[1], txbytes_total-2, 'A');
                                calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                            } else if (casio_cmd.type2=='H') { // is this hex format?
                                if (VERBOSE) usb_serial.printf("building hex packet for line 1\r\n");
                                if(PINGPONG) usb_serial.printf("  |<------[MEASUREMENT HEX]--------|\r\n");
                                casio_tx_buf[0]=':';
                                light_sensor->get_light_and_uv(&light, &uv);
                                value=(double)light/1000.0;
                                if (value>10.0) value=10.0;
                                if (value<-10.0) value = -10.0;
                                // convert value to a 12-bit number
                                scaled=10.92+value;
                                scaled=scaled*4096;
                                scaled=scaled/21.555;
                                scaled_u16=(uint16_t)scaled;
                                scaled_u16=scaled_u16&0x0fff;
                                
                                //value=value+0.1;
                                //if (value>10.0) value=-10;
                                
                                
                                casio_tx_buf[1]=(uint8_t)(scaled_u16 & 0x00ff);
                                casio_tx_buf[2]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                //casio_tx_buf[3]=(uint8_t)(scaled_u16 & 0x00ff);
                                //casio_tx_buf[4]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                //casio_tx_buf[5]=(uint8_t)(scaled_u16 & 0x00ff);
                                //casio_tx_buf[6]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                //casio_tx_buf[7]=(uint8_t)(scaled_u16 & 0x00ff);
                                //casio_tx_buf[8]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                txbytes_total=4;
                                sampnum=sampnum+1;
                             
                                // non-real-time mode is used for fast sample rates. Doesn't work : (
                                if ((hl_state==HL_SENDING) && (samp_trig_setup.mode==TRIG_MODE_NRT)){
                                    for (i=0; i<100; i++) {
                                        scaled=10.92+value;
                                        scaled=scaled*4096;
                                        scaled=scaled/21.555;
                                        scaled_u16=(uint16_t)scaled;
                                        scaled_u16=scaled_u16&0x0fff;
                                        value=value+0.1;
                                        if (value>10.0) value=-10;
                                        casio_tx_buf[(i*4)+1]=(uint8_t)(scaled_u16 & 0x00ff);
                                        casio_tx_buf[(i*4)+2]=(uint8_t)((scaled_u16 >> 8) & 0x00ff);
                                        casio_tx_buf[(i*4)+3]=0x00;
                                        casio_tx_buf[(i*4)+4]=0x00;
                                    }
                                    txbytes_total=(200*2)+2;
                                    sampnum=sampnum+100;
                                }                           
                              
                                if (HLPP) print_hlpp_r38(&casio_tx_buf[1], txbytes_total-2, 'H');
                                calc_checksum(casio_tx_buf, txbytes_total, (char*)&casio_tx_buf[txbytes_total-1]);
                                //sampnum=sampnum+100;
                            } else {
                                usb_serial.printf("error, unrecognizable type '%c'!\r\n", casio_cmd.type2);
                            }
                            //sampnum++;
                            if(DEVELOPER)usb_serial.printf("sending voltage sample %u, waiting for CODEB_OK\r\n", sampnum);
                            if ((hl_state==HL_SENDING) && (samp_trig_setup.mode==TRIG_MODE_NRT)){
                                if (sampnum>=100) {
                                    hl_state=HL_IDLE;
                                    //comm_state=COMM_WAITING_RX_PACKET_ACK;
                                    comm_state=COMM_WAITING_PERFORM_ROLESWAP;
                                    if(DEVELOPER)usb_serial.printf("Entering state HL_IDLE\r\n");
                                } else {
                                    // we will stay in this state if we are sending multiple data packets
                                    if(DEVELOPER)usb_serial.printf("then ready to send next data packet\r\n");
                                    
                                }
                                
                            } else {
                                comm_state=COMM_WAITING_RX_PACKET_ACK;
                            }
                            casio_send_buf(casio_tx_buf, txbytes_total /*15*/);
                        }
                    }
                    break;
                case CODEB_RETRY:
                    if(DEVELOPER) usb_serial.printf("received CODEB_RETRY\r\n");
                    if(PINGPONG) usb_serial.printf("  |----------CODEB_RETRY---------->|\r\n");
                    break;
                case CODEB_ERROR:
                    if(DEVELOPER) usb_serial.printf("received CODEB_ERROR\r\n");
                    if(PINGPONG) usb_serial.printf("  |----------CODEB_ERROR---------->|\r\n");
                    break;
                default:
                    if(DEVELOPER) usb_serial.printf("received unexpected value '%u', expected CODEB\r\n", casio_rx_buf[0]);
                    if(PINGPONG) usb_serial.printf("  |---------CODEB_UNKNOWN!-------->|\r\n");
                    break;
            }
            break;
        case COMM_WAITING_RX_PACKET_ACK:
            if(PINGPONG) usb_serial.printf("  |                COMM_WAITING_RX_PACKET_ACK\r\n");
            switch(casio_rx_buf[0]) {
                case CODEB_OK:
                    if(DEVELOPER) usb_serial.printf("received CODEB_OK, finished\r\n");
                    if(PINGPONG) usb_serial.printf("  |------------CODEB_OK----------->|\r\n");
                    casio_cmd.direction=0;
                    casio_cmd.direction2=0;
                    casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
                    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
                    comm_state=COMM_IDLE;
                    break;
                case CODEB_RETRY:
                    if(DEVELOPER) usb_serial.printf("received CODEB_RETRY\r\n");
                    if(PINGPONG) usb_serial.printf("  |----------CODEB_RETRY---------->|\r\n");
                    break;
                case CODEB_ERROR:
                    if(DEVELOPER) usb_serial.printf("received CODEB_ERROR\r\n");
                    if(PINGPONG) usb_serial.printf("  |----------CODEB_ERROR---------->|\r\n");
                    break;
                default:
                    if(DEVELOPER) usb_serial.printf("received unexpected value '%u', expected CODEB\r\n", casio_rx_buf[0]);
                    if(PINGPONG) usb_serial.printf("  |---------CODEB_UNKNOWN!-------->|\r\n");
                    break;
            }
            break;
        case COMM_WAITING_PERFORM_ROLESWAP:
            casio_cmd.direction=0;
            casio_cmd.direction2=0;
            casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
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
            if(DEVELOPER)usb_serial.printf("Sending Roleswap\r\n");
            calc_checksum(casio_tx_buf, 15, (char*)&casio_tx_buf[14]);
            casio_send_buf(casio_tx_buf, 15);   
            break;
        default:
            if(DEVELOPER) usb_serial.printf("unexpected comm state. Going to COMM_IDLE\r\n");
            if(PINGPONG) usb_serial.printf("  |                          COMM_UNKNOWN!\r\n");
            comm_state=COMM_IDLE;
            clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
            casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
            break;
    }

}

/**
* This is a callback! Do not call frequency-dependent operations here.
*
* For a more thorough explanation, go here: 
* https://developer.mbed.org/teams/SiliconLabs/wiki/Using-the-improved-mbed-sleep-API#mixing-sleep-with-synchronous-code
**/
void serialCb(int events) {
    /* Something triggered the callback, either buffer is full or 'S' is received */
    unsigned char i;
    if(events & SERIAL_EVENT_RX_CHARACTER_MATCH) {
        //Received 'S', check for buffer length
        for(i = 0; i < BUFF_LENGTH; i++) {
            //Found the length!
            if(rx_buf[i] == 'S') break;
        }
        
        // Toggle blinking
        if(blinking) {
            blinker.detach();
            blinking = false;
        } else {
            blinker.attach(blink, TOGGLE_RATE);
            blinking = true;
        }
    } else if (events & SERIAL_EVENT_RX_COMPLETE) {
        i = BUFF_LENGTH - 1;
    } else {
        rx_buf[0] = 'E';
        rx_buf[1] = 'R';
        rx_buf[2] = 'R';
        rx_buf[3] = '!';
        rx_buf[4] = 0;
        i = 3;
    }
    
    // Echo string, no callback
    usb_serial.write(rx_buf, i+1, 0, 0);
    
    // Reset serial reception
    usb_serial.read(rx_buf, BUFF_LENGTH, serialEventCb, SERIAL_EVENT_RX_ALL, 'S');
}

/*-------------------- Main ----------------------*/
int main()
{
    int i=0;
    
    clear_buf(casio_rx_buf, COMM_BUFF_LENGTH);
    // defaults, these should NOT be changed since they match the Casio calculator defaults
    samp_trig_setup.period_usec=200000; // 0.2 sec default
    samp_trig_setup.numsamp=101;
    samp_trig_setup.mode=TRIG_MODE_NRT;
    
    for (i=0; i<CHAN_MAX; i++) {
        chan_setup[i].operation=0;
    }
    
    serialEventCb.attach(serialCb);
    //serialEventCb.attach(callback(this,serialCb));
    
    // Setup USB serial connection
    usb_serial.baud(115200);
    usb_serial.printf("Welcome Mini Experimenter :-)\r\n");
    
    // enable light sensor
    env_en=1; // turn on the power to the environment sensors
    wait(0.1);
    light_sensor = new Si1133(PC4, PC5);
    if(!light_sensor->open()) {
        usb_serial.printf("error, light sensor failed!\r\n");
    }
    
    if((PINGPONG) || (HLPP)) usb_serial.printf("CASIO                            MiniE\r\n");
    if((PINGPONG) || (HLPP)) usb_serial.printf("  |                                |\r\n");
    usb_serial.read(rx_buf, BUFF_LENGTH, serialEventCb, SERIAL_EVENT_RX_ALL, 'S');
    
    // setup the Casio serial connection
    casio_serial.format(8, SerialBase::None, 2);
    
    //casio_serial.printf("Hello");
    casioEventCb.attach(casio_callback);
    casio_serial.read(casio_rx_buf, 15, casio_callback, SERIAL_EVENT_RX_ALL, CASIO_START_INDICATOR);
    
    /* Let the callbacks take care of everything */
    while(1) sleep();
}
