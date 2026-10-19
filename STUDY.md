# STUDY - what we did and why

The project's technical decisions and the reasons behind them, by topic.

1. Architecture: the cyclic executive
2. Board and wiring
3. MCC and the system clock
4. Clock: Timer1 and `clock.c`
5. Buttons and debouncing
6. I2C, TC74 and the LCD
7. ADC: luminosity
8. Monitoring, records and alarms
9. EEPROM
10. PWM alarm light
11. User interface
12. Power saving
13. Hardware stack depth
14. USART readiness

## 1. Architecture: the cyclic executive

No operating system. One infinite loop in `main.c` runs every task in turn. Interrupts only count and set flags; all real work happens in the loop.

```
ISRs:
  Timer1 (1 s)        ticks++
  IOC S1/S2           s1_pressed / s2_pressed = 1, start debounce lockout
  Timer0 (40 ms)      end the debounce lockout

main loop:
  while (ticks)       toggle D5, clock_tick(), monitor_tick(), alarm_out_tick()
  monitor_service()   read sensors when PMON is due, records, T/L alarms
  alarm_out_service() LEDs D3/D4, start PWM on a new alarm
  cfg_dirty           save config + hh:mm to EEPROM
  ui_task()           buttons, state machine, LCD
  power_sleep_if_idle()
```

- **Why ISRs only set flags:** short ISRs keep interrupt latency low, and only the main loop touches shared data, so there are no torn reads (e.g. reading hh:mm:ss while the ISR changes it).
- **`ticks` is a counter, not a flag:** if the loop is busy (EEPROM, LCD), seconds pile up and the loop catches up. No second is lost.
- **`system.h`** is the shared contract: `now`, `cfg`, `records`, `last_t`/`last_l`, `alarms`, `cfg_dirty`, and the ISR flags. Only declarations (`extern`); the variables are defined once in `main.c`.
  - `uint8_t` everywhere: 8-bit CPU, every value fits, byte operations are single instructions (atomic).
  - One byte per ISR flag: bits in a shared byte would need read-modify-write in the loop, and an ISR in between would be lost.
  - `volatile` on everything written by an ISR, or the compiler may keep a stale copy in a register.
- `cfg` has the same field order as the EEPROM offsets 1-8.

## 2. Board and wiring

Curiosity HPC (DM164136), PIC16F18875, all at **5 V**.

| What           | Connection                                         |
| -------------- | -------------------------------------------------- |
| LEDs D2-D5     | RA4-RA7, 1 kΩ each                                 |
| S1 / S2        | RB4 / RC5, active low, external 10 kΩ pull-ups     |
| Potentiometer  | RA0 (ANA0)                                         |
| I2C (TC74, LCD) | SCL = RC3, SDA = RC4 (mikroBUS 1), no board pull-ups |
| UART (future)  | TX = RC6, RX = RC7                                 |
| 32.768 kHz crystal | footprint Y2, **not mounted**                  |

- Why 5 V: the LCD needs it for contrast, and its backpack's pull-ups go to its VCC, which would push the I2C lines above a 3.3 V PIC's limit. PIC (1.8-5.5 V) and TC74 (2.7-5.5 V) both accept 5 V.
- Unused pins stay analog (MCC default): no digital input buffer, so floating pins draw no current.

## 3. MCC and the system clock

- **MCC Classic** generates the peripheral init code and drivers into `mcc_generated_files/`. We never edit those files (Generate overwrites them); our modules register callbacks (`TMR1_SetInterruptHandler()`, `IOCBF4_SetInterruptHandler()`, ...) and call the drivers.
- **Interrupt path:** a flag is set → `INTERRUPT_InterruptManager()` checks enable + flag → module ISR clears the flag → calls our callback. GIE and PEIE are enabled in `main.c` after all init, or no ISR runs.
- **Fosc = HFINTOSC 4 MHz:** 1 µs per instruction, low active current, enough for I2C 100 kHz, ADC and a future UART at 9600. If Fosc changes, recheck I2C (`SSP1ADD`), ADC (TAD) and UART baud.
- **Config bits:** watchdog off, LVP on (needed by the onboard programmer), `STVREN = ON` (stack overflow → reset, section 13).

