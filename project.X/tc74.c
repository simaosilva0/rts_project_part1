#include "mcc_generated_files/examples/i2c1_master_example.h"
#include "tc74.h"

#define TC74_RTR      0x00  /* temperature register */
#define TC74_RWCR     0x01  /* config register */
#define TC74_SHDN     0x80  /* RWCR: standby */
#define TC74_DATA_RDY 0x40  /* RWCR: conversion done */

uint8_t tc74_read(void)
{
    int8_t t;

    /* after power-up or wake the first conversion takes up to ~250 ms */
    while (!(I2C1_Read1ByteRegister(TC74_ADDR, TC74_RWCR) & TC74_DATA_RDY))
        ;
    t = (int8_t)I2C1_Read1ByteRegister(TC74_ADDR, TC74_RTR);
    if (t < 0)
        return 0;
    if (t > 50)
        return 50;
    return (uint8_t)t;
}

void tc74_standby(void)
{
    I2C1_Write1ByteRegister(TC74_ADDR, TC74_RWCR, TC74_SHDN);
}

void tc74_wake(void)
{
    I2C1_Write1ByteRegister(TC74_ADDR, TC74_RWCR, 0x00);
}
