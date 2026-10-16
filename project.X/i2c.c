#include "mcc_generated_files/mcc.h"
#include "i2c.h"

/* The MCC driver path (example helper -> MasterOperation -> Poller -> FSM ->
 * state function -> callback) is ~8 calls deep, which overflowed the
 * 16-level hardware stack once an interrupt landed on top of it.
 * These functions drive the MSSP registers directly, with no nested calls. */

/* Wait for the MSSP to finish the current step (start, byte, ack or stop) */
#define I2C_WAIT() do { while (!PIR3bits.SSP1IF); PIR3bits.SSP1IF = 0; } while (0)

/* MSSP on for the transfer only, off between transfers */
#define I2C_BEGIN() do { SSP1CON1bits.SSPEN = 1; PIR3bits.SSP1IF = 0; \
                         SSP1CON2bits.SEN = 1; I2C_WAIT(); } while (0)
#define I2C_END()   do { SSP1CON2bits.PEN = 1; I2C_WAIT(); \
                         SSP1CON1bits.SSPEN = 0; } while (0)

uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint8_t len)
{
    uint8_t i;
    uint8_t ok;

    I2C_BEGIN();
    SSP1BUF = (uint8_t)(addr << 1);             /* address + write */
    I2C_WAIT();
    for (i = 0; i < len && !SSP1CON2bits.ACKSTAT; i++) {
        SSP1BUF = data[i];
        I2C_WAIT();
    }
    ok = (uint8_t)!SSP1CON2bits.ACKSTAT;
    I2C_END();
    return ok;
}

uint8_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val)
{
    uint8_t ok = 0;

    I2C_BEGIN();
    SSP1BUF = (uint8_t)(addr << 1);             /* address + write */
    I2C_WAIT();
    if (!SSP1CON2bits.ACKSTAT) {
        SSP1BUF = reg;                          /* register pointer */
        I2C_WAIT();
        if (!SSP1CON2bits.ACKSTAT) {
            SSP1CON2bits.RSEN = 1;              /* repeated start */
            I2C_WAIT();
            SSP1BUF = (uint8_t)((addr << 1) | 1);   /* address + read */
            I2C_WAIT();
            if (!SSP1CON2bits.ACKSTAT) {
                SSP1CON2bits.RCEN = 1;          /* receive one byte */
                I2C_WAIT();
                *val = SSP1BUF;
                SSP1CON2bits.ACKDT = 1;         /* NACK: last byte */
                SSP1CON2bits.ACKEN = 1;
                I2C_WAIT();
                ok = 1;
            }
        }
    }
    I2C_END();
    return ok;
}
