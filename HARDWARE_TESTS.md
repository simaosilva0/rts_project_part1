# Final hardware test checklist

For the current PIC16F18875 firmware, 20 October 2026. Record PASS, FAIL or
NOT RUN for each ID. A missing instrument or debugger feature is NOT RUN.
This checklist does not mark anything as already passed.

## Preparation

- Use the successful production build you just checked. In MPLAB X, use
  **Make and Program Device** if that build is not already on the board.
  Run the main tests outside Debug mode, without breakpoints.
- Have a phone stopwatch and camera ready. Video is useful for short record
  screens, PWM duration and intermittent resets.
- Keep the LCD and TC74 connected. Record power source and initial settings.
  Existing EEPROM may override defaults: do not assume PMON=5 and TALA=3 just
  because those are the source defaults. If unknown, say so; advanced test A1
  below lets you inspect them.
- These tests intentionally change settings and reset records. Photograph any
  settings or records you want to keep first.
- Use ordinary press-and-release actions. Leave about 0.2 seconds after release
  before the next press so the debounce lockout can finish.
- With PMON=5, hold each new potentiometer position for at least 6 seconds.
  Generally allow PMON+1 seconds for a new sample. Do not mistake sampling delay
  for a potentiometer or LCD fault.

### Button reference

From normal mode, press S1 once for the threshold preview. Press S1 again within
2 seconds to enter configuration reliably. The cursor starts on clock hours.

S1 advances: clock hours, minutes, seconds, C, T, L, A/a, R, normal mode.
S2 increments clock fields. On C/T/L, the first S2 selects editing; subsequent
S2 presses increment the selected threshold. C edits hours, minutes and seconds
using S1; after the final subfield, S1 returns to C selection, then advances to T.
For T/L, S1 leaves editing, and another S1 advances to the next selection.
On A/a, S2 toggles alarms. On R, S2 resets and saves the records immediately.

S2 in normal mode displays temperature records, then luminosity records.
Top row is maximum, bottom row is minimum. Each row contains time, T and L.

### Accepted behavior to account for

- Threshold preview currently lasts 2-3 seconds; each record view lasts 1-2
  seconds. Measure these, but do not classify the already-known durations as
  a new failure. The specification asks for 2 seconds and 1 second respectively.
- After record reset, a maximum whose first value is zero retains placeholder
  data until a value greater than zero arrives. Test this separately in H10.
- UI configuration saves can wait until a subsequent wake. Wait at least
  2 seconds after the final edit before the normal persistence test.
- `!` is based on the last attempted TC74 reading. It can show at startup until
  the first successful sample. PMON=0 freezes that status along with sampling.
- S1 clears alarm notifications in normal/record mode. It does not stop an
  already-running PWM interval. A still-active condition can alarm again at a
  later sample after S1. The preview/configuration screens show C/T/L as field
  labels, so judge latched alarm letters after returning to normal mode.

## Main tests: production firmware on the board

### H01 - Startup and basic activity

1. Power off for 5 seconds and power on without reprogramming.
2. Watch the first 10-15 seconds, or longer than PMON if necessary.
3. Check LCD initialization, advancing seconds, D5 and sensor values.

Expected: normal screen, D5 changes state every second (a full on/off cycle takes
two seconds), and `!` clears after a successful sample. Temperature is plausible
for the room and L is between 0 and 7. Initial clock/configuration can come from
EEPROM. D2 is initially off but a valid alarm can subsequently activate it.

Report: startup screen/photo, time until `!` clears, T/L, any repeated boot or
LCD initialization, and whether the clock resumes a saved hour/minute.

### H02 - UI navigation and field wraparound

1. Press S1 once and wait for the automatic return; time it.
2. Enter configuration with two presses. Traverse all fields with S1.
3. On C/T/L, verify that the threshold replaces the current value in its field.
4. Exercise wraps: clock hours 23->00, minutes 59->00, seconds 59->00,
   alarm hours 23->00, alarm minutes/seconds 59->00, ALAT 50->00, ALAL 7->0.
