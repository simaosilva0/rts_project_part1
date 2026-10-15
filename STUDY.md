# STUDY - what we did and why

The technical decisions of the project and the reasons behind them, organized by topic. Check here first when something isn't clear.

## 1. The board (Curiosity HPC)

Source: Microchip "Curiosity HPC Schematics Rev 2" (DM164136), sheet 8, "mikroBUS and Target PIC Interface".

| What               | Connection                                                              |
| ------------------ | ----------------------------------------------------------------------- |
| LEDs D2-D5         | RA4-RA7 via jumpers J4-J7, each with a 1 kΩ resistor (R4, R6, R8, R12) |
| S1                 | RB4, 10 kΩ pull-up R3, 1 kΩ series R11, jumper J2                     |
| S2                 | RC5, 10 kΩ pull-up R9, 1 kΩ series R10, jumper J1                     |
| S3                 | Reset (MCLR/RE3), 10 kΩ pull-up R21                                    |
| Potentiometer      | R1 10 kΩ, wiper → R2 1 kΩ → J3 → RA0                               |
| 32.768 kHz crystal | Y2 + C21/C22,**not mounted**                                      |
| mikroBUS 1 I2C     | SCL = RC3 (J36), SDA = RC4 (J37),**no pull-ups**                  |
| mikroBUS 1 UART    | TX = RC6, RX = RC7, through 330 Ω (R39/R40)                            |

- "Solder-blob jumpers" (J1-J7 etc.) are pads bridged with solder. If one is open, that pin is disconnected from its LED/button.
- D1 is the board's own power LED, not ours. That's why the user LEDs start at D2.
- **Power-up delay:** the onboard debugger (PKOB4) boots first and then switches on the PIC's supply, which can take a second or two. On top of that, the first D5 toggle comes 1 s after the program starts (all LEDs start off). Neither is a bug.
- **Supply:** the board can give the target 3.3 V or 5 V. We use **5 V** for everything (section 7).
- Parts available: only the board, the TC74 and the LCD. No external resistors or capacitors.

## 2. MCC (MPLAB Code Configurator)

- **What it is:** a graphical tool that generates the C code configuring the peripherals (timers, I2C, ADC, pins...). The code goes in `mcc_generated_files/`, plus a `main.c`.
- **Why use it:** setting registers by hand is slow and error-prone. MCC writes correct init code and simple driver functions, so our time goes into the system logic.
- **Classic, not Melody:**
  - MCC has two content types; we use **Classic** (library `pic10-pic12-pic16-pic18 1.82.1`).
  - The two generate different file layouts and function names. For example, Classic has `mcc.h` + `TMR1_SetInterruptHandler()`, where Melody has `system/system.h` + `TMR1_OverflowCallbackRegister()`.
- **Never edit generated files:** every Generate rewrites `mcc_generated_files/`. Our code lives in our own `.c` files and calls the MCC functions.
  - `main.c` is created only once. Later generations keep our version ("User edits maintained").
  - Only one person changes the MCC config, so generated files never conflict.

### Generated files

| File                                                                    | What it does                                                                             |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `mcc.c/h`                                                             | `SYSTEM_Initialize()` calls every module's init, plus the oscillator setup             |
| `device_config.c`                                                     | configuration bits (`#pragma config`)                                                  |
| `pin_manager.c/h`                                                     | pin directions, pull-ups, IOC, PPS, macros like`LED_5_Toggle()`, `SW1_GetValue()`    |
| `interrupt_manager.c/h`                                               | the PIC16's single interrupt function: checks which flag is set, calls that module's ISR |
| `tmr0/tmr1/tmr2.c/h`, `pwm6`, `i2c1_master`, `adcc`, `memory` | one driver per peripheral (sections 5-10, 15)                                            |

### How an interrupt reaches our code

1. A peripheral sets its flag, e.g. Timer1 overflows → `TMR1IF`. The CPU jumps to `INTERRUPT_InterruptManager()`.
2. The manager sees `TMR1IE && TMR1IF` and calls `TMR1_ISR()`.
3. `TMR1_ISR()` clears the flag, does its work (reload), and calls the function we **registered** with `TMR1_SetInterruptHandler()`.
4. Same chain for the buttons: `PIN_MANAGER_IOC()` → `IOCBF4_ISR()` → our function from `IOCBF4_SetInterruptHandler()`.

We never edit these files; we only register our own callbacks.

### Why the interrupt enable lines in `main.c` are needed

Each source's enable bit (`TMR1IE`, `IOCIE`, ...) only allows that source. The CPU also needs:

- **GIE**, the global enable: `INTERRUPT_GlobalInterruptEnable()`;
- **PEIE**, the peripheral enable: `INTERRUPT_PeripheralInterruptEnable()`, because Timer1 is a "peripheral" interrupt.

Without them, flags get set but no ISR runs. MCC leaves these lines commented so we choose when interrupts start (after all init).

## 3. System clock and configuration bits

