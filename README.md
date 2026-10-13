# RTS Lab Project - Part 1

Monitoring and Alarm System on the Curiosity HPC board (PIC16F18875), MPLAB X + XC8.

Group: 2
Members: Simão Silva (110486), Simão Silveira (110049), Isam Abbasi (122334)

Deadline: 21/10/2026. Deliver a ZIP with sources, `pic.hex` and the report (2-3 pages).


## What the system does

- Clock (hh:mm:ss) from Timer1, interrupt every 1 s. Has to keep running during sleep.
- Reads temperature (TC74 over I2C) and luminosity (potentiometer, ADC, levels 0-7) every PMON seconds. PMON = 0 means no readings.
- Keeps max/min records for T and L (h, m, s, T, L = 5 bytes each) in EEPROM.
- Alarms: clock reaches alarm time (C), T > ALAT (T), L < ALAL (L). Shows the letter on the LCD and runs PWM on LED D2 for TALA seconds. Each alarm fires once until S1 is pressed.
- Config parameters + CLKH/CLKM saved at the start of EEPROM with magic word 0xA5 and a checksum. Defaults used if invalid.
- UI with S1 (RB4), S2 (RC5), LEDs D2-D5 (RA4-RA7) and a 16x2 LCD.
- Sleep whenever there is nothing to do.


## Architecture

Cyclic executive. ISRs only update counters and set flags, everything else runs in the main loop.

```
ISR:
  Timer1 (1 s)   -> update hh:mm:ss, set FLAG_TICK
  IOC RB4/RC5    -> set FLAG_S1 / FLAG_S2

main loop:
  FLAG_TICK      -> blink D5, check alarm clock, count PMON and TALA, refresh LCD
  PMON elapsed   -> read TC74 + ADC, update max/min, check T/L alarms
  FLAG_S1/S2     -> debounce, UI state machine
  config changed -> save to EEPROM
  nothing to do and PWM off -> SLEEP()
```

Things to decide early:

- Timer1 source: LFINTOSC (~31 kHz internal; the board has no 32.768 kHz crystal). Async mode so it counts in sleep.
- LCD: 16x2 HD44780 with a PCF8574 I2C backpack. It shares the I2C bus (RC3/RC4) with the TC74, so it needs no extra pins. Address 0x27 (PCF8574T) or 0x3F (PCF8574AT), check the chip.
- PWM output mapped to RA4 with PPS.
- PWM uses Timer2. If Timer2 runs from Fosc it stops in sleep, so either don't sleep while TALA is active or clock Timer2 from LFINTOSC.
- Luminosity: `level = adc >> 7` (10-bit ADC).
- USART at 9600 baud comes in a future version (~1 ms per byte). It's not configured now, but keep all tasks short and non-blocking.

EEPROM layout (addresses are offsets: the MCC functions `DATAEE_WriteByte/ReadByte` take the full address, `0xF000 + offset`):

| Address | Content |
|---|---|
| 0 | Magic word 0xA5 |
| 1 | PMON |
| 2 | TALA |
| 3 | ALAF |
| 4-6 | ALAH, ALAM, ALAS |
| 7 | ALAT |
| 8 | ALAL |
| 9-10 | CLKH, CLKM |
| 11 | Checksum |
| 16-35 | Records: Tmax, Tmin, Lmax, Lmin (5 bytes each) |

Note: saving CLKM every minute is about 1440 writes/day on the same bytes. At ~100k cycles that is around 70 days. Mention it in the report or do simple wear leveling.

Defaults: PMON 5, TALA 3, ALAF 0, ALAH 12, ALAM 0, ALAS 0, ALAT 25, ALAL 3, CLKH 0, CLKM 0.


## MCC

Peripheral drivers are generated with MCC Classic into `project.X/mcc_generated_files/`.

- Commit both the generated files and the MCC config file (`.mc3`), so anyone can open the project and regenerate.
- Only A edits the MCC config and regenerates. If you need a peripheral changed, ask A. That way the generated files never conflict in merges.
- Never hand-edit generated files. Your code goes in our own modules and calls the MCC APIs. In `main.c`, write code only outside the generated init calls.

Configured in MCC:

- Timer1: 1 s interrupt, LFINTOSC, async (runs in sleep)
- Timer2 + PWM on RA4 through PPS (LED D2)
- MSSP I2C host (TC74)
- ADC (potentiometer)
- NVM (EEPROM)
- IOC on RB4 (S1) and RC5 (S2)
- Pins: LEDs RA4-RA7 (the LCD is on the I2C bus, no pins of its own)


