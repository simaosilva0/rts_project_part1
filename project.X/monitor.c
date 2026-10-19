#include <stdint.h>

#include "adc.h"
#include "nvm.h"
#include "system.h"
#include "tc74.h"
#include "monitor.h"

static uint8_t monitor_elapsed;
static uint8_t sensor_due;
static uint8_t clock_latched;
static uint8_t temperature_latched;
static uint8_t luminosity_latched;
static uint8_t sample_valid;
static uint8_t temperature_valid;
static uint8_t tc74_asleep;

static void record_now(record_t *record, uint8_t temperature, uint8_t luminosity)
{
    record->h = now.h;
    record->m = now.m;
    record->s = now.s;
    record->t = temperature;
    record->l = luminosity;
}

static uint8_t update_records(uint8_t temperature, uint8_t luminosity)
{
    uint8_t changed = 0;

    if (temperature > records[REC_TMAX].t) {
        record_now(&records[REC_TMAX], temperature, luminosity);
        changed = 1;
    }
    if (temperature < records[REC_TMIN].t) {
        record_now(&records[REC_TMIN], temperature, luminosity);
        changed = 1;
    }
    if (luminosity > records[REC_LMAX].l) {
        record_now(&records[REC_LMAX], temperature, luminosity);
        changed = 1;
    }
    if (luminosity < records[REC_LMIN].l) {
        record_now(&records[REC_LMIN], temperature, luminosity);
        changed = 1;
    }

    return changed;
}

static void check_clock_alarm(void)
{
    uint8_t matches = now.h == cfg.alah &&
                      now.m == cfg.alam &&
                      now.s == cfg.alas;

    if (!matches) {
        clock_latched = 0;
    } else if (cfg.alaf && !clock_latched) {
        alarms |= ALARM_C;
        clock_latched = 1;
    }
}

static void check_temperature_alarm(uint8_t temperature)
{
    if (!cfg.alaf || temperature <= cfg.alat) {
        temperature_latched = 0;
    } else if (!temperature_latched) {
        alarms |= ALARM_T;
        temperature_latched = 1;
    }
}

static void check_luminosity_alarm(uint8_t luminosity)
{
    if (!cfg.alaf || luminosity >= cfg.alal) {
        luminosity_latched = 0;
    } else if (!luminosity_latched) {
        alarms |= ALARM_L;
        luminosity_latched = 1;
    }
}

void monitor_init(void)
{
    monitor_elapsed = 0;
    sensor_due = 0;
    clock_latched = 0;
    temperature_latched = 0;
    luminosity_latched = 0;
    sample_valid = 0;
    temperature_valid = 0;
    tc74_asleep = 0;
}

void monitor_clear_alarm_latches(void)
{
    clock_latched = 0;
    temperature_latched = 0;
    luminosity_latched = 0;
}

uint8_t monitor_sample_valid(void)
{
    return sample_valid;
}

uint8_t monitor_temperature_valid(void)
{
    return temperature_valid;
}

void monitor_tick(void)
{
    check_clock_alarm();

    if (cfg.pmon == 0) {
        monitor_elapsed = 0;
        sensor_due = 0;
        if (!tc74_asleep) {
            tc74_standby();
            tc74_asleep = 1;
        }
        return;
    }

    if (cfg.pmon == 1 && tc74_asleep) {
        tc74_wake();
        tc74_asleep = 0;
        monitor_elapsed = 0;
        sensor_due = 0;
        return;
    }

    if (++monitor_elapsed >= cfg.pmon) {
        monitor_elapsed = 0;
        sensor_due = 1;
    } else if (tc74_asleep && monitor_elapsed == cfg.pmon - 1) {
        tc74_wake();
        tc74_asleep = 0;
    }
}

void monitor_service(void)
{
    uint8_t temperature;
    uint8_t luminosity;

    if (!sensor_due)
        return;

    sensor_due = 0;
    if (tc74_asleep) {
        tc74_wake();
        tc74_asleep = 0;
        return;
    }
    /* The potentiometer is read even if the TC74 does not answer,
     * so luminosity and its alarm keep working without the sensor. */
    luminosity = adc_read_level();
    last_l = luminosity;
    sample_valid = 1;
    check_luminosity_alarm(luminosity);

    /* Records hold both values, so they need a valid temperature */
    temperature_valid = tc74_read(&temperature);
    if (temperature_valid) {
        last_t = temperature;
        check_temperature_alarm(temperature);
        if (update_records(temperature, luminosity))
            nvm_save_records();
    }

    if (cfg.pmon > 1 && !tc74_asleep) {
        tc74_standby();
        tc74_asleep = 1;
    }
}