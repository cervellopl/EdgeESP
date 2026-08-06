# EdgeESP

A GPS bike computer in the spirit of the Garmin Edge 520, built on an ESP32-S3, a u-blox
receiver, and the MCUFRIEND 3.6" 480×320 parallel TFT shield.

Records real **.fit** files that upload straight to Strava, Garmin Connect or komoot.
Connects to your phone over BLE. Runs off an internal cell or external USB power.

```
pio run -t upload -t monitor
```

Builds clean and flashes to 1.2 MB. **Read [HARDWARE.md](HARDWARE.md) before wiring
anything** — the 5 V shield needs a level-shifting decision, and the ESP32-S3 module
variant matters.

---

## What it does

**Ride computer** — speed, distance, moving/elapsed timers, average and max speed,
barometric elevation, ascent/descent, grade, temperature, calories. Auto-pause and
auto-lap, both on by default, both configurable.

**GPS** — u-blox NEO-M8/M9/M10 over UBX binary at 5 Hz. NMEA is disabled entirely; the
firmware auto-detects the module's baud rate, configures it (VALSET on M9/M10, legacy
CFG on M8), and reads a single `NAV-PVT` frame per solution.

**Sensors** — BLE heart rate, speed/cadence, and power meters. Pairs once, reconnects
automatically, survives the sensor going out of range mid-ride. Shows 3-second,
30-second and normalized power. *(ANT+ is not possible on ESP32 — see HARDWARE.md.)*

**Turn-by-turn navigation** — load a `.gpx` route from the card or upload one over
Wi-Fi. Turns are found in the course geometry itself, so a bare GPX with no cue sheet
still gets guidance: a drawn turn arrow, the angle, and a distance countdown announced at
400 m, 150 m and at the corner. Waypoint cues from RideWithGPS/komoot exports merge in
and supply street names. Leave the route and you get a red banner with a compass arrow
and a clock direction back to it. With no course loaded at all, **Navigate back to
start** turns the ride's own breadcrumb into a route home.

The map becomes a rider-centred navigation view with the route ahead in blue and the part
you have ridden dimmed; the Nav page shows the turn, the two after it, plus distance and
climb remaining, ETA and cross-track offset. All of it is covered by a
[host-side test suite](tools/course-test/) — 95 assertions over fourteen GPX shapes, no
hardware needed.

**Recording** — writes `.fit` and `.gpx` simultaneously to microSD, one record per
second, flushed every 15 s. Laps, session and activity messages are written per the FIT
profile. If the battery goes critical the ride is closed and saved automatically; if the
power vanishes without warning, the ride is **picked up again at the next boot**.

**Phone link** — BLE GATT server exposing Nordic UART, Battery and Device Information
services. Streams live telemetry as JSON, accepts start/stop/lap/save commands, sets the
clock, and shows incoming-call or message banners pushed from the phone. A ready-made
[Web Bluetooth companion page](companion/index.html) is included — open it in Chrome on
Android and press Connect; no app install, no build step.

**Wind and weather** — the head unit has no internet, so the companion fetches a forecast
for wherever the GPS says you are and pushes it down the BLE link. You get current
conditions, a 12-hour graph, sunset with daylight remaining, and a rain warning before it
arrives. Most usefully it resolves the wind against your heading: a status-bar number
showing headwind or tailwind in km/h on every page, and a wind dial on the weather page.
The headwind also feeds the power estimate, so a ride into a gale no longer reads as
suspiciously easy.

**Wi-Fi portal** — off by default. Toggle it from the menu to raise an access point (or
join a saved network) serving a page that lists and downloads recorded rides, shows live
data, and accepts firmware updates over the air.

**Power** — Li-ion gauge with a real discharge curve, backlight PWM with auto-dim,
deep sleep on the button ladder, and a runtime estimate. Roughly 9–11 h from a 3400 mAh
18650. Graded low-battery warnings during a ride, ending in the ride being closed and
saved while there is still power to write the file.

---

## Screens

Seventeen pages, `UP`/`DOWN` to move between them — and **Menu → Pages** switches off
the ones you do not use, so the rotation is only as long as you want it:

