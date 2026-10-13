# STUDY - what we did and why

An explanation of every decision in this project. Check here first when something isn't clear. New entries are added whenever something is done or a question is answered.


## 1. Repository setup

- **`.gitignore`** leaves out MPLAB X build output (`build/`, `dist/`, `debug/`), per-user IDE settings (`nbproject/private/`) and files MPLAB regenerates (`nbproject/Makefile-*.mk`, `Makefile-genesis.properties`). These change on every build or differ per computer, so committing them only causes merge conflicts.
- **What *is* committed:** the sources, `nbproject/configurations.xml` + `project.xml` (the project definition), the MCC config file (`.mc3`) and `mcc_generated_files/`. With these, anyone can open, build and regenerate the project.
- **Branch `master` only, no branches:** everything is committed straight to `master`, and only code that builds. For a small team with a short deadline, this keeps things simple: there are no merges to manage.
- **`mcc-manifest-autosave.yml` is ignored:** MCC rewrites it constantly while you edit, so it's noise. `mcc-manifest-generated-success.yml` *is* committed, because it records which MCC/library versions generated the code.

### Files owned by root (MCC "IllegalStateException")
- **Symptom:** MCC wouldn't open, showing `IllegalStateException: CoreRunner has not yet been instantiated`.
- **Real cause:** earlier in the log was `java.io.IOException: Permission denied`. The `project.X` folder was owned by `root` because MPLAB X had been started with `sudo`. MCC couldn't write its config file into the project, so loading failed, and the exception was just the follow-on error.
- **Fix:** `sudo chown -R simaosilva:simaosilva <project folder>`, and always start MPLAB X as the normal user, never with `sudo`.
- **Lesson:** when a tool shows a strange error, read the *first* error in its log, not the last one.


## 2. MCC (MPLAB Code Configurator)

- **What it is:** a graphical tool that generates the C code that configures the peripherals (registers for timers, I2C, ADC, pins...). It puts that code in `mcc_generated_files/`, plus a `main.c`.
- **Why use it:** setting registers by hand is slow and easy to get wrong. MCC writes correct init code and simple driver functions, so our time goes into the actual system logic.
- **Classic, not Melody:** MCC has two content types. We use **Classic**, which is what got installed and opened (library `pic10-pic12-pic16-pic18 1.82.1`). Classic and Melody generate different file layouts and function names, e.g. Classic `mcc.h` + `TMR1_SetInterruptHandler()` vs Melody `system/system.h` + `TMR1_OverflowCallbackRegister()`.
- **Rules:** never hand-edit generated files, because regenerating overwrites them. Our code lives in our own `.c` files and calls the MCC functions. Only one person changes the MCC config, so the generated files never conflict in merges.

### Removing a module added by mistake (e.g. TMR0)
In **Project Resources**, right-click the module → **Remove** (or click the remove icon next to it), then **Generate** again. If TMR0's files were already generated, regenerating deletes them, or you can delete `tmr0.c/h` yourself.


## 3. System Module: clock

- **Oscillator: HFINTOSC** (the internal high-frequency oscillator). It needs no external parts and it's accurate enough for the CPU. The 1 s clock accuracy comes from Timer1, not from this oscillator.
- **Frequency 4 MHz, divider 1:**
  - HFINTOSC can run at several frequencies (1, 2, 4, 8, 12, 16, 32 MHz). The divider (NDIV) then divides that.
  - 4 MHz with divider 1 gives **Fosc = 4 MHz**. 32 MHz with divider 8 also gives 4 MHz, but divider 1 is the direct, simplest setting.
  - Why 4 MHz:
    - The PIC executes one instruction every 4 clock cycles, so 4 MHz → 1 MHz instruction rate → **1 µs per instruction**. That makes timing measurements easy to reason about.
    - A lower clock means lower current while awake (part of the energy-saving requirement).
    - It's still fast enough for everything we need. I2C at 100 kHz: baud register = 4 MHz / (4 × 100 kHz) − 1 = 9. EUSART at 9600: high-speed BRG = 4 MHz / (4 × 9600) − 1 ≈ 103, which is about 0.2 % error.
  - If something turns out too slow later, raise the frequency here and regenerate.
- **Watchdog off:** the watchdog resets the chip unless the program clears it regularly. We don't use it; with it on, the board would reset by itself.
- **Low-voltage programming on:** this lets the onboard debugger program the chip without the high programming voltage. It's the normal setting for the Curiosity board.


## 4. Pins

### LEDs D2-D5 on RA4-RA7 (outputs)
- On the Curiosity HPC board, the user LEDs are labelled **D2, D3, D4, D5** on the silkscreen and connected to **RA4, RA5, RA6, RA7**. D1 belongs to the board itself (power indicator), not to our program, so the user LEDs start at D2. The project statement uses the same names.
- Use: D5 blinks every second (shows the clock is alive). D2 is the PWM alarm LED. D3/D4 are for alarm indication.
- Custom names (`LED_D2`…`LED_D5`) make MCC generate macros like `LED_D5_Toggle()`, so we don't have to remember `LATAbits.LATA7`.

### Buttons S1 (RB4) and S2 (RC5) (inputs), named `SW1` / `SW2`
- The custom names give macros like `SW1_GetValue()`. The name only affects the generated code, so `SW1`/`SW2` is fine. The README and the project statement call them S1/S2.
- **Weak pull-up (WPU):**
  - A button connects the pin to **GND** when pressed. When it's released, nothing drives the pin, so it would "float" and read random values.
  - The pull-up is an internal resistor to VDD. It holds the pin **high (1) when released**; pressing pulls it **low (0)**.
  - So the buttons are **active low**: pressed = 0.
  - **This board already has external pull-ups** (Curiosity HPC schematic, sheet 8):
    - **S1 → RB4:** pull-up **R3 = 10 kΩ** to TVDD, plus **R11 = 1 kΩ** in series to the pin.
    - **S2 → RC5:** pull-up **R9 = 10 kΩ**, plus **R10 = 1 kΩ** in series.
    - The 1 kΩ series resistors protect the pin if it's accidentally configured as an output while the button is pressed (that would short it to GND).
    - So the WPU on RB4/RC5 isn't needed. It's harmless, though: a much weaker resistor in parallel with the 10 kΩ. It can stay on or be turned off.
- **IOC (interrupt-on-change) on the negative / falling edge:**
  - IOC lets a pin change cause an interrupt, so the main loop doesn't have to poll the buttons. It also **wakes the PIC from sleep**, which we need because the system sleeps when idle.
  - Falling edge = the 1 → 0 transition = **the moment the button is pressed**. Positive edge would be the release. We only care about presses, so negative-only avoids a second interrupt per press.
  - Mechanical buttons **bounce**: one press makes several quick edges. That's why the ISR only sets a flag, and debouncing (~20-50 ms) happens in the main loop.


## 5. Timer1: the 1 s clock

