#ifndef TC74_H
#define TC74_H

#include <stdint.h>

#define TC74_ADDR 0x4D      /* TC74A5 */

uint8_t tc74_read(uint8_t *temperature); /* 1 if valid, 0 if not ready */
void    tc74_standby(void);
void    tc74_wake(void);    /* first reading valid ~250 ms later */

#endif
