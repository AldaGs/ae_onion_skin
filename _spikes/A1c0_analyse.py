"""
A1c0_analyse.py (Onion Skin) - reads the A1c0 signature sweep and says what each
parameter of saveBlittedImageToPng(boolean, File, integer, boolean) does.

Method: hold two parameters fixed, vary the third, and look at what changed in
the output - dimensions from the PNG IHDR, and a content hash. A parameter that
never changes anything is either inert here or needs a state we did not vary.
That distinction is reported, not glossed.

Usage:  python A1c0_analyse.py
"""

import hashlib
import os
import re
import struct
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = re.compile(r"^A1c0_([TF])_(\d+)_([TF])\.png$")


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(33)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        raise ValueError(f"{path}: not a PNG with a leading IHDR")
    return struct.unpack(">II", head[16:24])


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def load():
    out = {}
    for fn in sorted(os.listdir(HERE)):
        m = NAME.match(fn)
        if not m:
            continue
        b1 = m.group(1) == "T"
        n = int(m.group(2))
        b2 = m.group(3) == "T"
        p = os.path.join(HERE, fn)
        if os.path.getsize(p) == 0:
            continue
        try:
            w, h = png_size(p)
        except ValueError as e:
            print(f"  ! {fn}: {e}")
            continue
        out[(b1, n, b2)] = (w, h, sha(p), os.path.getsize(p))
    return out


def context():
    p = os.path.join(HERE, "A1c0_log.txt")
    if not os.path.exists(p):
        return None
    return open(p, encoding="utf-8", errors="replace").read()


def main():
    data = load()
    if not data:
        print("No A1c0_*.png found. Run A1c0_SweepBlit.jsx in AE first.")
        log = context()
        if log:
            print("\nA log exists, so the script ran but every call failed.")
            print("The throws are the result - here they are:\n")
            for line in log.splitlines():
                if "THROW" in line or "distinct error" in line or line.startswith("  x"):
                    print("  " + line.strip())
        return 1

    ctx = context() or ""
    print("=" * 70)
    print("A1c0 - saveBlittedImageToPng(boolean, File, integer, boolean)")
    print("=" * 70)
    for key in ("zoom =", "comp =", "whole-comp*zoom"):
        for line in ctx.splitlines():
            if line.startswith(key) or line.strip().startswith(key):
                print(line.strip())

    print(f"\n{len(data)} of 32 combinations produced a file.\n")

    print("  b1     n   b2      w x h        bytes    sha")
    for (b1, n, b2) in sorted(data):
        w, h, s, sz = data[(b1, n, b2)]
        print(f"  {str(b1):5} {n:3}  {str(b2):5}  {w:5} x {h:<5}  {sz:8}  {s[:12]}")

    # ---- which integers are legal ----------------------------------------
    legal = sorted({n for (_, n, _) in data})
    print(f"\nlegal integer values seen: {legal}")
    if legal and legal[-1] == 7:
        print("  NOTE: the sweep stopped at 7 and 7 was accepted, so the domain")
        print("  may extend further. Widen the sweep before treating this as")
        print("  the full enum.")

    # ---- isolate each parameter ------------------------------------------
    def variation(group_by, label):
        """Group by everything EXCEPT one parameter; report if that one matters."""
        groups = defaultdict(dict)
        for key, val in data.items():
            b1, n, b2 = key
            fixed = group_by(b1, n, b2)
            varied = {"b1": b1, "n": n, "b2": b2}[label]
            groups[fixed][varied] = val

        changed_dims = changed_bytes = comparable = 0
        for fixed, vals in groups.items():
            if len(vals) < 2:
                continue
            comparable += 1
            dims = {(v[0], v[1]) for v in vals.values()}
            hashes = {v[2] for v in vals.values()}
            if len(dims) > 1:
                changed_dims += 1
            if len(hashes) > 1:
                changed_bytes += 1

        print(f"\n{label}:")
        if not comparable:
            print("  no comparable pairs - cannot say.")
            return
        print(f"  {comparable} comparable group(s); "
              f"dimensions differed in {changed_dims}, bytes in {changed_bytes}")
        if changed_dims:
            print("  -> changes the OUTPUT SIZE. Likely a resolution/scale control.")
        elif changed_bytes:
            print("  -> changes CONTENT but not size. Likely a channel/overlay/"
                  "alpha toggle.")
        else:
            print("  -> changed NOTHING in this state. Either inert, or it")
            print("     depends on a viewer state this sweep did not vary")
            print("     (pan, overlays, resolution, preview mode). Not proven inert.")

    variation(lambda b1, n, b2: (n, b2), "b1")
    variation(lambda b1, n, b2: (b1, b2), "n")
    variation(lambda b1, n, b2: (b1, n), "b2")

    # ---- the question A1c actually needs ---------------------------------
    print("\n" + "-" * 70)
    print("WHAT THIS DOES NOT ANSWER")
    print("Nothing here says whether the blit encodes the PAN, because the pan")
    print("was constant for the whole sweep. Once a working argument tuple is")
    print("known, redo the three-run test (unpanned / panned same zoom /")
    print("different zoom) with that tuple hardcoded. That is the measurement")
    print("A1c exists for.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
