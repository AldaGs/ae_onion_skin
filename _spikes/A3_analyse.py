"""
A3_analyse.py (Onion Skin) - Phase 0, spike A3, steps a and b.

Reads the tick logs osA3.aex writes to %TEMP% and answers:

  A3a  Does anything inside AE run during a modal drag, and at what cadence?
  A3b  Can the transform be READ from a tick during motion, and what does it
       cost?

THE MEASUREMENT IS THE GAP, NOT THE COUNT. A run that averages 60 ticks/second
can still be unusable if it delivers 120 in one second and none in the next -
and "none in the next" is exactly what a modal drag would do. So the headline
numbers here are the WORST gaps, not the means.

Phases are not labelled in the log; they do not need to be. The per-second
timeline makes them obvious - if the idle hook goes silent while the timer keeps
ticking, that IS the drag, and that is the finding pieFX S2 predicts.

Usage:  python A3_analyse.py
"""

import os
import sys

import numpy as np

TEMP = os.environ.get("TEMP") or os.environ.get("TMP") or "."

# A 16 ms request that lands inside 33 ms is still one frame at 60 Hz's budget.
# Beyond that the overlay drops a frame; beyond ~100 ms a viewer would visibly
# smear behind the picture.
GAP_OK_MS = 33.0
GAP_BAD_MS = 100.0

# A read costing more than one tick period cannot be done every tick.
READ_OK_MS = 16.0


def load(path):
    meta, rows = {}, []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                parts = line[1:].strip().split("\t")
                if len(parts) >= 2:
                    meta[parts[0].strip()] = parts[1].strip()
                continue
            if line.startswith("kind\t"):
                continue
            p = line.split("\t")
            if len(p) < 5:
                continue
            rows.append((p[0], float(p[1]), float(p[2]), float(p[3]), int(p[4])))
    return meta, rows


def gaps_report(label, times_ms):
    if len(times_ms) < 3:
        print(f"  {label}: {len(times_ms)} tick(s) - too few to characterise.")
        return None
    g = np.diff(np.array(times_ms))
    p50, p95, p99 = np.percentile(g, [50, 95, 99])
    worst = g.max()
    over_ok = int((g > GAP_OK_MS).sum())
    over_bad = int((g > GAP_BAD_MS).sum())
    print(f"  {label}: {len(times_ms)} ticks over {times_ms[-1]/1000:.1f}s")
    print(f"      gap  p50 {p50:7.2f}   p95 {p95:7.2f}   p99 {p99:7.2f}"
          f"   WORST {worst:8.2f} ms")
    print(f"      gaps over {GAP_OK_MS:.0f}ms: {over_ok}"
          f"   over {GAP_BAD_MS:.0f}ms: {over_bad}")
    return {"worst": worst, "p99": p99, "over_bad": over_bad, "n": len(times_ms)}


def timeline(rows, dur_s):
    """Ticks per second, per kind. The shape of the drag shows up here."""
    print("\n  per-second timeline (t = timer, i = idle):")
    print("    sec |  timer  idle | reads_ok  read_ms_max")
    for sec in range(int(dur_s) + 1):
        lo, hi = sec * 1000.0, (sec + 1) * 1000.0
        t = [r for r in rows if r[0] == "timer" and lo <= r[1] < hi]
        i = [r for r in rows if r[0] == "idle" and lo <= r[1] < hi]
        reads = [r for r in (t + i) if r[2] > 0]
        ok = sum(1 for r in (t + i) if r[4])
        rmax = max((r[2] for r in reads), default=0.0)
        bar_t = "#" * min(60, len(t))
        flag = ""
        if not i and t:
            flag = "   <- idle silent, timer alive"
        elif not t and not i:
            flag = "   <- NOTHING RAN"
        print(f"    {sec:3d} | {len(t):6d} {len(i):5d} | {ok:8d}  {rmax:10.2f}  {bar_t}{flag}")


