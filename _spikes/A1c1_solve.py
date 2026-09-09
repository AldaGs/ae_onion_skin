"""
A1c1_solve.py (Onion Skin) - Phase 0, spike A1.

Solves the comp -> screen transform from a captured viewer panel, and judges it
against A1's pass criteria.

THE MODEL. A1b established the transform is a scale plus a translation (AE's
viewer does not rotate or shear):

    screen_x = sx * comp_x + tx
    screen_y = sy * comp_y + ty

sx and sy are solved independently rather than forced equal. They SHOULD be
equal for a square-pixel comp, so any difference between them is a free
diagnostic - non-square pixel aspect, or a bad marker detection.

WHAT IS BEING CHECKED, AND HOW IT CAN FAIL. Three independent checks, so that
one of them being satisfiable by accident does not carry the result:

  1. RESIDUAL. Fit on the four corner markers, then predict the CENTRE marker,
     which the fit never saw. Its error is the honest measure. A four-point fit
     to four points is exact by construction and proves nothing.
  2. CROSS-CHECK ON ZOOM. The solved scale must match the zoom ExtendScript
     reported for the same moment. Two independent sources that must agree.
  3. BROKEN CONTROLS. Two deliberately wrong transforms are run through the same
     predictor, and both must be caught. If either survives, the harness cannot
     detect failure and no PASS it prints is meaningful.

     The first control forced the scale to 1.0. That is DEGENERATE at 100% zoom,
     where 1.0 is the correct answer - so the moment the matrix gained its 100%
     states, the control silently stopped being a control on exactly the four
     new captures. The replacements are wrong everywhere: a fixed +3 px
     translation (error is 3 px regardless of zoom, comp size or marker
     position) and a 5% scale error. The lesson generalises: a control has to be
     wrong at every point in the matrix, or it stops guarding precisely where
     coverage was added.

Usage:  python A1c1_solve.py
"""

import json
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

TOL_PX = 1.0            # A1 pass criterion
ZOOM_TOL_REL = 0.01     # 1% agreement between solved scale and reported zoom


# ---------------------------------------------------------------------------
# BMP reading (24-bit, top-down or bottom-up) - no dependency beyond numpy
# ---------------------------------------------------------------------------
def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    hdr = struct.unpack_from("<IiiHH", data, 14)
    _, w, h, planes, bits = hdr
    if bits != 24:
        raise ValueError(f"{path}: expected 24-bit, got {bits}")
    top_down = h < 0
    h = abs(h)
    stride = ((w * 3) + 3) & ~3
    buf = np.frombuffer(data, dtype=np.uint8, count=stride * h, offset=off)
    img = buf.reshape(h, stride)[:, : w * 3].reshape(h, w, 3)
    if not top_down:
        img = img[::-1]
    # BMP stores BGR; return RGB
    return img[:, :, ::-1].astype(np.int16)


# ---------------------------------------------------------------------------
def find_marker(img, rgb, name):
    """Centroid of the pixels closest to this marker's colour.

    Colour distance, not an exact match: the viewer applies colour management
    and the capture is 8-bit, so exact equality would find nothing and report
    it as a missing marker.
    """
    target = np.array([c * 255 for c in rgb], dtype=np.int16)
    dist = np.abs(img - target).sum(axis=2)

    # A marker is saturated and unique; the background is mid-grey. Take the
    # tightest threshold that still yields a plausible blob.
    for thresh in (60, 90, 120, 160):
        mask = dist < thresh
        n = int(mask.sum())
        if n >= 4:
            ys, xs = np.nonzero(mask)
            # Reject a "blob" that is really scattered noise across the frame.
            spread = max(xs.max() - xs.min(), ys.max() - ys.min())
            if spread > 0.25 * max(img.shape[:2]):
                continue
            return float(xs.mean()), float(ys.mean()), n, thresh
    return None


# ---------------------------------------------------------------------------
def solve_affine(comp_pts, scr_pts):
    """Least squares for sx, tx and sy, ty independently."""
    cx = np.array([p[0] for p in comp_pts], dtype=float)
    cy = np.array([p[1] for p in comp_pts], dtype=float)
    sx_ = np.array([p[0] for p in scr_pts], dtype=float)
    sy_ = np.array([p[1] for p in scr_pts], dtype=float)

    Ax = np.column_stack([cx, np.ones_like(cx)])
    Ay = np.column_stack([cy, np.ones_like(cy)])
    (sx, tx), *_ = np.linalg.lstsq(Ax, sx_, rcond=None)
    (sy, ty), *_ = np.linalg.lstsq(Ay, sy_, rcond=None)
    return sx, tx, sy, ty


def predict(sx, tx, sy, ty, pt):
    return sx * pt[0] + tx, sy * pt[1] + ty


