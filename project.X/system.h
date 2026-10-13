/* Microchip Technology Inc. and its subsidiaries.  You may use this software 
 * and any derivatives exclusively with Microchip products. 
 * 
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER 
 * EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED 
 * WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A 
 * PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION 
 * WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION. 
 *
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
 * INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
 * WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS 
 * BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE 
 * FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS 
 * IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF 
 * ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE 
 * TERMS. 
 */

/* 
 * File:   
 * Author: 
 * Comments:
 * Revision history: 
 */

// This is a guard condition so that contents of this file are not included
// more than once.  
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

typedef struct {
    uint8_t h, m, s;
} clock_time_t;

/* Config parameters, same order as EEPROM offsets 1-8 */
typedef struct {
    uint8_t pmon;   /* monitoring period (s), 0 = off */
    uint8_t tala;   /* alarm PWM duration (s) */
    uint8_t alaf;   /* alarms enabled (1) / disabled (0) */
    uint8_t alah;   /* alarm clock hh */
    uint8_t alam;   /* alarm clock mm */
    uint8_t alas;   /* alarm clock ss */
    uint8_t alat;   /* temperature threshold (C) */
    uint8_t alal;   /* luminosity threshold (0-7) */
} config_t;

/* Max/min record: 5 bytes, same as EEPROM */
typedef struct {
    uint8_t h, m, s, t, l;
} record_t;

enum { REC_TMAX, REC_TMIN, REC_LMAX, REC_LMIN, REC_COUNT };

/* Pending alarm bits */
#define ALARM_C 0x01    /* clock */
#define ALARM_T 0x02    /* temperature */
#define ALARM_L 0x04    /* luminosity */

/* Shared data */
extern clock_time_t now;
extern config_t     cfg;
extern record_t     records[REC_COUNT];
extern uint8_t      last_t, last_l;
extern uint8_t      alarms;         /* ALARM_C | ALARM_T | ALARM_L */
extern uint8_t      cfg_dirty;      /* set by UI, main loop saves to EEPROM */

/* Set by ISRs, consumed by the main loop */
extern volatile uint8_t ticks;      /* +1 per second from Timer1 */
extern volatile uint8_t s1_pressed;
extern volatile uint8_t s2_pressed;

#endif /* SYSTEM_H */

