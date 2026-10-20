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
15. Real experiment: board observations

## 1. Architecture: the cyclic executive

No operating system. One infinite loop in `main.c` runs every task in turn. Interrupts count ticks, set flags and handle the short button-debounce logic; sensor I/O, records, EEPROM and LCD work happen in the loop.

```
ISRs:
  Timer1 (1 s)        ticks++
  IOC S1/S2           s1_pressed / s2_pressed = 1, start debounce lockout
  Timer0 (40 ms)      sample release, unlock after 2 stable periods

main loop:
  while (ticks)       toggle D5, clock_tick(), monitor_tick(), alarm_out_tick()
  monitor_service()   read sensors when PMON is due, records, T/L alarms
  alarm_out_service() LEDs D3/D4, start PWM on a new alarm
  cfg_dirty           save config + hh:mm to EEPROM
  ui_task()           buttons, state machine, LCD
  power_sleep_if_idle()
```

- **Why work is deferred:** short ISRs keep interrupt latency low. The clock, configuration and sensor records are only changed in the main-loop path, so an ISR cannot change hh:mm:ss halfway through a read.
- **`ticks` is a counter, not a flag:** if the loop is briefly busy (EEPROM, LCD), ticks accumulate and the loop catches up. This is not unlimited: the 8-bit counter wraps after 256 pending ticks, and it cannot recover overflows missed while interrupts are disabled.
- **`system.h`** is the shared contract: `now`, `cfg`, `records`, `last_t`/`last_l`, `alarms`, `cfg_dirty`, and the ISR flags. Only declarations (`extern`); the variables are defined once in `main.c`.
  - Shared fields use `uint8_t` where their values fit the 8-bit CPU. Single-byte accesses avoid multi-byte tearing; compound operations are not automatically atomic.
  - One byte per ISR flag avoids packing unrelated flags into a shared read-modify-write operation.
  - `volatile` on flags/counters shared between ISRs and the loop, or the compiler may reuse a stale copy.
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

- Why 5 V: the LCD needs it for contrast, and its backpack's pull-ups go to its VCC, which would push the I2C lines above a 3.3 V PIC's limit. PIC16F18875 (2.3-5.5 V) and TC74 (2.7-5.5 V) both accept 5 V.
- Unused pins stay analog (MCC default): disabling the digital input buffer avoids its switching current on floating inputs; this does not mean zero total leakage.

Hardware references: [Curiosity HPC board manual](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/UserGuides/Curiosity-HPC-Board-Users-Guide-DS40001856.pdf), [PIC16F18875 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/PIC16LF18855-75-Data-Sheet-40001802F.pdf), [TC74 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/21462D.pdf).

## 3. MCC and the system clock

- **MCC Classic** generates the peripheral init code and drivers into `mcc_generated_files/`. We never edit those files (Generate overwrites them); our modules register callbacks (`TMR1_SetInterruptHandler()`, `IOCBF4_SetInterruptHandler()`, ...) and call the drivers.
- **Interrupt path:** a flag is set → `INTERRUPT_InterruptManager()` checks enable + flag → module ISR clears the flag → calls our callback. GIE and PEIE are enabled in `main.c` after core initialization and callback registration, but before `ui_init()` initializes the LCD.
- **Fosc = HFINTOSC 4 MHz:** 1 µs instruction cycle, low active current, enough for I2C 100 kHz, ADC and a future UART at 9600. If Fosc changes, recheck I2C (`SSP1ADD`), ADC (TAD) and UART baud.
- **Config bits:** watchdog off, LVP on (needed by the onboard programmer), `STVREN = ON` (stack overflow → reset, section 13).

## 4. Clock: Timer1 and `clock.c`

- **Timer1 on LFINTOSC (~31 kHz), asynchronous:**
  - SOSC would need the 32.768 kHz crystal, which isn't mounted.
  - LFINTOSC keeps running in sleep. With synchronization off, the counter doesn't depend on the CPU clock, so it counts during `SLEEP()` and its overflow wakes the CPU.
  - Preload `0x86E8`: 65536 − 34536 = **31000 counts = nominally 1 s** at 31 kHz. If the clock runs slow, raise the preload; if fast, lower it.