| Page | Contents |
|---|---|
| **Ride 1** | Three large fields by default: speed, distance, timer — editable on the device |
| **Ride 2** | Six fields by default: avg speed, HR, cadence, power, ascent, grade — editable |
| **Summary** | The whole ride on one screen — distance and time large, then every total the ride actually has. Shown automatically when you save |
| **Workout** | The step you are in, a large countdown, your value against the target band, and what comes next |
| **Power zones** | Current zone and watts, ride avg/NP/max, then time in each of the seven power zones as a bar chart with ranges and percentages |
| **HR zones** | The same for the five threshold-HR zones, with ride average and max heart rate |
| **Cadence zones** | Five absolute rpm bands, plus coasting time counted separately and the share of the ride actually spent pedalling |
| **Gear** | The gear you are in, its ratio, gear inches and development, and the whole gear table with the current one lit and cross-chained combinations marked |
| **Load** | This ride's TSS, IF, NP and kJ; fitness, fatigue and form; and 90 days of daily stress with the two curves over it |
| **Laps** | The lap in progress in large figures, then a table of completed laps newest-first with distance, time, average speed, HR, power and climb; the fastest lap highlighted |
| **Map** | No course: breadcrumb auto-scaled to your ride. With a course: rider-centred nav view, route ahead in blue, ridden part dimmed, turn markers, off-course tether, and a guidance band with the next turn |
| **Nav** | The turn you are riding into with a drawn arrow and distance countdown, the two turns after it, progress bar, distance/climb remaining, ETA, cross-track offset |
| **Compass** | Track-up rose with the start marked on the ring where it actually lies, heading in degrees and cardinal, bearing and straight-line distance to the ride start |
| **Weather** | Conditions and icon, wind dial resolved against your heading with head/cross components, 12-hour temperature and rain graph, sunset and daylight left |
| **Weather history** | Eight hours of on-board temperature and pressure as stacked graphs, with the barometric tendency and the ride's high and low |
| **Profile** | The whole ride's elevation against distance, filled and coloured by gradient — or the whole course with your position on it |
| **Status** | GPS detail, every sensor, course, barometer, battery, storage, Wi-Fi |

## Gear

The **Gear** page shows which gear you are in — `50 x 17` — with its ratio, gear inches
and development, and the whole gear table underneath with the current combination lit.

**Nothing here is read from the drivetrain.** There is no electronic shifting to ask, so
the ratio is *measured* — wheel revolutions per crank revolution — and matched against the
chainrings and cassette you pick in **Settings → Gearing**. Eight presets cover the common
2x and 1x setups; the table itself is in `src/ride/Drivetrain.cpp`.

Two honest limits come with that:

- **The sprocket is reliable, the chainring is inferred.** On a 2x, two combinations can
  produce nearly the same ratio (50x21 and 34x14 are within a couple of percent). The page
  biases toward the ring you were already in, because chainring shifts are rare — and says
  *chainring uncertain* when a second combination fits about as well.
- **It needs speed and cadence together.** Coasting has no cadence and therefore no gear;
  below 25 rpm or 2 m/s the ratio is a division by noise and the page says so rather than
  guessing. A measured ratio that matches no real gear by more than 8 % is refused
  outright — that means the wheel size or the cassette is wrong, and naming a gear would
  be fiction.

Speed comes from a **wheel sensor** when one is paired, falling back to GPS. That matters
more here than anywhere else: the ratio divides by cadence, which magnifies both the lag
and the wander in a GPS speed.

Cross-chained combinations are marked in the table before you shift into one, and flagged
on the current gear. A 1x drivetrain can never cross-chain, and is not told that it can.
The footer answers the question the table is usually being read for: what cadence one
sprocket harder would give you at this speed.

## Zones

Three pages, one per sensor: **Power zones** covers Coggan's seven bands as percentages of
FTP, **HR zones** the five threshold-HR bands, **Cadence zones** five absolute rpm bands.
Each shows the zone you are in right now,
the ride's averages, and where the effort has actually gone — time in each band with its
absolute range, a bar and a percentage.

They are separate pages rather than one page that switches source, because a page that
silently changes what it is showing is worse than two honest ones — and a rider with both
sensors wants both, not power instead of heart rate.

**A zone page you have no sensor for is skipped in the page rotation.** With fourteen
pages, cycling past an empty chart is noise; the page reappears the moment its sensor
produces something.

### Cadence is not like the other two

There is no threshold cadence to take a percentage of, so the bands are **absolute rpm**
and are a convention rather than a derivation — edit the table in `src/ride/Zones.cpp` if
yours differ. The colours diverge instead of ramping: grinding and over-spinning are both
awkward and the comfortable place is in the middle, so a hot-at-the-top ramp would wrongly
imply that turning the pedals faster is always harder work.

**Zero rpm is coasting, not the bottom zone.** Freewheeling down a hill is not grinding,
and folding it into Z1 would make the whole page useless on any descent. Coasting gets its
own counter and its own line, the zone bands cover pedalling only, and the header shows
what share of the ride you were actually turning the pedals. A cadence sensor reporting
0 rpm is also distinct from no sensor at all — the first is coasting, the second is nothing.

Time accrues only while the timer runs, so a coffee stop is not filed as an hour of active
recovery. Bars are scaled to the **busiest** zone rather than to total time — scaling to
the total renders a ride spent mostly in one band as six invisible slivers. Zone time uses
**instantaneous** power, not the three-second average: the smoothed figure exists for
holding a target, the raw one is what the ride was made of.

## Training load

The **Load** page scores the ride you are on and puts it in context: TSS, intensity
factor, normalised power and kilojoules across the top; **fitness, fatigue and form**
beneath; and ninety days of daily stress as a bar chart with the two curves drawn over it.

