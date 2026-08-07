# Host-side test harness

Compiles the real `src/nav/Course.cpp` and `src/sensors/Weather.cpp` on the host
against tiny `Arduino.h` / `SD.h` shims and asserts on the results. No hardware,
no flashing — text parsing and geometry are the parts of this project most likely
to be quietly wrong, and also the parts that are miserable to debug on a bike.

## Run it

```bash
python tools/course-test/gen_gpx.py
```

Then compile and run. Any C++17 compiler works; if you do not have one,
`pip install ziglang` gives you a self-contained clang in a wheel:

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -I tools/course-test/shim -I include -I src -o tools/course-test/test_course.exe tools/course-test/test_course.cpp src/nav/Course.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -I tools/course-test/shim -I include -I src -o tools/course-test/test_weather.exe tools/course-test/test_weather.cpp src/sensors/Weather.cpp
```

`-nostdinc++ -nostdlib++` is deliberate: neither file uses anything beyond libc,
and skipping libc++ avoids building it. Run `test_course` **from the directory
holding the generated .gpx files**.

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -I tools/course-test/shim -I include -I src -o tools/course-test/test_geo.exe tools/course-test/test_geo.cpp src/nav/Geo.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -I tools/course-test/shim -I include -I src -o tools/course-test/test_settings.exe tools/course-test/test_settings.cpp src/Settings.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_laps.exe tools/course-test/test_laps.cpp src/ride/RideComputer.cpp src/nav/Geo.cpp src/Settings.cpp src/ride/ElevationProfile.cpp src/ride/Zones.cpp
```

`-fno-threadsafe-statics` keeps the compiler from emitting `__cxa_guard_*` calls
for function-local statics, which would want libc++abi.

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_workout.exe tools/course-test/test_workout.cpp src/ride/Workout.cpp src/Settings.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_load.exe tools/course-test/test_load.cpp src/ride/TrainingLoad.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_zones.exe tools/course-test/test_zones.cpp src/ride/Zones.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_envhistory.exe tools/course-test/test_envhistory.cpp src/sensors/EnvHistory.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_profile.exe tools/course-test/test_profile.cpp src/ride/ElevationProfile.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_drivetrain.exe tools/course-test/test_drivetrain.cpp src/ride/Drivetrain.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_checkpoint.exe tools/course-test/test_checkpoint.cpp src/ride/RideCheckpoint.cpp src/ride/FitEncoder.cpp src/ride/ElevationProfile.cpp src/Settings.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_battery.exe tools/course-test/test_battery.cpp src/power/BatteryWarn.cpp
```

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O1 -w -nostdinc++ -nostdlib++ -fno-exceptions -fno-rtti -fno-threadsafe-statics -I tools/course-test/shim -I include -I src -o tools/course-test/test_gpsfix.exe tools/course-test/test_gpsfix.cpp src/gps/GpsWarn.cpp
```

Expected: `95`, `67`, `43`, `75`, `64`, `75`, `48`, `62`, `42`, `35`, `43`, `24`,
`29` and `41 checks, 0 failures`.

## Caching, and why the first build is so much slower

**Expect the first command of the day to take about a minute, and every one after it
about a second.** That is not the firmware compiling — six translation units of this
size are nothing. On Windows the `x86_64-windows-gnu` target makes zig build the
mingw-w64 C runtime from source before it can link anything, and that is the minute.
Measured here on `test_laps`: **62 s against an empty cache, 1.3 s** for the same build
with one source file genuinely changed.

The cache is **global rather than per project** — `%LOCALAPPDATA%\zig` on Windows,
`~/.cache/zig` elsewhere. One test binary put 72 MB in it; across a project's life it
reaches a few hundred MB. It is safe to delete at any time and the next build refills
it, at the cost of paying that minute again.

It is also **content-addressed, not timestamp-addressed**. `touch` on a source changes
nothing — zig hashes what is in the file, so a rebuild after touching every file in the
tree still relinks from cache in about a second. Only edits that change bytes cost
anything, and only for the translation units they touch.

If a build ever fails in a way that makes no sense against the source in front of you —
a symbol that should be there, a header change that seems not to have landed — delete
that directory before believing the error. It is rare, and it is the only thing in this
harness that can lie to you.

**CI caches nothing for this suite, deliberately.** On the Linux runner the mingw build
never happens — zig links the system libc — so the numbers are completely different:
`pip install ziglang` takes 13 s, and building *and running* all fourteen binaries takes
9 s, for a 28 s job from a cold start. Restoring and saving a cache around a job shorter
than the restore itself buys nothing, and a stale one would cost more debugging than it
could ever save.

