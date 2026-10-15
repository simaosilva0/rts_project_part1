#include "nvm.h"
#include "system.h"

static void reset_record(record_t *record, uint8_t temperature, uint8_t luminosity)
{
    record->h = 0;
    record->m = 0;
    record->s = 0;
    record->t = temperature;
    record->l = luminosity;
}

static void load_defaults(void)
{
    cfg.pmon = 5;
    cfg.tala = 3;
    cfg.alaf = 0;
    cfg.alah = 12;
    cfg.alam = 0;
    cfg.alas = 0;
    cfg.alat = 25;
    cfg.alal = 3;
}

void nvm_reset_records(void)
{
    reset_record(&records[REC_TMAX], 0, 0);
    reset_record(&records[REC_TMIN], 255, 255);
    reset_record(&records[REC_LMAX], 0, 0);
    reset_record(&records[REC_LMIN], 255, 255);
}

void nvm_load(void)
{
    load_defaults();
    nvm_reset_records();
}

void nvm_save_config(void)
{
}

void nvm_save_records(void)
{
}