5. Toggle A/a both ways. End with alarms disabled (`a`). Exit configuration.
6. Press S2 in normal mode and record the sequence and screen durations.

Expected: every listed field is reachable; S2 changes the intended field;
record order is T maximum/minimum, then L maximum/minimum, then normal mode.
The live clock continues while setting fields; its own ticking can coincide
with a seconds edit. Do not judge clock accuracy during deliberate edits.

Report: any skipped/stuck field, every failed wrap, measured preview and record
durations. A short video is sufficient for uncertain behavior.

### H03 - Clock accuracy, activity and natural rollover

1. Start on a clearly observed LCD second transition and start the stopwatch.
   Write down the starting hh:mm:ss.
2. Let it run for at least 10 real minutes. Leave it untouched for the first
   five minutes; during the rest, occasionally open threshold and record views.
   Do not change clock fields during this measurement.
3. At the end, record stopwatch elapsed time and LCD hh:mm:ss. A video showing
   both clocks gives a better comparison than two separate observations.
4. Separately, set the clock close to 23:59:50, exit configuration and watch
   the natural rollover through 23:59:59 -> 00:00:00.
5. Leave configuration open on a clock field for several seconds without S2;
   confirm time still advances. Then edit a field and check that ticking resumes
   normally without a lasting pause.

Expected: no freezes, resets or skipped whole sequences; correct natural
rollover. LFINTOSC can drift, so report the measured error rather than applying
an invented accuracy tolerance. Error = displayed elapsed seconds - stopwatch
elapsed seconds; positive means the PIC runs fast.

Report: start/end times, reference duration, signed error, rollover result,
and whether button activity visibly interrupted the clock.

### H04 - Potentiometer range and sampling

1. Keep alarms disabled. Sweep the pot slowly, pausing at each position long
   enough for a sample. Reach both endpoints.
2. Find all levels 0-7. Hold one mid-range position for 30 seconds.
3. Immediately after a displayed level change, move the pot clearly to another
   level and measure the delay until the next update. Repeat three times.

Expected: both endpoints and all eight levels are reachable; fixed positions
away from boundaries are stable. Slight alternation near a boundary is possible.
A random movement can take anywhere from nearly zero to PMON seconds to appear.
For steady-period measurement, use A1/A2 or a logic analyzer rather than assuming
each hand movement coincides with a sample.

Report: reachable levels, stability, observed update delays, any reversed or
abrupt behavior. Uneven knob-angle spacing alone does not establish a defect.

### H05 - Threshold LEDs while alarms are disabled

1. Set ALAF=0 (`a`) and ALAL=4. Exit configuration.
2. Obtain displayed L=3, then 4, then 5, waiting for sampling at each position.
3. Note the current stable temperature T. Set ALAT below T, then equal to T,
   then above T. Recheck the actual displayed temperature at each comparison.

Expected: D3 on for L=3; off for L=4 and 5. D4 on only when displayed valid
temperature is strictly greater than ALAT. No new alarm letters or D2 interval
with alarms disabled. An old pending notification should first be cleared by S1.

Report: L/ALAL/D3 and T/ALAT/D4 for each case, plus unexpected D2/letters.

### H06 - Luminosity alarm, duration, persistence and rearming

1. Set ALAT=50 and alarm time away from the current time to isolate L.
   Set ALAL=4 and ALAF=1. Begin with L>=4 and return to normal.
2. Move to L<=3. Start timing when D2 lights, not when you move the pot.
3. Keep L low for at least three monitoring periods. Do not press S1.
4. Move L>=4, wait for a sample, then L<=3 and wait again, still without S1.
5. After D2 has finished, leave L low and press S1 once. Wait through the
   preview and the next monitoring sample.

Expected: the first low sample sets L and D3, and D2 runs for TALA. L remains
after D2 turns off. There is no repeated D2 interval in steps 3-4 for that same
uncleared alarm. After S1, the still-low condition can trigger a fresh interval.