The firmware workflow is the opposite case and does cache: its espressif32 toolchain is
a few hundred MB and minutes, keyed on `platformio.ini` so that changing the platform or
a library version refills the cache rather than quietly building against the old one.

The GPX fixtures are not cached either — `gen_gpx.py` regenerates them in about a second,
and they are `.gitignore`d precisely so a stale 20 001-point course cannot outlive the
generator that made it.

## What the GPX fixtures cover

| File | Exercises |
|---|---|
| `a_straight.gpx` | ordinary Garmin track, distance and snapping against a known 10 km line |
| `b_oneline.gpx`  | identical geometry with **no newlines at all** — the 512-byte chunk boundary |
| `c_selfclose.gpx`| `<trkpt ... />` self-closing tags, `lon` attribute before `lat` |
| `d_cues.gpx`     | `<wpt>` cue points listed out of order, `<rte>/<rtept>` instead of a track |
| `e_noele.gpx`    | no elevation data anywhere |
| `f_sawtooth.gpx` | 10 × 100 m climbs, checks ascent totals and elevation interpolation |
| `g_big.gpx`      | 20 001 points — forces the in-place decimation path |
| `h_outback.gpx`  | out-and-back with legs 12 m apart, the case a global nearest-point search gets wrong |
| `i_empty.gpx`    | valid XML with no track points, plus a missing file |
| `j_turns.gpx`    | three corners at known distances — 90° right, 90° left, 45° bear right |
| `j_named.gpx`    | the same corners with a waypoint 12 m before the first, which must merge into it rather than become a fourth cue |
| `k_bend.gpx`     | a 300 m radius 90° sweep, which must produce **zero** turns |
| `l_uturn.gpx`    | a reversal, which must classify as a U-turn and not as two sharp turns |
| `m_cuebend.gpx`  | two waypoints whose text names no direction — one on a 20° bend (must earn a bear-right arrow from the geometry under it), one on straight road (must stay a plain cue rather than an invented turn) |

Also covered without a fixture: the three-stage announcement schedule firing exactly once
each on approach, the compass bearing back to the route, and `buildFromTrack()` reversing
a breadcrumb into a route home.

## What the geo tests cover

`haversine`, `bearingDeg`, `relativeBearing` and `cardinalName` — the maths behind
the compass page. Known city-pair distances, the four cardinals, bearings that
must stay in 0..360 rather than coming back negative, the antimeridian, and every
sector boundary of the 16-point rose.

One case is there because it is an easy thing to get wrong by eye: stepping
+0.5° of latitude and −0.5° of longitude from 50°N is **not** a 315° bearing.
A degree of longitude there is only ~64 % of a degree of latitude, so the true
answer is ~327°. The first draft of this test asserted 322 and the code was right.

## What the zone tests cover

Every boundary of the seven power zones and five heart-rate zones, checked from
both directions: the watt value either side of each edge classifies correctly,
and the bound the page *prints* for a zone classifies back into that same zone.

That round-trip found a real bug. At FTP 250 the Z4 floor is 91 % = 227.5 W.
Rounding down printed **227**, but `powerZoneFor(227)` computes 90 % and files
it in Z3 — the screen would have named a target that lands one zone lower.
Low bounds now round up and high bounds sit one below the next zone's floor,
which also makes the printed bands contiguous. Both properties are asserted.

Also covered: monotonicity across 0–600 W (a zone may never decrease as power
rises), the 32-bit intermediate that stops 1500 W against a 60 W FTP from
wrapping a 16-bit multiply, and a zero FTP returning Z1 rather than dividing by
zero.

Cadence bands are absolute rpm, so they need no rounding — but they are checked
the same way, plus the one thing that is specific to them: the bottom band
starts at **1 rpm**, because 0 is coasting and belongs nowhere in the table.

## What the training-load tests cover

The TSS definition is pinned against hand-worked values: an hour at threshold
is exactly 100, half an hour is 50, an hour at 0.8 IF is **64** rather than 80
because intensity enters squared. If that drifts, every number on the page and
every day of stored history drifts with it silently.

Then the ring: rolling over midnight, a backwards clock being ignored, a
multi-day gap sliding entries the right distance, a 200-day absence emptying the
window, and the daily bucket saturating rather than wrapping its `uint16`.
The curves are checked behaviourally rather than against magic numbers — under a
constant load fatigue must sit above fitness and form must be negative; after a
week off form must go positive and fatigue must fall faster than fitness.

## What the workout tests cover

