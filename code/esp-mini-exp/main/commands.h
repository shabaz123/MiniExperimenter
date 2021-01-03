

#ifndef _COMMANDS_H
#define _COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif


// initialize console
void initialize_console(void);

// wifi
int get_wifi_details(char* ssid, char* wifipw); 
void register_wifi(void);       // example: wifi myssid mypassword

// iot
int get_iot_details(char* iotdev, char* iotscope, char* iotkey);
void register_iot_cmd(void);    // example: iot mydeviceid myidscope mysaskey





#ifdef __cplusplus
}
#endif

#endif /* _COMMANDS_H */