- **Oscillator: HFINTOSC at 4 MHz, divider 1** (`OSCCON1 = 0x60`, `OSCFRQ = 0x02`):
  - HFINTOSC is internal and needs no parts. The 1 s accuracy comes from Timer1, not from this clock.
  - The divider (NDIV) divides the chosen frequency. 4 MHz ÷ 1 is the direct setting (32 MHz ÷ 8 gives the same).
  - **Why 4 MHz:**
    - One instruction = 4 clock cycles → **1 µs per instruction**, easy for timing.
    - A lower clock means lower current while awake.
    - It's fast enough for everything: I2C 100 kHz (SSP1ADD = 9), UART 9600 (SPBRG = 103, 0.16 % error), ADC (FOSC/8).
  - If Fosc ever changes, recheck the I2C, UART and ADC clock settings (section 8).
- **`RSTOSC = HFINT1`:** the chip starts at 1 MHz, and the oscillator init then switches it to 4 MHz.
- **Watchdog off (`WDTE = OFF`):** the watchdog resets the chip unless the program clears it regularly. We don't use it.
- **Low-voltage programming on (`LVP = ON`):** lets the onboard debugger program the chip. This is the normal setting for this board.
- **`STVREN = ON`:** a hardware stack overflow causes a reset (section 17).

## 4. Pins: LEDs and buttons

### LEDs D2-D5 on RA4-RA7 (outputs)

- `TRISA = 0x0F`: bits 4-7 = 0 → outputs (TRIS: 0 = Output, 1 = Input).
- Use:
  - D5 toggles every second (the clock is alive);
  - D2 is the PWM alarm light (section 10);
  - D3/D4 are for alarm indication.
- The custom names `LED_2`…`LED_5` give macros like `LED_5_Toggle()` instead of `LATAbits.LATA7`.
- **Toggle vs blink:** toggling once per second = 1 s on, 1 s off = one blink every 2 s. To check timing, count toggles (60 per minute).

### Buttons S1 (RB4) and S2 (RC5), named `SW1` / `SW2` (inputs)

- **Active low:** a press connects the pin to GND (reads 0). Released, the pull-up holds it at 1.
- **Pull-ups:**
  - The board already has external 10 kΩ pull-ups (R3, R9). The 1 kΩ series resistors protect the pin if it's ever configured as an output while pressed.
  - The internal WPU (`WPUB = 0x10`, `WPUC` bit 5) is therefore not needed, but harmless: a much weaker resistor in parallel.
- **Digital:** `ANSELB` bit 4 and `ANSELC` bit 5 = 0. An analog pin always reads 0 digitally.
- **IOC (interrupt-on-change) on the falling edge** (`IOCBN4 = 1`, `IOCCN5 = 1`):
  - A press causes an interrupt, so the main loop doesn't poll. IOC also **wakes the PIC from sleep**.
  - Falling edge = 1 → 0 = the moment of the press. The rising edge (release) isn't needed.
  - Bounce is handled in `buttons.c` (section 15).
