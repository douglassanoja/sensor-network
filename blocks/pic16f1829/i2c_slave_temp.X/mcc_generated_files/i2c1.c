#include "mcc.h"
#include "protocol.h"

#define I2C_SLAVE_ADDRESS 0x01 
#define I2C_SLAVE_MASK    0x7F

#define DEFAULT    0x00
#define SET_VALUE  0x01
#define EXT_LENGTH 0x02
#define EXT_VALUE  0x03

typedef enum
{
    SLAVE_NORMAL_DATA,
    SLAVE_DATA_ADDRESS,
    SLAVE_GENERAL_CALL,
} SLAVE_WRITE_DATA_TYPE;

/**
 Section: Global Variables
*/

volatile uint8_t    I2C_slaveWriteData      = 0x55;

/**
 Section: Local Functions
*/
void I2C1_StatusCallback(I2C1_SLAVE_DRIVER_STATUS i2c_bus_state);

void I2C1_Initialize(void)
{
    
    // delay 2 sec for the master to finish its start up process
    __delay_ms(2000);

    // initialize the hardware
    // R_nW write_noTX; P stopbit_notdetected; S startbit_notdetected; BF RCinprocess_TXcomplete; SMP Standard Speed; UA dontupdate; CKE disabled; D_nA lastbyte_address; 
    SSP1STAT = 0x80;
    // SSPEN enabled; WCOL no_collision; CKP disabled; SSPM 7 Bit Polling; SSPOV no_overflow; 
    SSP1CON1 = 0x26;
    // ACKEN disabled; GCEN enabled; PEN disabled; ACKDT acknowledge; RSEN disabled; RCEN disabled; ACKSTAT received; SEN enabled; 
    SSP1CON2 = 0x81;
    // ACKTIM ackseq; SBCDE disabled; BOEN disabled; SCIE disabled; PCIE disabled; DHEN disabled; SDAHT 100ns; AHEN disabled; 
    SSP1CON3 = 0x00;
    // SSPMSK 127; 
    SSP1MSK = (I2C_SLAVE_MASK << 1);  // adjust UI mask for R/nW bit            
    // SSPADD 8; 
    SSP1ADD = (PROTOCOL_Read_Device_Address() << 1);  // adjust UI address for R/nW bit

    // clear the slave interrupt flag
    PIR1bits.SSP1IF = 0;
    // enable the master interrupt
    PIE1bits.SSP1IE = 1;

}

void I2C1_ISR ( void )
{
    uint8_t     i2c_data                = 0x55;

    // NOTE: The slave driver will always acknowledge
    //       any address match.

    PIR1bits.SSP1IF = 0;        // clear the slave interrupt flag
    i2c_data        = SSP1BUF;  // read SSPBUF to clear BF
    if(1 == SSP1STATbits.R_nW)
    {
        if((1 == SSP1STATbits.D_nA) && (1 == SSP1CON2bits.ACKSTAT))
        {
            // callback routine can perform any post-read processing
            I2C1_StatusCallback(I2C1_SLAVE_READ_COMPLETED);
        }
        else
        {
            // callback routine should write data into SSPBUF
            I2C1_StatusCallback(I2C1_SLAVE_READ_REQUEST);
        }
    }
    else if(0 == SSP1STATbits.D_nA)
    {
        // this is an I2C address

        if(0x00 == i2c_data)
        {
            // this is the General Call address
            I2C1_StatusCallback(I2C1_SLAVE_GENERAL_CALL_REQUEST);
        }
        else
        {
            // callback routine should prepare to receive data from the master
            I2C1_StatusCallback(I2C1_SLAVE_WRITE_REQUEST);
        }
    }
    else
    {
        I2C_slaveWriteData   = i2c_data;

        // callback routine should process I2C_slaveWriteData from the master
        I2C1_StatusCallback(I2C1_SLAVE_WRITE_COMPLETED);
    }

    SSP1CON1bits.CKP    = 1;    // release SCL

}

void I2C1_StatusCallback(I2C1_SLAVE_DRIVER_STATUS i2c_bus_state)
{

    static uint8_t slaveWriteType   = SLAVE_NORMAL_DATA;
    static uint8_t next = DEFAULT;
    static uint8_t ext_len = 0;
    static uint8_t ext_cnt = 0;
    static char ext_buf[BUF_SIZE];
    uint8_t *pdata;
    switch (i2c_bus_state)
    {
        case I2C1_SLAVE_WRITE_REQUEST:
            slaveWriteType  = SLAVE_DATA_ADDRESS;
            break;

        case I2C1_SLAVE_GENERAL_CALL_REQUEST:
            // the master will be sending general call data next
            slaveWriteType  = SLAVE_GENERAL_CALL;
            break;

        case I2C1_SLAVE_WRITE_COMPLETED:

            switch(slaveWriteType)
            {
                case SLAVE_DATA_ADDRESS:
                    switch(next) {
                        case SET_VALUE:
                            PROTOCOL_SET(I2C_slaveWriteData);
                            next = DEFAULT;
                            break;
                        case DEFAULT:
                            switch(I2C_slaveWriteData) {
                                case STA_I2C:
                                    PROTOCOL_STA();
                                    break;
                                case STP_I2C:
                                    PROTOCOL_STP();
                                    break;
                                case SAV_I2C:
                                    PROTOCOL_SAV();
                                    break;
                                case INV_I2C:
                                    PROTOCOL_INV();
                                    break;
                                case RST_I2C:
                                    PROTOCOL_RST();
                                    break;
                                case SET_I2C:
                                    next = SET_VALUE;
                                    break;
                                case EXT_I2C:
                                    next = EXT_LENGTH;
                                    break;
                            }
                            break;
                    }
                    break;

                case SLAVE_GENERAL_CALL:
                    if (I2C_slaveWriteData == PLG_I2C) {
                        SSP1CON2bits.GCEN = 0;  // Disable General Call reception
                        PROTOCOL_Backplane_Slave_Enabled();
                    }
                    break;

                case SLAVE_NORMAL_DATA:
                    switch(next) {
                        case EXT_LENGTH:
                            ext_len = I2C_slaveWriteData;
                            ext_cnt = 0;
                            next = EXT_VALUE;
                            break;
                        case EXT_VALUE:
                            ext_buf[ext_cnt++] = (char)I2C_slaveWriteData;
                            if (ext_cnt >= ext_len) {
                                if (!PROTOCOL_Read_Lock()) PROTOCOL_EXT(&ext_buf[0]);
                                next = DEFAULT;
                            }
                            break;
                    }
                    break;
                default:
                    break;

            }

            slaveWriteType  = SLAVE_NORMAL_DATA;
            break;

        case I2C1_SLAVE_READ_REQUEST:
            switch (I2C_slaveWriteData)
            {
                case WHO_I2C:
                    SSP1BUF = PROTOCOL_I2C_WHO();
                    break;
                case SEN_I2C:
                    pdata = PROTOCOL_I2C_SEN();
                    SSP1BUF = *pdata;
                    break;
                case GET_I2C:
                    SSP1BUF = PROTOCOL_I2C_GET();
                    break;
            }
            break;

        case I2C1_SLAVE_READ_COMPLETED:
            break;
        default:
            break;

    }
}