TSS is Coggan's — an hour at threshold is 100 — and intensity enters squared, so an hour
at 0.8 IF scores 64, not 80. Fitness (CTL) is a 42-day exponentially weighted average of
daily TSS, fatigue (ATL) a 7-day one, and form (TSB) is the difference. Form is the
number with a meaning attached to its sign, so it is the one that changes colour: green
when you are fresh, red when you are in a hole.

The curves are **recomputed by walking the history** rather than stored, so a gap in
riding, a restored backup or a flat battery all settle to the right answer by themselves.
The ride in progress is folded in before it is saved, which is what makes form answer
"what will I be like tomorrow if I finish this".

Ninety days of daily scores live in NVS (180 bytes). A ride's score is banked **only when
you save it** — discarding a ride leaves no mark on months of history.

### Where the score comes from

| You have | You get |
|---|---|
| A power meter | **TSS** from normalised power against your FTP |
| A heart-rate strap | **hrTSS** from average HR against your threshold HR |
| Neither | Nothing, and the page says so |

The last row is deliberate. The drag model behind the calorie figure guesses at CdA and
often has no wind data, so a TSS derived from it would look every bit as authoritative as
a real one while quietly feeding months of training history. hrTSS is a genuinely
different measurement that happens to share a scale, so it is labelled as such rather than
blended in.

Set **FTP** and **Threshold HR** in Settings. Days are UTC days — the device has no
timezone, so a ride finishing after midnight UTC lands on the next day.

> Fitness needs history. From a cold start CTL under-reads for the first couple of months,
> because there is nothing behind it yet; the 42-day average has to fill up before it means
> anything.

## Workouts

**Menu → Start workout.** Seven sessions are built in, so it works with no SD card and
nothing to write: 4×4 VO2max, 3×8 threshold, 3×12 sweet spot, 20×30/30, 8×20 s sprints,
open endurance, and a cadence session. Picking one starts the ride if it is not already
running.

The page shows the step you are in, a **large countdown** of what is left in it, and your
current value against the target band — green on target, red off it, with a needle on a
bar so you can see how far out you are. Underneath is the next step, so the effort ahead
is never a surprise. Interval countdowns redraw four times a second; the last five
seconds of a 30-second effort matter.

Built-in workouts are written as **percentages of FTP**, resolved against the FTP in
settings, so they suit any rider without editing. Set yours in **Menu → Settings → FTP**.

Power compliance is judged on **three-second power**, not instantaneous — a range is
something a rider can hold, a raw number is something that swings 60 W between pedal
strokes. Going off target only complains after a sustained 6 s, and forgives after 4 s
back inside, so a gear change is not a telling-off.

### Workout steps become laps

Every step boundary raises a lap. Each interval therefore lands as one row on the Laps
page and one lap in the `.fit` file, so a session opens in Strava already split into its
efforts. During a workout the `LAP` button is the **step** button — the same thing a
Garmin does — which is also how an open-ended step ends.

### Writing your own

Put a `.wko` file in `/workouts`:

```
# 3 x 8 min at threshold
name  Thursday intervals
warmup   time 15:00
repeat 3
work     time 8:00  power 285 305
rest     time 5:00  power 120 160
end
cooldown time 10:00
```

Steps are `warmup` / `work` / `rest` / `cooldown`, ended by `time m:ss`, `dist 5km` (or
`800`, or `2mi`), or `open` for "until I press LAP". Targets are `power`, `hr` or
`cadence` with a low and high bound, in absolute watts, bpm or rpm — a file is written for
one rider, unlike the presets. `repeat n` … `end` expands inline, up to 64 steps total.
Unknown keywords are skipped rather than fatal.

Speed targets are deliberately absent: they would need a unit convention the rest of the
format does not have, and power or heart rate is what cycling workouts are actually
written against.

## Laps

Press `LAP`, or let auto-lap fire at the configured distance. Either way you get a banner
with the lap's distance, time and average speed, a chirp, and a row on the **Laps** page.

The page puts the lap in progress in a band across the top — distance, time, average
speed, and whichever of power, heart rate or climb you actually have a sensor for — with
completed laps listed underneath, **newest first**, so the one you just finished sits
directly below the one you are riding. The fastest lap is highlighted, ignoring anything
under 200 m so a double press at a junction cannot crown a two-metre lap.

The last 64 laps are held for display; the footer says so when there have been more. The
`.fit` file always gets every lap regardless.

## Settings

**Menu → Settings.** `UP`/`DOWN` moves, `ENTER` increases, `LAP` decreases, `BACK` saves.
Everything lives in NVS, so none of it needs a rebuild.