## 4. Clock: Timer1 and `clock.c`

- **Timer1 on LFINTOSC (~31 kHz), asynchronous:**
  - SOSC would need the 32.768 kHz crystal, which isn't mounted.
  - LFINTOSC keeps running in sleep. With synchronization off, the counter doesn't depend on the CPU clock, so it counts during `SLEEP()` and its overflow wakes the CPU.
  - Preload `0x86E8`: 65536 − 34536 = **31000 counts = 1 s**. If the clock runs slow, raise the preload; if fast, lower it.
- **Accuracy is the trade-off:** LFINTOSC can be off by a few % (1 % ≈ 14 min/day). Measure the drift against a stopwatch and report it. Also, the MCC ISR stops the timer, reloads it and restarts it, so any interrupt latency is lost time; that's why EEPROM writes keep interrupts off as briefly as possible (section 9).
- **`clock.c`** is pure logic, no hardware:
  - `clock_tick()` adds 1 s with rollover (s → m → h → 0) and returns 1 when the minute changes, so the main loop saves hh:mm to EEPROM.
  - `clock_set()` only writes the variables and **never touches Timer1**, so setting the clock doesn't stall the seconds (a requirement).
  - `clock_init(h, m)` starts at the saved hh:mm with seconds = 0.
  - `>=` in the rollovers: a corrupt value still returns to range.

## 5. Buttons and debouncing

- **IOC on the falling edge** of RB4/RC5: a press interrupts (and wakes from sleep), no polling.
- **Bounce:** a contact bounces for a few ms on press **and release**, each bounce a new edge.
- **Options:** a delay in the ISR blocks all interrupts (no); `__delay_ms()` in the loop blocks the loop (bad for USART); an RC filter needs parts we don't have. **Chosen: a non-blocking lockout with Timer0.**
- **How it works (`buttons.c`, all in ISRs):**
  - A press sets `s1_pressed`/`s2_pressed`, locks both buttons and starts Timer0.
  - Every 40 ms: if a pin is low, the stable count resets; after **2 periods in a row** with both high, Timer0 stops and the buttons unlock. Two periods, because the release also bounces.
- **Timer0:** LFINTOSC async (runs in sleep), 8-bit, 1:8, `TMR0H = 154` → ≈ 40 ms. Off except during a lockout.

## 6. I2C, TC74 and the LCD

- **I2C:** 2 wires, open-drain, the PIC is master. TC74A5 at **0x4D**, LCD backpack at **0x27** (0x3F for a PCF8574AT). 100 kHz (TC74 maximum), `SSP1ADD = 9`.
- **Pull-ups:** none on the board or TC74. The PIC's weak pull-ups on RC3/RC4 plus the LCD backpack's resistors pull the lines high. If reads fail, try a lower bus speed.
- **`i2c.c` replaces the MCC I2C driver.** The MCC path is ~8 calls deep and overflowed the 16-level hardware stack (section 13). `i2c.c` drives the MSSP registers directly with no nested calls:
  - `i2c_write(addr, data, len)` and `i2c_read_reg(addr, reg, &val)` (repeated start, NACK on the last byte). Both return 0 on a NACK.
  - **Polled:** a TC74 read is ~0.5 ms every PMON s; interrupts still run during the waits.
  - MSSP on only during a transfer.
  - **Limit:** no timeout. A missing device NACKs and is handled, but a line stuck low freezes the main loop.
- **TC74 (`tc74.c`):**
  - `tc74_read()` polls the data-ready bit a bounded number of times, then reads the temperature (signed °C, clamped to 0-50). Returns 0 on NACK or not ready.
  - **Standby (optional in the spec):** for PMON > 1 the TC74 is woken 1 s before a reading (first conversion takes ~250 ms) and put back in standby after it. PMON = 1 keeps it awake; PMON = 0 keeps it in standby.
- **LCD (`lcd.c`):** HD44780 16x2 behind a PCF8574 I2C expander, so it uses 4-bit mode and no extra pins. Each nibble is sent with E high then E low, so a character is 4 I2C bytes (~0.5 ms). Shares the bus with the TC74; both are only used from the main loop, so they never overlap. The blue potentiometer is the contrast.

## 7. ADC: luminosity