- **Accuracy is the trade-off:** LFINTOSC can be off by a few % (1 % ≈ 14 min/day). The board test in section 15 measured a clock gain of 8 s over 330 s (+2.42%); no calibration has been applied. Also, the MCC ISR stops the timer, reloads it and restarts it, so interrupt latency and reload overhead add lost time; that's why EEPROM writes keep interrupts off as briefly as possible (section 9).
- **`clock.c`** is pure logic, no hardware:
  - `clock_tick()` adds 1 s with rollover (s → m → h → 0) and returns 1 when the minute changes, so the main loop saves hh:mm to EEPROM.
  - `clock_set()` only writes the variables and **never touches Timer1**, so setting the clock doesn't stall the seconds (a requirement).
  - `clock_init(h, m)` starts at the saved hh:mm with seconds = 0.
  - Rollovers use the 60-second, 60-minute and 24-hour boundaries. EEPROM loading and UI editing separately constrain the input ranges.

## 5. Buttons and debouncing

- **IOC on the falling edge** of RB4/RC5: a press interrupts (and wakes from sleep), without continuous main-loop polling. Timer0 samples the pins during debounce.
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
  - `i2c_write(addr, data, len)` and `i2c_read_reg(addr, reg, &val)` (repeated start, master NACK on the last byte). Both return 0 on an unexpected slave NACK.
  - **Polled:** one ready-status read plus one temperature read requires about 0.72 ms of bus clocking at 100 kHz, plus software overhead; extra ready polls take longer. This is an estimate, not a board measurement. Interrupts still run during the waits.
  - MSSP on only during a transfer.
  - **Limit:** no timeout. A missing device NACKs and is handled, but a line stuck low freezes the main loop.
- **TC74 (`tc74.c`):**
  - `tc74_read()` polls the data-ready bit a bounded number of times, then reads the temperature (signed °C, clamped to 0-50). Returns 0 on NACK or not ready.
  - **Standby (optional in the spec):** for PMON > 1 the TC74 is woken one nominal second before a reading and put back in standby after it. PMON = 1 keeps it awake; PMON = 0 keeps it in standby. The datasheet allows up to 250 ms from power-on to data ready; this is not a measured board startup time.
  - Wake/standby writes do not propagate their success to `monitor.c`; its asleep flag tracks the commanded state. The `!` indicator reports temperature-read success, not confirmation of standby or a continuous connection test.
- **LCD (`lcd.c`):** HD44780 16x2 behind a PCF8574 I2C expander, so it uses 4-bit mode and no extra pins. Each nibble is sent with E high then E low, so a character is 4 I2C bytes (~0.5 ms). Shares the bus with the TC74; both are only used from the main loop, so they never overlap. The blue potentiometer is the contrast.

## 7. ADC: luminosity

- 10-bit ADCC on RA0, basic mode, VDD/VSS references (the pot is also across VDD/GND, so supply variation cancels out).
- `level = adc >> 7` → 0-7 (128 counts per level). Near a boundary the level can flicker; hysteresis or averaging would fix it.
- Clock FOSC/8 → TAD = 2 µs (must be 1-9 µs). Acquisition 10 TAD = 20 µs. A conversion takes approximately 45 µs from the configured timing, not a board measurement; polled, no interrupt.

## 8. Monitoring, records and alarms

