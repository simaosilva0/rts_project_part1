/**
  Generated Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This is the main file generated using PIC10 / PIC12 / PIC16 / PIC18 MCUs

  Description:
    This header file provides implementations for driver APIs for all modules
  selected in the GUI. Generation Information : Product Revision  :  PIC10 /
  PIC12 / PIC16 / PIC18 MCUs - 1.81.8 Device            :  PIC16F18875 Driver
  Version    :  2.00
*/

/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries.
  
    Subject to your compliance with these terms, you may use Microchip software
   and any derivatives exclusively with Microchip products. It is your
   responsibility to comply with third party license terms applicable to your
   use of third party software (including open source software) that may
   accompany Microchip software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
    FOR A PARTICULAR PURPOSE.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
    SOFTWARE.
*/

#include "mcc_generated_files/mcc.h"

#include "buttons.h"
#include "clock.h"
#include "alarm_out.h"
#include "monitor.h"
#include "nvm.h"
#include "power.h"
#include "system.h"

clock_time_t now;
config_t     cfg;
record_t     records[REC_COUNT];
uint8_t      last_t, last_l;
uint8_t      alarms;
uint8_t      cfg_dirty;

volatile uint8_t ticks;
volatile uint8_t s1_pressed;
volatile uint8_t s2_pressed;

static void tick(void) { 
    ticks++;
}

/*
                         Main application
 */
void main(void) {
  uint8_t saved_clock_h;
  uint8_t saved_clock_m;

  // initialize the device
  SYSTEM_Initialize();

  nvm_load(&saved_clock_h, &saved_clock_m);
  clock_init(saved_clock_h, saved_clock_m);
  monitor_init();
  alarm_out_init();
  power_init();

  // Add handler registration
  TMR1_SetInterruptHandler(tick);
  
  buttons_init();

  // When using interrupts, you need to set the Global and Peripheral Interrupt
  // Enable bits Use the following macros to:

  // Enable the Global Interrupts
  INTERRUPT_GlobalInterruptEnable();

  // Enable the Peripheral Interrupts
  INTERRUPT_PeripheralInterruptEnable();

  // Disable the Global Interrupts
  // INTERRUPT_GlobalInterruptDisable();

  // Disable the Peripheral Interrupts
  // INTERRUPT_PeripheralInterruptDisable();

  lcd_init();

  while (1) {
    while (ticks) {
        ticks--;
        LED_5_Toggle();
        if (clock_tick())
          cfg_dirty = 1;
        monitor_tick();
        alarm_out_tick();
    }
    monitor_service();
    alarm_out_service();
    if (cfg_dirty) {
      nvm_save_config(now.h, now.m);
      cfg_dirty = 0;
    }
    if (s1_pressed) {
        s1_pressed = 0;
        alarm_out_clear();
      monitor_clear_alarm_latches();
    }
    if (s2_pressed) {
        s2_pressed = 0;
    }
    power_sleep_if_idle();
  }
}
/**
 End of File
*/