Report: TALA if known, three measured D2 durations if practical, retained L,
whether D2 repeated without S1, and rearming result. Phone video is adequate
for a rough seconds measurement; brightness alone does not prove PWM frequency.

### H07 - Temperature alarm

1. Set ALAL=0 to avoid L alarms. Keep clock alarm away from now.
2. With ALAF=1, set ALAT one or two degrees below stable measured temperature.
   Return to normal and wait for a sample.
3. Leave it for three periods without S1. Then acknowledge with S1 and wait.
4. Set ALAT above the current temperature and observe D4 after returning.

Expected: T letter, D4 on and a TALA interval on D2; no repeated interval
without acknowledgment; rearming after S1 while T>ALAT. D4 goes off when
T<=ALAT. A previously latched T letter is separate from D4's live condition.
No artificial heating is needed to test the alarm comparison.

Report: T, ALAT, D4, T letter, D2 duration and rearming behavior.

### H08 - Clock alarm

1. Set ALAT=50, ALAL=0 and ALAF=1. Set the alarm clock about two minutes ahead
   of the current time, allowing enough time to exit configuration.
2. In normal mode, film the LCD as it reaches the programmed hh:mm:ss.
3. Wait at least 10 seconds after the event, then press S1.

Expected: C and D2 activate at the matching displayed time. C remains after
TALA expires; no repeat every second. S1 clears C, with no immediate recurrence
once the alarm time has passed.

Report: programmed alarm time, observed trigger time, D2 duration and clearing.

### H09 - Disable alarms and exercise multiple alarm types

1. Disable alarms, keep one sensor beyond its threshold and return to normal.
   Observe for three monitoring periods. LEDs D3/D4 should still follow thresholds.
2. Re-enable alarms and confirm an active condition can notify again.
3. Enable both T and L conditions together. Observe both letters and D2.
4. If practical, trigger a different alarm while D2 is already on, using a
   clock alarm scheduled near a sensor event. Record the actual event times.

Expected: disabled alarms generate no new letters/PWM. Both active types can
show letters. Current implementation does not extend/restart an active D2
interval when a different alarm arrives during it. If you cannot arrange this
overlap reliably, mark only step 4 NOT RUN.

Report: disable/enable results, combined letters, and overlap behavior if tested.

### H10 - Records, paired values and reset

1. Disable alarms. Set L around 3 with a stable positive temperature, then
   select R/S2. After the next sample, use S2 from normal mode and film all rows.
2. Hold L=1 for a sample; record the approximate sample time and temperature.
   Then hold L=6 for a sample. View records again.
3. Gently warm the TC74 package with a finger if accessible without touching
   exposed contacts, then let it cool. Allow several samples and inspect T
   extremes. A noticeable temperature change is enough; never use a flame or
   other strong heat source.
4. Set L=3 again, reset records, allow a sample and verify the old 1/6 extremes
   have been replaced by new records.
5. Separately, hold L=0, reset records and allow a sample. Observe the accepted
   Lmax placeholder case. Then increase L above zero and confirm Lmax updates.

Expected: each extreme row carries both values and its timestamp. A luminosity
record's temperature is the temperature at that sample, not necessarily Tmax.
Equal values do not refresh timestamps. With valid positive first samples,
reset establishes new extrema; sampling may repopulate them before you view
an empty state. Step 5 records the known limitation rather than a new failure.

Report all four rows as: Tmax=(h,m,s,T,L), Tmin=(...), Lmax=(...), Lmin=(...).
Include before/after reset captures and whether warming produced a new extreme.

### H11 - Configuration, clock and record persistence

1. Set distinctive values, for example alarm=13:17:19, ALAT=31, ALAL=4,
   ALAF=0. Note current clock and capture all four records.
2. Wait through a natural minute rollover, then another 5 seconds. Record the
   current hh:mm just before powering off. Avoid changing sensor extremes.
3. Switch power off for 5 seconds, then on. **Do not reprogram the PIC.**
4. Film startup: verify restored hh:mm, with seconds starting at 0 rather than
   including time spent powered off. Check the thresholds and A/a.
