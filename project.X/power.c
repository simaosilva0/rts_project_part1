#include <xc.h>
#include "system.h"
#include "power.h"

void power_init(void)
{
    VREGCONbits.VREGPM = 1;     /* regulator in low-power mode during sleep */
}

/* Anything the main loop still has to handle blocks sleep */
static uint8_t busy(void)
{
    return ticks || s1_pressed || s2_pressed;
}

void power_sleep_if_idle(void)
{
    INTCONbits.GIE = 0;         /* no ISR between the check and SLEEP */
    if (!busy()) {
        SLEEP();                /* any enabled interrupt flag wakes us */
        NOP();
    }
    INTCONbits.GIE = 1;         /* pending interrupt is serviced now */
}
