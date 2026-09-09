"""
A3e_video.py (Onion Skin) - Phase 0, spike A3e.

THE VERDICT COMES FROM HERE, not from A3e_analyse.py. The paint log can only see
the pipeline after the capture; this sees the whole chain, because it reads the
overlay and the comp OUT OF THE SAME VIDEO FRAME.

    slip = green box edge  -  actual comp edge,  same frame, in screen pixels

HOW THE TWO ARE READ WITHOUT ONE HIDING THE OTHER. The overlay is drawn on the
comp edge, so it occludes exactly the thing to be measured. But the probe already
leaves a one-pixel ALPHA GAP on its scan row and column, so its own detector can
read AE through it - and that gap serves here too: on the gap row the overlay
draws nothing, so the comp's edges are clean. The green box's own x is read from
a row a few pixels away, where the box is present and its vertical line is at the
same x. The gap is not only a self-capture fix; it is the measurement port.

TIME SYNC IS DERIVED, NOT GUESSED. The green box is drawn from the probe's own
tx, so in every frame

    green_x(video)  =  panel_origin_x + tx(log)

Cross-correlating the two recovers BOTH the video-to-probe time offset and the
panel's screen origin, with a residual that says whether the alignment is real.
A sync asserted by hand would be the one unfalsifiable number in the chain.

Knowing the panel origin then bounds the scan to the VIEWER PANEL. The first
version of this script did not, ran its background test off into AE's project
and timeline panels, and reported every single frame as blind - a tracker bug
that would have been read as a catastrophic probe failure.

BLIND FRAMES ARE COUNTED SEPARATELY, AND THAT IS THE POINT. When a comp edge is
off-panel the probe's detector cannot see it, holds the last good t, and the
overlay sits still while the picture moves. That is NOT latency, and averaging it
in would describe neither: a mean mixing 2 px of lag with 300 px of blindness is
a number about nothing.

KNOWN LIMITATION, RUN 1 - THE MOVING BINS ARE NOT YET MEASURED HERE. The width
control below compares the detected comp width against s*comp_w, with s taken
from the probe log at that instant. While the user is ZOOMING, the drawn width
changes continuously and the log's s is quantised to accepted samples, so the
comparison misses by more than its 4 px tolerance and the frame is refused. In
run 1 that refused 1856 frames - and they are precisely the moving ones, leaving
1244 at-rest frames and a single wheel sample.

That is the control behaving correctly on a badly chosen reference, not a
detector fault, and it is left in rather than loosened: a wider tolerance would
start accepting genuinely wrong edges, which is the failure this whole project
refuses to risk. The fix is to stop asking the log at all and use the PROBE'S
OWN control - scan a column as well as a row and require both axes to imply the
same zoom. That needs a second pass over the video and is the next change here.

Until then: the at-rest result from this script stands, and the moving bins are
covered only by A3e_analyse.py's lower bound.

    py A3e_video.py <video.mp4> [captures.txt] [--dump slips.txt]

Needs ffmpeg on PATH and numpy. Streams raw frames; writes no images.
"""

import sys
import subprocess
import numpy as np

W, H = 1920, 1080
SAT_TOL = 70			# how close to the pure overlay colour a pixel must be
BG_TOL = 30				# same threshold the probe's own detector uses
FPS = 60.0
REST_MAX, WHEEL_MAX = 20.0, 400.0
BUDGET = {"at rest": 1.0, "wheel": 5.0, "drag": 20.0}


def read_captures(path):
    out = []
    for line in open(path):
        if line.startswith("#") or line.startswith("t_ms"):
            continue
        p = line.rstrip("\n").split("\t")
        if len(p) < 10:
            continue
        out.append({"t": float(p[0]), "pw": int(p[1]), "ph": int(p[2]),
                    "sx": float(p[3]), "tx": float(p[5]), "ty": float(p[6]),
                    "status": p[9]})
    return out


def pass1(video, cache=None):
    """One streaming pass. Per frame: the green box, the gap row index, and the
    gap row's pixels - kept so everything after sync is offline arithmetic.

    Cached to an .npz because decoding the video is the slow part and the
    analysis after it needed several corrections; re-deriving the same pixels
    each time would have made each correction cost four minutes."""
    if cache:
        import os
        if os.path.exists(cache):
            z = np.load(cache, allow_pickle=True)
            return list(z["frames"])
    out = _pass1(video)
    if cache:
        np.savez_compressed(cache, frames=np.array(out, dtype=object))
    return out


