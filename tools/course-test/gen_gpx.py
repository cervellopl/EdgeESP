import math, os, json

OUT = os.path.dirname(os.path.abspath(__file__))
LAT0 = 50.0

# Same projection constants the firmware uses, so ground truth and code agree.
phi = math.radians(LAT0)
M_LAT = 111132.92 - 559.82 * math.cos(2 * phi) + 1.175 * math.cos(4 * phi)
M_LON = 111412.84 * math.cos(phi) - 93.5 * math.cos(3 * phi)

def lon_for(m):   return m / M_LON
def lat_for(m):   return m / M_LAT

def haversine(a, b, c, d):
    R = 6371008.8
    dlat, dlon = math.radians(c - a), math.radians(d - b)
    h = math.sin(dlat/2)**2 + math.cos(math.radians(a))*math.cos(math.radians(c))*math.sin(dlon/2)**2
    return 2 * R * math.asin(math.sqrt(h))

def write(name, body):
    with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
        f.write(body)

meta = {}

# --- A: plain Garmin-style track, straight 10 km east, one point per 10 m ----
N, STEP = 1001, 10.0
pts = [(LAT0, lon_for(i * STEP)) for i in range(N)]
truth = sum(haversine(*pts[i], *pts[i+1]) for i in range(N-1))
meta["straight_km"] = truth

body = ['<?xml version="1.0"?>\n<gpx version="1.1" creator="test">\n'
        '<trk><name>Straight Ten</name><trkseg>\n']
for i, (la, lo) in enumerate(pts):
    body.append(f'<trkpt lat="{la:.7f}" lon="{lo:.7f}"><ele>{100 + (i % 50)}</ele>'
                f'<time>2026-01-01T00:00:{i%60:02d}Z</time></trkpt>\n')
body.append('</trkseg></trk></gpx>\n')
write("a_straight.gpx", "".join(body))

# --- B: same geometry, no newlines at all (chunk-boundary stress) ------------
write("b_oneline.gpx", "".join(body).replace("\n", ""))

# --- C: self-closing tags, lon attribute before lat -------------------------
body = ['<?xml version="1.0"?><gpx><trk><name>SelfClose</name><trkseg>']
for la, lo in pts:
    body.append(f'<trkpt lon="{lo:.7f}" lat="{la:.7f}"/>')
body.append('</trkseg></trk></gpx>')
write("c_selfclose.gpx", "".join(body))

# --- D: RideWithGPS shape - waypoint cues first, then rtept route -----------
cues = [(8000.0, "Turn left onto Polna"), (2000.0, "Right onto Krakowska"),
        (5000.0, "Continue straight")]
body = ['<?xml version="1.0"?>\n<gpx><metadata><name>Cued Route</name></metadata>\n']
for d, nm in cues:      # deliberately out of order in the file
    body.append(f'<wpt lat="{LAT0:.7f}" lon="{lon_for(d):.7f}">'
                f'<name>{nm}</name><sym>Turn</sym></wpt>\n')
body.append('<rte>\n')
for la, lo in pts:
    body.append(f'<rtept lat="{la:.7f}" lon="{lo:.7f}"><ele>120</ele></rtept>\n')
body.append('</rte></gpx>\n')
write("d_cues.gpx", "".join(body))

# --- E: no elevation anywhere ----------------------------------------------
body = ['<gpx><trk><trkseg>']
for la, lo in pts:
    body.append(f'<trkpt lat="{la:.7f}" lon="{lo:.7f}"></trkpt>')
body.append('</trkseg></trk></gpx>')
write("e_noele.gpx", "".join(body))

# --- F: sawtooth climb, 10 x (up 100 m, down 100 m) => 1000 m ascent --------
body = ['<gpx><trk><name>Sawtooth</name><trkseg>']
for i, (la, lo) in enumerate(pts):
    seg = i // 50
    within = (i % 50) / 49.0
    ele = 100 + (within * 100 if seg % 2 == 0 else 100 - within * 100)
    body.append(f'<trkpt lat="{la:.7f}" lon="{lo:.7f}"><ele>{ele:.1f}</ele></trkpt>')
body.append('</trkseg></trk></gpx>')
write("f_sawtooth.gpx", "".join(body))
meta["sawtooth_ascent"] = 1000.0

# --- G: 200 km, 20001 points - forces the decimation path -------------------
BIG = 20001
big = [(LAT0, lon_for(i * STEP)) for i in range(BIG)]
meta["big_km"] = sum(haversine(*big[i], *big[i+1]) for i in range(0, BIG-1))
body = ['<gpx><trk><name>Very Long</name><trkseg>']
for la, lo in big:
    body.append(f'<trkpt lat="{la:.7f}" lon="{lo:.7f}"/>')