def analyse(path, mode):
    if not os.path.exists(path):
        print(f"\n{mode}: no log at {path}")
        print(f"   Run the '{mode}' menu item in AE (on, exercise, off).")
        return None

    meta, rows = load(path)
    if not rows:
        print(f"\n{mode}: log exists but holds no samples.")
        return None

    print(f"\n{'='*70}\n{mode}   ({len(rows)} samples, "
          f"requested tick {meta.get('requested_tick_ms','?')} ms)\n{'='*70}")

    t_times = [r[1] for r in rows if r[0] == "timer"]
    i_times = [r[1] for r in rows if r[0] == "idle"]
    dur = max((r[1] for r in rows), default=0.0) / 1000.0

    print("\n  CADENCE")
    t_stats = gaps_report("timer", t_times)
    i_stats = gaps_report("idle ", i_times)

    timeline(rows, dur)

    read_stats = None
    reads = [r[2] for r in rows if r[2] > 0]
    if reads:
        a = np.array(reads)
        oks = [r for r in rows if r[2] > 0 and r[4]]
        p50, p95, p99 = np.percentile(a, [50, 95, 99])
        print(f"\n  ZOOM READ  ({len(reads)} attempts, "
              f"{len(oks)} succeeded = {100.0*len(oks)/len(reads):.1f}%)")
        print(f"      p50 {p50:.3f}   p95 {p95:.3f}   p99 {p99:.3f}"
              f"   WORST {a.max():.3f} ms")
        zs = sorted({round(r[3], 6) for r in rows if r[4]})
        print(f"      distinct zoom values seen: {len(zs)}"
              + (f"  {zs[:6]}{' ...' if len(zs) > 6 else ''}" if zs else ""))
        if len(zs) <= 1:
            print("      Only one zoom value - the run never actually zoomed, so")
            print("      this says nothing about tracking a CHANGING transform.")
        read_stats = {"p95": float(p95), "worst": float(a.max()),
                      "ok_frac": len(oks) / len(reads), "distinct": len(zs)}

    return {"timer": t_stats, "idle": i_stats, "read": read_stats, "dur": dur}


def main():
    a = analyse(os.path.join(TEMP, "onionskin_A3a.txt"), "A3a")
    b = analyse(os.path.join(TEMP, "onionskin_A3b.txt"), "A3b")

    print(f"\n{'='*70}\nA3a / A3b VERDICT\n{'='*70}")
    if not a and not b:
        print("No logs. Nothing measured.")
        return 1

    # --- A3a -------------------------------------------------------------
    if a and a["timer"]:
        t = a["timer"]
        if t["over_bad"] == 0 and t["p99"] <= GAP_OK_MS:
            print(f"A3a PASS: the thread timer held cadence throughout "
                  f"(p99 {t['p99']:.1f} ms, worst {t['worst']:.1f} ms).")
        elif t["worst"] < GAP_BAD_MS:
            print(f"A3a MARGINAL: worst gap {t['worst']:.1f} ms - dropped frames,")
            print("     but nothing a viewer would read as a stall.")
        else:
            print(f"A3a FAIL: worst gap {t['worst']:.1f} ms. The overlay would")
            print("     visibly lag the picture during that window.")
        if a["idle"] and a["idle"]["worst"] > a["timer"]["worst"] * 2:
            print(f"     As predicted by pieFX S2, the IDLE hook is the weaker"
                  f" path (worst {a['idle']['worst']:.1f} ms"
                  f" vs timer {a['timer']['worst']:.1f} ms).")
            print("     Build on the timer, not on idle.")

    # --- A3b -------------------------------------------------------------
    if b and b["read"]:
        r = b["read"]
        if r["distinct"] <= 1:
            print("A3b INCONCLUSIVE: the zoom never changed during the run, so")
            print("     nothing here shows the transform being tracked in motion.")
        elif r["ok_frac"] > 0.98 and r["p95"] <= READ_OK_MS:
            print(f"A3b PASS: zoom readable every tick "
                  f"(p95 {r['p95']:.2f} ms, {100*r['ok_frac']:.1f}% success).")
            print("     The transform is knowable in motion. A3c is worth building.")
        elif r["ok_frac"] > 0.5:
            print(f"A3b MARGINAL: p95 {r['p95']:.2f} ms, "
                  f"{100*r['ok_frac']:.1f}% success. Reading every tick is too")
            print("     expensive; the overlay would have to interpolate between")
            print("     reads, which is a design change, not a tuning problem.")
        else:
            print(f"A3b FAIL: only {100*r['ok_frac']:.1f}% of reads succeeded.")
            print("     AE will not answer while it is being dragged, so the")
            print("     transform must be INFERRED from raw input - and drift in")
            print("     the transform is visible misalignment.")
    elif b:
        print("A3b: no read timings in the log (was A3b mode actually on?).")

    print("\nNeither of these is A3c. Cadence and readability are necessary,")
    print("not sufficient - only a real overlay judged on video settles whether")
    print("it stays glued.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
