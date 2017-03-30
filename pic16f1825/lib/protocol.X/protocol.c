#include "mcc_generated_files/mcc.h"
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

// function pointers (event handlers)
void (*PROTOCOL_Start_Handler)(void) = 0;
void (*PROTOCOL_Stop_Handler)(void) = 0;
void (*PROTOCOL_Set_Handler)(uint8_t value) = 0;
void (*PROTOCOL_Loop_Func)(void) = 0;
void (*PROTOCOL_Extension_Handler)(uint8_t *buf) = 0;

uint8_t c;
uint8_t cnt = 0;
uint8_t buf[48];
uint8_t value;
const char *device_id_;

// initialization
void PROTOCOL_Initialize(const char *device_id, void *start_handler, void *stop_handler, void *set_handler) {
    device_id_ = device_id;
    PROTOCOL_Start_Handler = start_handler;
    PROTOCOL_Stop_Handler = stop_handler;
    PROTOCOL_Set_Handler = set_handler;
    
    if (PROTOCOL_Stop_Handler) PROTOCOL_Stop_Handler();
    value = DATAEE_ReadByte(0);  // read value from EEPROM
    if (PROTOCOL_Set_Handler) PROTOCOL_Set_Handler(value);
    if (PROTOCOL_Start_Handler) PROTOCOL_Start_Handler();
}

void PROTOCOL_Set_Func(void *loop_func) {
    PROTOCOL_Loop_Func = loop_func;
}

void PROTOCOL_Set_Extension_Handler(void *extension_handler) {
    PROTOCOL_Extension_Handler = extension_handler;
}

/*
 * USART Rx reader
 */
void PROTOCOL_Loop() {
    while (1) {
        if (PROTOCOL_Loop_Func) PROTOCOL_Loop_Func();
        if (EUSART_DataReady) {
            c = EUSART_Read();
            buf[cnt++] = c;
            if (c == '\n') {
                buf[--cnt] = '\0';
                cnt = 0;

                if (!strncmp(WHO, buf, 3)) {  // who are you?
                    printf("%s\n", device_id_);
                } else if (!strncmp(SAV, buf ,3)) {  // save the current setting
                    DATAEE_WriteByte(0, value);
                } else if (!strncmp(STA, buf, 3)) {  // start measurement
                    PROTOCOL_Start_Handler();
                } else if (!strncmp(STP, buf, 3)) {  // stop measurement
                    PROTOCOL_Stop_Handler();
                    printf("%s\n", ACK);
                } else if (!strncmp(SET, buf, 3)) {  // set value
                    value = atoi(&buf[4]);
                    PROTOCOL_Set_Handler(value);
                } else if (!strncmp(GET, buf, 3)) {  // get value
                    printf("VAL:%d\n", value);
                } else {
                    PROTOCOL_Extension_Handler(buf);
                }
            }
        };
    }
}