- **`monitor_tick()`** (once per nominal second, in the main loop): checks the alarm clock, counts PMON and handles scheduled TC74 wake/standby commands. **`monitor_service()`** reads when due. PMON = 0 means no sensor readings.
- **Reading order:** the potentiometer first, every period, so luminosity, D3 and the L alarm work even if the TC74 doesn't answer. If the TC74 fails, the temperature, T alarm and records are skipped for that period (a record needs both values).
- **TC74 status indicator:** `monitor_temperature_valid()` controls the bottom-right character (row 2, column 16): `!` means no successful sample yet or the latest attempted read failed; blank means that attempt succeeded. It appears in normal, threshold and config views, which share `draw_row1()`, but not the record views. PMON = 0 leaves this status unchanged.
- **On a failed temperature read:** `last_t` keeps its previous value (initially 0), D4 turns off, no new T alarm is evaluated, and all paired records are skipped. An already latched T letter is not cleared by the failure. Luminosity, D3 and the L alarm remain independently usable. A later successful read clears `!`; live reconnection was observed in section 15.
- **Records:** Tmax, Tmin, Lmax, Lmin, each `{h, m, s, t, l}` = 5 bytes: the time plus **both** readings at that moment. A new extreme replaces the whole record and the records are saved to EEPROM. Reset values are max = 0 and min = 255. The next valid pair replaces the minima, but a maximum only changes on a strictly larger value: a first value of 0 leaves its maximum at the zero placeholder, including timestamp and companion reading. `R` in config mode resets and saves the records.
- **Alarms** (only if ALAF = 1):
  - C: hh:mm:ss equals ALAH:ALAM:ALAS. T: temperature > ALAT. L: level < ALAL.
  - Each sets a bit in `alarms` (shown as a letter, cleared by S1).
  - **Once until user interaction:** condition latches suppress repeated triggering while active; persistent `alarms` bits and `seen_alarms` prevent another PWM start for the same uncleared notification even after the condition recurs. S1 clears the letters and latches, allowing a still-active condition to be evaluated again.
- **`alarm_out.c`:** D4 = valid temperature above ALAT, D3 = sampled luminosity below ALAL, independent of ALAF. With ALAF enabled and TALA > 0, a new alarm bit starts D2 PWM for TALA clock ticks. Another alarm during that time neither restarts it nor queues a later pulse. S1 clears notifications but does not itself stop an active PWM pulse.

## 9. EEPROM

| Offset | Content                                     |
| ------ | ------------------------------------------- |
| 0      | Magic word 0xA5                             |
| 1-8    | PMON, TALA, ALAF, ALAH, ALAM, ALAS, ALAT, ALAL |
| 9-10   | CLKH, CLKM                                  |
| 11     | Checksum                                    |
| 16-35  | Records Tmax, Tmin, Lmax, Lmin (5 bytes each) |

- The MCC functions take the full address `0xF000 + offset`.
- **Checksum:** 8-bit sum of the magic word and offsets 1-10. On boot the block is used only if magic, checksum, flag, clock and threshold ranges are valid; otherwise defaults (PMON 5, TALA 3, ALAF 0, alarm 12:00:00, ALAT 25, ALAL 3, clock 00:00). PMON and TALA accept their full byte range, 0-255.
- **Records:** validated separately by field ranges, with 255/255 allowed for reset minima. If any record is invalid, all four reset in RAM. Valid records survive a bad config block. Records have no checksum, so plausible but partially written data can pass validation.
- **When it's saved:** every minute (CLKH/CLKM) and after a UI setting change (`cfg_dirty`). The save check precedes `ui_task()`, so a UI change is saved on the next loop iteration, potentially after sleeping until the next interrupt. Seconds are restored as 0.
- **Torn saves:** fields are written first, then checksum, then magic. This detects many interrupted saves, but is not an atomic transaction: an 8-bit checksum can collide, so recovery to defaults is not guaranteed for every partial write.
- **Our `write_byte()`** instead of MCC's `DATAEE_WriteByte`:
  - A write takes ~4-5 ms; the CPU keeps running and polls `WR`.
  - Only the unlock sequence (`0x55`, `0xAA`, `WR = 1`) must be uninterrupted. MCC keeps interrupts off for the whole write, which delays Timer1 (clock drift) and would lose UART bytes at 9600. Ours turns them back on right after `WR = 1`.
  - It skips bytes that didn't change. An ordinary per-minute save usually changes just CLKM and checksum, rather than rewriting the whole 12-byte block; hour rollover and edits can change more.
- **Wear:** using ~100k writes per cell, CLKM changing every minute gives ~1440 writes/day → ~70 days. This is an endurance estimate, not a measured failure time; wear leveling would extend it.

## 10. PWM alarm light