### Why Timer1 (and not Timer0)
- The project statement asks for Timer1 to drive the clock.
- Timer1 is the PIC's "real-time clock" timer:
  - It's **16-bit**.
  - It can be clocked from the low-frequency sources that keep running in sleep (LFINTOSC, or SOSC with a 32.768 kHz watch crystal).
- It can run **asynchronously**, from its own clock, so it keeps counting during **sleep** and its interrupt wakes the CPU. The clock must not stop while sleeping.
- Timer0 is left free; we don't need it now. A TMR0 added by mistake should be removed (see section 2).

### Settings
- **Clock source: LFINTOSC** (internal ~31 kHz oscillator):
  - **Why not SOSC:** SOSC needs an external 32.768 kHz crystal. The Curiosity HPC schematic (Rev 2, sheet 8) has a footprint for it, **Y2** (with capacitors C21/C22), but it's marked **N.M. / DNP** (not mounted). With no crystal, SOSC simply never oscillates and the clock would never tick.
  - **Why LFINTOSC works:** it's built into the chip, needs no parts, and **keeps running during sleep**, which is exactly what the clock needs.
  - **The cost:** it's much less accurate than a crystal. Its frequency can be off by several percent and changes with temperature and voltage, so the clock will drift (a 1 % error is about 14 min per day).
  - **Calibration knob:** at ~31 kHz, 1 s ≈ 31000 counts, so MCC sets the preload to about 65536 − 31000. Measure the real drift against a stopwatch over 10+ minutes. If it's consistently fast or slow, adjust the period/preload value to correct it.
  - Write the measured drift in the report. It's a real trade-off: low power and no extra parts, in exchange for accuracy.
- **Prescaler 1:1, period 1 s:** MCC computes the preload value so the counter overflows every second.
- **TMRI vs TMRGI in the Interrupt Module:** adding TMR1 makes MCC list two interrupts.
  - **TMRI** = the Timer1 **overflow** interrupt. This is our 1 s tick. Enable it.
  - **TMRGI** = the Timer1 **gate** interrupt. Timer1 has a "gate" feature: an external signal (the T1G pin, a comparator, etc.) starts and stops counting, which is used to measure how long a pulse lasts. The gate interrupt fires when one such measurement finishes.
  - We don't use the gate (Timer1 counts all the time), so **leave TMRGI disabled**. Enabling it would only add an interrupt source that never does anything useful for us.
- **Synchronization off (asynchronous):**
  - **What synchronization is:** Timer1 has its own clock (LFINTOSC), which isn't aligned with the CPU clock (Fosc). With synchronization **on**, each Timer1 clock edge first goes through a small synchronizer circuit. That circuit samples the edge using the **CPU clock**, and only then lets the counter increment. This keeps the counter changing in step with the CPU, so the CPU never reads it halfway through a change.
  - **Why it stops in sleep:** `SLEEP()` turns off the CPU clock. The synchronizer needs that clock to pass edges through, so the edges are blocked and the counter freezes. No overflow happens, so there's no interrupt and the chip never wakes up from the timer. The clock would stop while sleeping.
  - **Asynchronous (synchronization off):** the LFINTOSC edges go **straight to the counter**, without passing through the CPU clock. LFINTOSC keeps running in sleep, so the counter keeps counting. The overflow sets the interrupt flag, and that interrupt **wakes the CPU**. This is how we get a 1 s tick while sleeping.
  - **The cost:** because the counter changes independently of the CPU, reading or writing `TMR1H:TMR1L` while it runs could catch the two bytes mid-change. To avoid this, we don't read the counter at all: we only use the overflow interrupt and keep hh:mm:ss in variables. When the clock is set, we change those variables, not the timer, so the timer never has to stop (this is also the requirement that "setting the clock doesn't stall the seconds").
- **Timer interrupt on:** each overflow (each second) calls our handler. The handler only updates hh:mm:ss and sets a flag; the main loop does the rest (cyclic executive, short ISRs).


## 6. What MCC generated (Classic)

| File | What it does |
|---|---|
| `main.c` | Our program. MCC creates it once; after that it's ours to edit. |
| `mcc.c/h` | `SYSTEM_Initialize()` calls every module's init, plus the oscillator setup (`OSCCON1 = 0x60` → HFINTOSC, NDIV 1; `OSCFRQ = 0x02` → 4 MHz). |
| `device_config.c` | Configuration bits (`#pragma config`): `WDTE = OFF`, `LVP = ON`, `RSTOSC = HFINT1` (starts at 1 MHz, then the oscillator init switches to 4 MHz). |
| `pin_manager.c/h` | Pin directions, pull-ups, IOC, and macros like `LED_5_Toggle()`, `SW1_GetValue()`, `IOCBF4_SetInterruptHandler()`. |
| `tmr1.c/h` | Timer1 init, the ISR, and `TMR1_SetInterruptHandler()`. |
| `interrupt_manager.c/h` | The single interrupt function of the PIC16 (`__interrupt()`). It checks which flag is set and calls the right module ISR. |

### How the interrupt reaches our code
1. Timer1 overflows and sets the `TMR1IF` flag. The CPU jumps to `INTERRUPT_InterruptManager()`.
2. The manager sees `TMR1IE && TMR1IF` and calls `TMR1_ISR()`.
3. `TMR1_ISR()` clears the flag, reloads the counter, and calls the function we registered with `TMR1_SetInterruptHandler()`.
4. The same chain applies to the buttons: `PIN_MANAGER_IOC()` → `IOCBF4_ISR()` → our function registered with `IOCBF4_SetInterruptHandler()`.

We never edit these files. We only **register our own functions** (callbacks).

### Reading the generated values (to check the configuration)
- **Pins:**
  - `TRISA = 0x0F`: bits 4-7 = 0, so **RA4-RA7 are outputs** (TRIS bit 0 = output, 1 = input, mnemonic "0 = Output").
  - `WPUB = 0x10` (bit 4) and `WPUC = 0x20` (bit 5): pull-ups on RB4 and RC5.
  - `ANSELB = 0xEF` and `ANSELC = 0xDF`: RB4 and RC5 are digital. They must be, or they always read 0.
  - `IOCBN4 = 1`, `IOCCN5 = 1`: falling-edge interrupt on the buttons.
- **Timer1:**
  - `T1CLK = 0x04` → LFINTOSC.
  - `T1CON = 0x05` → prescaler 1:1, `nT1SYNC` = not synchronized, timer on.
  - Preload `TMR1H:TMR1L = 0x86E8` = 34536, and 65536 − 34536 = **31000 counts = 1 s at 31 kHz**.
  - **This preload is the calibration knob**: if the clock runs slow, use a slightly higher preload (fewer counts), and vice versa.

### Small inaccuracy from the reload
After each overflow, `TMR1_ISR()` writes the preload again. Any counts between the overflow and that write are lost, so each second is a few tens of µs longer: about 1 count in 31000, roughly 0.003 %. That's negligible next to LFINTOSC's own error (percent level), so we accept it.