The `.wko` parser (repeat expansion, `m:ss` and `5km` durations, all three
target types, reversed ranges, junk lines, an empty file), every preset
expanding within the 64-step limit, step advance on time / distance / LAP, and
target compliance with its dwell.

`test_workout.cpp` writes its own `.wko` fixtures, so it needs nothing from
`gen_gpx.py`.

Two cases are there because they would be wrong in a way nobody notices until
mid-interval:

- **A paused ride must hold the workout.** Thirty seconds stopped at a junction
  should not burn thirty seconds of a recovery step.
- **No power meter is not zero watts.** Without the sensor check, a missing
  meter reads as 0 W, sits permanently under target, and complains about it
  forever. Compliance returns `NoTarget` instead.

## What the lap tests cover

Laps are ridden rather than faked — the test feeds real `GpsFix` updates through
`RideComputer::update()`, so distance and timing come out of the same accumulation
path the firmware uses, which is why the numbers land at 996.95 m rather than a
suspiciously exact 1000.

The case that matters is the **lap event**. `fillLapSummary()` reads the live
counters, so a summary taken after `lap()` describes the empty lap that just
began, not the one that ended. The test asserts the event carries the finished
distance. That bug is why auto-laps were missing from FIT files entirely: only
the manual button path called `markLap()`, and it happened to call it before the
reset.

Also covered: the ring buffer dropping the oldest laps while keeping indices
strictly increasing, and a zero-distance lap being recorded but barred from
winning "best lap".

The same file exercises **time in zone**, which is where `Zones` meets
`RideComputer`: riding at 80 % of FTP must put every second in power Z3 and
nothing in its neighbours, riding harder must move the time rather than
double-count it, zone time must sum to the time actually ridden, and an absent
sensor must file *nothing* rather than everything in Z1. A stopped rider is
checked too — twenty seconds of coffee stop may cost at most the single sample
it takes for auto-pause to notice.

Coasting gets its own case, and the test helper had to grow a parameter for it:
a cadence sensor **connected and reporting 0 rpm** is not the same as no sensor,
and only the first is freewheeling. Twenty-five seconds of descending must land
in the coasting counter, never in the grinding band, and pedalling plus coasting
must add up to the whole ride.

## What the settings tests cover

Every unit conversion against hand-checked values (−40° is the one place the
temperature scales meet; 1609.344 m is a mile exactly), the step and clamp logic,
and the NVS round trip.

The last block matters most: it plants a **corrupt store** — a 5000 kg rider, a
zero-weight bike, a 100 mm wheel, a NaN — and checks each one falls back to its
compile-time default. Load-time validation is written as a positive range test
(`riderKg >= 30 && riderKg <= 200`) rather than a negative one precisely so NaN
fails it; `!(x < 30 || x > 200)` would have let NaN straight through into the
power model.

## What the battery-warning tests cover

The thresholds, and above all the **hysteresis**. The test walks the level from
100 % to 0 and asserts each warning fires exactly once, then holds a reading
wobbling either side of a threshold for fifty samples and asserts it says
nothing more — a cell sags under load on every climb, and without the margin the
device would warn on each hill.

Also covered: re-arming only once the reading is genuinely clear, so a real
second drop does warn; plugging in clearing the warning and unplugging at a
still-low level raising it again; a sag that skips a tier landing on the tier it
reached rather than the one it passed; and critical holding until the *voltage*
recovers, since that tier is a voltage judgement and a percentage cannot release
it.

The test drives the same state machine `Power::updateWarnings()` runs, rather
than the two helpers in isolation — the sequencing is where the bugs live.

## What the GPS-warning tests cover

The thresholds, and above all the **dwell**. A dropout one second short of the hold must
say nothing — that is every bridge and underpass on the ride — and a real one must warn
exactly once however long it lasts.

The case that drove the design is a **flickering fix**: the test feeds one good second in
five for a minute, which is what a city street actually gives you, and asserts it is
still called an outage. An implementation that resets the clock on each good sample
passes every other test in the file and never warns here, leaving a rider with no usable
position for ten minutes perfectly uninformed.

Also covered: nothing being said before the first fix of the day, since a cold start in a
car park is not a fault; a recovery that does not hold for its full five seconds leaving
the warning up; the outage length reported at recovery being the outage itself and not
the recovery with it; escalation from a lost fix to a silent receiver firing immediately
rather than serving the dwell again, and dropping back from silent to merely lost saying
nothing; and the ordering of the three dwells, since a receiver that has stopped talking
must always be believed sooner than a fix that has merely gone loose.

One tier check exists for a specific way to get it wrong: a module that has stopped
sending can still leave a perfectly healthy-looking 3D solution behind it, so silence has
to outrank what the last message said.