| Setting | |
|---|---|
| **Units** | Metric or Imperial — km/h, km, m, °C or mph, mi, ft, °F |
| **Rider weight** | 30–200 kg, feeds the climbing and calorie model |
| **FTP** | 60–600 W, what workouts scale against and what TSS is measured against |
| **Threshold HR** | 100–220 bpm, used for hrTSS when there is no power meter |
| **Gearing** | eight chainring/cassette presets, 2x and 1x, for the gear page |
| **Map zoom** | Auto, or a fixed span from 100 m to 5 km (300 ft to 3 mi in imperial) |
| **Bike weight** | 3–40 kg, shown with the combined total |
| **Wheel circumference** | 900–2500 mm, for a BLE speed sensor |
| **Backlight** | applied live as you change it, floor of 10 % |
| **Auto pause** | stop the timer when you stop rolling |
| **Auto lap** | off, or round distances in whatever unit you ride in |

Weights step in **the unit on screen** — half a kilo metric, a whole pound imperial — so
the number you are reading moves by a round amount instead of drifting in fractions.
Auto-lap does the same: the metric table is 1/2/5/10/20 km, the imperial one 1/2/5/10/20
miles, because 8.05 km is not a lap mark anyone chose.

**Units convert what is displayed, never what is stored.** Recorded `.fit` and `.gpx`
files are SI as the formats require, and the BLE telemetry stays SI too — so switching to
imperial cannot corrupt a ride or confuse Strava. The elevation/speed history graph is
also kept in SI internally, otherwise changing units mid-ride would bend the curve.

Values are range-checked on load, not just on entry: a corrupt or older NVS blob falls
back to the compile-time defaults from `config.h` rather than feeding a 5000 kg rider
into the power model.

## Choosing which pages you see

**Menu → Pages** lists every page with an on/off switch, two columns, `ENTER` to toggle
and `BACK` to save. The choice lives in NVS.

Three things it does beyond the obvious:

- **At least one page must stay on.** Switching off the last one is refused with a note
  rather than leaving a device with an empty rotation and no way back.
- **Switching off the page you are standing on** moves you to the next one that is still
  in the rotation, instead of leaving you somewhere you cannot return to.
- **The firmware stops dragging you to hidden pages.** An off-course alert, a workout step
  change or saving a ride would normally jump to the relevant page; if you switched that
  page off, the jump does not happen. The banner and the chime still do — you asked not to
  see the page, not to stop being told.

Sensor-gated hiding still applies on top: the power, HR and cadence zone pages and the
gear page take themselves out of the rotation when their sensor has produced nothing, so
switching them on does not put an empty chart in your way.

A mask written by an older build with fewer pages is migrated rather than trusted — pages
that did not exist when it was saved come back **on**, so a firmware update never hides a
new page behind a setting you have never seen.

## Editing the data pages

**Menu → Edit data fields.** No rebuild, no recompile — the two ride pages are configured
on the device and kept in NVS.

Pick the page, then any slot, and choose from the 37-entry field list: speeds, distances,
timers, elevation, HR/cadence/power including 3 s, 30 s and normalised, course remaining
and next turn, headwind and wind speed, heading and distance to start. **Every row shows
what that field reads right now**, which is the fastest way to tell `POWER 3s` from
`POWER 30s` without a manual.

"Fields on page" cycles 1 → 2 → 3 → 4 → 6 → 8. One to three stack full width; four and up
go in two columns, which is why 5 and 7 are not offered — they would leave a hole. A
miniature of the resulting grid is drawn in the corner as you change it.

`BACK` or `Done` saves and jumps to the page you edited so you see the result. Changes
apply live, so "Reset this page to defaults" is the way back out.

## Buttons

| Button | Short press | Long press |
|---|---|---|
| `UP` / `DOWN` | change page | — |
| `ENTER` | start / pause the timer | open menu |
| `LAP` | take a lap | save and close the ride |
| `BACK` | open menu | sleep |

The menu covers start/stop, lap, save, discard, load/clear course, sensor pairing, Wi-Fi,
reset and sleep. Any button dismisses an off-course banner.

## Map zoom

**Settings → Map zoom** is either **Auto** or one of eight fixed spans across the screen —
100 m to 5 km in metric, 300 ft to 3 miles in imperial. As with auto-lap, the levels are
round numbers in the units you actually ride in, rather than 100 m rendered as 328 ft.

Auto is the original behaviour: the window sizes itself to roughly the next two minutes of
riding, so it opens out as you go faster, bounded by `NAV_ZOOM_MIN_M` and `NAV_ZOOM_MAX_M`.

Picking a fixed level also changes the map with **no course loaded**. On Auto that page
fits your whole ride on the screen, which is the more useful thing when there is nothing to
follow; ask for a specific zoom and it becomes rider-centred at that span instead, showing
the breadcrumb, the scale bar and distance ridden.

> There is no on-the-fly zoom button. All five buttons are spoken for, and quietly
> overloading `LAP` — which is also the workout step button — to mean zoom on one page
> would be a nasty surprise mid-interval.

## Navigation

