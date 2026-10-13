#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

void    clock_init(uint8_t h, uint8_t m);
void    clock_set(uint8_t h, uint8_t m, uint8_t s);
uint8_t clock_tick(void);   /* +1 s, returns 1 when the minute changed */

#endif	/* CLOCK_H */