## Shared header

`system.h` is written together on day 1 and everyone codes against it. It has the config struct, current time, last T/L values, pending alarm flags (C, T, L) and the 4 records. Don't change it without telling the others.


## Who does what

### A - Core, clock, power (also does integration)

- `clock.c`: Timer1 1 s interrupt, hh:mm:ss rollover, `clock_set()` without stopping the timer.
- MCC configuration and regeneration.
- `main.c`: main loop, flags, interrupt setup, IOC on RB4/RC5.
- `power.c`: SLEEP() logic, what blocks sleep.
- Keep the design ready for USART (future version, not configured now): tasks short and non-blocking.
- Measure task times (toggle a spare pin, check with scope/logic analyser).

### B - Sensors, EEPROM, alarms

- `tc74.c` on top of the MCC I2C host driver: read temperature, check address (TC74A5 = 0x4D). Optional: standby between readings (SHDN bit).
- `adc.c`: potentiometer -> level 0-7, using the MCC ADC driver.
- `monitor.c`: periodic reading, max/min records with timestamp, alarm detection.
- `nvm.c`: EEPROM byte read/write through the MCC NVM driver, load/save config block with magic word and checksum.
- `alarm_out.c`: LEDs D3/D4 and PWM on D2 for TALA seconds. (Moved here from C to balance the work.)

### C - User interface

- `lcd.c`: HD44780 driver (init, goto, print, cursor on/off) over the PCF8574 I2C backpack, in 4-bit mode, using the MCC I2C driver. Hand-written (MCC has none). Shares the bus with the TC74, so don't call it from an ISR.
- `buttons.c`: debounce in main loop (~20-50 ms), starting from the MCC IOC callbacks on RB4/RC5.
- `ui.c`: state machine:
  - NORMAL, S1: clear CTL, show thresholds for 2 s. S1 again within 2 s -> CONFIG.
  - CONFIG, S1 moves: clk-h, clk-m, clk-s, C, T, L, A/a, R, back to NORMAL. S2 increments with wrap, or selects on C/T/L/R.
  - On C/T/L show the threshold. S2 enters edit for that threshold.
  - NORMAL, S2: show T max/min (1 s), then L max/min (1 s), back to NORMAL.
- Flag config changes so they get saved.

LCD layout:

```
hh:mm:ss CTL AR
tt C L l
```

Ranges: hh 0-23, mm 0-59, ss 0-59, tt 0-50, l 0-7.


## Schedule

| Dates | What |
|---|---|
| Oct 6-7 | Setup MPLAB X/XC8, MCC project generated by A, blink LED, write `system.h`, wire TC74 and LCD |
| Oct 8-12 | Each module tested alone with its own test main |
| Oct 13-15 | Integration (hard internal deadline Oct 15) |
| Oct 16-17 | Sleep, EEPROM recovery, timing measurements |
| Oct 18-19 | Full test list, bug fixing |
| Oct 20 | Report |
| Oct 21 | Build `pic.hex`, ZIP, deliver |

Check-in every 2-3 days.


## Test list

- [ ] Clock accurate over 10+ min, also while sleeping and while pressing buttons
- [ ] Setting the clock doesn't stall the seconds
- [ ] PMON = 0 stops readings, other values give the right period
- [ ] Each alarm fires once, shows its letter, PWM lasts TALA, cleared by S1
- [ ] Alarms disabled -> no notifications
- [ ] 2 s threshold view works, double S1 enters config
- [ ] All fields wrap correctly (23->00, 59->00, 50->00, 7->0)
- [ ] R resets max/min records
- [ ] S2 record display: T then L, 1 s each
- [ ] Power cycle restores params and hh:mm, bad checksum -> defaults
- [ ] Current draw lower with sleep than without (write the numbers down for the report)


## Report (2-3 pages)

Each person writes their part, A puts it together.

- Architecture and cyclic executive diagram (A)
- Tasks, periods, execution times (A)
- Interrupts used and why (A)
- Energy saving and trade-offs (A)
- EEPROM layout and consistency check, sensor timing (B)
- UI state machine diagram, alarm handling (C)
- How USART at 9600 baud fits in later (A)


## Rules

- No branches: commit to master, and only code that builds.
- Don't commit build folders (`build/`, `dist/`, `nbproject/private/`). Do commit `mcc_generated_files/` and the `.mc3` file.
- Keep ISRs short. No delays inside ISRs.