- **PWM6** drives D2 (RA4 via PPS `RA4PPS = 0x0E`). Period from **Timer2 on LFINTOSC**, so the PWM keeps running in sleep (from Fosc it would stop).
- `T2PR = 255` → 256 × 32.26 µs = 8.26 ms ≈ **121 Hz nominal**, intended to appear steady. Duty 511/1024 ≈ 49.9%. Frequency and duration depend on LFINTOSC; neither was measured on the board.
- Off at startup. On: `TMR2_Start(); PWM6EN = 1`. Off: `PWM6EN = 0; TMR2_Stop()`.

## 11. User interface

LCD layout (normal mode):

```
hh:mm:ss CTL A
tt C L l
```

States in `ui.c`:

| State   | Shows                         | S1                               | S2                    |
| ------- | ----------------------------- | -------------------------------- | --------------------- |
| NORMAL  | time, T, L, alarm letters, A/a | clear alarms → THRESH            | records: REC_T        |
| THRESH  | alarm time, ALAT, ALAL (3 ticks) | → CONFIG                       | -                     |
| CONFIG  | cursor on the selected field  | next field (after R → NORMAL)    | increment / select    |
| REC_T / REC_L | max/min records (2 ticks each) | clear alarms → THRESH      | -                     |

- **Config fields:** clock h, m, s, then C, T, L, A, R. On C/T/L the threshold is shown in its data field; S2 enters edit mode, S2 then increments (C edits h, m, s in turn with S1). A toggles alarms. R resets the records.
- **Ranges (wrap with S2):** hh 0-23, mm/ss 0-59, tt 0-50, l 0-7.
- **Letters/layout:** normal mode shows only latched C/T/L notifications and A/a for enabled/disabled alarms. Threshold and config views show all C/T/L labels; R appears only in config. The bottom-right `!` follows section 8 even when the displayed numbers are thresholds.
- **Timed views** count observed changes in `now.s`. With regular servicing, the threshold view lasts 2-3 nominal seconds and the first record view 1-2; the second record view starts at a tick boundary and normally lasts two full tick periods. These are not exact stopwatch durations, especially with clock drift or a blocked loop. Manual observations are in section 15.
- The screen is redrawn once per second and after each button press, always from the main loop.
- Every setting change sets `cfg_dirty`; the main loop saves it.

## 12. Power saving

- **`SLEEP()` whenever idle:** the 4 MHz clock stops, reducing CPU consumption. No board-current measurement has been made; the LCD, LEDs and programmer also consume power.
- **Still running in sleep:** Timer1 (clock), Timer0 (debounce), Timer2/PWM6 (alarm light), all on LFINTOSC; IOC wakes on a press.
- **Sleep-entry race protection (`power.c`):** GIE = 0, check pending ticks/button flags, `SLEEP()`, then GIE = 1. An enabled wake-up interrupt can wake the CPU with GIE = 0; if already pending at sleep entry it prevents remaining asleep. The ISR runs when GIE is restored. This protects the check-to-sleep interval; it is not a guarantee against all lost work, and `cfg_dirty` is not part of the idle check (section 9).
- **`VREGPM = 1`:** low-power regulator in sleep, slightly slower wake-up.
- **TC74 standby** between readings (section 6).
- **Possible further savings:** ADC off between readings, PMD registers to disable unused modules, LCD backlight off.
- **Measuring:** compare board current with and without sleep, programmed with Make and Program (not Debug). The onboard debugger also draws current, so the difference is what counts.

## 13. Hardware stack depth

- The PIC16F18875 stores return addresses in a **16-level hardware stack**. Each call uses one level, and an interrupt adds its own chain on top of whatever the main loop was doing. Overflow resets the chip (`STVREN`).
- **What went wrong:** the MCC I2C driver alone is ~8 calls deep. A TC74 read through it reached 11 levels, plus a button interrupt (6) = **17 > 16**. XC8 warned `(1393) possible hardware stack overflow`.
- **Fix:** the flat `i2c.c` (section 6).
- **Source-level estimate:** the deepest path is the LCD: main → `ui_task` → `draw` → `draw_row0` → `put_time` → `lcd_put2` → `lcd_putc` → `write_byte` → `i2c_write` = 8, plus a button interrupt = **14 of 16**. This estimate is not a compiler-proven maximum; keep new UI/LCD code flat and inspect each build's warnings. The supplied successful build had no `(1393)` warning (section 15).

