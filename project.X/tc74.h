#ifndef TC74_H
#define TC74_H

#include <stdint.h>

#define TC74_ADDR 0x4D      /* TC74A5 */

uint8_t tc74_read(uint8_t *temperature); /* 1 if valid, 0 if not ready */
/* Foreground-only power control; do not call from an ISR or SLEEP path. */
void    tc74_standby(void); /* stop TC74 conversions */
void    tc74_wake(void);    /* first valid reading may take ~250 ms */

#endif