Put `.gpx` files in `/courses` on the card, or upload them from the Wi-Fi portal. Then
**Menu → Load course**, pick one, and the device jumps to the Nav page. Both `<trkpt>`
tracks and `<rtept>` routes are accepted, along with `<wpt>` cue points.

### Where the turns come from

Most GPX files in the wild are a bare line with no instructions at all, so the firmware
finds the turns itself. At each point it measures the heading change across a **25 m
baseline** either side, rather than between adjacent points — point-to-point angles at
5 m spacing are pure noise, and a long sweeping bend must not read as a turn just because
it adds up. Anything past 28° becomes a turn; a cluster of candidates around one corner
collapses into its sharpest point.

| Angle | Instruction |
|---|---|
| 28–55° | Bear left / right |
| 55–120° | Turn left / right |
| 120–160° | Sharp left / right |
| > 160° | U-turn |

`<wpt>` waypoints then merge in. A waypoint within 30 m of a detected corner supplies its
street name, and if its text names a direction (`"Turn left onto Polna"`, and Polish
`lewo`/`prawo` too) that wins over the measured angle — a human-written instruction beats
anything geometry can infer. A waypoint with no corner near it becomes a cue of its own,
which is how a "continue straight through the junction" instruction survives.

A cue whose text names no direction at all — `"Feed stop"`, `"Kładka"` — would otherwise
draw as a featureless dot. So the course is measured underneath it, at a lower threshold
(`NAV_CUE_TURN_MIN_DEG`, 12°) than bare geometry gets: whoever placed the cue already
decided something happens there, so a shallow bend is enough to earn a real arrow. On
genuinely straight road it stays a plain cue rather than inventing a turn.

Every upcoming cue draws its own arrow on the map, not just the imminent one — the
nearest larger and in amber, the rest smaller — and the glyph flips below its marker when
the cue sits near the top edge of the screen.

Each turn is announced three times — at 400 m, 150 m, and at the corner — and the
guidance band goes amber then red as you close on it. Approaching a turn from inside one
of those radii jumps straight to the right stage instead of replaying the earlier calls.

### Off course

Detection uses hysteresis on both edges: you must be beyond `OFF_COURSE_THRESHOLD_M`
(40 m) for a sustained `OFF_COURSE_HOLD_S` (8 s) before it alerts, and back inside for
5 s before it clears. Without that, a wide roundabout or a GPS wobble under a bridge would
set it off every few minutes. The alert gives the distance and a **clock direction**
relative to your heading — "route 120 m away, 4 o'clock" — because that is an instruction
you can act on without stopping, and a compass bearing is not.

Snapping searches a window around your last known position rather than the whole route.
That is not an optimisation — on an out-and-back or a lollipop, the outbound and return
legs run metres apart, and a global nearest-point search will happily decide you are on
the return leg and report the ride as nearly finished. There is a full-course fallback
for when you genuinely are lost, or have just loaded a course mid-ride.

## Compass

A track-up rose: the dial turns under a fixed index, so whatever is straight ahead of the
bike is straight up on the screen. The ride's start is marked on the ring **where it
actually lies relative to you**, with a needle from the centre, plus bearing, cardinal,
clock direction and straight-line distance.

> The distance shown is a straight line, not a route. Menu → **Navigate back to start**
> retraces your breadcrumb, which is the one that goes round the lake rather than through
> it.

### Where the heading comes from

**By default, from GPS course over ground — which only exists while you are moving.**
Below about 5 km/h it is noise, and stopped it does not exist at all, which is exactly
when you look at a compass. The page says `no heading — start moving` rather than
drifting a plausible-looking needle.

Fitting an optional **QMC5883L / HMC5883L** on the existing I²C bus closes that gap, and
becomes the heading source for the wind dial and off-course arrow at standstill too. The
firmware prefers GPS course while moving (it is already true north and rock steady at
speed) and falls back to the magnetometer below 1.5 m/s. See
[HARDWARE.md](HARDWARE.md) — it is about 5 zł, and the module's silkscreen usually lies
about which chip is on it, so both are detected.

### Calibration is not optional

A magnetometer next to a Li-ion cell, a switching regulator and a steel handlebar reads
garbage until its hard-iron offset is measured. Menu → **Calibrate compass**, then turn
the whole unit slowly through a full circle while a 12-segment dial fills in. It refuses
to save below 75 % coverage rather than store a calibration that points the wrong way,
and until one exists it reports no heading instead of a confident wrong one.

Headings are smoothed as a **vector**, never as an angle — averaging 359° and 1° as
numbers gives 180°, which would swing the needle to due south once per revolution.

Set `MAG_DECLINATION_DEG` for where you ride: GPS bearings are true north and a
magnetometer reads magnetic, so without it the needle and the bearing-to-start disagree
by that angle.

## Low battery

Three warnings on the way down, each fired once, with a beep and a banner:

| Level | What happens |
|---|---|
| **20 %** | "Battery low" with the estimated hours left |
| **10 %** | Sticky red alert, **and the backlight drops to its dim level** |
| **critical** (3.4 V) | The ride is saved and closed properly, then a sticky alert |

