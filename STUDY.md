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
  - If the board already has external pull-ups, the internal one is harmless (it's just in parallel).
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
  - **Why not SOSC:** SOSC needs an external 32.768 kHz crystal, and this board doesn't have one fitted. With no crystal, SOSC simply never oscillates and the clock would never tick.
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