- 10-bit ADCC on RA0, basic mode, VDD/VSS references (the pot is also across VDD/GND, so supply variation cancels out).
- `level = adc >> 7` → 0-7 (128 counts per level). Near a boundary the level can flicker; hysteresis or averaging would fix it.
- Clock FOSC/8 → TAD = 2 µs (must be 1-9 µs). Acquisition 10 TAD = 20 µs. A conversion takes ~45 µs, polled, no interrupt.

## 8. Monitoring, records and alarms

- **`monitor_tick()`** (once per second): checks the alarm clock and counts PMON. **`monitor_service()`** does the slow reads when due. PMON = 0 means no readings.
- **Reading order:** the potentiometer first, every period, so luminosity, D3 and the L alarm work even if the TC74 doesn't answer. If the TC74 fails, the temperature, T alarm and records are skipped for that period (a record needs both values).
- **TC74 status indicator:** the normal LCD row shows `!` in its far-right column until a valid TC74 sample has been received, and again after a failed temperature read. Luminosity remains independently usable, so D3 and the L alarm can still update while D4 and temperature records/alarms wait for a valid TC74 sample.
- **Records:** Tmax, Tmin, Lmax, Lmin, each `{h, m, s, t, l}` = 5 bytes: the time plus **both** readings at that moment. A new extreme replaces the whole record and the records are saved to EEPROM. Reset value: max = 0, min = 255, so the next reading replaces all four. `R` in config mode resets them.
- **Alarms** (only if ALAF = 1):
  - C: hh:mm:ss equals ALAH:ALAM:ALAS. T: temperature > ALAT. L: level < ALAL.
  - Each sets a bit in `alarms` (shown as a letter, cleared by S1).
  - **Once until user interaction:** a latch stops the same condition from firing again while it stays active. S1 clears the letters and the latches, so a condition that is still active can fire again after S1.
- **`alarm_out.c`:** D4 = temperature above ALAT, D3 = luminosity below ALAL (always shown, independent of ALAF). On a newly set alarm bit it runs the PWM on D2 for TALA seconds; another alarm during that time doesn't restart it.

## 9. EEPROM

| Offset | Content                                     |
| ------ | ------------------------------------------- |
| 0      | Magic word 0xA5                             |
| 1-8    | PMON, TALA, ALAF, ALAH, ALAM, ALAS, ALAT, ALAL |
| 9-10   | CLKH, CLKM                                  |
| 11     | Checksum                                    |
| 16-35  | Records Tmax, Tmin, Lmax, Lmin (5 bytes each) |

- The MCC functions take the full address `0xF000 + offset`.
- **Checksum:** 8-bit sum of the magic word and offsets 1-10. On boot the block is used only if magic, checksum and every range are valid; otherwise defaults (PMON 5, TALA 3, ALAF 0, alarm 12:00:00, ALAT 25, ALAL 3, clock 00:00). Records are validated separately, so valid records survive a bad config block.
- **When it's saved:** every minute (CLKH/CLKM) and whenever the UI changes a setting (`cfg_dirty`). Seconds are restored as 0.
- **Torn saves:** the fields are written first and the checksum near the end. A reset in the middle leaves a mismatch, so the next boot uses defaults instead of half-written values.
- **Our `write_byte()`** instead of MCC's `DATAEE_WriteByte`:
  - A write takes ~4-5 ms; the CPU keeps running and polls `WR`.
  - Only the unlock sequence (`0x55`, `0xAA`, `WR = 1`) must be uninterrupted. MCC keeps interrupts off for the whole write, which delays Timer1 (clock drift) and would lose UART bytes at 9600. Ours turns them back on right after `WR = 1`.
  - It skips bytes that didn't change, so the per-minute save writes ~2 bytes, not 13.
- **Wear:** ~100k writes per cell. CLKM changes every minute → ~1440 writes/day → ~70 days. A known limit; wear leveling would extend it.

## 10. PWM alarm light

- **PWM6** drives D2 (RA4 via PPS `RA4PPS = 0x0E`). Period from **Timer2 on LFINTOSC**, so the PWM keeps running in sleep (from Fosc it would stop).
- `T2PR = 255` → 256 × 32.26 µs = 8.26 ms ≈ **121 Hz**, above the ~100 Hz flicker limit. Duty 511/1024 = 50 %.
- Off at startup. On: `TMR2_Start(); PWM6EN = 1`. Off: `PWM6EN = 0; TMR2_Stop()`.