body.append('</trkseg></trk></gpx>')
write("g_big.gpx", "".join(body))

# --- H: an out-and-back, where a naive global snap picks the wrong leg ------
# Out 5 km east, then back along a line 12 m to the north.
body = ['<gpx><trk><name>Out And Back</name><trkseg>']
for i in range(501):
    body.append(f'<trkpt lat="{LAT0:.7f}" lon="{lon_for(i*STEP):.7f}"/>')
for i in range(500, -1, -1):
    body.append(f'<trkpt lat="{LAT0 + lat_for(12.0):.7f}" lon="{lon_for(i*STEP):.7f}"/>')
body.append('</trkseg></trk></gpx>')
write("h_outback.gpx", "".join(body))

# --- I: garbage / not a course ---------------------------------------------
write("i_empty.gpx", '<?xml version="1.0"?><gpx><metadata><name>Nothing</name></metadata></gpx>')

# --- J: known corners ------------------------------------------------------
# East 900, right 90 to south 900, left 90 back to east 800, bear right 45 for 700.
# Turns therefore sit at 900 (Right), 1800 (Left), 2600 (SlightRight).
def walk(legs, step=10.0):
    """legs = [(bearing_deg, length_m)]; returns metric offsets from origin."""
    pts, x, y = [(0.0, 0.0)], 0.0, 0.0
    for brg, length in legs:
        n = int(length / step)
        dx = math.sin(math.radians(brg)) * step
        dy = math.cos(math.radians(brg)) * step
        for _ in range(n):
            x += dx; y += dy
            pts.append((x, y))
    return pts

def to_gpx(pts, name, cues=None):
    b = [f'<gpx><trk><name>{name}</name><trkseg>']
    for x, y in pts:
        b.append(f'<trkpt lat="{LAT0 + y / M_LAT:.7f}" lon="{x / M_LON:.7f}"/>')
    b.append('</trkseg></trk>')
    for x, y, nm in (cues or []):
        b.append(f'<wpt lat="{LAT0 + y / M_LAT:.7f}" lon="{x / M_LON:.7f}">'
                 f'<name>{nm}</name></wpt>')
    b.append('</gpx>')
    return "".join(b)

corners = walk([(90, 900), (180, 900), (90, 800), (135, 700)])
write("j_turns.gpx", to_gpx(corners, "Three Corners"))
meta["turns_at"] = [900, 1800, 2600]

# Same corners, with a waypoint naming the first one 12 m before it.
first_corner = walk([(90, 888)])[-1]
write("j_named.gpx", to_gpx(corners, "Named Corners",
                            [(first_corner[0], first_corner[1], "Turn right onto Polna")]))

# --- K: a 300 m radius sweeping bend - must NOT read as a turn -------------
bend, x, y, brg = [(0.0, 0.0)], 0.0, 0.0, 90.0
for _ in range(30):                      # 300 m straight lead-in
    x += math.sin(math.radians(brg)) * 10; y += math.cos(math.radians(brg)) * 10
    bend.append((x, y))
for _ in range(47):                      # quarter circle, R = 300 m
    brg += math.degrees(10.0 / 300.0)
    x += math.sin(math.radians(brg)) * 10; y += math.cos(math.radians(brg)) * 10
    bend.append((x, y))
for _ in range(30):
    x += math.sin(math.radians(brg)) * 10; y += math.cos(math.radians(brg)) * 10
    bend.append((x, y))
write("k_bend.gpx", to_gpx(bend, "Sweeping Bend"))

# --- L: out 500 m, U-turn, back 500 m eight metres to the side -------------
uturn = walk([(90, 500)])
ux, uy = uturn[-1]
uturn += [(ux - i * 10.0, uy + 8.0) for i in range(1, 51)]
write("l_uturn.gpx", to_gpx(uturn, "There And Back"))

# --- M: cue points the geometry alone would not flag -----------------------
# A 20 deg bend at 600 m (below NAV_TURN_MIN_DEG, so no turn is detected there)
# carrying a waypoint whose text names no direction. It must still get an arrow.
# A second waypoint sits on dead-straight road and must stay a plain cue.
cuebend = walk([(90, 600), (110, 600)])
bend_pt = walk([(90, 600)])[-1]
straight_pt = walk([(90, 300)])[-1]
write("m_cuebend.gpx", to_gpx(cuebend, "Shallow Bend Cue", [
    (bend_pt[0], bend_pt[1], "Feed stop"),
    (straight_pt[0], straight_pt[1], "Water tap"),
]))

meta["m_lat"] = M_LAT
meta["m_lon"] = M_LON
meta["lat0"] = LAT0
with open(os.path.join(OUT, "truth.json"), "w") as f:
    json.dump(meta, f, indent=1)
print(json.dumps(meta, indent=1))