Dimming at 10 % is the only thing that meaningfully buys runtime — the backlight is the
biggest single draw — so it is done and then *said*, rather than done quietly. Plugging in
clears the alert and gives the brightness back.

Closing the ride at critical rather than relying on the checkpoint is deliberate: a
finished `.fit` needs no recovery prompt and uploads as it is. It goes through the same
save path as pressing the button, so the TSS is banked and the summary comes up.

Warnings re-arm only once the reading has climbed **5 % clear** of the level that raised
them. A cell sags under load on every climb and recovers on every descent; without that
margin the device would warn on each hill.

## Surviving a power loss

A ride's `.fit` file has no valid header or CRC until the ride is closed, so a flat
battery used to cost the whole thing. Now a small **checkpoint** (1464 bytes) is written
beside it on the same 15-second beat as the flush: the ride's totals, the elevation
profile, how many FIT bytes have been written, and the paths of both files.

Find one at boot and the last ride ended with the power going away. The device says so and
offers two answers, **both of which keep the ride**:

- **ENTER — carry on riding.** The totals go back into the ride computer, the `.fit` file
  is reopened and appended to. The definitions are already in the file, so they are not
  written a second time.
- **BACK — save and finish it.** The file is closed off properly, header and CRC written,
  and the TSS is banked. It uploads like any other ride.

There is deliberately no third option that throws it away.

The prompt shows the **battery**, because it is the one fact the choice turns on. A flat
battery is the most likely reason the ride stopped, so the percentage is right there and
the advice follows it: under 20 % it says finishing now is the safer answer, on charge it
says carrying on is fine. The battery is not otherwise visible here — the prompt hides the
status bar.

What comes back: every total, every zone, the elevation profile, and the whole `.fit` file.
What does not: the breadcrumb trail and the on-screen lap list, which are display data the
`.fit` file already holds properly. The map and *Navigate back to start* therefore begin
again from where you resumed.

A checkpoint is rejected unless its magic, version, CRC **and the size of `RideState`** all
match. That last check is the one that matters after a firmware update: change the ride
state and every byte after it shifts, so the totals would come back as plausible nonsense
rather than as an obvious error.

## Ride summary

**Menu → Save ride** finishes the ride and puts the **Summary** page up by itself, which
is what a bike computer does when you press stop. Distance and moving time large across
the top with the date, then every total underneath: elapsed, average and max speed,
ascent, descent, calories, laps, and whatever the sensors actually gave you — heart rate,
power with normalised and max, cadence, TSS, work in kilojoules, and the temperature range
from the barometer.

The grid is **built from what the ride has**, not a fixed shape with blanks. A ride with
no power meter simply has fewer cells and larger ones; nothing reads `--`. The page is
also in the normal rotation, so the same totals are there mid-ride.

The footer shows the file the ride was written to, or says plainly that it was not
recorded.

## Elevation profile

The **Profile** page draws the ride's elevation against **distance**, filled and coloured
by gradient using the scheme every climbing app uses — blue descending, green to 3 %,
yellow, orange, red, magenta past 12 %. Header carries current elevation, gradient and the
ride's climbing; a key sits under the axis.

Distance, not time, is the point. Plotted against time, ten minutes at a cafe becomes a
flat line occupying a sixth of the graph and a fast descent gets squashed into nothing.

The sample interval **starts at 20 m and doubles whenever the buffer fills**, so a 5 km
spin and a 300 km audax both draw a full-width profile from the same 1 KB. The footer says
what the current spacing is. Decimation halves the resolution, never the span — the x axis
keeps telling the truth about how far you have ridden.

With a course loaded the page switches to the **whole course** with a marker at your
position: ground already ridden is filled solid, the road ahead is drawn dimmed with a
coloured crest, so progress and what is still to come read at a glance.

## Weather history

The **Weather history** page is the on-board sensor's own record, not the forecast: one
sample a minute for eight hours, drawn as stacked temperature and pressure graphs sharing
an x axis. 480 samples map one-to-one onto the 480 px screen, so nothing is resampled.
Header shows the current reading, the ride's high and low, humidity, and the pressure.

The useful part is the **barometric tendency** — the change over the window, worded the
way a forecaster words it (steady / rising or falling slowly, quickly, very rapidly),
normalised to the conventional three-hour rate. Falling pressure is the one thing a bike
computer can tell you about weather that has not happened yet, so a drop of 2 hPa or more
gets a warning line.

The wording only appears once there is **an hour of history** behind it. Below that you
still get the raw figure and the span it covers, but a twenty-minute slope extrapolated to
three hours turns a passing gust into a storm warning.

The forecast's air temperature is drawn as a dashed reference line when it is in range.
The two rarely agree: the on-board sensor sits in the sun and next to a battery, and that
gap is worth seeing rather than hiding.

> History lives in RAM and starts fresh each boot. The trend is at its most useful when
> the device has been running a while before you set off.