## What the checkpoint tests cover

A checkpoint that unpacks wrong is a ride lost, so every way of getting it wrong
is checked: a changed byte, a foreign file, a newer version, a torn half-write,
an all-zero block, and a checkpoint naming no file.

The one worth spelling out is **`stateSize`**. A firmware update that adds a
field to `RideState` shifts every byte after it; without that check the totals
would come back as plausible nonsense instead of an obvious error. The test
plants a mismatched size on an otherwise perfectly sealed checkpoint.

The CRC covers `offsetof(RideCheckpoint, crc)` bytes rather than
`sizeof - 2`, because the struct can carry trailing padding after the checksum
and hashing indeterminate bytes would make it unrepeatable. The buffer is also
zeroed before use so the padding *between* members is deterministic.

## What the drivetrain tests cover

Gear inches, ratio and development against hand-worked values (50x17 on a
2105 mm wheel is 2.94, 6.19 m and 77.6 in), then the part that actually
matters: **every combination in the table is ridden at its own speed and must
come back as itself**. A matcher that is subtly wrong still looks plausible on
one gear.

Also checked: coasting and crawling produce no gear rather than an infinite
one; a ratio that matches nothing is refused rather than snapped to the nearest
entry; a single stray sample does not move the display but a sustained shift
does; cross-chaining is flagged at the ends of a 2x block and **never** on a 1x,
which has nowhere else to be; and changing the gearing clears the reading rather
than leaving a stale gear from the old cassette on screen.

The preset table is validated too — sprockets must ascend, or "one sprocket
harder" walks the wrong way — and `DRIVETRAIN_PRESET_COUNT` in `config.h` is
asserted against the table both at compile time and here, since `Settings`
range-checks the stored index against the constant without linking the table.

## What the elevation-profile tests cover

That the profile is indexed by **distance rather than call count** — five hundred
stationary samples must add nothing, which is the entire reason it exists — plus
gradient against known ramps, and the half-metre storage resolution.

The adaptive interval gets the most attention. A ride nine times longer than the
buffer's base span must still fit, still use most of the buffer, and above all
still report the right **covered distance**: decimation may halve the
resolution, never the span, or the graph silently claims a 92 km ride was 12 km.
Gradient is re-checked after decimation, since it is measured over a ground
distance rather than a fixed number of samples.

A distance jump (a tunnel exit, a GPS glitch) is checked too: the gap is filled
so the x axis cannot fall behind the distance it claims to show.

## What the environment-history tests cover

The one-a-minute rate limit, the ring dropping oldest-first while keeping the
newest sample newest, and the barometric tendency wording at each rate band.

Two cases guard against numbers that would look plausible and be wrong:

- **A missing reading is not a zero.** A sample taken before the sensor
  answered stores NAN, and the trend walks in from both ends to find real
  pressure readings. Treating an empty slot as 0 hPa would report a 1000 hPa
  collapse and a storm that is not coming.
- **A short window gets a figure but no words.** Under an hour of history the
  page shows the raw change and its span, but not the three-hour wording —
  extrapolating a twenty-minute slope to three hours turns a passing gust into
  a warning.

Out-of-range readings (-300 °C, 5000 hPa) are discarded as sensor faults rather
than stored and plotted.

## What the weather tests cover

The `WX` / `WXH` line format, wind resolved against heading versus hand-worked
cases, air density, rain alerting and staleness. Two of them exist specifically
because they are the failures that stay silent:

- **`p=` vs `pp=` vs `pr=`.** Pressure, precipitation probability and
  precipitation all begin with the same letter. A scanner that matches on a bare
  prefix reads the wrong one, and a rainfall figure ends up in the pressure
  field looking perfectly plausible.
- **`sr=` / `ss=` precision.** A unix timestamp does not survive a `float` — at
  1.75e9 the gap between representable values is 128 seconds, so sunrise parsed
  through a `float` lands minutes off. This test caught exactly that, and the
  parser now uses `double`.

## Notes on the numbers

- Distances read **+0.3 % against great-circle truth**. That is the local
  equirectangular projection, and it is the expected magnitude — the firmware
  trades it for float-only maths in the snapping inner loop.
- Ascent on the sawtooth reads **980 m against 1000 m**. That is the 3 m
  deadband refusing to count the last two metres of each ramp, which is exactly
  what it is there for.
- The 200 km fixture decimates 20 001 points to 7 673 and keeps its distance,
  because that course is straight. A curvy course loses a fraction of a percent
  when decimated — corners get cut.