5. Check records promptly. Existing records should survive; a new extreme after
   reboot may legitimately replace one. For an exact frozen comparison use
   PMON=0 through A1 before shutdown, then repeat this test.
6. Repeat the power cycle once more.

Expected: saved settings return, saved hour/minute returns, seconds restart at
0, and records survive unless legitimately updated by new samples.

Report: settings and hh:mm before/after, four records before/after, and whether
there was any reprogramming between captures.

### H12 - TC74 missing at boot and restored

1. Power off. Disconnect only the TC74 connection; keep LCD and bus pull-ups
   connected. Avoid leaving loose wires that can short together.
2. Power on. Wait beyond PMON; try the clock, UI and potentiometer. With ALAL=4,
   check D3 below/at/above 4. Optionally enable L alarms to test their independence.
3. Power off, reconnect TC74 correctly and power on again.

Expected: without TC74, `!` stays visible, D4 is off, and luminosity, D3, clock
and UI continue. No new paired records are collected without a valid temperature.
After reconnection and reboot, `!` clears following a successful sample.
This verifies missing-at-boot/reconnected-at-boot behavior, not live hot-plug
recovery. A bus line held low is a different, known unsupported fault.

Report: `!`, clock/UI responsiveness, L/D3 operation and successful recovery.

### H13 - Buttons and sustained operation

1. Hold S1 for 2 seconds and release. In a configuration field, hold S2 for
   2 seconds and release. Check for unintended repeated actions.
2. Perform 20 deliberate press/release cycles with at least 0.2 seconds between
   release and the next press. Note ignored or doubled actions.
3. Run for 15-20 minutes with occasional record views, threshold changes and
   sensor alarms. Avoid deliberate clock edits while checking continuity.

Expected: a held button produces one action; ordinary presses are reliable;
no spontaneous reset, LCD corruption, stuck UI or stopped clock. Very rapid
presses during lockout can be intentionally ignored. Test simultaneously held
buttons only as an observation; the project does not specify a two-button chord.

Report: doubled/missed actions, duration of the run and any failure's exact
screen, settings and preceding button/sensor action.

## Advanced tests: debugger or instruments

Run these after the production tests. Debug halts affect time and power, so do
not mix their results into H03 or power measurements. Never assume an edited
IDE memory buffer has already changed the running device.

### A1 - PMON=0, 1 and 5; inspect TALA

1. Start an MPLAB X debug session with the existing firmware. Pause after
   initialization. Add watches for `cfg.pmon`, `cfg.tala`, `cfg_dirty`, `now`,
   `last_t` and `last_l`. Record current settings.
2. If your debugger permits variable modification, set `cfg.pmon=0` and
   `cfg_dirty=1`. Resume, allow 2 seconds to settle, then move the pot and wait
   15 seconds. Sensor values should stay unchanged while clock/UI continue.
   A clock alarm should still work. The `!` state can remain unchanged too.
3. Pause, set PMON=1 and cfg_dirty=1, then resume. Allow the wake transition,
   and check approximately one-second sensor updates.
4. Repeat with PMON=5; check approximately five-second periods after settling.
5. Restore your intended PMON/TALA, set cfg_dirty=1 and resume long enough to
   save. Return to a production run for final checks.

Report: values, observed periods, PMON=0 behavior, any `!` that remains stuck
after resuming sampling. If watch editing is unavailable, mark NOT RUN and
report what the IDE exposes; no source change is required just for this step.

### A2 - PWM and TC74 standby on a scope/logic analyzer

- Probe D2/RA4 with a common ground. Capture one alarm. Expected nominal PWM is
  about 121 Hz, approximately 50% duty, lasting TALA. Measure, do not infer duty
  from apparent LED brightness. Use equipment compatible with the 5 V board.
