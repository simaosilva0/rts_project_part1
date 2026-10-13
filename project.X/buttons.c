#include "mcc_generated_files/mcc.h"
#include "system.h"
#include "buttons.h"

/*
 * Debounce: after an accepted press both buttons are locked out.
 * Timer0 fires every 40 ms; unlock after STABLE_PERIODS periods
 * in a row with both buttons released (pins high).
 * All of this runs in interrupt context only.
 */
#define STABLE_PERIODS 2

static uint8_t locked;
static uint8_t stable;

static void lock(void)
{
    locked = 1;
    stable = 0;
    TMR0_WriteTimer(0);         /* full first period */
    TMR0_StartTimer();
}

static void s1_isr(void)
{
    if (!locked) {
        s1_pressed = 1;
        lock();
    }
}

static void s2_isr(void)
{
    if (!locked) {
        s2_pressed = 1;
        lock();
    }
}

static void tmr0_isr(void)
{
    if (!SW1_GetValue() || !SW2_GetValue()) {
        stable = 0;             /* still held or bouncing */
    } else if (++stable >= STABLE_PERIODS) {
        TMR0_StopTimer();
        locked = 0;
    }
}

void buttons_init(void)
{
    IOCBF4_SetInterruptHandler(s1_isr);
    IOCCF5_SetInterruptHandler(s2_isr);
    TMR0_SetInterruptHandler(tmr0_isr);
}