# ---------------------------------------------------------------------------
def load_calib():
    p = os.path.join(HERE, "A1c1_calib.json")
    if not os.path.exists(p):
        print("Missing A1c1_calib.json - run A1c1_MakeCalib.jsx in AE first.")
        return None
    return json.load(open(p, encoding="utf-8"))


def load_zooms():
    """n -> {'zoom': float, 'w': int|None, 'h': int|None, 'par': float|None}

    Newer log lines carry the comp geometry; older ones do not. Geometry has to
    travel WITH the reading, because A1c1_calib.json only ever holds whichever
    comp MakeCalib built last - and a run using two comp sizes overwrites it.
    """
    p = os.path.join(HERE, "A1c1_zoom_log.txt")
    out = {}
    if not os.path.exists(p):
        return out
    for line in open(p, encoding="utf-8", errors="replace"):
        f = line.rstrip("\r\n").split("\t")
        if len(f) < 2 or not f[0].isdigit():
            continue
        rec = {"zoom": float(f[1]), "w": None, "h": None, "par": None}
        if len(f) >= 7:
            try:
                rec["w"], rec["h"], rec["par"] = int(f[4]), int(f[5]), float(f[6])
            except ValueError:
                pass
        out[int(f[0])] = rec
    return out


def load_geometry_override():
    """Optional A1c1_geometry.json: {"9-12": [640, 360], ...}, ranges inclusive.

    For datasets captured before the zoom log carried geometry. This is asserted
    knowledge, so the solver labels it INFERRED wherever it uses it - and the
    inference is falsifiable: applying it must leave every residual unchanged
    AND make the zoom cross-check agree. If it does not, the inference is wrong.
    """
    p = os.path.join(HERE, "A1c1_geometry.json")
    if not os.path.exists(p):
        return {}
    raw = json.load(open(p, encoding="utf-8"))
    out = {}
    for key, wh in raw.items():
        if key.startswith("_"):
            continue
        lo, _, hi = key.partition("-")
        for n in range(int(lo), int(hi or lo) + 1):
            out[n] = (int(wh[0]), int(wh[1]))
    return out


def load_capture_meta(n):
    p = os.path.join(HERE, f"A1c1_cap_{n}.txt")
    d = {}
    if not os.path.exists(p):
        return d
    for line in open(p, encoding="utf-8", errors="replace"):
        if "=" in line:
            k, v = line.split("=", 1)
            d[k.strip()] = v.strip()
    return d


