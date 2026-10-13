/*
 * File:   clock.c
 * Author: simaosilva
 *
 * Created on October 13, 2026, 12:08 PM
 */


#include "system.h"
#include "clock.h"

void clock_init(uint8_t h, uint8_t m)
{
    clock_set(h, m, 0);
}

void clock_set(uint8_t h, uint8_t m, uint8_t s)
{
    now.h = h;
    now.m = m;
    now.s = s;
}

uint8_t clock_tick(void)
{
    if (++now.s < 60)
        return 0;
    now.s = 0;
    if (++now.m >= 60) {
        now.m = 0;
        if (++now.h >= 24)
            now.h = 0;
    }
    return 1;
}