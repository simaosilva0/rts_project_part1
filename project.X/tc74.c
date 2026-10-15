#include "mcc_generated_files/examples/i2c1_master_example.h"
#include "tc74.h"

#define TC74_RTR      0x00  /* temperature register */
#define TC74_RWCR     0x01  /* config register */
#define TC74_SHDN     0x80  /* RWCR: standby */
#define TC74_DATA_RDY 0x40  /* RWCR: conversion done */
#define TC74_READY_POLLS 8

static uint8_t last_temperature;

uint8_t tc74_read(void)
{
    uint8_t poll;
    int8_t t;

    /* Do not wait forever if the first conversion is not ready. */
    for (poll = 0; poll < TC74_READY_POLLS; poll++) {
        if (I2C1_Read1ByteRegister(TC74_ADDR, TC74_RWCR) & TC74_DATA_RDY)
            break;
    }
    if (poll == TC74_READY_POLLS)
        return last_temperature;

    t = (int8_t)I2C1_Read1ByteRegister(TC74_ADDR, TC74_RTR);
    if (t < 0)
        t = 0;
    if (t > 50)
        t = 50;
    last_temperature = (uint8_t)t;
    return last_temperature;
}

void tc74_standby(void)
{
    I2C1_Write1ByteRegister(TC74_ADDR, TC74_RWCR, TC74_SHDN);
}

void tc74_wake(void)
{
    I2C1_Write1ByteRegister(TC74_ADDR, TC74_RWCR, 0x00);
}