## Weather

The device has no internet on a ride, so the companion page fetches a forecast and pushes
it over the BLE link as plain text lines — one for current conditions, one per forecast
hour:

```
WX  t=18.4 f=17.1 h=62 p=1013 ws=5.2 wd=270 g=9.1 pr=0.2 code=61 sr=… ss=…|Light rain
WXH 0 t=18.4 ws=5.2 wd=270 pr=0.0 pp=10 code=2
WXH 1 t=18.9 ws=6.1 wd=275 pr=0.4 pp=60 code=61
```

Every key is optional and order does not matter, so a different data source can send only
what it has without a firmware change. Weather codes are WMO, the same set Open-Meteo and
most European services use. The companion refreshes on connect, every ten minutes, and
whenever the bike has moved 20 km; it uses the **device's** GPS position, falling back to
the browser only if there is no fix yet.

> Fetching a forecast sends your coordinates to `open-meteo.com` — no key or account, but
> it does leave the device. Everything else works without ever pressing Refresh weather.

### Wind is the part that matters

A weather readout is mildly interesting; wind resolved against your heading is
actionable. The firmware computes it from the meteorological wind direction and your GPS
course:

```
relative = windFrom − heading      headwind = wind·cos(relative)      cross = wind·sin(relative)
```

Positive headwind means it is in your face. That number sits in the status bar on every
page, coloured red for a headwind and green for a tail, and it feeds the aero term of the
no-power-meter estimate — drag works against **air** speed, not ground speed, so a 5 m/s
headwind roughly doubles the aero cost at 25 km/h. Air density comes from the on-board
barometer rather than the forecast, because it is local and already measured.

Heading is only used above 1.5 m/s. Standing still, GPS heading is noise, and a spinning
wind arrow is worse than none — so it blanks instead.

Rain warnings fire once when the forecast puts wet weather within
`WEATHER_RAIN_ALERT_MIN` (75 min), and re-arm after the band passes so a second front
still warns. Weather greys out and is marked stale after 45 minutes.

### Getting home without a course

**Menu → Navigate back to start** reverses the ride's own breadcrumb into a course and
loads it. Every piece of guidance above then works unchanged — turns are detected in your
own track, and wandering off it alerts exactly as a loaded route would. There is no map
on board to invent a shortcut with, but retracing is always available.

| Knob | Default | |
|---|---|---|
| `OFF_COURSE_THRESHOLD_M` | 40 m | how far off counts as off |
| `OFF_COURSE_HOLD_S` | 8 s | sustained before alerting |
| `NAV_TURN_BASELINE_M` | 25 m | baseline the heading change is measured over |
| `NAV_TURN_MIN_DEG` | 28° | below this it is a bend, not a turn |
| `NAV_CUE_TURN_MIN_DEG` | 12° | ...but a placed cue point earns an arrow sooner |
| `NAV_ANNOUNCE_FAR/NEAR/NOW_M` | 400 / 150 / 30 m | the three announcement stages |
| `COURSE_MAX_POINTS` | 8000 | ~190 KB, held in PSRAM; longer routes decimate |
| `NAV_ZOOM_SECONDS` | 120 | map window = this much riding time ahead |

---

## Layout

```
include/config.h        every pin, and the defaults for everything below
src/Settings            runtime settings + all unit conversion, host-tested
src/main.cpp            wiring, 10 Hz ride loop, 10 Hz render loop
src/gps/UbloxGps        UBX parser + auto-configuration
src/ride/RideComputer   metrics, auto-pause, laps, normalized power, breadcrumb buffer
src/ride/FitEncoder     FIT protocol writer (definitions, records, laps, session, CRC)
src/ride/Recorder       SD card, .fit + .gpx streaming, crash recovery
src/ride/RideCheckpoint the totals written beside the ride, host-tested
src/ride/Workout        presets, .wko parser, step advance, target compliance
src/ride/TrainingLoad   TSS scoring, 90-day history, fitness/fatigue/form
src/ride/Zones          Coggan power zones and threshold-HR zones, host-tested
src/ride/ElevationProfile  ride elevation vs distance, adaptive interval
src/ride/Drivetrain     gear inference from speed and cadence, host-tested
src/sensors/BleSensors  BLE central for HR / CSC / power, own FreeRTOS task
src/sensors/Baro        BME280 altitude with GPS-leashed reference
src/sensors/Weather     forecast pushed from the phone, wind resolved to heading
src/sensors/EnvHistory  eight-hour temperature/pressure ring and trend, host-tested
src/nav/Course          GPX parsing, snapping, off-course logic, cues
src/nav/Geo             haversine, bearing, cardinal names - pure, host-tested
src/sensors/Compass     optional QMC5883L/HMC5883L, hard-iron calibration
src/power/Power         battery curve, backlight, deep sleep
src/power/BatteryWarn   warning thresholds and their hysteresis, host-tested
src/input/Buttons       resistor-ladder decoder with debounce and long-press
src/ui/Display          LovyanGFX i80 16-bit binding + panel ID probe
src/ui/Ui               pages, fields, menu, banners, course picker, field editor
src/ui/Beeper           non-blocking piezo patterns
src/link/PhoneLink      BLE GATT server, telemetry + command protocol
src/link/WebPortal      Wi-Fi file browser, GPX upload and OTA
companion/index.html    Web Bluetooth companion page
tools/course-test/      host-side test suite for the GPX parser
```

