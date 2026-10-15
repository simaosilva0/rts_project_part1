#include "nvm.h"
#include "mcc_generated_files/memory.h"
#include "system.h"

#define NVM_BASE        0xF000u
#define NVM_MAGIC       0u
#define NVM_PMON        1u
#define NVM_TALA        2u
#define NVM_ALAF        3u
#define NVM_ALAH        4u
#define NVM_ALAM        5u
#define NVM_ALAS        6u
#define NVM_ALAT        7u
#define NVM_ALAL        8u
#define NVM_CLKH        9u
#define NVM_CLKM        10u
#define NVM_CHECKSUM    11u
#define NVM_RECORDS     16u
#define NVM_MAGIC_WORD  0xA5u

static uint8_t read_byte(uint8_t offset)
{
    return DATAEE_ReadByte(NVM_BASE + offset);
}

static void write_byte(uint8_t offset, uint8_t value)
{
    DATAEE_WriteByte(NVM_BASE + offset, value);
}

static uint8_t config_checksum(uint8_t clock_h, uint8_t clock_m)
{
    uint8_t checksum = NVM_MAGIC_WORD;

    checksum += cfg.pmon;
    checksum += cfg.tala;
    checksum += cfg.alaf;
    checksum += cfg.alah;
    checksum += cfg.alam;
    checksum += cfg.alas;
    checksum += cfg.alat;
    checksum += cfg.alal;
    checksum += clock_h;
    checksum += clock_m;
    return checksum;
}

static uint8_t config_is_valid(uint8_t clock_h, uint8_t clock_m)
{
    return read_byte(NVM_MAGIC) == NVM_MAGIC_WORD &&
           read_byte(NVM_CHECKSUM) == config_checksum(clock_h, clock_m) &&
           cfg.alaf <= 1 && cfg.alah < 24 && cfg.alam < 60 &&
           cfg.alas < 60 && cfg.alat <= 50 && cfg.alal <= 7 &&
           clock_h < 24 && clock_m < 60;
}

static uint8_t record_is_valid(uint8_t index)
{
    record_t *record = &records[index];
    uint8_t reset_record = record->t == 255 && record->l == 255;

    if (record->h >= 24 || record->m >= 60 || record->s >= 60)
        return 0;
    if (reset_record)
        return index == REC_TMIN || index == REC_LMIN;
    return record->t <= 50 && record->l <= 7;
}

static void load_records(void)
{
    uint8_t index;
    uint8_t offset;

    for (index = 0; index < REC_COUNT; index++) {
        offset = NVM_RECORDS + (uint8_t)(index * sizeof(record_t));
        records[index].h = read_byte(offset);
        records[index].m = read_byte(offset + 1);
        records[index].s = read_byte(offset + 2);
        records[index].t = read_byte(offset + 3);
        records[index].l = read_byte(offset + 4);
        if (!record_is_valid(index)) {
            nvm_reset_records();
            return;
        }
    }
}

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

void nvm_load(uint8_t *clock_h, uint8_t *clock_m)
{
    uint8_t stored_clock_h = read_byte(NVM_CLKH);
    uint8_t stored_clock_m = read_byte(NVM_CLKM);

    load_defaults();
    nvm_reset_records();

    cfg.pmon = read_byte(NVM_PMON);
    cfg.tala = read_byte(NVM_TALA);
    cfg.alaf = read_byte(NVM_ALAF);
    cfg.alah = read_byte(NVM_ALAH);
    cfg.alam = read_byte(NVM_ALAM);
    cfg.alas = read_byte(NVM_ALAS);
    cfg.alat = read_byte(NVM_ALAT);
    cfg.alal = read_byte(NVM_ALAL);

    if (!config_is_valid(stored_clock_h, stored_clock_m)) {
        load_defaults();
        *clock_h = 0;
        *clock_m = 0;
        return;
    }

    *clock_h = stored_clock_h;
    *clock_m = stored_clock_m;
    load_records();
}

void nvm_save_config(uint8_t clock_h, uint8_t clock_m)
{
    uint8_t checksum = config_checksum(clock_h, clock_m);

    /* Invalidate first, then publish the magic word only after all bytes exist. */
    write_byte(NVM_MAGIC, 0);
    write_byte(NVM_PMON, cfg.pmon);
    write_byte(NVM_TALA, cfg.tala);
    write_byte(NVM_ALAF, cfg.alaf);
    write_byte(NVM_ALAH, cfg.alah);
    write_byte(NVM_ALAM, cfg.alam);
    write_byte(NVM_ALAS, cfg.alas);
    write_byte(NVM_ALAT, cfg.alat);
    write_byte(NVM_ALAL, cfg.alal);
    write_byte(NVM_CLKH, clock_h);
    write_byte(NVM_CLKM, clock_m);
    write_byte(NVM_CHECKSUM, checksum);
    write_byte(NVM_MAGIC, NVM_MAGIC_WORD);
}

void nvm_save_records(void)
{
    uint8_t index;
    uint8_t offset;

    for (index = 0; index < REC_COUNT; index++) {
        offset = NVM_RECORDS + (uint8_t)(index * sizeof(record_t));
        write_byte(offset, records[index].h);
        write_byte(offset + 1, records[index].m);
        write_byte(offset + 2, records[index].s);
        write_byte(offset + 3, records[index].t);
        write_byte(offset + 4, records[index].l);
    }
}