## 14. USART readiness

- USART at 9600 baud is for a future version; it's not configured now.
- 9600 baud, 8N1 ≈ **1 ms per byte**, and the receiver buffers only 2 bytes. So interrupts must never be off for more than ~1-2 ms, and tasks must be short and non-blocking. That's why debouncing uses a timer and why EEPROM writes keep interrupts on (section 9).
- **To add it:** EUSART at 9600 (`SP1BRG = 103` with BRG16 = BRGH = 1 → 9615 baud, 0.16 % error), TX = RC6, RX = RC7, interrupt-driven with buffers. `power.c` must not sleep while a byte is still being sent; reception and wake-up behavior also need a deliberate sleep strategy.
- **Remaining blocking in the loop:** the LCD redraw (estimated ~20 ms) and EEPROM writes poll in the main loop, but with interrupts on, so an RX interrupt could still be served. This does not establish that future receive buffers cannot overflow.

## 15. Real experiment: board observations

Recorded on 2026-10-20 from the user's physical-board testing and supplied MPLAB X build log. These are user-reported results, not tests run by Codex. H numbers refer to `HARDWARE_TESTS.md`. Stopwatch readings are manual; timings below are observations, not precision measurements. Firmware was not changed or calibrated as part of this documentation update.

### Build evidence

- The supplied IDE log reports **CLEAN SUCCESSFUL** (74 ms) and **BUILD SUCCESSFUL** (9 s), using XC8 2.45 and PIC16F1xxxx_DFP 1.24.387.
- Program memory: 4021/8192 words (49.1%); RAM: 151/1024 bytes (14.7%). No `(1393) possible hardware stack overflow` warning appeared in that log.
- Remaining warnings included unused generated functions and conversion warnings in generated PWM/flash routines. The inspected conversions fit their intended use; this is not a warning-free build. The EEPROM allocation shown as 0% does not mean runtime EEPROM saves are absent.

### Startup and TC74 status (H01, H12)

- LCD initialization succeeds, D5 visibly toggles, and normal operation eventually shows plausible temperature and luminosity values. Initially A was uppercase with no other alarm letters.
- Once running, readings were seen at five-second displayed-clock intervals, e.g. `00:35:10`, `00:35:15`. This does not establish five seconds of real elapsed time because the clock runs fast.
- With the TC74 disconnected at power-up, `!` remained and temperature stayed at `00 C`. The user connected it while running at displayed `00:28:01`; at `00:28:05` temperature updated and `!` disappeared. This demonstrates recovery on a later successful sample. Disconnecting after a valid sample was not separately reported; retaining the previous temperature on failure is established by code (section 8).

**Cold-start issue (unresolved):** unplugging/reconnecting USB causes the displayed clock to advance about two seconds and return to the saved minute before counting normally (observed `00:35:00 → 00:35:02 → 00:35:00`). This does not happen after the board reset button, programming or Debug startup. Initial temperature was reported roughly 10 s after the jump, with `!` shown until a valid reading. Debug breakpoints at `SYSTEM_Initialize()` and `clock_set()` each stopped once; initial `PCON0` was `0x3C`, but the fault was absent, so those tests cannot establish its cause. Power-up/reset or LCD initialization remains a possibility; no cause was confirmed or fix applied. Investigation stopped at the user's request.

### Buttons, screens and editing (H02)

- S1 from normal mode showed the threshold screen below. Another S1 entered config; editing worked, and S1 after the R field returned to normal.

  ```
  12:00:00 CTL A
  34 C L 2
  ```