---

## First boot

1. **Find your panel controller.** The firmware bit-bangs the bus at boot, before
   LovyanGFX claims it, and prints whatever the panel answers to registers 0x04, 0xD3,
   0xBF and 0x09:

   ```
   --- panel ID probe ---
     RDDID   (0x04): 0000 0000 0094 0086
   ```

   `00 94 86` means ILI9486. Set the matching `-DPANEL_ILI9486` / `_ILI9481` / `_R61581`
   / `_HX8357B` in `platformio.ini` and rebuild. This costs one boot and saves an
   afternoon of guessing.

2. **Check the boot screen.** It reports display, microSD, barometer, GPS baud rate and
   BLE line by line. Anything red is not wired the way the config thinks it is.

3. **Get a fix outdoors.** First fix from cold takes 30–60 s. The Status page shows
   satellites, horizontal accuracy and pDOP.

4. **Pair sensors.** Menu → Pair sensors, then wake the sensor (wet the HR strap
   contacts, spin the crank). It is remembered in NVS and reconnects on its own from
   then on.

5. **Ride.** `ENTER` starts the timer and opens the file. `LAP` long-press saves.

---

## Tuning

Units, rider and bike weight, wheel size, backlight, auto-pause and auto-lap are all
editable on the device — see **Settings** above. The values in `config.h` are their
defaults. Everything else lives there permanently:

```c
#define GPS_NAV_RATE_HZ         5      // 10 works on M8N with a single constellation
#define REC_INTERVAL_MS      1000      // FIT record period
#define RIDER_CDA            0.32f     // drag area, hoods on a road bike
#define RIDER_CRR            0.005f    // rolling resistance, 25 mm tyre on tarmac
#define MAG_DECLINATION_DEG   6.2f     // true vs magnetic north where you ride
#define LCD_BUS_FREQ_HZ  20000000      // drop to 10 MHz if the panel glitches
```

---

## Known limitations

- **No ANT+.** Physically impossible on ESP32. Every dual-band sensor made since ~2016
  works over BLE instead; genuinely ANT+-only sensors will not connect.
- **No basemap.** The map page draws your track and your loaded course, not the road
  network — the same thing an Edge 520 does, for the same reason (no map data on board).
  Guidance follows a route you supply; it cannot *route* you anywhere. Off course, it
  gives you the direction and distance back to the line, not a way there.
- **Turns are geometry, not street names.** Without `<wpt>` cues the device can tell you
  "sharp left in 150 m" but not what you are turning onto. Street names only exist if the
  GPX carries them.
- **A hairpin and a junction look the same to it.** Turn detection reads the shape of the
  line. A switchback on a climb is a real 160° turn and will be announced as one.
- **Course elevation comes from the GPX.** If the file has no `<ele>` data, climb
  remaining and the course profile page are unavailable — the barometer only knows where
  you are, not where you are going.
- **Weather is only as fresh as the last push.** Ride out of Bluetooth range of the
  phone, or leave the companion page, and it ages in place. It greys out and says how old
  it is rather than pretending, but it will not update itself.
- **Forecast wind is not the wind in the lane you are in.** It is a 10 m open-ground
  figure for a grid cell several kilometres across. In a valley, a forest, or behind a
  town it can be badly wrong. Treat the headwind number as a good explanation for why the
  ride feels hard, not as a measurement.
- **Hourly resolution rounds rain warnings up.** Slot 0 is the hour you are already in,
  so "rain in ~60 min" can mean anywhere in that hour.
- **No compass at standstill without the optional magnetometer.** GPS course needs
  movement. The page says so rather than showing a stale needle.
- **The magnetometer has no tilt compensation.** Correct while roughly level, wrong if
  you pick the unit up and tilt it to sight along — that needs an accelerometer this
  design does not have.
- **Verify your first FIT file.** The encoder follows the FIT profile and the CRC is the
  official algorithm, but before you trust a 200 km ride to it, upload a short test ride
  and check it parses. Garmin's `FitCSVTool` is the strict reference; Strava is the
  practical one.
- **Bigger and thirstier than an Edge 520.** ~100 × 65 mm and ~10 h, against 49 × 73 mm
  and 15 h. That is the cost of a backlit colour TFT versus a transflective display, and
  it buys you a screen that is far easier to read.
- **The 5 V shield needs a level-shifting decision.** See HARDWARE.md — it is the one
  thing that can stop this working entirely.