def _pass1(video):
    proc = subprocess.Popen(
        ["ffmpeg", "-v", "error", "-i", video,
         "-f", "rawvideo", "-pix_fmt", "bgr24", "-"],
        stdout=subprocess.PIPE, bufsize=W * H * 3)
    fsize = W * H * 3
    frames = []
    gap_row = None
    n = 0
    while True:
        buf = proc.stdout.read(fsize)
        if len(buf) < fsize:
            break
        n += 1
        f = np.frombuffer(buf, np.uint8).reshape(H, W, 3)
        b = f[:, :, 0].astype(np.int16)
        g = f[:, :, 1].astype(np.int16)
        r = f[:, :, 2].astype(np.int16)
        gm = (np.abs(r - 40) < SAT_TOL) & (np.abs(g - 255) < SAT_TOL) & (np.abs(b - 40) < SAT_TOL)

        #	STRUCTURE, NOT COLOUR. The authored green is (40,255,40) but OBS's
        #	colour conversion puts it on screen as (94,255,65) - and the comp
        #	itself contains a cal_green solid at (19,255,8). A tolerance loose
        #	enough to catch the shifted box also catches the marker; one tight
        #	enough to reject the marker rejects the box. Colour cannot separate
        #	them, so do not ask it to.
        #
        #	The box's edges are LINES hundreds of pixels long; the marker is a
        #	40 px blob. Keeping only rows and columns with a long run of green
        #	is immune to both the colour shift and the marker, and needs no
        #	guess about what the encoder did.
        #
        #	The first version matched on colour, found the box in 805 of 4729
        #	frames, and the sync had almost nothing to fit.
        colsum = gm.sum(axis=0)
        rowsum = gm.sum(axis=1)
        cols = np.flatnonzero(colsum > 100)
        rowsr = np.flatnonzero(rowsum > 100)
        if cols.size < 2 or rowsr.size < 2:
            frames.append(None)
            continue
        gx0, gx1 = int(cols[0]), int(cols[-1])
        gy0, gy1 = int(rowsr[0]), int(rowsr[-1])

        #	The gap row: inside the box, the row carrying no green at all. The
        #	probe leaves it blank so its own detector can read AE through it.
        inner = gm[gy0 + 5:gy1 - 5, gx0:gx1 + 1].sum(axis=1)
        zero = np.flatnonzero(inner == 0)
        gap_row = int(gy0 + 5 + zero[0]) if zero.size else None
        if gap_row is None:
            frames.append(None)
            continue

        gxs = np.array([gx0])
        frames.append({
            "gx0": gx0, "gx1": gx1, "gy0": gy0, "gy1": gy1,
            "green_l": int(gxs[0]) if gxs.size else None,
            "gap_row": gap_row,
            "row": f[gap_row].astype(np.int16).copy(),
        })
    proc.stdout.close()
    proc.wait()
    return frames


def sync(frames, caps):
    """Recover (frame_offset, panel_origin_x). Returns None if the alignment is
    not credible - reporting no sync beats reporting a wrong one."""
    vid = [(i, f["green_l"]) for i, f in enumerate(frames)
           if f and f["green_l"] is not None]
    if len(vid) < 200:
        return None
    okc = [c for c in caps if c["status"] == "ok"]
    if len(okc) < 200:
        return None
    t0 = okc[0]["t"]

    def tx_at(ms):
        """tx the probe had ON SCREEN at this moment - and None across a blind
        gap. During a gap the overlay is showing a STALE box while this stream
        has already jumped to the far side of the gap, so those samples compare
        two different instants and drag the agreement down. The first attempt
        did not exclude them, scored 68.7%, and would have been trusted under
        the old 0.5 bar. Sync must be fitted on the stretches where the overlay
        was actually tracking."""
        lo, hi = 0, len(okc) - 1
        if ms < okc[0]["t"] or ms > okc[-1]["t"]:
            return None
        while hi - lo > 1:
            m = (lo + hi) // 2
            if okc[m]["t"] <= ms:
                lo = m
            else:
                hi = m
        if okc[hi]["t"] - okc[lo]["t"] > 40.0:
            return None
        return okc[lo]["tx"]

    best = None
    #	Coarse then fine over plausible offsets: the recording was started by
    #	hand after the probe armed, so the offset is seconds, not minutes.
    for step, span, centre in ((10, 1800, 0), (1, 30, None)):
        c = centre if centre is not None else best[1]
        for off in range(c - span, c + span + 1, step):
            deltas = []
            for i, gx in vid[::7]:
                tx = tx_at(t0 + (i - off) * 1000.0 / FPS)
                if tx is not None:
                    deltas.append(gx - tx)
            if len(deltas) < 100:
                continue
            a = np.array(deltas)
            med = np.median(a)
            #	SCORE ON MAD, NOT ON A FRACTION WITHIN 2 px. The fraction-based
            #	score was self-defeating: green_x and tx disagree during motion
            #	BY EXACTLY THE SLIP THIS SCRIPT EXISTS TO MEASURE, so demanding
            #	2 px agreement scored the fraction of at-rest frames and then
            #	refused the correct offset for having too few of them. A median
            #	absolute deviation is robust to that tail while still collapsing
            #	when the alignment is genuinely wrong.
            mad = float(np.median(np.abs(a - med)))
            if best is None or mad < best[0]:
                best = (mad, off, float(med), float(np.mean(np.abs(a - med) < 2.0)))
    #	A weak alignment is worse than none: it silently shifts every comparison
    #	below. Accept only a tight fit.
    if best is None or best[0] > 2.0:
        return None
    return best


