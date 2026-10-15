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

static void check_sensor_alarms(uint8_t temperature, uint8_t luminosity)
{
    if (!cfg.alaf || temperature <= cfg.alat) {
        temperature_latched = 0;
    } else if (!temperature_latched) {
        alarms |= ALARM_T;
        temperature_latched = 1;
    }

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
}

void monitor_tick(void)
{
    check_clock_alarm();

    if (cfg.pmon == 0) {
        monitor_elapsed = 0;
        sensor_due = 0;
        return;
    }

    if (++monitor_elapsed >= cfg.pmon) {
        monitor_elapsed = 0;
        sensor_due = 1;
    }
}

void monitor_service(void)
{
    uint8_t temperature;
    uint8_t luminosity;

    if (!sensor_due)
        return;

    sensor_due = 0;
    temperature = tc74_read();
    luminosity = adc_read_level();
    last_t = temperature;
    last_l = luminosity;

    if (update_records(temperature, luminosity))
        nvm_save_records();

    check_sensor_alarms(temperature, luminosity);
}