# ---------------------------------------------------------------------------
def analyse_one(n, calib, zooms, geom_override):
    bmp = os.path.join(HERE, f"A1c1_cap_{n}.bmp")
    img = read_bmp(bmp)
    meta = load_capture_meta(n)

    print(f"\n{'='*70}\nCAPTURE {n}   {img.shape[1]} x {img.shape[0]}")
    if meta.get("uniform colour", "").startswith("YES"):
        print("  SKIPPED: the capture tool flagged this as a flat blank frame.")
        return None
    for k in ("class", "client origin", "dpi", "method"):
        if k in meta:
            print(f"  {k:14}= {meta[k]}")

    # Resolve this capture's comp geometry, then scale the marker table to it.
    zrec = zooms.get(n) or {}
    cw, ch = zrec.get("w"), zrec.get("h")
    src = "zoom log"
    if not cw and n in geom_override:
        cw, ch = geom_override[n]
        src = "A1c1_geometry.json (INFERRED)"
    if not cw:
        cw, ch = calib["width"], calib["height"]
        src = "A1c1_calib.json (last build only)"

    gx = cw / float(calib["width"])
    gy = ch / float(calib["height"])
    if abs(gx - 1) > 1e-9 or abs(gy - 1) > 1e-9:
        print(f"  comp geometry = {cw}x{ch} from {src}"
              f"   (markers scaled x{gx:g}, x{gy:g})")
    elif src.startswith("A1c1_calib"):
        print(f"  comp geometry = {cw}x{ch} from {src}")

    by_name = {m["name"]: dict(m, x=m["x"] * gx, y=m["y"] * gy)
               for m in calib["markers"]}
    found, missing = {}, []
    for m in calib["markers"]:
        r = find_marker(img, m["rgb"], m["name"])
        if r is None:
            missing.append(m["name"])
        else:
            found[m["name"]] = r

    print(f"\n  markers found: {len(found)}/{len(calib['markers'])}")
    for name, (x, y, cnt, th) in found.items():
        print(f"    {name:12} at ({x:8.2f},{y:8.2f})  {cnt:6} px  thresh {th}")
    if missing:
        print(f"    MISSING: {', '.join(missing)}")

    # ---- marker integrity ------------------------------------------------
    # A partially-detected marker still yields a centroid, and that centroid is
    # BIASED - it just looks like a slightly different position. In capture 1
    # the red marker came back at 69 px against 361 for the others (AE was
    # drawing selection handles over it) and biased its centre by ~1.1 px:
    # most of A1's entire budget, hidden inside a result that otherwise passed.
    # So compare each marker's area against the median and refuse the outliers.
    partial = []
    if len(found) >= 3:
        areas = sorted(r[2] for r in found.values())
        median = areas[len(areas) // 2]
        for name, (x, y, cnt, th) in found.items():
            if cnt < 0.5 * median or cnt > 2.0 * median:
                partial.append((name, cnt, median))

    if partial:
        print("\n  MARKER INTEGRITY FAILURE:")
        for name, cnt, median in partial:
            print(f"    {name}: {cnt} px against a median of {median}")
        print("    A partial marker gives a biased centroid, not a missing one,")
        print("    so this would have solved and silently reported a wrong")
        print("    transform. Common cause: AE drawing selection handles or an")
        print("    overlay over the marker. Deselect all layers, hide overlays,")
        print("    and recapture. NOT ANALYSED.")
        return None

    corners = ["cal_red", "cal_green", "cal_blue", "cal_yellow"]
    if any(c not in found for c in corners):
        print("\n  CANNOT SOLVE: need all four corner markers.")
        print("  Is the whole comp visible in the viewer? Zoom out / fit.")
        return None
    if "cal_magenta" not in found:
        print("\n  Centre marker missing - the residual check is unavailable,")
        print("  so this capture cannot produce a trustworthy PASS.")
        return None

    comp_pts = [(by_name[c]["x"], by_name[c]["y"]) for c in corners]
    scr_pts = [(found[c][0], found[c][1]) for c in corners]
    sx, tx, sy, ty = solve_affine(comp_pts, scr_pts)

    # AE's viewer applies the pixel aspect ratio on the X axis only: the drawn
    # image is comp.width * PAR * zoom wide but comp.height * zoom tall. So the
    # expected sx/sy is exactly PAR, not 1.
    #
    # PAR is MEASURED here, not asserted. A1c1_calib.json holds only whichever
    # PAR the calibration comp was last built at, and this run mixed two of them
    # (12 square captures, then 3 anamorphic) - so trusting the file would
    # mislabel twelve captures. The marker geometry is identical across both, so
    # sx/sy is a clean observable: report it, and say which of the intended
    # values it matches. That is better than the assertion it replaces.
    EXPECTED_PARS = (1.0, 2.0)
    print(f"\n  SOLVED   sx={sx:.6f}  sy={sy:.6f}   tx={tx:.2f}  ty={ty:.2f}")
    ratio = sx / sy if abs(sy) > 1e-9 else float("nan")
    best = min(EXPECTED_PARS, key=lambda p: abs(ratio - p) / p)
    off = abs(ratio - best) / best
    par_ok = off < 0.01
    print(f"  sx/sy = {ratio:.6f}  -> PAR {best:g} "
          f"({off*100:.3f}% off){'' if par_ok else '   <-- matches NEITHER 1.0 nor 2.0'}")

    # ---- check 1: residual on the held-out centre marker -----------------
    mx, my = by_name["cal_magenta"]["x"], by_name["cal_magenta"]["y"]
    px, py = predict(sx, tx, sy, ty, (mx, my))
    ax, ay = found["cal_magenta"][0], found["cal_magenta"][1]
    err = float(np.hypot(px - ax, py - ay))
    print(f"\n  [1] HELD-OUT CENTRE: predicted ({px:.2f},{py:.2f}) "
          f"actual ({ax:.2f},{ay:.2f})")
    print(f"      error {err:.3f} px   -> {'PASS' if err <= TOL_PX else 'FAIL'}"
          f" (tolerance {TOL_PX} px)")

    # ---- check 2: cross-check against ExtendScript zoom ------------------
    z = (zooms.get(n) or {}).get('zoom')
    zoom_ok = None
    if z is None:
        print(f"\n  [2] ZOOM CROSS-CHECK: no zoom reading {n} logged - "
              f"run A1c1_Zoom.jsx before each capture.")
    else:
        # Compare on the Y axis: sy is the raw zoom, untouched by PAR.
        # (sx would need dividing by PAR first, which just adds a place for
        # the comparison itself to be wrong.)
        rel = abs(sy - z) / max(abs(z), 1e-9)
        zoom_ok = rel <= ZOOM_TOL_REL
        print(f"\n  [2] ZOOM CROSS-CHECK: reported {z:.8f}  solved sy={sy:.8f}"
              f"  ({rel*100:.3f}% apart)")
        print(f"      -> {'PASS' if zoom_ok else 'FAIL'} "
              f"(tolerance {ZOOM_TOL_REL*100:.0f}%)")
        if not zoom_ok:
            print("      The two independent sources disagree. Either the")
            print("      zoom reading and the capture were not made in")
            print("      lockstep, or one of them is not measuring what we")
            print("      think it is. Do not proceed on this.")

    # ---- check 3: the broken controls ------------------------------------
    # The original control forced the scale to 1.0. That is degenerate at 100%
    # zoom, where 1.0 IS the right answer - so on the four Set B captures the
    # "broken" transform was correct and the harness reported that it could not
    # detect breakage. A control has to be wrong at EVERY point in the matrix,
    # otherwise it silently stops being a control exactly where new coverage was
    # added.
    #
    # Two now, and both must be caught:
    #   translation - off by a fixed 3 px, so its error is 3 px regardless of
    #                 zoom, comp size or marker position. Cannot go degenerate.
    #   scale       - 5% larger, which also catches a predictor that ignores
    #                 comp coordinates entirely and just returns the offset.
    controls = []

    bpx, bpy = predict(sx, tx + 3.0, sy, ty + 3.0, (mx, my))
    controls.append(("translation +3px",
                     float(np.hypot(bpx - ax, bpy - ay))))

    bpx, bpy = predict(sx * 1.05, tx, sy * 1.05, ty, (mx, my))
    controls.append(("scale x1.05",
                     float(np.hypot(bpx - ax, bpy - ay))))

    print("\n  [3] BROKEN CONTROLS:")
    for label, berr in controls:
        print(f"      {label:18} error {berr:8.3f} px"
              f"  -> {'correctly FAILS' if berr > TOL_PX else 'ALSO PASSES'}")
    control_ok = all(berr > TOL_PX for _, berr in controls)
    if not control_ok:
        print("      A control the harness cannot detect means nothing it")
        print("      reports as PASS is meaningful for this capture.")

    return {
        "n": n, "err": err, "zoom_ok": zoom_ok, "par": best, "par_ok": par_ok,
        "control_ok": control_ok, "sx": sx, "sy": sy, "tx": tx, "ty": ty,
    }


# ---------------------------------------------------------------------------
def main():
    calib = load_calib()
    if calib is None:
        return 1
    zooms = load_zooms()

    caps = []
    n = 1
    while os.path.exists(os.path.join(HERE, f"A1c1_cap_{n}.bmp")):
        caps.append(n)
        n += 1
    if not caps:
        print("No A1c1_cap_*.bmp found. Run os_A1c1.exe over the comp viewer.")
        return 1

    print(f"calibration: {calib['comp']} {calib['width']}x{calib['height']}"
          f"  par={calib['pixelAspect']}  {len(calib['markers'])} markers")
    print(f"captures: {len(caps)}   zoom readings: {len(zooms)}")

    geom = load_geometry_override()
    results = [r for r in (analyse_one(i, calib, zooms, geom) for i in caps) if r]

    print(f"\n{'='*70}\nA1 VERDICT\n{'='*70}")
    if not results:
        print("No capture produced a usable solve. A1 is UNPROVEN, not failed.")
        return 1

    worst = max(r["err"] for r in results)
    controls_ok = all(r["control_ok"] for r in results)
    zoom_checked = [r for r in results if r["zoom_ok"] is not None]
    zooms_ok = all(r["zoom_ok"] for r in zoom_checked) if zoom_checked else None

    print(f"usable captures : {len(results)}")
    print(f"worst residual  : {worst:.3f} px   (criterion {TOL_PX} px)")
    print(f"broken control  : {'detected everywhere' if controls_ok else 'NOT DETECTED'}")
    print(f"zoom cross-check: "
          f"{'all agree' if zooms_ok else ('DISAGREE' if zooms_ok is False else 'not run')}"
          f"   (on {len(zoom_checked)} of {len(results)} captures)")
    pars = {}
    for r in results:
        pars.setdefault(r["par"], []).append(r["n"])
    print("measured PAR    : " + ", ".join(
        f"{p:g} on captures {v}" for p, v in sorted(pars.items())))
    if not all(r["par_ok"] for r in results):
        bad = [r["n"] for r in results if not r["par_ok"]]
        print(f"                  captures {bad} match NEITHER 1.0 nor 2.0")

    print()
    if not controls_ok:
        print("INVALID - the harness cannot detect a broken transform.")
    elif zooms_ok is False:
        print("INVALID - independent sources disagree about zoom.")
    elif worst > TOL_PX:
        print("A1 FAILS on these captures.")
    elif len(results) < 12:
        print(f"PROMISING but INCOMPLETE: {len(results)} of the 12 required")
        print("states measured. A1 needs 3 zooms x 2 pans x 2 panel sizes,")
        print("including one non-square-pixel comp, before it can pass.")
    else:
        print("A1 PASSES.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