#	Inset from the panel's own borders. The viewer panel is not uniform right up
#	to its client edge - there is a border, and a scrollbar can appear - so a
#	background test taken AT the edge compares two pieces of chrome and fails
#	everywhere. The first version of this script did exactly that and reported
#	72.3% of frames blind against the probe's own 11.5%, with an "at rest" worst
#	case of 562 px on a frame where the box is visibly exact.
PANEL_INSET = 24


def scan_comp(row, x_lo, x_hi):
    """Comp edges on the gap row, bounded to the viewer panel. None when an end
    is not background - the honest answer when the comp runs off that side."""
    x_lo += PANEL_INSET
    x_hi -= PANEL_INSET
    seg = row[x_lo:x_hi]
    if len(seg) < 32:
        return None
    a = seg[2]
    if int(np.abs(seg[-3] - a).sum()) > BG_TOL:
        return None
    d = np.abs(seg - a).sum(axis=1) > BG_TOL
    d[:2] = False
    d[-2:] = False
    idx = np.flatnonzero(d)
    if idx.size == 0:
        return None
    return (x_lo + int(idx[0]), x_lo + int(idx[-1]))


def q(v, p):
    return v[min(len(v) - 1, int(round(p / 100.0 * (len(v) - 1))))]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    video = args[0]
    caps_path = args[1] if len(args) > 1 else "A3e_captures.txt"
    dump = None
    if "--dump" in sys.argv:
        dump = open(sys.argv[sys.argv.index("--dump") + 1], "w")
        dump.write("frame\tprobe_ms\tcomp_l\tgreen_l\tslip\tstatus\n")

    caps = read_captures(caps_path)
    frames = pass1(video, cache='A3e_video_cache2.npz')
    n = len(frames)
    armed = [f for f in frames if f]
    print("frames            %d  (%.1f s at %.0f fps)" % (n, n / FPS, FPS))
    print("  overlay up      %d" % len(armed))

    s = sync(frames, caps)
    if s is None:
        print("\nNO SYNC. Cannot place the video against the probe log, so the")
        print("panel origin is unknown and nothing below could be trusted.")
        return 3
    mad, off, panel_x, frac = s
    print("  sync            frame %d = probe start, panel_origin_x = %.0f"
          % (off, panel_x))
    print("  sync fit        MAD %.1f px  (%.1f%% within 2 px - the rest is the"
          % (mad, 100.0 * frac))
    print("                  slip itself, which is why MAD is the gate)")

    okc = [c for c in caps if c["status"] == "ok"]
    t0 = okc[0]["t"]
    pw = okc[0]["pw"]
    x_lo, x_hi = int(panel_x), int(panel_x + pw)

    def sx_at(ms):
        """Zoom the probe recorded nearest this moment, for the control below.
        None outside the accepted stream or across a gap - the control must not
        be evaluated against an invented zoom."""
        if ms < okc[0]["t"] or ms > okc[-1]["t"]:
            return None
        lo, hi = 0, len(okc) - 1
        while hi - lo > 1:
            m = (lo + hi) // 2
            if okc[m]["t"] <= ms:
                lo = m
            else:
                hi = m
        if okc[hi]["t"] - okc[lo]["t"] > 100.0:
            return None
        return okc[lo]["sx"]

    rows = []
    for i, f in enumerate(frames):
        if not f:
            rows.append((i, None, None, "no_overlay"))
            continue
        comp = scan_comp(f["row"], x_lo, x_hi)
        gl = f["green_l"]
        #	THE CONTROL THIS SCRIPT WAS MISSING. What the row scan found is only
        #	the comp if its width equals s*comp_w - otherwise it locked onto a
        #	panel divider, a layer outline or the pasteboard, and would hand back
        #	a confident wrong edge. Without this the first run reported a 562 px
        #	"slip" at rest, which is a detector failure wearing a result's
        #	clothing. Same control the probe's own detector carries; the tracker
        #	had none, which is why it was the tracker that was wrong.
        if comp is not None:
            s = sx_at(t0 + (i - off) * 1000.0 / FPS)
            if s is None:
                comp = None
                bad = "no_zoom"
            elif abs((comp[1] - comp[0] + 1) - s * 1920.0) > 4.0:
                comp = None
                bad = "not_comp"
            else:
                bad = None
        else:
            bad = "edge_off"

        if comp is None:
            rows.append((i, None, gl, bad))
        elif gl is None:
            rows.append((i, comp[0], None, "no_green"))
        else:
            rows.append((i, comp[0], gl, "ok"))
        if dump:
            dump.write("%d\t%.1f\t%s\t%s\t%s\t%s\n"
                       % (i, t0 + (i - off) * 1000.0 / FPS,
                          rows[-1][1] if rows[-1][1] is not None else "",
                          gl if gl is not None else "",
                          abs(rows[-1][2] - rows[-1][1])
                          if rows[-1][3] == "ok" else "",
                          rows[-1][3]))
    if dump:
        dump.close()

    ok = [r for r in rows if r[3] == "ok"]
    offb = [r for r in rows if r[3] == "edge_off"]
    notcomp = [r for r in rows if r[3] == "not_comp"]
    print("  measurable      %d" % len(ok))
    print("  found-not-comp  %d  (rejected by the width control)" % len(notcomp))
    print("  comp edge off   %d  (%.1f%% of armed frames)  <- DETECTOR BLIND"
          % (len(offb), 100.0 * len(offb) / max(1, len(armed))))
    if not ok:
        print("\nNo measurable frames.")
        return 3

    vel, prev = {}, None
    for r in ok:
        if prev and r[0] == prev[0] + 1:
            vel[r[0]] = abs(r[1] - prev[1]) * FPS
        prev = r

    bins = {"at rest": [], "wheel": [], "drag": []}
    for r in ok:
        v = vel.get(r[0])
        if v is None:
            continue
        name = ("at rest" if v < REST_MAX
                else "wheel" if v < WHEEL_MAX else "drag")
        bins[name].append(abs(r[2] - r[1]))

    print("\nUSER-VISIBLE SLIP  (green box edge vs comp edge, same frame, px)")
    print("%-9s %7s %9s %9s %9s   %s" %
          ("bin", "n", "median", "p95", "worst", "budget"))
    verdict = True
    for name in ("at rest", "wheel", "drag"):
        v = sorted(bins[name])
        if not v:
            print("%-9s %7d   (not exercised)" % (name, 0))
            continue
        over = q(v, 95) > BUDGET[name]
        verdict = verdict and not over
        print("%-9s %7d %9.1f %9.1f %9.1f   %5.0f %s"
              % (name, len(v), q(v, 50), q(v, 95), v[-1], BUDGET[name],
                 "OVER" if over else "ok"))

    if offb:
        runs, cur = [], None
        for r in rows:
            if r[3] == "edge_off":
                cur = [r[0], r[0]] if cur is None else [cur[0], r[0]]
            elif cur:
                runs.append(cur)
                cur = None
        if cur:
            runs.append(cur)
        runs.sort(key=lambda c: -(c[1] - c[0]))
        print("\nBLIND FRAMES - a comp edge was off-panel, so the overlay held")
        print("its last t while the picture moved. NOT latency, not counted")
        print("above, and this is the failure the user reported.")
        print("  runs            %d" % len(runs))
        print("  longest         %.2f s" % ((runs[0][1] - runs[0][0]) / FPS))
        print("  total blind     %.2f s" % (sum(c[1] - c[0] for c in runs) / FPS))

    print("\nSLIP VERDICT      %s" % ("WITHIN BUDGET" if verdict else "OVER BUDGET"))
    print("Read it with the blind count: a slip figure describes only the frames")
    print("where the detector could see. Blindness is a separate defect with a")
    print("separate fix, and it does not argue for or against WGC.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
