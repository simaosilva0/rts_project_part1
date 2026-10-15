#include <stdint.h>
#include <xc.h>

#include "alarm_out.h"
#include "mcc_generated_files/pin_manager.h"
#include "mcc_generated_files/pwm6.h"
#include "mcc_generated_files/tmr2.h"
#include "monitor.h"
#include "system.h"

#define ALARM_DUTY 511u

static uint8_t pwm_remaining;
static uint8_t seen_alarms;

static void update_status_leds(void)
{
    if (!monitor_sample_valid()) {
        LED_3_SetLow();
        LED_4_SetLow();
        return;
    }

    if (last_l < cfg.alal)
        LED_3_SetHigh();
    else
        LED_3_SetLow();

    if (last_t > cfg.alat)
        LED_4_SetHigh();
    else
        LED_4_SetLow();
}

static void pwm_stop(void)
{
    PWM6CONbits.PWM6EN = 0;
    TMR2_Stop();
}

static void pwm_start(void)
{
    PWM6_LoadDutyValue(ALARM_DUTY);
    TMR2_Start();
    PWM6CONbits.PWM6EN = 1;
}

void alarm_out_init(void)
{
    pwm_remaining = 0;
    seen_alarms = alarms;
    pwm_stop();
    update_status_leds();
}

void alarm_out_tick(void)
{
    if (pwm_remaining == 0)
        return;

    pwm_remaining--;
    if (pwm_remaining == 0)
        pwm_stop();
}

void alarm_out_service(void)
{
    uint8_t new_alarms = alarms & (uint8_t)~seen_alarms;

    seen_alarms = alarms;
    update_status_leds();

    if (!cfg.alaf || cfg.tala == 0) {
        pwm_remaining = 0;
        pwm_stop();
        return;
    }

    if (new_alarms && pwm_remaining == 0) {
        pwm_remaining = cfg.tala;
        pwm_start();
    }
}

void alarm_out_clear(void)
{
    alarms = 0;
    seen_alarms = 0;
}