- If I2C decoding is available, probe SCL/SDA. At PMON=5 expect TC74 address
  0x4D: config-register write 0x00 about one second before sampling, readiness
  and temperature reads, then config-register write 0x80 for standby. The first
  startup cycle can differ because the sensor begins awake.
- At PMON=1 there should be no repeated standby cycling; PMON=0 stops periodic
  sensor reads. LCD traffic at address 0x27 continues and must not be mistaken
  for TC74 traffic.

Report: frequency, duty, duration, sample spacing, standby/wake trace or NOT RUN.

### A3 - Invalid EEPROM configuration recovery

This deliberately invalidates configuration. Save desired settings first.
Only perform it if your debugger supports reading and writing **device Data
EEPROM** without erasing/programming the whole application.

1. Set a breakpoint just before `nvm_load()` and restart the debug run to reach
   it. Read and save EEPROM offsets 0-35 (mapped addresses 0xF000-0xF023).
2. Leave magic at 0xA5, change checksum offset 11 to a different byte, and
   confirm the changed byte is actually in device EEPROM. Continue through load.
3. Inspect cfg and the clock: defaults should be PMON=5, TALA=3, ALAF=0,
   alarm=12:00:00, ALAT=25, ALAL=3, clock=00:00:00 at initialization.
4. Verify previously valid records survived independently of the bad config.
5. Repeat with invalid magic (offset 0) if practical. Restore intended settings
   afterwards and allow them to save.

Report: original/changed bytes, defaults loaded, record survival, and whether
the operation altered only EEPROM. If the IDE only offers buffer editing or
full-device programming, stop this test and mark NOT RUN rather than treating
a full erase as proof of checksum validation.

### A4 - Energy saving

If you have a suitable meter/current probe, measure steady operation with the
LCD and connections fixed, outside Debug mode. Record supply voltage, where
current is measured, LCD backlight state, LED states and averaging method.
Use a series current connection, never put a meter in current mode directly
across the supply.

A valid sleep-versus-no-sleep comparison needs an otherwise identical control
build with sleep disabled. If no such build is available, record absolute
current only and mark the comparison NOT RUN; debug versus production is not
a controlled comparison. Coordinate a temporary control build with A if needed.
The LCD/backlight and onboard debugger may dominate whole-board consumption.

Report: measurement setup, readings, and exactly which builds/modes were compared.

## Send back this template

Copy and fill this in; send results in batches if easier. For a failure include
the exact steps, actual values and a photo/video when useful. Do not just write
"does not work". For an accepted limitation write KNOWN plus the observation.

```text
Build tested / programmed when:
Production or Debug:
Power source:
PMON / TALA (or unknown):

H01 Startup: PASS/FAIL/NOT RUN - ! cleared after __ s; T=__; L=__
H02 UI: __ - threshold __ s; T records __ s; L records __ s; wraps __
H03 Clock: __ - start __; end __; reference __ s; error __ s; midnight __
H04 Pot: __ - levels reached __; stable __; update delays __
H05 LEDs: __ - L/ALAL/D3 __; T/ALAT/D4 __; disabled notifications __
H06 L alarm: __ - D2 durations __; no repeat __; S1 rearm __
H07 T alarm: __ - T/ALAT __; D4 __; D2 __; S1 rearm __
H08 C alarm: __ - set __; triggered __; D2 __; clear __
H09 Disable/multiple: __ - disabled __; enabled __; overlap __
H10 Records: __ - before/after captures __; zero case __
  Tmax=(h,m,s,T,L):
  Tmin=(h,m,s,T,L):
  Lmax=(h,m,s,T,L):
  Lmin=(h,m,s,T,L):
H11 Power-cycle: __ - settings before/after __; clock __; records __
H12 Missing TC74: __ - ! __; clock/UI __; luminosity __; restored __
H13 Buttons/soak: __ - missed/double presses __; ran __ min; resets __

A1 PMON 0/1/5: __
A2 PWM/I2C measurements: __
A3 Invalid EEPROM recovery: __
A4 Current measurement/comparison: __

Unexpected behavior and exact reproduction:
Final settings left on board:
```
