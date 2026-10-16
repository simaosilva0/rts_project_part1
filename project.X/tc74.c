#include "i2c.h"
#include "tc74.h"

#define TC74_RTR      0x00  /* temperature register */
#define TC74_RWCR     0x01  /* config register */
#define TC74_SHDN     0x80  /* RWCR: standby */
#define TC74_DATA_RDY 0x40  /* RWCR: conversion done */
#define TC74_READY_POLLS 8

uint8_t tc74_read(uint8_t *temperature)
{
    uint8_t poll;
    uint8_t reg;
    int8_t t;

    /* Do not wait forever if the first conversion is not ready.
     * A NACK (TC74 missing) fails the read too. */
    for (poll = 0; poll < TC74_READY_POLLS; poll++) {
        if (!i2c_read_reg(TC74_ADDR, TC74_RWCR, &reg))
            return 0;
        if (reg & TC74_DATA_RDY)
            break;
    }
    if (poll == TC74_READY_POLLS)
        return 0;

    if (!i2c_read_reg(TC74_ADDR, TC74_RTR, &reg))
        return 0;
    t = (int8_t)reg;
    if (t < 0)
        t = 0;
    if (t > 50)
        t = 50;
    *temperature = (uint8_t)t;
    return 1;
}

static void write_rwcr(uint8_t val)
{
    uint8_t buf[2];

    buf[0] = TC74_RWCR;
    buf[1] = val;
    i2c_write(TC74_ADDR, buf, 2);
}

void tc74_standby(void)
{
    write_rwcr(TC74_SHDN);
}

void tc74_wake(void)
{
    write_rwcr(0x00);
}