- S2 from normal mode showed temperature max/min records first, then luminosity max/min, including timestamps and both readings in the clock/temperature/luminosity layout.
- Manual timings: threshold view approximately **2.1 s**, temperature records **1.2 s**, luminosity records **1.4 s**. The user noted uncertainty from manually operating the timer. These do not precisely validate the source-derived timing in section 11; in particular, the second record view normally spans two clock ticks.
- Edited hours wrapped **23 → 0**; minutes and seconds each wrapped **59 → 0**. Seconds continued counting during editing. Threshold-range wrapping was not separately reported.
- Holding buttons for more than 2 s produced no special action or repeated increments.
- The user described R as resetting alarms and records. Source clarification: **R resets/saves records only**; S1 clears alarm notifications when entering the threshold screen, before reaching config.

### Clock accuracy and rollover (H03)

- Natural rollover **23:59:59 → 00:00:00** worked and counting continued.
- Initial short comparison, starting at LCD `00:01:00`: 60 s on the phone corresponded to about 62 s on the LCD.
- Longer comparison, preserving the reported endpoints:

  | Reference | Start | End | Elapsed |
  | --- | --- | --- | --- |
  | LCD clock | `00:38:00` | `00:43:38` | 338 s |
  | Phone stopwatch | `00:00` | `05:30` | 330 s |

- The LCD gained **8 s in 5 min 30 s**, or **8 / 330 × 100 = +2.42%**. This establishes a fast clock during this run. LFINTOSC accuracy is a plausible contributor, but the test does not isolate the cause. No preload adjustment was applied. If this rate stayed constant it would imply about 1 min 27 s gain/hour; that is an extrapolation, not a measured hour-long result.

### Potentiometer and alarms (H04-H09)

- Potentiometer midpoint gave level 3. The user estimated that levels 2-4 occupy roughly the middle 15% of travel, with wider spacing elsewhere. This is a subjective observation; neither ADC linearity nor potentiometer taper/fault was measured.
- An initial observation put luminosity one displayed second ahead of temperature (e.g. `:04` versus `:05`). **After reboot this could not be reproduced:** both updated together at five-second intervals. Do not treat the earlier offset as a confirmed persistent defect.
- With **ALAL = 4**: at L = 4, D3 was off and only the clock LED blinked; at L = 3, D3 turned on; at L = 5, it turned off. No new alarm letters appeared in that observation.
- With lowercase **a** (alarms disabled), a clock alarm set for 12 minutes produced no alarm light/event when reached. Low luminosity still lit D3 and temperature above its threshold lit D4, without C/T/L alarm notifications.
- With uppercase **A**, L appeared when luminosity was below threshold, and C appeared when the clock reached/passed the configured alarm time. The exact trigger instant was not timed; the code uses equality, not a greater-than comparison.
- The user reported temperature/luminosity alarms working from earlier testing and declined repeating H06/H07. D2 PWM duration, exact frequency, one-shot behavior and rearming were not separately measured in this session.

### Records and power-cycle persistence (partial H10, H11)

- R visibly reset the saved records shown through S2. With luminosity held at 0 after reset, one displayed row was `00:00:00`, `0 C`, L = 0; the other was `04:35:00`, `26 C`, L = 0.
- **Correction preserved:** the timestamp was initially typed as `40:35:00`; the user explicitly corrected it to **`04:35:00`**. There is no reported invalid 40-hour record. The zero maximum placeholder is consistent with the strict-comparison limitation in section 8; the full H10 extrema test was not completed.
- Power was removed at displayed **`00:27:07`** for about **10 s**. On reboot the clock restored **`00:27:00`**. The distinctive configured alarm time, thresholds and existing records were retained. Their exact configured values were not supplied. Restoring seconds as zero matches the EEPROM design; elapsed power-off time is not tracked.

### Limits of this validation

- No multimeter was available, so sleep/TC74-standby energy savings were **not measured**. Successful readings do not establish that standby writes always succeed.
- PMON = 0/1 behavior, precise TALA timing, the full record-extrema sequence, corrupted-EEPROM recovery, and the remaining checks without reported results remain unverified on hardware. The user declined further follow-up checks; no pass is implied.
- The successful build and functional observations support the tested behavior, not a claim that the full hardware checklist or professor's complete requirements have been validated.