### Why the interrupt enable lines must be uncommented
`TMR1IE` / `IOCIE` only allow each source. The CPU also needs:
- **GIE** (global interrupt enable, `INTERRUPT_GlobalInterruptEnable()`);
- **PEIE** (peripheral interrupt enable, `INTERRUPT_PeripheralInterruptEnable()`), because Timer1 is a "peripheral" interrupt.

Without both, the flags get set but the ISR never runs. MCC leaves these lines commented so we choose *when* interrupts start (after all init is done).

### Toggle vs blink rate
`LED_5_Toggle()` once per second means 1 s on, then 1 s off: **one full blink every 2 s**. To check the timing, count toggles (state changes) instead of blinks: 60 per minute.


## 7. Regenerating MCC code: what gets replaced

- **`mcc_generated_files/`** is rewritten every time you click Generate. If you edited one of these files, MCC Classic opens a merge window and asks what to keep, but the rule is simple: **never edit them**, so there's nothing to lose.
- **`main.c`** is created only when it doesn't exist yet. Regenerating doesn't replace it. If MCC wants to change it, it shows the merge window, where you keep your version.
- **Our own files** (`clock.c`, `lcd.c`, ...) are never touched by MCC.
- **Safety net:** **commit before regenerating**. Afterwards, `git diff` shows exactly what MCC changed, and `git checkout -- <file>` restores anything it shouldn't have touched.
- So it's safe to add peripherals (I2C, ADC, PWM, ...) and regenerate as the project grows, without configuring everything up front.

### Which pins can be configured now
- **TC74 sensor (I2C):** MSSP1 in I2C Master mode, 100 kHz. Its pins (SCL/SDA) have to match how the TC74 is wired.
  - On the Curiosity HPC, the mikroBUS I2C pins are RC3 = SCL and RC4 = SDA. Check the board/wiring before choosing them.
  - I2C lines need pull-up resistors: the TC74 board usually has them, otherwise use the pins' WPU.
- **Potentiometer (ADC):** ADCC on the potentiometer pin (RA0 on the Curiosity HPC; check the board). Set it as an analog input.
- **LCD:** the pins depend on the LCD type and how it's wired (4-bit parallel: RS, E, D4-D7; or an I2C backpack, which shares the I2C bus with the TC74). Configure them once the wiring is known.


## 8. TC74 (I2C) and potentiometer (ADC) in MCC

### TC74 temperature sensor → MSSP1 as I2C Master
- **I2C** is a 2-wire bus: **SCL** (clock, driven by the master) and **SDA** (data, both directions). The PIC is the **master**; the TC74 is a **slave** with a fixed 7-bit address (TC74A5 = **0x4D**; the letter/number on the chip says which).
- The PIC does I2C in hardware with the **MSSP** module (Master Synchronous Serial Port). MSSP1 in "I2C Master" mode generates the clock and the start/stop/ack bits, so the software only gives it bytes.
- **100 kHz (standard mode):** the TC74 supports up to 100 kHz. With Fosc = 4 MHz the baud register is `SSPADD = 4 MHz / (4 × 100 kHz) − 1 = 9`.
- **Polled, not interrupt-driven:**
  - **Polled:** the driver starts each step (start bit, send byte, read byte, stop) and then **waits in a loop** until the MSSP hardware sets its "done" flag. The function returns only when the whole transfer is finished. It's simple, the order of operations is obvious, and it's easy to debug.
  - **Interrupt-driven:** the driver starts the transfer and returns immediately. Every finished step causes an MSSP interrupt, and the ISR runs a state machine that starts the next step. The CPU is free during the transfer, but the code is more complex: a state machine inside an ISR, an extra interrupt source in the cyclic executive, and the caller has to check later whether the result is ready.
  - **Why polled is enough here:**
    - A TC74 read is about 4 bytes ≈ 40 bits ≈ **0.4 ms** at 100 kHz.
    - It happens once every PMON seconds (≥ 1 s).
    - Blocking 0.4 ms once per second is **< 0.05 %** of the CPU time, and far shorter than the 1 s tick, so no deadline is missed and the clock still updates on time (the Timer1 interrupt still interrupts the wait).
    - It also matches the cyclic executive design: ISRs stay minimal (only the clock and buttons), and all work happens in the main loop.
  - **When interrupt-driven would be worth it:** long or frequent transfers (e.g. many bytes, 400 kHz streams), or when the main loop has tight timing that 0.4 ms would break. Neither applies here.
  - **Risk to know:** if the bus is broken (no pull-ups, sensor unplugged, SCL stuck low), a polled driver can wait forever on a flag that never comes, and the main loop freezes. If the system "hangs" when reading the sensor, check the wiring and pull-ups first.