## 11. User interface

LCD layout (normal mode):

```
hh:mm:ss CTL AR
tt C L l
```

States in `ui.c`:

| State   | Shows                         | S1                               | S2                    |
| ------- | ----------------------------- | -------------------------------- | --------------------- |
| NORMAL  | time, T, L, alarm letters, A/a | clear alarms → THRESH            | records: REC_T        |
| THRESH  | alarm time, ALAT, ALAL (~2 s) | → CONFIG                          | -                     |
| CONFIG  | cursor on the selected field  | next field (after R → NORMAL)    | increment / select    |
| REC_T / REC_L | max/min records (~1 s each) | clear alarms → THRESH         | -                     |

- **Config fields:** clock h, m, s, then C, T, L, A, R. On C/T/L the threshold is shown in its data field; S2 enters edit mode, S2 then increments (C edits h, m, s in turn with S1). A toggles alarms. R resets the records.
- **Ranges (wrap with S2):** hh 0-23, mm/ss 0-59, tt 0-50, l 0-7.
- **Timed views** count Timer1 ticks. The first tick comes 0-1 s after the press, so N s needs N + 1 ticks: the threshold view lasts 2-3 s, each record view 1-2 s.
- The screen is redrawn once per second and after each button press, always from the main loop.
- Every setting change sets `cfg_dirty`; the main loop saves it.

## 12. Power saving

- **`SLEEP()` whenever idle:** the 4 MHz clock stops and current drops from mA to µA.
- **Still running in sleep:** Timer1 (clock), Timer0 (debounce), Timer2/PWM6 (alarm light), all on LFINTOSC; IOC wakes on a press.
- **Race-free sleep (`power.c`):** GIE = 0, check for pending work (`ticks`, button flags), `SLEEP()`, then GIE = 1. An interrupt still wakes the CPU with GIE = 0, and if a flag is already set `SLEEP()` acts as a NOP, so no event is missed. The pending ISR runs as soon as GIE = 1.
- **`VREGPM = 1`:** low-power regulator in sleep, slightly slower wake-up.
- **TC74 standby** between readings (section 6).
- **Possible further savings:** ADC off between readings, PMD registers to disable unused modules, LCD backlight off.
- **Measuring:** compare board current with and without sleep, programmed with Make and Program (not Debug). The onboard debugger also draws current, so the difference is what counts.

## 13. Hardware stack depth

- The PIC16F18875 stores return addresses in a **16-level hardware stack**. Each call uses one level, and an interrupt adds its own chain on top of whatever the main loop was doing. Overflow resets the chip (`STVREN`).
- **What went wrong:** the MCC I2C driver alone is ~8 calls deep. A TC74 read through it reached 11 levels, plus a button interrupt (6) = **17 > 16**. XC8 warned `(1393) possible hardware stack overflow`.
- **Fix:** the flat `i2c.c` (section 6).
- **Now:** the deepest path is the LCD: main → `ui_task` → `draw` → `draw_row0` → `put_time` → `lcd_put2` → `lcd_putc` → `write_byte` → `i2c_write` = 8, plus a button interrupt = **14 of 16**. Keep new UI/LCD code flat.

## 14. USART readiness

- USART at 9600 baud is for a future version; it's not configured now.
- 9600 baud, 8N1 ≈ **1 ms per byte**, and the receiver buffers only 2 bytes. So interrupts must never be off for more than ~1-2 ms, and tasks must be short and non-blocking. That's why debouncing uses a timer and why EEPROM writes keep interrupts on (section 9).
- **To add it:** EUSART at 9600 (`SP1BRG = 103` with BRG16 = BRGH = 1 → 9615 baud, 0.16 % error), TX = RC6, RX = RC7, interrupt-driven with buffers. `power.c` must not sleep while a byte is still being sent.
- **Remaining blocking in the loop:** the LCD redraw (~20 ms) and EEPROM writes poll in the main loop, but with interrupts on, so an RX interrupt would still be served.
