#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/* Register-level I2C master on MSSP1 (set up by MCC's I2C1_Initialize).
 * Addresses are 7-bit. Both return 1 if every byte was acked, 0 on NACK.
 * Blocking. Main loop only, never from an ISR. */
uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint8_t len);
uint8_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val);

#endif