- **Unused pins** stay **analog** (`ANSEL = 1`, MCC's default). This disconnects their digital input buffers, so floating pins don't draw current.

## 5. Timer1: the 1 s tick

### Why Timer1

- The project statement asks for it.
- It's 16-bit and can run **asynchronously** from a low-frequency clock that keeps running in sleep, so it counts during sleep and its interrupt wakes the CPU. The clock must not stop while sleeping.

### Clock source: LFINTOSC (~31 kHz internal)

- **Why not SOSC:** SOSC needs an external 32.768 kHz crystal. The board has the footprint (Y2) but it isn't mounted, so SOSC would never oscillate.
- **Why LFINTOSC works:** it's built in, needs no parts, and keeps running in sleep.
- **The cost: accuracy.** It can be off by several percent, varying with temperature and voltage. 1 % is about 14 min per day. Measure the drift against a stopwatch over 10+ minutes and put it in the report: it's a trade-off of low power and no parts against accuracy.
- **Calibration knob:**
  - The preload `TMR1H:TMR1L = 0x86E8` = 34536, so 65536 − 34536 = **31000 counts = 1 s** at a nominal 31 kHz.
  - If the clock runs slow, use a higher preload (fewer counts), and vice versa.

### Synchronization off (asynchronous)

- **What synchronization is:** with it on, each Timer1 clock edge passes through a circuit that samples it with the **CPU clock** before the counter increments. That keeps the counter in step with the CPU.
- **Why it must be off:**
  - `SLEEP()` stops the CPU clock. A synchronized timer would freeze in sleep: no overflow, no interrupt, no wake-up.
  - Asynchronous, the LFINTOSC edges go straight to the counter, so it keeps counting, and its overflow wakes the CPU.
- **The cost:** reading or writing `TMR1H:TMR1L` while the timer runs could catch the two bytes mid-change. We never read it: we only use the overflow interrupt and keep the time in variables (section 14).

### Interrupts: TMRI on, TMRGI off

- **TMRI** = the overflow interrupt, our 1 s tick.
- **TMRGI** = the gate interrupt: Timer1's "gate" lets an external signal start and stop counting, to measure pulse lengths. We don't use it, so it stays off.

### Generated values

- `T1CLK = 0x04` → LFINTOSC.
- `T1CON = 0x05` → prescaler 1:1, `nT1SYNC` = not synchronized, timer on.
- **Small reload error:** after each overflow, `TMR1_ISR()` writes the preload again. Counts between the overflow and that write are lost (about 1 in 31000, ≈ 0.003 %). That's negligible next to LFINTOSC's own error.

## 6. I2C bus (MSSP1): TC74 and LCD

- **I2C** is a 2-wire bus: **SCL** (clock, driven by the master) and **SDA** (data, both directions).
  - The PIC is the **master**.
  - Each slave has a 7-bit address: the TC74 (TC74A5 = **0x4D**, the chip marking tells which) and the LCD backpack (0x27 or 0x3F, section 7).
  - Two devices on the same two wires is normal as long as their addresses differ.
- The **MSSP** module does I2C in hardware: it generates the clock, the start/stop and the acks, and the software just gives it bytes.
- **Pins: RC3 = SCL1, RC4 = SDA1** (mikroBUS 1, the MSSP1 defaults), mapped through **PPS** (Peripheral Pin Select).
  - Each I2C pin needs **both** an output and an input mapping, because the module drives and reads the same wire: `RC3PPS = 0x14` + `SSP1CLKPPS = 0x13`, and `RC4PPS = 0x15` + `SSP1DATPPS = 0x14`.
  - Input PPS values encode port × 8 + pin (A = 0, B = 1, C = 2), so RC3 = 19 = 0x13.
  - `TRISC` stays input on these pins: the MSSP drives them itself, open-drain.

### 100 kHz, standard speed

- 100 kHz is the TC74's maximum. `SSP1ADD = 4 MHz / (4 × 100 kHz) − 1 = 9`.
- **Slew rate control off** (`SSP1STAT = 0x80`, `SMP = 1`):
  - Slew rate = how fast an edge rises or falls.
  - Slew rate control slows edges on purpose, which the I2C spec needs only at 400 kHz (fast mode) to reduce ringing.
  - At 100 kHz the datasheet says to disable it. The setting must match the bus speed.
- `SSP1CON1 = 0x08` → I2C Master mode. The driver turns the module on and off around each transfer.

### Pull-ups: the PIC's WPU on RC3/RC4

- I2C lines are **open-drain**: devices only pull them low, and a resistor pulls them high. Without pull-ups, the bus stays low and nothing works.
- The board has none, the TC74 chip has none, and we can't add resistors. The LCD backpack usually has them (section 7). The WPU on RC3/RC4 (`WPUC = 0x38`) makes sure there are always some.
- **WPU limits:**
  - It's weak (~25-35 kΩ), so edges rise slowly: ~1.5 µs with ~50 pF of wiring, while standard mode allows 1 µs.
  - Out of spec, but it normally works with short wires and two devices.
  - **Knob if reads fail:** lower the I2C clock (e.g. 50 kHz). Slower clocks give slow edges more time, and the TC74 doesn't need speed.

### Polled driver, not interrupt-driven

- **Polled:** the driver starts each step (start, byte, stop) and waits in a loop until the hardware's "done" flag is set. It's simple and the order of operations is obvious.
- **Interrupt-driven:** returns immediately, and a state machine in the ISR runs each step. The CPU is free meanwhile, but there's more code, another interrupt source, and the result arrives later.
- **Why polled is enough:**
  - A TC74 read is ~4 bytes ≈ **0.4 ms**, once every PMON seconds: under 0.05 % of the CPU time.
  - The Timer1 interrupt can still interrupt the wait, so no tick is late.
  - The LCD writes are longer (section 7) but still a few ms, once per second.
- **Hang risk (confirmed in the generated code):**
  - The polled driver waits with `while(1) { if (PIR3bits.SSP1IF) break; }`, which has no timeout. The example helpers also loop on `while (I2C1_BUSY == I2C1_Open(address));`.
  - If the bus is broken (device unplugged, no pull-ups, a line stuck low), the main loop **freezes inside the I2C call**. The seconds keep counting in the ISR, but nothing else happens.
  - So if the program "stops" at the first sensor read or LCD write, suspect the wiring, pull-ups and address first.

### TC74 API

- `I2C1_Read1ByteRegister(0x4D, 0x00)` (from `examples/i2c1_master_example.h`) reads register 0x00 = TEMP: °C, signed 8-bit.

## 7. The LCD: 16x2 HD44780 with a PCF8574 I2C backpack

- **What it is:** a QAPASS 1602 character LCD (HD44780 controller), with a black "MH" backpack on the back. The backpack has a **PCF8574** chip, a blue potentiometer and a 4-pin header (GND, VCC, SDA, SCL).
- **What the backpack does:**
  - The HD44780 normally needs 6+ wires (RS, E, D4-D7).
  - The PCF8574 is an I2C **I/O expander**: one byte sent over I2C sets its 8 output pins, which are wired to the LCD's RS, RW, E, backlight and D4-D7.
  - So the LCD is driven in **4-bit mode**, by sending bytes to the PCF8574.
- **Consequences:**
  - **No extra PIC pins:** it shares the I2C bus with the TC74.
  - **Address:** 0x27 for a **PCF8574T**, 0x3F for a **PCF8574AT** (read the chip).
  - **Slow:** each character needs several I2C bytes (each 4-bit half is sent with E high, then E low), so a full line takes a few ms. Only refresh what changed, and never write to the LCD from an ISR.
  - **Shared bus = one user at a time:** the TC74 read and the LCD update both run in the main loop, so they never overlap.
  - **TC74 readiness timeout:** `tc74_read()` checks the RWCR data-ready bit a bounded number of times instead of waiting forever. If the first conversion is still not ready, it returns the last valid temperature, so a normal delayed conversion does not corrupt the shared reading or freeze this TC74-level loop.
  - **Remaining I2C limitation:** the generated example functions used by the TC74 still contain unbounded waits while opening or completing an I2C transaction. We do not edit generated files; therefore a physically stuck I2C bus can still block inside MCC before `tc74_read()` regains control. Replacing that helper with a timeout-aware hand-written transaction is a separate change, not part of this small TC74 readiness fix.
- **Blue potentiometer = contrast.** If the LCD shows nothing or full black boxes, turn it first. The jumper by the header switches the backlight.
- **Pull-ups:** these backpacks usually have SDA/SCL pull-ups (resistors marked `472` = 4.7 kΩ or `103` = 10 kΩ). If so, they serve the whole bus.
- **Voltage:**
  - The backpack's pull-ups go to its own VCC. An LCD at 5 V with the PIC at 3.3 V would pull SDA/SCL above what the PIC pins accept.
  - So **everything runs at 5 V**. The LCD needs it for contrast, and the PIC (1.8-5.5 V) and TC74 (2.7-5.5 V) both support it.
- `lcd.c` is hand-written (MCC has no HD44780/PCF8574 driver) on top of the MCC I2C driver.

## 8. ADC (ADCC): the potentiometer

- **ADCC** = "ADC with Computation", this PIC's **10-bit** ADC: a voltage between VSS and VDD becomes 0-1023.
- **Pin RA0** (ANA0): input, analog, no pull-up (a pull-up would add current and shift the voltage). Generated macros are `POT_*`, and the channel is `POT`.
- **Basic mode** (`ADCON2 = 0x00`):
  - The ADCC can also accumulate, average, low-pass filter or compare against thresholds in hardware. Basic is one plain conversion per request.
  - We only need **8 levels** (128 counts each), and the noise is a few counts, so filtering adds nothing visible.
  - **Known limit:** with the knob exactly on a level boundary, the level can flicker (3 ↔ 4). The fix, if needed, is Average mode or hysteresis in software.
- **Clock: FOSC/8** (`ADCLK = 0x03`, TAD = 2 µs):
  - Each conversion step takes one TAD, which must be **1-9 µs** on this PIC.
  - At 4 MHz: FOSC/4 = 1 µs (no margin), **FOSC/8 = 2 µs**, FOSC/16 = 4 µs. FOSC/8 is the fastest with margin.
  - MCC Classic only offered FOSC dividers here. The internal FRC clock would be independent of Fosc and work in sleep, but we read while awake anyway.
  - **If Fosc changes, recompute:** TAD = divider / Fosc must stay within 1-9 µs.
- **References VDD / VSS** (`ADREF = 0x00`): the pot is also between VDD and GND, so the reading depends only on the knob position, not on the exact supply voltage.
- **Right-aligned** (`ADFM = 1`): the result is a plain 0-1023, so `level = adc >> 7` gives 0-7.
- **Acquisition: 10 TAD = 20 µs** (`ADACQ = 0x0A`):
  - Before converting, the internal capacitor charges to the pin voltage.
  - Our source is ≤ ~2.5 kΩ (pot wiper) + 1 kΩ (R2) ≈ 3.5 kΩ, which needs only a few µs, so 20 µs is comfortably settled.
  - Knob: if readings jump, increase it.
- **No interrupt:** `ADCC_GetSingleConversion(POT)` selects the channel, starts the conversion, waits for `ADGO` to clear (≈ 20 + 24 = **45 µs**), and returns the result. Unlike I2C, it can't hang.
- **Energy:** `ADON` stays 1 between readings. The read function turns it on itself, so `ADCON0bits.ADON = 0;` after each reading is a free saving to measure.

### ADC application module

- `adc.c` is the hand-written boundary around the generated ADCC driver. It calls `ADCC_GetSingleConversion(POT)` and returns the luminosity level as `sample >> 7`, giving exactly 0-7 from the 10-bit result.
- No ADC interrupt is enabled: a conversion takes about 45 us and is requested only by the foreground monitoring task. This keeps the ISR paths short and leaves the shared `last_l` value under main-loop ownership.
- No pin or peripheral change is needed in MCC for this module. MCC already configures RA0 as analog channel `POT`, uses VDD/VSS references, right alignment, FOSC/8, and 10 TAD acquisition. The generated `adcc.*` files remain read-only.
- The wrapper intentionally does not add filtering or hysteresis. A reading exactly on a 128-count boundary may change between adjacent levels; filtering can be added later if the board test shows that this matters.

## 9. EEPROM (Memory module)

- **256 bytes of data EEPROM**, non-volatile: it holds the config block and the max/min records (README, EEPROM layout).
- **API:** `DATAEE_WriteByte(uint16_t addr, uint8_t data)` and `DATAEE_ReadByte(uint16_t addr)` take the **full address 0xF000 + offset**. The README lists offsets, so the magic word (offset 0) is at 0xF000 and the records (offset 16) start at 0xF010.
- `NVMREGS = 1` selects EEPROM/config space instead of program flash.
- **Write time and interrupts:**
  - Each byte takes ~**4-5 ms**.
  - The routine disables interrupts (GIE = 0), runs the unlock sequence (`0x55`, `0xAA`, `WR = 1`), and **waits until the write finishes** before restoring GIE.
  - The unlock must not be interrupted, or the hardware refuses the write. That's a safety feature against accidental writes.
- **Consequence:**
  - During each write, the ISRs are delayed by a few ms. Timer1 keeps counting in hardware and its flag stays set, so **no second is lost**.
  - The 12-byte config block blocks for ~50 ms. That's acceptable because it's rare, but write only the bytes that changed.
- **Reading** is instant.
- **Wear:** ~100k writes per cell (the README note about saving CLKM every minute).

### Configuration block implementation

- `nvm.c` keeps EEPROM addresses private and converts each offset to the full MCC address `0xF000 + offset`. Other modules use `nvm_load()` and `nvm_save_config()` instead of knowing the memory map.
- The checksum is the 8-bit sum, with natural modulo-256 wraparound, of the magic word and configuration bytes at offsets 1-10. Loading requires the magic word, checksum, and all bounded time/threshold fields to be valid; otherwise the documented defaults are used.
- Configuration writes invalidate the block by clearing the magic word first. The fields and checksum are then written, and the magic word is written last. A reset during a partial write therefore causes defaults on the next boot instead of accepting incomplete configuration.
- The saved clock hours and minutes are loaded before `clock_init()`. Seconds are deliberately restored as 0, as required by the project.
- The records are stored consecutively at offsets 16-35, five bytes per `record_t`. On load, invalid timestamps or sensor ranges reset all four records to the documented max/min sentinels. Record writes are kept separate from configuration writes because they have different update frequencies and wear behavior.
- Configuration persistence is now connected to the cyclic executive: a minute change marks `cfg_dirty`, and the foreground loop saves the current configuration and clock hours/minutes. The same path will save UI changes when the UI sets `cfg_dirty`.
- Configuration and record validity are independent. If the configuration block is invalid, defaults and a zero clock are selected, but valid saved records are still loaded.

## 10. PWM on D2 (Timer2 + PWM6)

- **PWM** = a square wave with a fixed period and a variable **duty cycle** (% of the time high). On an LED, a fast PWM looks like a steady light whose brightness is the duty. D2 lights this way for TALA seconds during an alarm.
- **PWM6:** a dedicated 10-bit PWM module, simpler than a CCP module in PWM mode. Its period comes from **Timer2** (`CCPTMRS1bits.P6TSEL = 1`).
- **Timer2 on LFINTOSC** (`T2CLKCON = 0x04`), prescaler **not synchronized** (`T2HLT = 0x00`):
  - From Fosc/4, Timer2 would stop in sleep, so the alarm light would freeze, or the system couldn't sleep during alarms.
  - From LFINTOSC (async), the **PWM keeps running while the CPU sleeps**.
  - To confirm on the board: start an alarm and check that D2 stays lit while the system sleeps.
- **Period:**
  - The formula is period = (PR2 + 1) × prescaler × 32.258 µs (one LFINTOSC tick at 31 kHz).
  - MCC asks for a time and shows the range **32.258 µs** (PR2 = 0) to **8.258 ms** (PR2 = 255). We use the maximum: `T2PR = 0xFF`, **≈ 121 Hz**.
  - Above ~100 Hz the eye sees no flicker. Prescaler 1:2 would allow up to 16.5 ms, but that's ≈ 60 Hz, which flickers visibly.
- **Duty 50 %:**
  - `PWM6DCH:DCL` = 511 of 4 × (PR2 + 1) = 1024.
  - Knob: `PWM6_LoadDutyValue(0..1023)`.
- **Off at startup:** `PWM6EN = 0`, `TMR2ON = 0`. D2 stays dark and Timer2 draws nothing until an alarm.
  - On: `TMR2_Start(); PWM6CONbits.PWM6EN = 1;`
  - Off: `PWM6CONbits.PWM6EN = 0; TMR2_Stop();`
- **Pin:** `RA4PPS = 0x0E`, so PWM6OUT drives RA4 instead of `LATA4`. While PWM6 is disabled, its output is low.

## 11. USART: not in this version

- **Serial communication (USART at 9600 baud) comes in a future version, not this one.** So it isn't configured in MCC and there's no driver or stub. An enabled peripheral nothing uses would only be dead code, and its pins (RC6/RC7, the mikroBUS 1 UART) stay free.
- **What this version does instead is stay ready for it.** The constraint it brings:
  - 9600 baud, 8N1 = 10 bits per byte ≈ **1 ms per byte**. With a 1-byte hardware buffer, the CPU must feed or drain it about every 1 ms, or bytes are lost.
  - So **every task must be short and non-blocking**: no `__delay_ms()`, no busy-waiting for long. That's why debouncing uses a timer (section 15), and why the main loop only reacts to flags.
- **What adding it later will need:**
  - MCC: EUSART, 9600 baud (BRG16 = 1, BRGH = 1, `SP1BRG = 103` → 4 MHz / (4 × 104) = 9615 baud, 0.16 % error; UART tolerates about ±2 %). TX = RC6, RX = RC7 through PPS. Preferably interrupt-driven with software buffers, so the main loop never waits.
  - `power.c`: a byte still shifting out when `SLEEP()` runs gets cut off (the baud generator runs from Fosc, which stops in sleep). So "TX in progress" (`TRMT = 0`) or a non-empty TX buffer must block sleep, like the pending flags do. Received bytes can wake the CPU only with the auto-baud/wake-up feature, otherwise RX means no sleep.
  - Task times measured in this version (timing pin toggles) show how much margin is left in the loop.

## 12. `system.h`: the shared contract

- The one header every module includes. It fixes **the names and types of the data shared between modules**, so each module can be written independently.
- **Declarations only** (`extern`, types, `#define`s). The variables are **defined once**, in `main.c`. A variable defined in a header would be duplicated in every file that includes it, and the link would fail.
- **Contents:**

  | Item                           | Type                                                            | Used for                                                   |
  | ------------------------------ | --------------------------------------------------------------- | ---------------------------------------------------------- |
  | `now`                        | `clock_time_t { h, m, s }`                                    | LCD, records, alarm clock check                            |
  | `cfg`                        | `config_t { pmon, tala, alaf, alah, alam, alas, alat, alal }` | same order as EEPROM offsets 1-8, so saving is a byte copy |
  | `last_t`, `last_l`         | `uint8_t`                                                     | latest readings                                            |
  | `alarms`                     | bits`ALARM_C`, `ALARM_T`, `ALARM_L`                       | pending alarms, shown as letters, cleared by S1            |
  | `records[REC_COUNT]`         | `record_t { h, m, s, t, l }`                                  | max/min records (section 13)                               |
  | `cfg_dirty`                  | `uint8_t`                                                     | the UI sets it, the main loop saves to EEPROM              |
  | `ticks`                      | `volatile uint8_t`                                            | +1 per second from the Timer1 ISR                          |
  | `s1_pressed`, `s2_pressed` | `volatile uint8_t`                                            | set by the button ISRs                                     |
- **`uint8_t` everywhere:** 8-bit CPU, and every value fits in a byte. Byte operations are single instructions: fast and atomic.
- **One byte per ISR flag:**

  - If S1 and S2 were bits in one byte, clearing S1 in the main loop (`flags &= ~FLAG_S1`) would be a read-modify-write. An S2 interrupt in the middle would be overwritten and lost.
  - Separate bytes can't interfere. `ticks++` (ISR) and `ticks--` (main loop) are one instruction each (`incf`/`decf`).
- **`volatile`:** required for anything written in an ISR and read in the main loop. Otherwise the compiler can keep a stale copy in a register (`while (!flag);` would loop forever).
- **Only shared things belong here.** A constant used by one module stays in that module, e.g. `STABLE_PERIODS` in `buttons.c`. Changing it then touches one file and doesn't change the contract.
- Since it's shared, every change to `system.h` is announced to the group.

## 18. Monitoring and temporary NVM layer

- `monitor.c` is a foreground service, not an interrupt module. `monitor_tick()` is called once for every consumed Timer1 second and only updates the PMON counter and clock-alarm latch. `monitor_service()` performs the slower TC74 and ADC reads when a reading is due.
- PMON counts logical seconds rather than wall-clock reads. A value of 0 clears the pending reading and disables sensor collection. A non-zero value requests one reading after that many seconds, then repeats.
- A sensor sample updates `last_t` and `last_l`. If it exceeds or falls below one of the four extrema, the complete record is replaced with the current time and both sensor values. Records are saved through `nvm_save_records()` only when at least one record changes.
- Temperature and luminosity alarm conditions use internal latches. A condition triggers once when it becomes active and cannot retrigger while it remains active, even if S1 later clears the displayed alarm. Returning to the safe range releases the latch for a future alarm.
- The clock alarm is checked once per second against ALAH, ALAM and ALAS. It is latched for the matching second so it generates only one `ALARM_C` event.
- `nvm.c` is currently a placeholder: `nvm_load()` installs the assignment defaults and initial record limits in RAM, while save functions do nothing. This allows the monitor and alarm logic to be integrated before EEPROM address, checksum, and write-timing code is added. No persistence is claimed in this stage.
- The current TC74 API still performs its existing blocking read. The bounded I2C failure behavior must be added before the integrated system is considered robust against a disconnected sensor or stuck bus.

## 13. `records`: max/min with timestamps

- **What the project asks for:** the **maximum and minimum** temperature and luminosity seen so far, each with the **time it happened**.

  - They survive power cycles (EEPROM offsets 16-35).
  - S2 shows them (T max/min for 1 s, then L max/min for 1 s).
  - The **R** option in config mode clears them.
- **One record = 5 bytes = `{ h, m, s, t, l }`:** the event's time plus **both** readings at that moment. For example, Tmax = "at 14:32:05 the temperature was 31 °C, the highest so far, and the luminosity then was 5".

  | Entry                 | Replaced when a new reading has |
  | --------------------- | ------------------------------- |
  | `records[REC_TMAX]` | T > stored`t`                 |
  | `records[REC_TMIN]` | T < stored`t`                 |
  | `records[REC_LMAX]` | L > stored`l`                 |
  | `records[REC_LMIN]` | L < stored`l`                 |

  On a replacement, all 5 bytes get the current time and readings.
- **Array + enum instead of 4 variables:**

  - EEPROM stores the 4 records back to back (4 × 5 = 20 bytes), so saving/loading is one loop, and record i is at `0xF000 + 16 + 5·i`.
  - `REC_COUNT` keeps the size and the names in sync.
- **Reset value** (after R or invalid EEPROM): max `t`/`l` = 0, min `t`/`l` = 255, so the next real reading replaces all four.

## 14. `clock.c`: the time of day

- **Functions:**
  - `clock_init(h, m)` starts at hⓂ️00, from CLKH/CLKM in EEPROM, so after a power cycle the clock continues from the last saved minute.
  - `clock_set(h, m, s)` is used by the config menu.
  - `clock_tick()` adds one second.
- **Design: the ISR only counts, the main loop advances the time.**
  - Timer1 ISR: `ticks++`. Main loop: `while (ticks) { ticks--; clock_tick(); ... }`.
  - **Why not update hh:mm:ss in the ISR:**
    - The time is 3 bytes. If the ISR changes them while the main loop copies them (e.g. at 12:59:59 → 13:00:00), the loop can read a mix like 13:59:59 ("torn read").
    - `clock_set()` writing while the ISR increments has the same problem.
    - With the ISR only counting, only the main loop touches the time, so neither can happen.
    - It's also the cyclic-executive rule: ISRs only count and set flags.
  - **No second is lost** when the main loop is busy (EEPROM write, LCD update): `ticks` accumulates, and the loop catches up.
  - The Timer1 interrupt wakes the CPU every second, so the clock advances during sleep periods too.
- **`clock_tick()`:**
  - `if (++now.s < 60) return 0;` increments, then compares. Then it chains the rollovers: seconds 60 → 0 and minute + 1; minutes 60 → 0 and hour + 1; hours 24 → 0.
  - **Returns 1 when the minute changes:** that's when CLKH/CLKM get saved. Returning it instead of saving keeps `clock.c` pure logic (no hardware), so it can be tested on the PC, and saving is the main loop's decision.
  - `>=` instead of `==`: a bad value (e.g. h = 30) still rolls back to a valid range instead of counting to 255.
- **`clock_set()` never touches Timer1**, so setting the clock doesn't stall the seconds (a requirement). The sub-second phase isn't reset: the next second comes 0-1 s later, which is fine with no fractions displayed.
- **The D5 toggle is in the main loop, not the ISR:** it's work, and it proves the whole ISR → counter → main loop chain.
- **PC test:**
  - `gcc -I. test/test_clock.c clock.c` checks 23:59:59 + 1 = 00:00:00 (minute changed), a normal second (no minute change), and 86400 ticks = one day, with exactly 1440 minute changes.
  - The test defines its own `now`, because there's no `main.c` on the PC.

## 15. Buttons: flags and debouncing (`buttons.c`)

### The problem: bounce

- A mechanical contact bounces for a few ms, on press **and on release**. Each bounce can be a falling edge, i.e. another IOC interrupt.
- So one press can count twice, and even a release can count as a press.

### Options considered

| Option                                            | Verdict                                                                                                                                                       |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Delay inside the ISR                              | **No:** it blocks every other interrupt (the 1 s tick too). No delays in ISRs.                                                                          |
| `__delay_ms(30)` in the main loop after a press | Works, but**blocks** the main loop for 30 ms. That breaks "all tasks short and non-blocking", which matters once the USART needs servicing every ~1 ms. |
| Capacitor on the button (RC filter)               | Not possible: no extra parts, and the board has none there.                                                                                                   |
| **Non-blocking lockout with a timer**       | **Chosen:** no waiting anywhere, and the CPU can even sleep during the lockout.                                                                         |

### The chosen design

- **Press:** the IOC callback sets `s1_pressed = 1` (or S2), then **locks both buttons** and starts Timer0. While locked, IOC from either button is ignored.
- **Every 40 ms while locked** (Timer0 interrupt):
  - If either pin reads low (held or bouncing), the stable count goes back to 0.
  - If both read high, it counts one stable period. After **2 in a row**, Timer0 stops and the buttons unlock.
- **Why 2 stable periods:** the release bounces too. One high reading could fall inside a release bounce, and the next bounce would count as a new press. Two high readings 40 ms apart mean the contact has settled (bounce is < ~20 ms).
- **One lock for both buttons:** one timer, one state. Pressing S1 and S2 within ~80 ms of each other isn't needed by the UI (its windows are 1-2 s).
- **Timing:** a tap locks for ~80 ms after release, a held button for as long as it's held + ~80 ms.
- **Knobs:** `STABLE_PERIODS` in `buttons.c`, and the Timer0 period in MCC.
- **`locked` / `stable` are `static`, not `volatile`:** only ISR code touches them. On a PIC16, interrupts can't interrupt each other, so the IOC and Timer0 callbacks never run at the same time.
- **The Timer0 logic sits in the ISR:** it's a few instructions (read 2 pins, compare, count), so it's still short, and moving it to the main loop would need another flag.
- **The main loop doesn't change:** it still just consumes `s1_pressed` / `s2_pressed`.

### Timer0 settings

- **LFINTOSC, asynchronous** (`T0CON1 = 0x93`, `T0ASYNC = 1`):
  - It runs in sleep, so the lockout ends even while the CPU sleeps, and its interrupt wakes it.
  - Synchronized, it would freeze in sleep (as in section 5), and the buttons would stay locked.
- **8-bit mode, prescaler 1:8:** one tick = 8 / 31 kHz ≈ 258 µs, so the max period is ≈ 66 ms. `TMR0H = 154` → (154 + 1) × 258 µs ≈ **40 ms**.
  - In 8-bit mode, Timer0 counts up to `TMR0H`, then restarts at 0 and sets its flag, so it interrupts every 40 ms until stopped.
  - `TMR0_WriteTimer(0)` before starting makes the first period a full 40 ms.
- **Off at startup** (`T0CON0 = 0x00`): it only runs during a lockout.

## 16. `power.c`: sleep when idle

- **Goal:** the CPU runs only when there's work. Otherwise `SLEEP()`: the 4 MHz clock stops, and current drops from mA to µA.
- **Still running in sleep:** Timer1 (the 1 s tick), Timer0 (debounce lockout), Timer2/PWM6 (alarm light). All of them run asynchronously from LFINTOSC. IOC also works in sleep.
- **What wakes the CPU:** any interrupt whose enable bit is set: Timer1, IOC, Timer0. Execution continues right after `SLEEP()`.
- **What blocks sleep (`busy()`):** work the main loop hasn't handled yet (`ticks`, `s1_pressed`, `s2_pressed`).
- **The race, and why GIE is turned off around the check:**
  - Without protection: the loop checks "no work", then an interrupt sets a flag, then `SLEEP()` runs. The CPU sleeps with work pending, up to 1 s late.
  - With `GIE = 0`, no ISR can run between the check and `SLEEP()`. On this PIC, an interrupt **still wakes the CPU with GIE = 0**, and if a flag is already set, `SLEEP()` acts as a `NOP`.
  - After waking: `NOP()` (recommended after SLEEP), then `GIE = 1`. The pending interrupt is serviced immediately, and the loop handles it on the next pass. Nothing can be missed.
- **`VREGPM = 1`:** the internal voltage regulator goes into low-power mode during sleep, which lowers the sleep current considerably. The cost is a slightly slower wake-up (µs), which is irrelevant here.
- **Further savings (knobs for the report):**
  - ADC off between readings (section 8);
  - `PMD` registers to disable unused modules;
  - TC74 standby mode between readings.
- **Measuring (for the report):**
  - Measure the board current with and without `power_sleep_if_idle()`. The onboard debugger also draws current, so the **difference** is what counts.
  - Use **Make and Program Device**, not Debug: sleep behaves differently under the debugger.

## 17. Hardware stack depth

- **The limit:** the PIC16F18875 keeps return addresses in a **hardware stack of 16 levels**. Every function call uses one level, and an interrupt adds one more (plus the ISR's own calls) on top of whatever main was doing.
- **The compiler warning** `(1393) possible hardware stack overflow detected; estimated stack depth: N` means the deepest chain plus an interrupt at the worst moment needs N > 16.
- **What happens on overflow:** the oldest return address is overwritten. With `STVREN = ON`, the PIC resets. It may work in testing and fail only when an interrupt hits the deepest point.
- **The usual culprit:** the LCD → I2C path. The MCC I2C driver is already several calls deep (example helper → `I2C1_Open`/`I2C1_MasterOperation` → state machine → state function), and LCD functions on top (`lcd_putc` → byte → nibble → I2C helper) add more.
- **Fixes:**
  - Fewer nested calls in the LCD send path (e.g. one function that sends a byte directly).
  - Keep ISR callbacks shallow.
  - The compiler's call graph (in the map/listing files) shows the exact chain.
- **Current state of master:** no warning. The deepest ISR chain is ISR → `PIN_MANAGER_IOC` → `IOCBF4_ISR` → `s1_isr` → `lock` → `TMR0_WriteTimer`.