- **Slew rate: "Standard speed (100 kHz)":**
  - **Slew rate** = how fast a pin's voltage rises or falls on each edge.
  - MSSP has a **slew rate control** setting (the `SMP` bit in `SSP1STAT`). When enabled, it slows the edges down on purpose.
  - The I2C spec requires that only for **Fast mode (400 kHz)**: there, the edges are close together, and slowed edges reduce ringing, glitches and interference.
  - At **100 kHz (standard mode)**, slew rate control isn't needed. The datasheet says to **disable** it for 100 kHz (and 1 MHz); "Standard speed" in MCC does exactly that (`SMP = 1`).
  - The rule: the slew-rate setting must match the bus speed. Since we chose 100 kHz (the TC74's maximum), we choose standard speed.
- **Decision: WPU on RC3 (SCL) and RC4 (SDA).**
  - We only have the Curiosity HPC, the TC74 and the display. No external resistors are allowed, and neither the board nor the TC74 has I2C pull-ups.
  - The PIC's internal weak pull-ups are therefore the only option. Their rise time is slower than the I2C spec (see above), but with short wires and only one or two devices on the bus, it normally works.
  - **If reads fail or come back wrong (e.g. 0xFF or a hang):**
    1. Check the wiring and that the address is 0x4D.
    2. **Lower the I2C clock** in MCC (e.g. 100 kHz → 50 kHz or less). A slower clock gives each slowly-rising edge more time, so the weak pull-ups are enough. This is the tuning knob: the TC74 doesn't need speed (one byte every PMON seconds).
    3. Keep the wires to the TC74 short.
  - **Update: the display does have an I2C backpack** (see section 9). These modules usually have SDA/SCL pull-ups on board, and since the bus is shared, they would serve the TC74 too. Until that's checked, keep WPU on; it's harmless in parallel.
- **Pins, via PPS:** on this PIC, peripheral pins can be moved (Peripheral Pin Select).
  - We use **RC3 = SCL1, RC4 = SDA1**, the I2C pins of mikroBUS 1 on the Curiosity HPC (and the MSSP1 defaults).
  - I2C needs **both** an input and an output mapping on each pin, because the module drives and reads the same wire. MCC sets both.
- **Pull-ups:**
  - I2C lines are **open-drain**: devices only pull the line low; a resistor pulls it high.
  - Without pull-ups, the bus stays low and nothing works.
  - **The Curiosity HPC has no I2C pull-ups.** In the schematic (sheet 8), RC3/RC4 go to the mikroBUS SCL/SDA pins only through solder-blob jumpers **J36/J37** (module 1) and **J42/J43** (module 2), with no resistors to VDD and no footprint for them.
  - **The TC74 chip has no internal pull-ups.** They have to come from the board it's mounted on, from external resistors, or from the PIC's WPU.
  - **How to check whether there are pull-ups (multimeter):**
    - **Board unpowered, resistance mode:** measure between SDA and VDD (3.3 V/5 V pin), then between SCL and VDD.
      - A few kΩ (typically 2.2k, 4.7k or 10k) → there's a pull-up.
      - Open (OL) or MΩ → there's none.
    - **Board powered, PIC held in reset or not driving the pins, voltage mode:** measure SDA and SCL to GND.
      - Steady ≈ VDD → there's a pull-up.
      - ≈ 0 V or a wandering value → there's none (the line is floating).
      - In reset, all pins are inputs with WPU off, so the reading shows only the external parts.
    - **Or look at it:** small resistors on the TC74 board between each line and VDD, or check its schematic.
  - **What to do with the result:**
    - **External pull-ups present:** WPU can stay off. Turning it on doesn't hurt: it's just a much weaker resistor in parallel, which changes almost nothing.
    - **No pull-ups:** the proper fix is **4.7 kΩ from SDA to VDD and 4.7 kΩ from SCL to VDD**. The PIC's WPU is a fallback. It's weak (≈140 µA → roughly 25-35 kΩ), so the edges rise slowly: R·C with ~50 pF of wires gives ~1.5 µs, while standard mode allows max 1 µs rise time. It's out of spec but often works with short wires. If the reads fail or are flaky, add the real resistors.
- **Generated API:**
  - `I2C1_Open/Close` etc. are generated in `i2c1_master.c`.
  - MCC also generates an example file with `I2C1_Read1ByteRegister(address, reg)`.
  - Reading the TC74 temperature is `I2C1_Read1ByteRegister(0x4D, 0x00)`: register 0x00 = TEMP, in °C, signed 8-bit.
  - This is B's part (`tc74.c`).

### Potentiometer → ADCC
- **ADCC** = "ADC with Computation", the PIC16F18875's **10-bit** ADC. It converts a voltage between VSS (0 V) and VDD into 0-1023.
- The potentiometer on the Curiosity HPC is on **RA0** (analog channel ANA0). Confirmed in the schematic (sheet 8): **R1 = 10 kΩ pot** between TVDD and GND, its wiper through **R2 = 1 kΩ** and solder-blob jumper **J3** to RA0. The pin must be **input + analog** (ANSEL = 1). Its pull-up must be **off**, because a pull-up would add current and shift the voltage.
- **Operating mode: Basic_mode:**
  - The ADCC ("with Computation") can do extra work in hardware after each conversion:
    - **Accumulate:** add up several results.
    - **Average / Burst Average:** take N conversions and divide.
    - **Low-pass filter:** smooth the value over time.
    - **Threshold compare:** interrupt when the value crosses a limit.
  - **Basic** = plain single conversions: one start, one result, no extra computation.
  - We only need **8 levels** (128 ADC counts per level). The noise on the pot reading is a few counts, much smaller than one level, so averaging or filtering adds configuration for no visible benefit.
  - **Known limit:** when the knob sits exactly on a level boundary, the level could flicker between two values (e.g. 3 ↔ 4) from one reading to the next. If that ever matters, switch to Average mode (e.g. 4-16 samples) or add hysteresis in software.
- **Clock: FOSC/8** (the CPU clock divided by 8):
  - The ADC converts one bit per clock period, called **TAD**. For this PIC, TAD must be between **1 µs and 9 µs** (datasheet). Faster, and the result is wrong; slower, and the capacitor leaks during the conversion.
  - The first idea was **FRC** (the ADC's own RC clock): always a valid TAD whatever the CPU speed, and it works in sleep. But MCC Classic only offered **FOSC with a divider** for this module, so we pick the divider ourselves.
  - With Fosc = 4 MHz:
    - FOSC/4 → TAD = 1 µs (exactly the minimum, no margin).
    - **FOSC/8 → TAD = 2 µs** (safely inside 1-9 µs).
    - FOSC/16 → 4 µs (also fine, just slower).
  - **FOSC/8** is the fastest choice with margin. A 10-bit conversion takes about 12 TAD ≈ **24 µs**.
  - **What we lose compared to FRC:**
    - The ADC can't convert during sleep. That doesn't matter: we read the pot while awake, in the main loop.
    - **If Fosc ever changes, the divider must be recalculated**: TAD = divider / Fosc, which must stay within 1-9 µs. For example, at 32 MHz FOSC/8 = 0.25 µs is too fast, and you'd need FOSC/32 or FOSC/64.
- **References VDD / VSS:** full-scale = the supply voltage, which is the same voltage across the potentiometer. So the reading only depends on the knob position, not on the exact supply voltage.
- **Right-aligned result:** the 10-bit value sits in the low bits of a 16-bit number (0-1023), so `level = adc >> 7` gives **0-7** (1024 / 8 = 128 per level).
- **Acquisition time:**
  - Before converting, the ADC's internal capacitor has to charge to the pin voltage.
  - The potentiometer is a low-resistance source, so a short time is enough.
  - Our source resistance: the pot wiper is at most ~2.5 kΩ (10 kΩ pot in the middle), plus **R2 = 1 kΩ** in series, so about 3.5 kΩ. With that, the capacitor needs only a few µs to charge (datasheet acquisition formula).
  - **Acquisition count = 10** means 10 ADC clock periods (TAD) before the conversion starts. With TAD = 2 µs, that's **20 µs**: several times the few µs needed, so the reading is always settled, and the cost is negligible since we read once every PMON seconds.
  - With 0, the conversion would start immediately, and the first reading after switching channels can be off.
  - This is a tuning knob: if readings jump, increase it.
- **No ADC interrupt:** one conversion takes tens of µs, so waiting for it (`ADCC_GetSingleConversion(channel)`) is simpler than an interrupt.
- **Generated API:** `ADCC_GetSingleConversion(<channel>)` in `adcc.h`. The channel name comes from the pin's custom name (`POT`). This is B's part (`adc.c`).


## Reference: Curiosity HPC schematic

Source: Microchip "Curiosity HPC Schematics Rev 2" (DM164136), sheet 8, "mikroBUS and Target PIC Interface".

| What | Connection |
|---|---|
| LEDs D2-D5 | RA4-RA7 via jumpers J4-J7, each with a 1 kΩ resistor (R4, R6, R8, R12) |
| S1 | RB4, 10 kΩ pull-up R3, 1 kΩ series R11, jumper J2 |
| S2 | RC5, 10 kΩ pull-up R9, 1 kΩ series R10, jumper J1 |
| S3 | Reset (MCLR/RE3), 10 kΩ pull-up R21 |
| Potentiometer | R1 10 kΩ, wiper → R2 1 kΩ → J3 → RA0 |
| 32.768 kHz crystal | Y2 + C21/C22, **not mounted** |
| mikroBUS 1 I2C | SCL = RC3 (J36), SDA = RC4 (J37), **no pull-ups** |
| mikroBUS 2 I2C | SCL = RC3 (J42), SDA = RC4 (J43), **no pull-ups** |

"Solder-blob jumpers" (J1-J7 etc.) are pads bridged with solder on the board. If one is cut/open, that pin is disconnected from its LED/button.


## 9. The display: 16x2 LCD with an I2C backpack

- **What it is:**
  - A 16x2 character LCD (QAPASS 1602) with an **HD44780**-compatible controller.
  - On the back is a black **"MH" backpack** with a **PCF8574** chip, a blue potentiometer and a 4-pin header (GND, VCC, SDA, SCL).
- **What the backpack does:**
  - The HD44780 normally needs 6+ wires (RS, E, D4-D7...).
  - The **PCF8574** is an I2C "I/O expander": it has 8 output pins, and you set them by sending it one byte over I2C. The backpack wires those 8 pins to the LCD (RS, RW, E, backlight, D4-D7).
  - So we drive the LCD in **4-bit mode** by sending bytes to the PCF8574, using only the two I2C wires.
- **Consequences for the project:**
  - **No extra PIC pins for the LCD.** It sits on the **same I2C bus as the TC74** (RC3 = SCL, RC4 = SDA). Two devices with different addresses on the same two wires is normal I2C.
  - **Address:** **0x27** if the chip is a **PCF8574T**, **0x3F** if it's a **PCF8574AT**. Read the marking on the chip. The TC74 is 0x4D, so there's no conflict.
  - **Speed:**
    - Each LCD character needs several I2C bytes (each 4-bit half is sent with E high, then E low), so writing a full line takes a few ms at 100 kHz.
    - That's fine for refreshing once per second, but only refresh what changed, and never do it from an ISR.
  - **Shared bus = one user at a time:** the TC74 read and the LCD update must never overlap. They both run from the main loop (never from an ISR), so they're naturally one after the other.
- **The blue potentiometer** sets the **contrast**. If the LCD shows nothing, or only full black boxes, turn it first. The jumper next to the header switches the **backlight** on and off.
- **Pull-ups:**
  - These backpacks usually have SDA/SCL pull-ups on board (small resistors, often marked `472` = 4.7 kΩ or `103` = 10 kΩ, near the header). If so, they serve the TC74 too.
  - Keeping the PIC's WPU on as well is harmless.
- **Voltage, important:**
  - The backpack's pull-ups go to **its own VCC**. If the LCD is powered from **5 V** while the PIC runs at **3.3 V**, SDA/SCL would be pulled to 5 V, above what the PIC pins accept at 3.3 V.
  - **Power everything (PIC, TC74, LCD) from the same supply.** 5 V is the natural choice, because the LCD needs it for good contrast. The PIC16F18875 (1.8-5.5 V) and the TC74 (2.7-5.5 V) both work at 5 V.
  - The Curiosity HPC can supply the target with 3.3 V or 5 V (selectable on the board). Check that it's set to 5 V.
- **MCC:** nothing extra to configure. The LCD uses the MSSP1 I2C driver. `lcd.c` (C's part) is hand-written, because MCC has no HD44780/PCF8574 driver.


## 10. What MCC generated for MSSP1 (I2C)

New files: `i2c1_master.c/h` (the driver) and `examples/i2c1_master_example.c/h` (ready-made helpers like `I2C1_Read1ByteRegister()`). `SYSTEM_Initialize()` now also calls `I2C1_Initialize()`.

### Checking the generated values
- **`SSP1STAT = 0x80`:** bit 7 is `SMP = 1`, meaning slew rate control is **off**, which is right for 100 kHz standard mode (section 8).
- **`SSP1CON1 = 0x08`:**
  - `SSPM = 1000` → I2C Master mode, clock = Fosc / (4 × (SSP1ADD + 1)).
  - `SSPEN` is 0 here. The driver turns the module on when a transfer starts (`I2C1_Open`) and off again at the end.
- **`SSP1ADD = 0x09`:** 4 MHz / (4 × 10) = **100 kHz**.
- **Pins:**
  - `ANSELC = 0xC7`: bits 3, 4, 5 = 0, so RC3, RC4 (I2C) and RC5 (S2) are digital.
  - `WPUC = 0x38`: pull-ups on RC3, RC4, RC5. These are the I2C pull-ups decided in section 8.
  - `TRISC` stays all inputs. For I2C that's correct: the MSSP takes over the pins and drives them open-drain itself.
- **PPS:** each I2C pin is mapped both ways (section 8).
  - `RC3PPS = 0x14` (output: SCL1 on RC3) and `SSP1CLKPPS = 0x13` (input: SCL1 read from RC3).
  - `RC4PPS = 0x15` (output: SDA1 on RC4) and `SSP1DATPPS = 0x14` (input: SDA1 read from RC4).
  - Input PPS values encode the pin as port (A = 0, B = 1, C = 2) × 8 + pin number, so RC3 = 2·8 + 3 = 19 = 0x13 and RC4 = 0x14.
- **Interrupts:** `interrupt_manager.c` is unchanged, so the driver is the **polled** version, as chosen.

### The hang risk, confirmed in the generated code
- The polled driver waits for each I2C step with `while(1) { if (PIR3bits.SSP1IF) break; }`, which has **no timeout**. The `time_out` fields in the driver are only used by the interrupt version.
- The example helpers also loop `while (I2C1_BUSY == I2C1_Open(address));`.
- So if the bus is broken (TC74/LCD unplugged, no pull-ups, SDA or SCL stuck low), the main loop **freezes inside the I2C call**. The Timer1 ISR still runs, so the seconds keep counting, but nothing else happens (no LCD refresh, no buttons).
- **Debugging rule:** if the program "stops" at the first sensor read or LCD write, suspect the I2C wiring/pull-ups/address first, not the logic.


## 11. What MCC generated for ADCC

New files: `adcc.c/h`. `SYSTEM_Initialize()` now also calls `ADCC_Initialize()`.

### Checking the generated values
- **`ADCON0 = 0x84`:**
  - Bit 7 `ADON = 1`: ADC on.
  - Bit 2 `ADFM = 1`: right-aligned result.
  - `ADCS = 0`: the clock comes from Fosc through `ADCLK`.
  - `ADCONT = 0`: no continuous mode, one conversion per request.
- **`ADCLK = 0x03`:** divider = 2 × (3 + 1) = **FOSC/8** → TAD = 2 µs at 4 MHz.
- **`ADACQ = 0x0A`:** 10 TAD of acquisition = 20 µs.
- **`ADREF = 0x00`:** positive reference VDD, negative VSS.
- **`ADCON2 = 0x00`:** `ADMD = Basic_mode` (no averaging or filtering).
- **`ADPCH = 0x00`:** channel ANA0 = RA0.
- **Pin RA0** (no change needed in `pin_manager.c`):
  - `TRISA` bit 0 = 1 → input.
  - `ANSELA` bit 0 = 1 → analog.
  - `WPUA = 0x00` → no pull-up.
  - The generated pin macros are named `POT_*`.
- **Channel enum:** `POT = 0x0`, so a reading is `ADCC_GetSingleConversion(POT)`. The other entries (`channel_Temp`, `channel_FVR_buf1`, ...) are internal channels we don't use.
- **No ADC interrupt:** `interrupt_manager.c` is unchanged.

### How a reading works (`ADCC_GetSingleConversion`)
1. It selects the channel, turns the ADC on, and sets `ADGO = 1` to start.
2. The hardware waits the acquisition time (ADACQ), then converts.
3. The function waits in `while (ADCON0bits.ADGO)` until the hardware clears `ADGO`, about 20 µs + 24 µs ≈ **45 µs**.
4. It returns `ADRESH:ADRESL` (0-1023).

Like the I2C driver, this is a polled wait. Unlike I2C, it can't hang: the ADC always finishes on its own, there's no external device involved.

### Energy note (for the report)
- `ADON` stays **1** after init and after each reading, so the ADC module stays powered between readings. The idle current is small, but it isn't zero.
- Since `ADCC_GetSingleConversion()` sets `ADON = 1` itself, the code can turn it off after reading (`ADCON0bits.ADON = 0;`) with no other change.
- That's a possible energy-saving step: measure the current with and without it.


## 12. The remaining peripherals: EEPROM, PWM, EUSART

### EEPROM → Memory module
- **What it is:** the PIC16F18875 has **256 bytes of data EEPROM**. It's non-volatile (it keeps its content without power), so it's where the config block and the max/min records live (README, EEPROM layout).
- **MCC:** the **Memory** module with **"Add DataEE Routines"** ticked generates `DATAEE_WriteByte(addr, data)` and `DATAEE_ReadByte(addr)`. No interrupts.
- **Address note:** on this PIC, EEPROM is mapped at **0xF000-0xF0FF** in the NVM address space. Check in `memory.h` whether the generated functions take the full address (0xF000 + offset) or just the offset (0-255), and use README offsets accordingly.
- **Write time and interrupts:**
  - One byte takes about **4-5 ms** to write.
  - The MCC routine disables interrupts (GIE = 0) for the unlock sequence (`0x55`, `0xAA`, `WR = 1`) and **waits until the write is done**, with interrupts still off.
  - The unlock sequence must not be interrupted, or the write is refused. That's a hardware safety feature against accidental writes.
  - **Consequence:** during each byte write (~4 ms), the Timer1 and button ISRs are delayed. The Timer1 *counter* keeps running in hardware and the overflow flag stays set, so the ISR runs as soon as interrupts come back. **No second is lost**, just served a few ms late.
  - Saving the 12-byte config block blocks for ~50 ms. That's acceptable when it happens rarely (on config change, once per minute for CLKM), but it's another reason to write only bytes that changed.
- **Wear:** each EEPROM cell survives about 100k writes (README note about CLKM).

### PWM on LED D2 → TMR2 + PWM6
- **What PWM is:** a square wave with a fixed period and a variable **duty cycle** (% of the time high). On an LED, a fast PWM looks like a steady light whose brightness is the duty cycle. This is the alarm indication: D2 lights up via PWM for TALA seconds.
- **Why PWM6 (not CCP):** the PIC16F18875 has dedicated 10-bit PWM modules (**PWM6, PWM7**) and CCP modules that can also do PWM. PWM6 is the simplest: no capture/compare modes to configure. It needs a timer for its period, which is **Timer2**.
- **Timer2 clock: LFINTOSC.** This is the important decision:
  - If Timer2 ran from Fosc/4, it would **stop in sleep** (like synchronized Timer1, section 5). Then during the TALA seconds the system couldn't sleep, or the LED would freeze.
  - On this PIC, Timer2 can be clocked from **LFINTOSC**, which keeps running in sleep, so the **PWM keeps going while the CPU sleeps**. That saves energy during alarms, and the main loop doesn't need a special case.
  - To confirm on the hardware: start an alarm, check that D2 keeps lighting while the system is asleep.
- **Period:** LFINTOSC ≈ 31 kHz, prescaler 1:1, period register PR2 = 255 → 31000 / 256 ≈ **121 Hz** (~8.3 ms).
  - Above ~100 Hz, the eye doesn't see flicker, so it looks like a steady light.
  - PR2 = 255 gives the full duty resolution.
  - **How MCC shows it ("Timer Period" field):** MCC asks for the period as a *time*, not as PR2, and computes PR2 itself.
    - **Formula:** period = (PR2 + 1) × prescaler × T_clk, with T_clk = 1 / 31 kHz = **32.258 µs** (one LFINTOSC tick).
    - The range shown is **32.258 µs ≤ period ≤ 8.258065 ms**:
      - minimum = PR2 = 0 → 1 tick = 32.258 µs;
      - maximum = PR2 = 255 → 256 ticks = 256 × 32.258 µs = **8.258 ms**.
    - "8.3 ms" is just above the maximum, so MCC clamps it to 8.258065 ms. The "Actual Period" is 8.258 ms, which means PR2 = 255, as intended. Entering 8.258065 ms directly gives the same result.
    - Real frequency = 1 / 8.258 ms ≈ **121 Hz**. That's computed with LFINTOSC = exactly 31 kHz; the real LFINTOSC varies by a few %, so the real PWM frequency does too. That's irrelevant for an LED.
    - A longer period would need a bigger prescaler (1:2 → max 16.5 ms ≈ 60 Hz). But below ~100 Hz the LED starts to flicker visibly, so 1:1 with the maximum period is the right choice.
- **Duty 50%:** a clearly visible, medium-bright light. Changing it is one call (`PWM6_LoadDutyValue()`), which is a knob if the alarm should look brighter or dimmer.
- **Off at startup:**
  - PWM6 **not enabled** and Timer2 **not started** after init, so D2 stays dark until an alarm.
  - The alarm code (B's `alarm_out.c`) starts Timer2 / enables PWM6 for TALA seconds, then stops them.
  - Timer2 not running = no current wasted.
- **Pin:**
  - In the Pin Manager, **PWM6OUT → RA4** (D2) through PPS. The PPS mapping makes the PWM drive the pin instead of `LATA4`.
  - RA4 stays an output.

### EUSART (USART) at 9600 baud
- **What it is:** the serial port (asynchronous: TX and RX lines, no clock line). The project adds communication over it later (part 2). For now we only configure it, so the pins and the baud rate are fixed early.
- **Pins:** **TX = RC6, RX = RC7** (the EUSART defaults, and the mikroBUS 1 TX/RX lines, through 330 Ω resistors R39/R40 on the board).
- **9600 baud at 4 MHz:**
  - With BRG16 = 1 and BRGH = 1: SPBRG = 4 MHz / (4 × 9600) − 1 ≈ 103 → real baud 9615, **0.16 % error**. UART tolerates about ±2 %, so this is fine.
  - Check the error % that MCC shows.
- **Settings:**
  - 8 data bits, no parity, 1 stop bit (8N1, the standard).
  - Transmit and receive enabled.
  - **Interrupts off** for now (decided when part 2 is designed).
  - **No "Redirect STDIO"** (printf), because it costs code space we may need.
- **Timing (README):** 9600 baud ≈ 1 ms per byte. That's why every task must stay short and non-blocking: later, the USART will need servicing about every ms.


## 13. What MCC generated for EEPROM, PWM and EUSART

New files: `memory.c/h`, `tmr2.c/h`, `pwm6.c/h`, `eusart.c/h`. `SYSTEM_Initialize()` now also calls `PWM6_Initialize()`, `TMR2_Initialize()`, `EUSART_Initialize()`. `interrupt_manager.c` is unchanged, so none of these use interrupts.

### EEPROM (`memory.c`)
- `DATAEE_WriteByte(uint16_t addr, uint8_t data)` and `DATAEE_ReadByte(uint16_t addr)` take the **full address**: EEPROM byte n is at **0xF000 + n**. The README layout lists offsets, so e.g. the magic word (offset 0) is at 0xF000, and the records (offset 16) start at 0xF010.
- `NVMREGS = 1` selects the EEPROM/config space (not program flash).
- **The write confirms section 12:** GIE = 0, unlock `0x55`/`0xAA`, `WR = 1`, then `while (NVMCON1bits.WR)` waits ~4-5 ms **with interrupts off**, then GIE is restored. Reading is instant (two NOPs).

### Timer2 (`tmr2.c`)
- `T2CLKCON = 0x04` → **LFINTOSC**.
- `T2PR = 0xFF` → PR2 = 255 → 8.258 ms ≈ 121 Hz.
- `T2CON = 0x00` → prescaler 1:1, postscaler 1:1, **TMR2ON off** (stopped until an alarm).
- `T2HLT = 0x00`:
  - Software control (free-running, no external start/reset).
  - Prescaler **not synchronized** to Fosc. That matters for sleep, same reason as Timer1 (section 5): a synchronized prescaler would stop when Fosc stops.

### PWM6 (`pwm6.c`)
- `PWM6CON = 0x00` → **PWM6EN disabled**, active-high.
- `CCPTMRS1bits.P6TSEL = 1` → PWM6 uses **Timer2**.
- **Duty:**
  - `PWM6DCH = 0x7F`, `PWM6DCL = 0xC0`, so duty value = (0x7F << 2) | (0xC0 >> 6) = 508 + 3 = **511**.
  - The full scale is 4 × (PR2 + 1) = 1024, so 511 / 1024 ≈ **50 %**.
  - `PWM6_LoadDutyValue(0..1023)` changes it.
- **Pin:** `RA4PPS = 0x0E` → RA4 outputs PWM6OUT (D2). While PWM6 is disabled its output is low, so D2 stays off. That's why D2 is dark and D5 still blinks.
- **Turning the alarm light on/off later:** `TMR2_Start(); PWM6CONbits.PWM6EN = 1;` and `PWM6CONbits.PWM6EN = 0; TMR2_Stop();`.

### EUSART (`eusart.c`)
- `BAUD1CON = 0x08` → BRG16 = 1 (16-bit baud generator).
- `TX1STA = 0x24` → TXEN on, asynchronous, BRGH = 1 (high speed).
- `RC1STA = 0x90` → SPEN (serial port on), CREN (receive on), 8 bits.
- `SP1BRG = 103` → 4 MHz / (4 × 104) = **9615 baud** (0.16 % error).
- **Pins:**
  - `RC6PPS = 0x10` → TX on RC6. `TRISC = 0xBF` → RC6 is an output.
  - `RXPPS = 0x17` → RX read from RC7, which is digital (`ANSELC` bit 7 = 0) and an input.
- Error handlers are only defaults; nothing is called by interrupts.


## 14. `system.h` and `clock.c`: what they are for

### `system.h`: the shared contract
- It's the one header everyone includes. It defines **the data shared between modules**, so B and C can write their code before A's exists, all using the same names and types.
- It contains **declarations** (`extern ...`, types, `#define`s). The variables themselves are **defined once** in one `.c` file (e.g. `main.c`). A variable defined in a header would be duplicated in every file that includes it, and the linker would fail.
- **What goes in it:**

  | Item | Type | Why |
  |---|---|---|
  | Current time | `struct { uint8_t h, m, s; }` | read by the UI/LCD, the records and the alarm clock check |
  | Config parameters | `struct { pmon, tala, alaf, alah, alam, alas, alat, alal }`, all `uint8_t` | one byte each, same order as the EEPROM layout, so saving/loading is a byte copy |
  | Last readings | `uint8_t last_t, last_l` | written by the monitor (B), shown by the UI (C) |
  | Pending alarms | one byte with bits `ALARM_C`, `ALARM_T`, `ALARM_L` | set by the alarm checks, shown as letters, cleared by S1 |
  | Records | `struct { uint8_t h, m, s, t, l; } records[4]` (Tmax, Tmin, Lmax, Lmin) | exactly the 5-byte EEPROM format |
  | Event flags | `volatile uint8_t` tick counter / button flags | set by ISRs, consumed by the main loop |
  | "Config changed" flag | `uint8_t` | the UI sets it, the main loop saves to EEPROM |

- **Why `uint8_t` everywhere:** it's an 8-bit CPU. Every value fits in a byte (hh ≤ 23, tt ≤ 50, l ≤ 7), and byte operations are single instructions, so they're fast and naturally atomic.
- **One byte per ISR flag** (`s1_pressed`, `s2_pressed`), not bits packed into one `flags` byte:
  - With a shared byte, the main loop clearing bit S1 (`flags &= ~FLAG_S1`) is a read-modify-write.
  - If the S2 interrupt lands in the middle, it sets S2 in the byte, but the main loop then writes back its old copy, and the S2 press is lost.
  - Separate bytes can't interfere with each other: setting one to 1 or 0 is a single instruction.
  - `ticks` is a single byte for the same reason: `ticks++` in the ISR and `ticks--` in the main loop each compile to one instruction (`incf`/`decf`) on this PIC.
- **`volatile`:** any variable written in an ISR and read in the main loop must be `volatile`. Otherwise the compiler may keep a copy in a register and never see the ISR's change (e.g. `while (!flag);` loops forever).
- **Rule:** it's shared, so any change has to be announced to the group (README).

### `clock.c`: the time of day
- **Job:** keep hh:mm:ss from the 1 s Timer1 interrupt, with rollover (59 s → 0 and minute + 1; 59 min → 0 and hour + 1; 23 h → 0), and allow setting the time.
- **Functions:**
  - `clock_init(h, m)`: start from CLKH/CLKM loaded from EEPROM, seconds = 0. After a power cycle, the clock continues from the last saved minute.
  - `clock_tick()`: advance one second with rollover. It returns/flags when the **minute** changes, because the minute change is when CLKH/CLKM get saved to EEPROM.
  - `clock_set(h, m, s)`: used by the UI in config mode.
- **Design decision: the ISR only counts, the main loop advances the time.**
  - Timer1 ISR: `ticks++` (one instruction). The main loop: `while (ticks) { ticks--; clock_tick(); ... }`.
  - **Why not update hh:mm:ss inside the ISR** (as first sketched in the README architecture):
    - **Torn reads:** the time is 3 bytes. If the ISR changes them while the main loop is reading them (e.g. copying them to the LCD at 12:59:59 → 13:00:00), the main loop can get a mix like 12:00:00 or 13:59:59. With the ISR only counting, the time is changed only by the main loop, so this can't happen.
    - **`clock_set()` races:** same problem the other way around. The UI writing h/m/s while the ISR increments them.
    - **Matches the cyclic executive rule:** ISRs only update counters and set flags (README).
  - **No second is lost** even if the main loop is busy (an EEPROM write, an LCD update): the counter accumulates, and the loop catches up on the next pass.
  - The main loop runs after every wake-up anyway (the Timer1 interrupt wakes the CPU), so the clock still advances during sleep periods.
- **"Setting the clock doesn't stall the seconds"** (a requirement and a test): `clock_set()` only changes the variables. Timer1 is never stopped or reloaded, so the ticks keep coming at the same rate.
- **Testing the rollover logic:** `clock_tick()` is plain C with no hardware access, so it can be compiled and tested on the PC with gcc (e.g. 86400 ticks from 00:00:00 must come back to 00:00:00, and 23:59:59 + 1 = 00:00:00). That's faster than waiting on the board.


## 15. The `records` variable

- **What the project asks for:** keep the **maximum and minimum** temperature and luminosity seen so far, each with the **time it happened**. They survive power cycles (EEPROM offsets 16-35), are shown with S2 (T max/min for 1 s, then L max/min for 1 s), and are cleared with the **R** option in config mode.
- **One record = 5 bytes = `record_t { h, m, s, t, l }`:** the time of the event plus **both** readings at that moment. For example, the Tmax record says "at 14:32:05 the temperature was 31 °C (the highest so far), and the luminosity at that moment was 5". The project statement defines the record this way (h, m, s, T, L).
- **`records[4]`, indexed by the enum:**

  | Index | Name | Updated when |
  |---|---|---|
  | 0 | `records[REC_TMAX]` | a new reading has T > the stored `t` |
  | 1 | `records[REC_TMIN]` | T < the stored `t` |
  | 2 | `records[REC_LMAX]` | L > the stored `l` |
  | 3 | `records[REC_LMIN]` | L < the stored `l` |

  On each update, the whole record (time + T + L) is overwritten with the current values.
- **Why an array + enum instead of 4 separate variables:**
  - The EEPROM layout stores the 4 records back to back: 4 × 5 = 20 bytes, offsets 16-35. With an array, saving/loading is one loop over 20 bytes in memory order, and the address of record i is `0xF000 + 16 + 5·i`.
  - `REC_COUNT` (= 4) is the array size, so the size and the names always agree.
- **Who uses it:**
  - B (`monitor.c`) updates it after each reading.
  - C (`ui.c`) displays it.
  - B (`nvm.c`) saves/loads it.
  - The R option (C) resets it.
- **Reset value:** after R (or invalid EEPROM), the next reading should become all four records. A simple way is to set max `t`/`l` to 0 and min `t`/`l` to 255, so any real reading replaces them.

### Build check
- `system.h` + `main.c` compile together with all MCC files with XC8 v3.10 (PIC16F18875, DFP 1.24.387): no errors.
- The only warnings come from the generated files: implicit conversions, and "function is never called" for driver functions we don't use yet. They're harmless, and we don't edit generated files.


## 16. `clock.c` in detail

- **Files:** `clock.h` (the 3 function declarations) and `clock.c`. It uses the shared `now` from `system.h`; it doesn't own its own copy, because the LCD, the records and the alarm check all read `now`.
- **`clock_tick()`:**
  - `if (++now.s < 60) return 0;` increments first, then compares. 59 → 60 fails the test and falls through to the rollover.
  - The rollover goes in a chain: seconds 60 → 0 and minute + 1; minutes 60 → 0 and hour + 1; hours 24 → 0.
  - **Return value 1 = "the minute changed".** That's the moment to save CLKH/CLKM to EEPROM (the requirement: hh:mm survive a power cycle). It's returned instead of saving inside `clock_tick()`, so `clock.c` stays pure logic (no EEPROM, no hardware), and saving is the main loop's decision. That also makes it testable on the PC.
  - `>=` instead of `==` in the minute/hour checks: if a bad value ever got in (e.g. h = 30), it still rolls back to a valid range instead of counting up to 255.
- **`clock_set()`:**
  - It only writes the 3 variables and **never touches Timer1**, so the seconds keep coming at the same rate ("setting the clock doesn't stall the seconds").
  - The sub-second phase isn't reset: after setting the time, the next second can come anywhere between 0 and 1 s later. That's acceptable for a clock with no displayed fractions of a second.
- **`clock_init(h, m)`:** starts at h:m:00, with the values that come from EEPROM (CLKH/CLKM). Until B's `nvm.c` exists, it's called with the defaults (0, 0).
- **Wiring in `main.c`:**
  - The Timer1 handler becomes just `ticks++;`.
  - The main loop runs `while (ticks) { ticks--; ... clock_tick() ... }`.
  - The D5 blink moves from the ISR into that loop. It's "work", so by the cyclic-executive rule it belongs in the main loop, and if D5 blinks, it proves the whole chain ISR → counter → main loop works.
- **The PC test (`project.X/test/test_clock.c`, not part of the MPLAB project):**
  - 23:59:59 + 1 s = 00:00:00, with "minute changed" = 1.
  - A normal second returns 0.
  - 86400 ticks (one day) from 12:00:00 come back to 12:00:00, with exactly 1440 minute changes.
  - Run it with `gcc -I. test/test_clock.c clock.c -o test_clock && ./test_clock` from `project.X/`. It prints `clock ok`, or stops at the first failing `assert`.
  - The test defines its own `now`, because on the PC there's no `main.c` to provide it.
- **Verified:**
  - The project builds with XC8 v3.10: program memory **591 of 8192 words (7.2 %)**, data memory **91 of 1024 bytes (8.9 %)**. That's plenty of room left.
  - The PC test passes (`clock ok`).
  - D5 blinks as before on the board.
