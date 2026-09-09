# B3 result — run 1, 2026-09-09. **PASS. The Option B gate is green.**

Log: `_spikes/B3_run1.txt` (310 lines). 151 writes.

| # | Claim | Verdict | Evidence |
|---|---|---|---|
| 1 | Both halves load, from two binaries | **pass** | `FX GLOBAL_SETUP` + `STARTUP AEGP half loaded` |
| 2 | Brightness visibly changes the render | **pass** | operator |
| 3 | Effect-controls button opens the panel | **pass** | lines 5–6, 14–15 |
| 3 | Clicking again does not close it | **pass** | lines 16–17: two presses, panel not recreated |
| 4 | Viewer repaints on a panel write | **pass** | operator |
| 4 | **Selection intact on every write** | **pass** | **0 occurrences of `SELECTION CHANGED` in 151 writes** |
| 4 | Works with a non-effect layer selected | **pass** | `sel 23`, `sel 25` |
| 4 | Works with nothing selected | **pass** | `sel -1` (lines 184–218, 263–276) |
| 5 | No-op write causes no visible change | **pass** | lines 194–203, `70.0 -> 70.0`, readback 70.0 |
| 5 | Dead button logs nothing | **pass** | no stray lines |
| 5b | Direct button still fails | **pass** | line 204–206: `readback=-999.0 err=3` |
| 6 | One write = one undo step | **pass, with a defect found** | see below |
| 7 | Keyframed stream refused, not corrupted | **pass** | 29 `REFUSED` lines, 278–306 |

## The gate row

**151 writes, zero selection changes.** `sel 23 -> 23`, `sel 25 -> 25`,
`sel -1 -> -1` — never once different across the arrow. The panel can write a
param on a layer the user has not selected without disturbing what they *have*
selected.

That is Option B's premise, measured rather than assumed. **The gate is green and
Phase 1 is unblocked.**

## The deferral is confirmed, not assumed

The direct-path control did its job. Same code, same param, same moment:

```
line 204  DIRECT  calling DoWrite straight from the wndproc
line 205  WRITE   PLUS  0.0 -> 10.0  readback=-999.0
line 206  WRITE   PLUS  err=3
```

`readback=-999.0` is the sentinel for "the read-back never happened". Every
queued write in the same session reports a readback equal to what it wrote. So
the deferral is what fixed it — a one-sided test could not have established that.

**Phase 2 rule, now evidenced:** panel controls may not touch AEGP project APIs
from their window callback. They queue; the idle hook writes.

## Idle latency — cheap enough to ignore

Measured over 141 queue→write pairs:

| min | median | p90 | max |
|---|---|---|---|
| 1 ms | 27 ms | 43 ms | 54 ms |

Under two frames at 24fps in the worst case, typically under one. The deferral
costs nothing a user will notice, which was the open question when it was
introduced.

## A defect found and fixed: unbalanced undo group

The operator saw **"Group Mismatch"** once on Ctrl+Z and could not reproduce it.
The cause is in this spike's own code, and it is deterministic:

```c
ERR(...AEGP_StartUndoGroup(...));      // ERR = "if (!err) err = FUNC" -> SKIPPED on a prior error
...
suites.UtilitySuite3()->AEGP_EndUndoGroup();   // called unconditionally
```

A write that had already failed skipped the Start and still ran the End. That is
exactly the Direct button's path (`err=3`), which is why it appeared around that
test and not again afterwards.

Fixed by tracking the group with a flag rather than trusting call order. **The
lesson generalises past this spike:** `ERR()` makes every subsequent call
conditional, so any *acquire/release* or *begin/end* pair written with `ERR` on
the opening call and a bare call on the closing one is unbalanced on the error
path. Worth grepping for elsewhere.

## Unexplained: value discontinuities

Several writes start from a value no previous write left behind:

- 13:52:23 — reads `60.0` when the last write left `70.0` (backwards)
- 13:52:33 — reads `50.0` when the last left `80.0` (backwards)
- **13:52:39 — reads `140.0` when the last left `30.0` (forwards)**

The backwards jumps are consistent with the Ctrl+Z testing in step 6 and need no
explanation. **The forward jump does not**: nothing in this plug-in raises the
value except a `PLUS` write, and none is logged between.

Most likely a redo (Ctrl+Shift+Z) or a manual slider drag during the undo test —
both invisible to this log, which only records what *we* write. Recorded as
unexplained rather than guessed at. It does not touch any gate row: every write
read back exactly what it wrote, whatever the starting value.

If Phase 2 ever needs to know that a value changed underneath it, that is a
*watcher* problem and there is no evidence here either way.

## Verdict

**B3 passes. Both gate rows (B2, B3) are green. Phase 0 is complete.**

Answers carried into Phase 1 and 2:

1. Panel writes do not disturb selection.
2. Writes must be queued from the panel and performed in an idle hook, ~27 ms.
3. Keyframed streams cannot be written with `AEGP_SetStreamValue` — the Phase 2
   panel needs a keyframe-aware path or must refuse and say so.
4. Two binaries: effect and panel, finding each other by match name.
5. Undo groups must be balanced by a flag, never by call order under `ERR`.
