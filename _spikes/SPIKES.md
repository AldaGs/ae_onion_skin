# Onion Skin — Phase 0 spike log

## DECISION (2026-09-09): Option A is closed

**Called by the user after watching A3f run 1 in AE:** the errors seen in normal
use would cost the customer. Recorded here as a decision on evidence, not a
change of heart, because the temptation later will be to remember it as
"we ran out of patience".

### The number that closed it is not 27.8%

27.8% fully blind was a *fixable* number, and the fix was already costed. What
closed Option A is the projection of the **fully repaired** detector — grid
sampling plus the one-edge solve, both measured, neither built:

| zoom | recoverable after every planned fix |
|---|---|
| 0.25 | 93.7% |
| 0.50 | 95.2% |
| 0.63 | 86.3% |
| **1.00** | **65.3%** |
| 1.50 | 49.2% |

**100% zoom is where animators work,** and a third of pan positions there have no
recoverable transform at all. With stage 2 behaving correctly that means the
overlay *disappears* — unpredictably, in the primary use case. And stage 2 is the
good branch; the alternative is the lying box the roadmap was written to forbid.

So the ceiling, after all the remaining work, is a tool that is untrustworthy
exactly where it is most needed. Further spikes cannot raise it, because the
limit is geometric: at 100% zoom the comp is larger than the panel, and a
transform recovered from the comp's own edges cannot be recovered when no edge is
on screen.

### The rest of the ledger, which the ceiling makes moot

None of these were fatal alone; all of them were still outstanding.

- **A2 viewer identity** never solved. Nothing can name AE's panels (A1a).
- **PAR correction toggle unreadable.** `sx = zoom·PAR` when it is on, and
  `ViewOptions` does not expose it — a second piece of transform-affecting state
  that must be inferred.
- **Occlusion.** The screen blit reads the desktop, so any window over the viewer
  corrupts the read. WGC would fix it, and WGC was never the bottleneck.
- **Self-capture** costs a permanent alpha gap on every sample line.
- **macOS** needs the Screen Recording grant, and the app prompted is After
  Effects, not the plug-in (A5, never run).

### What Phase 0 actually bought

The gate did its job. It cost Phase 0 and nothing else — exactly the trade the
roadmap set up when it declared A and B to share almost no code.

**Proved and kept:**

- `screen = s·comp + t` is the right model, to 0.559 px worst residual (A1).
- `views[i].options.zoom` is exact and readable *during a modal drag* at 0.19 ms
  (A3b).
- `SetTimer(NULL, 0, ...)` is dispatched by AE's modal loops; the idle hook is
  not (A3a).
- GDI screen capture costs one composition sync (16.7 ms); `GetDC(hwnd)` on AE's
  viewer returns nothing; `PrintWindow` costs 50 ms (A3d2).
- The overlay must be out of process, and `AEGP_ExecuteScript` pumps messages, so
  a timer-driven plug-in is re-entrant by default (A3d).
- The calibration comp, the capture tooling and the measurement harnesses — which
  verify **B's** alignment too, for free.

**Thrown away:** overlay window code, transform reconstruction, the strip/grid
detector, per-axis fusion, viewer identity work.

### Why the answer was never going to come from more capture engineering

Three architectures were tried for `t`, and the failure was the same each time,
wearing different clothes:

1. **Infer from input** (A3c) — missed the wheel scroll, and would have missed
   the next gesture too. Failed because it cannot see the causes it does not
   enumerate.
2. **Measure from the comp's edges** (A3d/A3e/A3f) — cannot see an edge that is
   off screen.
3. **Hold the last good `s`** — the zoom changed across 6 of 7 blind runs.
   Blindness is *caused* by zooming.

Each works when it is not needed and fails when it is. That is the signature of a
missing input, not of an unfinished implementation: **AE does not expose the comp
viewer's pan, and everything above was an attempt to reconstruct it from the
outside.** Option B does not reconstruct it. AE applies it.


---

*Everything below is the Phase 0 record, kept as evidence. It is closed work.*

Roadmap: `_aePlugins/onion-skin-roadmap.md`. Gate rule: A1–A3 are hard stops to
Option B.

---

## A1 — Transform: can we know where comp (x, y) lands on screen?

Three possible analytic sources, cheapest first. Each is a separate sub-spike so
that a dead end costs only itself.

### A1-0 — AEGP headers (desk check) — **NOTHING THERE**

`AEGP_ItemViewSuite1` (`AE_GeneralPlug.h:554`, frozen AE 13.6) contains exactly
one call: `AEGP_GetItemViewPlaybackTime`. The only other API taking an
`AEGP_ItemViewP` is `AEGP_ColorSettingsSuite6`'s
`AEGP_DoesViewHaveColorSpaceXform` / `AEGP_XformWorkingToViewColorSpace`.

There is **no AEGP source for viewer zoom, scroll offset, or drawn-image rect.**

### A1a — OS window tree — **DEAD** (2026-09-08)

`A1a_WindowProbe.cpp` → `os_A1a.exe`. Walks AE's visible top-level windows and
every descendant, printing class / text / rect, and flags three things:
zoom-like text, scrollbar-like classes, and everything that is not AE's own
`DroverLord - Window Class`.

Run 2 (the valid one): 3 top-level windows, **149 windows total**.

| Finding | Hits |
|---|---|
| [1] zoom-like text | **0** |
| [2] scrollbar-like classes | **0** |
| [3] non-`DroverLord` classes | 52 |

All 52 of [3] belong to CEP extensions (`WC_PLUGPLUG_HTMLEXTENSION_CLASS_` and
their `CefBrowserWindow` / `Chrome_WidgetWin_0` subtrees — MTAG Color, MTAG
Easing, MTAG Toolbar, MTAG Secondary, Declutter, mattetool2), expression editors
(`Scintilla`), stray `Edit` boxes belonging to those panels, `SysShadow`, and
AE's own `AE_CApplication_26.3` frame.

**Nothing in AE's native UI is an OS control.** AE draws its magnification popup
itself, so there is no window text to read and no `GetScrollInfo` to call.
Confirms and extends pieFX S2: not only can we not *name* AE's panels, we cannot
*read* anything inside them.

#### Two corrections made before the result was trusted

- **Run 1 was invalid and looked fine.** `EnumChildWindows` already enumerates
  every descendant, not just immediate children, so recursing into it re-walked
  each subtree at every level: 3219 "windows" for a tree of 149, almost all
  duplicates. That blew the 256-entry cap on finding [3] — which meant an empty
  finding would have been indistinguishable from a *truncated* one. Fixed by
  enumerating once per top-level window and deriving depth from the parent chain;
  caps raised to 4096.
- **Added a matcher self-test** (9 cases, including negatives: `""`,
  `"Composition"`, bare `"%"`). Reports 9/9 and aborts on failure. Without it,
  "no zoom control found" and "my matcher is broken" produce identical output —
  and zero was the expected result, which is exactly when a silent matcher bug
  would have been believed.

DPI: the probe sets `PER_MONITOR_AWARE_V2` dynamically, falling back to
`SetProcessDPIAware`. Without it every rect comes back virtualised and every
later transform is silently wrong by the DPI scale.

### A1b — ExtendScript object model — **ZOOM YES, PAN NO** (2026-09-08)

`A1b_ViewerReflect.jsx`, run on AE 26.3x87. Reflects (does not guess) over
`app.activeViewer`, its `views[]`, and each view's `options`.

**`views[0].options.zoom = 0.14272835850716`.** A live magnification, not a
round default — the viewer was on a Fit value, which is exactly why the run was
specified at a non-100% zoom. Zoom is readable, exactly, as a float.

Everything `ViewOptions` has: `channels`, `checkerboards`, `exposure`,
`fastPreview`, `guidesLocked`, `guidesSnap`, `guidesVisibility`, `rulers`,
`zoom`. **No scroll offset, no pan, no drawn-image rect.** `Viewer` itself has
only `active`, `activeViewIndex`, `maximized`, `type`, `views`; `View` has only
`active` and `options`.

So the transform decomposes into:

| Component | Source | Status |
|---|---|---|
| scale | `views[i].options.zoom` | **solved, exact** |
| translation (pan) | nothing exposes it | **2 unknowns remain** |

This is a much better position than "empirical solve of the whole transform".
With zoom known and the panel rect known from A1a, the only unknown is a 2-DOF
translation — and 2 DOF is tractable by correlation, where a full similarity
solve would not have been.

#### Two unplanned finds in the same dump

- **`views[i].saveBlittedImageToPng`** — writes out what the viewer is actually
  displaying. A better instrument than screen capture for recovering pan: correlate
  the blit against our own render of the comp scaled by the known `zoom`, and the
  crop offset *is* the pan. Offline, in numpy, per [[verify-offline-before-rebuild]].
  Also usable as A1's own verifier, with no screen capture in the loop at all.
- **`Viewer.type` = 7612** and `app.activeViewer` — AE will name its own viewer
  kind and hand us the active one. This is a lead on **A2** (identity), which was
  scoped assuming window-tree archaeology after pieFX S2. May be much cheaper than
  planned. Not yet tested against multiple viewers or a floating viewer.

Both are leads, not results. Neither has been measured.

---

## A3 — Sync: does it stay glued under motion?

### The question, sharpened (2026-09-09)

The roadmap framed A3 as "can we repaint fast enough". That is the easy half, and
framing it that way would have measured the wrong thing.

**Repaint cadence is nearly free if the overlay is out of process.** pieFX S3B
already proved an out-of-process layered window wins AE's z-order, and a separate
process has its own message loop — AE's modal drag loop cannot starve it.

**The hard half is knowing WHAT to paint.** A1 established the transform is
`screen = s·comp + t`. During a drag both terms change continuously, and pieFX S2
established that **AE sits in a modal loop for the whole duration of a mouse
press and does not pump AEGP idle time**. So at exactly the moment the transform
is changing fastest, the two ways we know of reading it — an AEGP idle hook and
ExtendScript's `views[i].options.zoom` — may both be unavailable.

So A3 is really: **can we know the transform while the user is dragging?** If
not, the overlay must *infer* it from raw input (pan = mouse delta, zoom = wheel
ticks), which drifts, and drift in this transform is visible misalignment — the
exact failure that makes an onion skin worse than none.

### Decomposition, riskiest first

| | question | kills A3 if |
|---|---|---|
| **A3a** | Does anything inside AE run during a modal drag, and at what cadence? | nothing ticks |
| **A3b** | Can the transform be READ from a tick during motion, and how fast? | reads block or are too slow |
| **A3c** | Does a real overlay stay glued, judged on video? | it visibly smears |

pieFX S2 already found the likely answer to A3a: a `SetTimer(NULL, 0, ...)`
thread timer **is** dispatched by AE's modal loops where the idle path is not.
A3a re-measures that for this purpose and logs the actual cadence; A3b is the
open question and the real gate.

A3a and A3b need a minimal AEGP plug-in. That is justified now A1 has passed, and
the scaffolding is the same one Phase 1 needs.

### Tooling (built 2026-09-09, awaiting an AE session)

`OnionSkin/A3/` — `osA3.aex`, cloned from the pieFX Phase 0 spike scaffolding.
Two menu toggles under Window; no drawing, no project changes.

- **Timestamp before any AE call.** A slow read and a late tick are different
  failures — one means AE starved us, the other means AE answered too slowly —
  and the log has to tell them apart.
- **A3a and A3b are separate commands.** Doing the zoom read inside the cadence
  measurement would perturb the thing being measured: if a read costs 40 ms,
  cadence collapses and the two facts become indistinguishable.
- **Both paths logged side by side.** The idle hook is half the A3a measurement,
  not a fallback — the question is precisely whether it goes quiet while the
  timer keeps running.
- Samples buffered in memory (writing from a tick would measure the file system);
  the timer kills itself after 60 s so a forgotten toggle cannot leave one
  running.

`_spikes/A3_analyse.py` judges it. **The measurement is the worst GAP, not the
mean rate** — 60 ticks/second averages fine while delivering 120 in one second
and none in the next, and "none in the next" is exactly what a modal drag does.
A per-second timeline makes the phases self-evident without labelling them: idle
going silent while the timer keeps ticking *is* the drag.

Build notes for this tree: the project sits one directory shallower than the
pieFX spike it was cloned from, so every SDK-relative path needed one level
removed. `dumpbin /EXPORTS osA3.aex` shows a bare `EntryPointFunc` — the
ten-second check that catches the whole class of AEGP load failures.

### Results (2026-09-09) — **A3a MARGINAL, A3b PASS**

A3a: 3822 samples over 60 s. A3b: 1780 samples over 34 s.

| path | p50 gap | p99 gap | worst gap |
|---|---|---|---|
| thread timer (A3a) | 30.56 | 45.18 | **94.35 ms** |
| idle hook (A3a) | 46.99 | 62.42 | **9175.94 ms** |
| thread timer (A3b) | 30.28 | 45.52 | 81.75 ms |
| idle hook (A3b) | 46.95 | 63.14 | 7941.91 ms |

**The decisive window is A3b seconds 11–24.** Idle ticks: **0**. Timer ticks:
~40/s, unbroken. Zoom reads: ~40 per second, **every one succeeding**, worst
0.29 ms. A 14-second sustained modal drag in which AE answered ExtendScript
30 times a second while never once pumping the idle hook.

- **`views[i].options.zoom` is readable during a drag, and it is cheap.** 1780
  attempts, **100% success**, p50 0.186 ms, p95 0.289 ms, worst 1.856 ms. 33
  distinct zoom values, so the transform really was changing — this is not the
  inconclusive "read the same number 1780 times" case.
- **The idle hook is unusable and the thread timer is not.** 9.2 s and 7.9 s of
  total idle silence against a worst timer gap of 94 ms — a factor of ~100.
  pieFX S2 confirmed on a different AE version, for a different purpose.
  **Architecture: build on `SetTimer(NULL, 0, ...)`, never on idle.**

#### Two things the numbers say that the verdict line does not

- **The cadence is ~33 Hz, not the 60 Hz requested.** p50 gap 30.5 ms against a
  16 ms request. That is Windows' default 15.6 ms timer resolution rounding a
  16 ms request up to two ticks — not AE, and not contention. Very likely fixed
  by `timeBeginPeriod(1)`, which A3c should test explicitly rather than assume.
  33 Hz may well be enough for an overlay; the point is that the current number
  is an artefact, so it should not be used to judge A3c either way.
- **A3b proved `s` is readable. It proved nothing about `t`.** The transform is
  `screen = s·comp + t`, and A1b found **no source anywhere for the pan
  translation** — not AEGP, not the window tree, not ExtendScript. A1 recovered
  `t` by marker calibration from a screen capture, which cannot be done 30 times
  a second. So the remaining risk is now exactly one thing, and it is sharper
  than when A3 started: **recovering `t` continuously.**

  The promising line for A3c: during a hand-tool drag the image tracks the cursor
  1:1, so `Δt` *is* the mouse delta — exact, not inferred, with drift only from
  missed events. That is a measurable claim and A3c should measure it, with a
  periodic re-sync by capture as the fallback.

#### `t` is ANALYTIC when unpanned (2026-09-09) — re-analysis of A1's captures

Free result: no new AE session, just the 14 solved transforms tested against the
hypothesis `t = panel_centre − s·comp_centre`.

| | max \|dx\| | max \|dy\| |
|---|---|---|
| centred captures (7) | **0.50 px** | **1.00 px** |
| panned captures (7) | 226.50 px | 234.00 px |

**AE centres the comp in the panel, and it does so exactly.** The residual is
sub-pixel and consistent (dx ≈ −0.5, dy ≈ −0.5…−1.0) — most likely our own
half-pixel centroid quantisation on even-sized markers, not an AE offset, and
well inside A1's tolerance either way. The panned captures miss by up to 234 px,
so this is a discriminating test rather than one that fits anything.

**Consequence for A3c: `t` never has to be recovered, only the PAN OFFSET does** —
and that is zero until the user pans, changes only during explicit pan gestures,
and is 1:1 with the cursor while one is in progress. The continuous-unknown
problem A3b left open is now a bounded, event-driven one.

    s  = zoom            (read per tick, 0.19 ms, proven in motion by A3b)
    t  = panel_centre - s*comp_centre + pan_offset
    pan_offset          (accumulated from cursor deltas during pan drags)

### A3c — the glued overlay (built 2026-09-09, awaiting an AE session)

Third command in `osA3.aex`. A layered topmost window over the comp viewer,
drawing a green rectangle where the comp *should* be plus a centre crosshair,
repainted from a thread timer.

Implements exactly the model the earlier results established:

    s = zoom                                   read per tick (A3b: 0.19 ms)
    t = panel_centre - s*comp_centre + pan     centring proven analytic to <1 px
    pan                                        accumulated from cursor deltas

**What it is actually testing** is the one remaining claim: that during a pan
drag the picture tracks the cursor 1:1, so `Δpan` *is* the mouse delta rather
than an estimate of it. Everything else in the model is already measured.

Deliberate choices:

- **`timeBeginPeriod(1)` plus an 8 ms request.** A3a's ~30 ms cadence was
  Windows' 15.6 ms granularity rounding a 16 ms request to two ticks — an
  artefact of the request, not an AE ceiling. A3c raises the resolution for the
  whole run and reports mean and worst gap on exit, so the 33 Hz question is
  answered rather than inherited.
- **`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`.** An overlay that ate the viewer's
  clicks or stole its focus would fail as a product however well it tracked.
- **The overlay follows the panel every tick** (`SetWindowPos` from the live
  client rect), because a panel resize or a window move is exactly the case an
  overlay pinned at arm time would get wrong.
- **PAR correction assumed OFF** (its default; ExtendScript cannot read it). If
  it is on, the box will be visibly wrong in x by the PAR factor — that is a
  demonstration of the unread state, not a bug to paper over.
- **A2 is not solved, so the user points at the viewer** — nothing can name AE's
  panels (A1a).

Judged **by eye, on video**. A message trace measures messages, not what the user
sees; this is the same lesson pieFX S2 paid for.

#### A3c result (2026-09-09) — **glued at rest, FAILS on pan**

Screen recording measured rather than eyeballed: the overlay's green rectangle and
the magenta calibration marker were both detected per frame at 5 fps and the
error tabulated (`scratchpad/track.py`).

| phase | box centre | error |
|---|---|---|
| at rest, 26.8–35.6 s | 792.0, 366.5 (constant) | **0.5, −1.1 px** |
| pan from 35.8 s | **unchanged** | grows to 110 px within 1.2 s |
| whole run | — | median 59.9 px, **max 222.4 px** |

**Two separate results, and they must not be blurred together.**

- **The model is confirmed.** At rest the overlay sat on the comp to within
  **0.5 px in x and 1.1 px in y** for nine continuous seconds, and the box
  *dimensions* tracked zoom all the way through the run
  (1296→1345→1527→1097→959 px). So `s = zoom` read per tick and
  `t = panel_centre − s·comp_centre` both work, live, exactly as A1 and A3b
  predicted.
- **Pan tracking never engaged at all.** Not drift — the box centre did not move
  by a single pixel while the comp moved 110 px. The mouse hook never fired.

**Cause: the gesture was not a mouse drag.** The frame at 36.4 s shows the cursor
as a plain arrow, stationary, *outside* the panel, while the comp slides upward
with a smooth decelerating profile (Δy per 200 ms: 32, 24, 18, 14, 10 px). That
is AE's smooth scroll, not a drag. The hook only watches `WM_MBUTTONDOWN` and
space+`WM_LBUTTONDOWN`, so it saw nothing.

**The lesson is bigger than the missing case.** AE changes the pan through at
least: wheel scroll, shift+wheel, space-drag, middle-drag, the Hand tool,
scrollbars, zoom-about-cursor, panel resize, and menu commands like Fit. Adding
`WM_MOUSEWHEEL` would fix this recording and leave the approach just as fragile —
and worse, the failure mode of a missed gesture is a *silently wrong* overlay,
which is precisely the outcome that makes an onion skin worse than none.
**Inferring `t` from input is the wrong architecture, and this is the evidence.**

Confirmed with the user: the pan was a **wheel scroll**, the most common pan
gesture there is.

### A3d — measure `t` instead of inferring it (built 2026-09-09)

Fourth command in `osA3.aex`. Same overlay; `t` comes from measurement rather
than from gesture tracking.

**The method.** We know `s` exactly, therefore the comp's on-screen size. Blit
one horizontal and one vertical **1-pixel strip** through the panel, find where
the comp's edges cross them, and `t` is the top-left crossing. Two thin BitBlts
plus an O(w+h) scan — no correlation, no full-frame capture, and **cause-agnostic
by construction**: wheel, drag, scrollbar, Hand tool, zoom-about-cursor, panel
resize and Fit all move the comp, and none of them need to be recognised.

**Three things that make it honest rather than merely clever:**

- **The control.** The detected span must equal `s·comp_size` within
  `OS_STRIP_SIZE_TOL` (3 px) on *both* axes, or the frame is **rejected**. A
  detector without this would silently lock onto a panel divider or a layer
  outline and hand back a confident wrong `t` — precisely A3c's failure mode
  wearing a different hat. Rejections are counted by reason and reported.
- **Reading past our own overlay.** The overlay is layered and topmost, so a
  screen blit would sample the green box and the detector would converge on its
  own previous output. Fixed by never painting on the two sample lines — alpha
  stays 0 there, so the composited screen shows AE's pixels through the gap. The
  sample lines are offset from the panel centre (37, 53 px) so they cannot
  coincide with the centre crosshair.
- **Rejected frames hold the last good `t`** rather than falling back to the
  analytic value, which would make the box jump on every rejection — a worse
  artefact than being briefly stale.

Known limits, stated up front: at high zoom the comp fills the panel and no edge
is on screen, so detection *must* fail (reported as `no-bg`/`no-edge`, not as a
wrong answer). Comp content that exactly matches AE's panel grey at the edge
would fool the edge finder — the size control is what catches it.

Reports on exit: attempts, accept rate, rejections by reason, and mean/worst
detection cost. **The cost number is the gate** — if this cannot run inside a
tick, the approach is dead regardless of accuracy.

#### Run 1 CRASHED AE, during a scrub (2026-09-09) — cause found and fixed

**`AEGP_ExecuteScript` pumps messages.** A `WM_TIMER` dispatched inside that pump
re-enters the paint, and the re-entrant call reaches `EnsureStrips` →
`FreeStrips()` → `DeleteObject`/`DeleteDC` **on the handles the outer call is
still using**. Use-after-free on GDI objects.

Why it appeared only now, and only on a scrub:

- A3c held no shared GDI state across the script call, so the same re-entrancy
  was harmless. A3d caches strip DCs across it, which turned a latent bug into a
  crash. The bug was arguably always there; A3d just gave it something to break.
- A busy AE makes `ExecuteScript` slower, which widens the window for a tick to
  land inside it. Scrubbing is exactly that.

Three fixes:

1. **Re-entrancy guard** (`InterlockedCompareExchange`) — a nested tick returns
   immediately instead of corrupting the outer one's state.
2. **Throttled the script call.** A3c/A3d tick at 8 ms with `timeBeginPeriod(1)`,
   so they were making **up to 125 `ExecuteScript` calls a second**. A3b measured
   30 Hz at 100% success and 0.19 ms; 125 Hz is four times the pressure on AE for
   no extra fidelity, since the overlay can repaint from a cached zoom between
   reads. Now capped at ~30 Hz, and a failed read holds the last good value
   instead of blanking the overlay.
3. **Teardown is now re-entrancy-aware.** The stop command can itself be
   dispatched from inside a paint — the menu click is sitting in the queue that
   `ExecuteScript` pumps — so destroying the window and DCs there is the *same*
   use-after-free. Stop now clears the flags and kills the tick first, and skips
   the destroy if a paint is on the stack; the next Start reclaims it, safely,
   because no timer is running by then.

**The general lesson, worth carrying to Phase 1: any AEGP call that can pump
messages is a re-entrancy point, and a timer-driven plug-in is therefore
re-entrant by default.** State held across such a call needs a guard.

#### Run 2 also went down — so A3d moved OUT of AE (2026-09-09)

The re-entrancy fixes did not save it. No Windows Error Reporting entry for the
event (the only `AfterFX.exe` records are an unrelated AppHang from the previous
afternoon), so Windows never saw a fault — consistent with AE's own crash handler
or a freeze that had to be killed.

**Two AE sessions lost to bugs in the harness is the signal to change approach,
not to debug harder.** A3d's question — can the comp's edges be found cheaply and
reliably from two 1px strips? — has nothing to do with running inside AE. So it
now runs as `os_A3d.exe`, out of process, where a bug costs a process nobody
minds. Same move pieFX made for S3B.

Suspected in-process cause, left unfixed because it no longer matters for
answering A3d: at an 8 ms tick the plug-in was allocating and blitting a
full-panel DIB (1288×697×4 ≈ 3.6 MB) up to 125 times a second **on AE's UI
thread** — around 450 MB/s of GDI work competing with AE's own drawing. That is a
plausible route to a freeze regardless of the re-entrancy bug, and it is a real
constraint on the eventual product: **the overlay must not repaint at tick rate,
and must not reallocate its bitmap per frame.**

**Out of process the control also gets stronger.** In-process the detected span
was validated against `s·comp_size`, with `s` from ExtendScript — so the check
depended on AE answering. Out here the two axes check each other:

    span_w / comp_w  and  span_h / comp_h  are both the zoom, so they must agree

No AE involved, and it catches exactly the failure that matters: a detector that
locked onto a panel divider on one axis will disagree with the other. A run where
every sample "succeeds" but the axes disagree is a **failed** run. It also
measures the zoom as a by-product — a free cross-check against what ExtendScript
reported in A3b.

`os_A3d.exe [comp_w] [comp_h] [seconds]` samples at ~60 Hz, writes
`A3d_strips.txt`, sends no input and never touches AE's process.

#### A3d result (2026-09-09) — **the method works; the capture is too slow**

545 samples over 30 s of real interaction (1592×734 panel, comp 1920×1080).

| | |
|---|---|
| accepted | **455 (83.5%)** |
| no edge | 84 (16%) — correct: at high zoom the comp fills the panel |
| **axes disagree** | **6 (1.1%)** — times it would have lied |
| axis disagreement, accepted | mean 0.14%, max 0.68% |
| distinct zooms recovered | 15, from 0.2198 to 0.6328 |
| **cost mean** | **31.6 ms** |
| cost worst | 45.0 ms |

**The detection itself is good.** It found the comp's edges through real
scrubbing, wheel-scrolling and zooming, agreed with itself across two
independent axes to within 0.14% on average, and recovered the zoom as a
by-product across a 3× range — a free cross-check on A3b's ExtendScript reads.
The 16% `no_edge` are the honest answer at high zoom, not failures.

**The cost is disqualifying, and it is entirely the BitBlt.** Flat distribution
(p5 24 ms, p50 31 ms, p99 41 ms), identical whether an edge was found (31.7 ms)
or not (30.9 ms), and invariant with panel size. The O(w+h) scan is free; the
screen readback is not.

**Hypothesis worth one more measurement before calling A3d dead.** 31.6 ms for
*two* blits on a 60 Hz display is close to 2 × 16.7 ms, which would mean each
readback blocks on a composition sync — cost **per call**, not per pixel. If so,
one full-panel blit costs the same as one thin strip while giving both axes, and
A3d fits a 60 Hz budget after all.

`A3d2_CaptureBench.cpp` → `os_A3d2.exe` times six strategies back to back
against the same window: two thin blits / one thin blit / one full-panel blit,
each from the screen DC and the window DC, plus `PrintWindow`. The reading is
pre-committed so the result cannot be rationalised afterwards:

- **A ≈ 2×B and C ≈ B** → per call. A3d lives.
- **A ≈ B and C much worse** → per pixel. The thin strips were already the cheap
  version; A3d is dead as designed.
- **D or E far below A** → the window DC skips the desktop readback, the cheapest
  fix available.

Caveat built into the tool: a blit returning in well under a millisecond may be
reading a **stale or blank** surface rather than being fast, so any strategy that
looks free must be re-checked with `os_A3d.exe` for accept rate before it is
believed.

#### A3d2 result (2026-09-09) — both hypotheses confirmed, 176× on the table

60 iterations each, 1592×734 panel, AE idle.

| strategy | mean | p50 | worst |
|---|---|---|---|
| A two thin blits, screen DC | 33.443 | 33.360 | 36.938 |
| B one thin blit, screen DC | 16.575 | 16.677 | 26.012 |
| C one **full-panel** blit, screen DC | 16.744 | 16.651 | 24.130 |
| **D two thin blits, window DC** | **0.189** | **0.128** | 1.675 |
| E one full-panel blit, window DC | 1.145 | 1.028 | 2.896 |
| F PrintWindow whole window | 50.008 | 49.998 | 52.314 |

**A ≈ 2×B and C ≈ B**, exactly as the pre-committed reading required: the
screen-DC cost is **one composition sync per call** — 16.7 ms is one frame at
60 Hz — and the pixels are free. A full-panel blit costs the same as a 1-pixel
strip.

**And `GetDC(hwnd)` skips the sync entirely: 0.189 ms, 176× faster than A.**
Comfortably inside any tick budget. Also note `PrintWindow` at 50 ms — the method
A1c1 used for the calibration captures, never timed until now; fine for one-shot
calibration, hopeless for live tracking.

**Not yet believed.** The pre-registered caveat applies precisely here: 0.189 ms
is fast enough to be suspicious, and a window DC may be handing back a stale or
blank redirection surface rather than the live window. `os_A3d.exe` now runs
**differentially** — every iteration detects the comp origin through *both* paths
and logs both, with the window-DC path timed first so the screen blit's sync
cannot mask its cost.

The test needs no external ground truth:

- **at rest** the two must agree *exactly*; a standing difference means the
  window DC is not showing the live window;
- **in motion** any difference is **lag**, and its size in pixels is the number
  that decides whether it can drive an overlay;
- a blank or garbage window-DC path shows up as its own detection failures while
  the screen path succeeds.

`dx`/`dy` are logged as `-9999` when the two are not comparable, so a missing
comparison can never be misread as agreement.

#### Differential result (2026-09-09) — **the window DC is a mirage**

534 samples over 30 s of real interaction.

| path | cost mean | detections |
|---|---|---|
| window DC | 0.876 ms | **0 of 534** |
| screen DC | 32.455 ms | 384 ok, 9 axis-disagree, 141 no-edge |

**`GetDC(hwnd)` on AE's viewer returns a surface with no content.** The 176×
speedup was the cost of reading nothing. Zero comparable samples, so the lag
question never even arose. This is exactly the trap the pre-registered caveat
described, and the only reason it was caught is that the "fix" was required to
prove itself before being believed.

Removed from the probe rather than kept as a failing control: its ~0.9 ms would
land inside the cost measurement, and cost is the gate.

**What remains is the shipping configuration: one full-panel screen blit.**
A3d2 measured that at 16.744 ms against 16.575 ms for a single strip — one
composition sync, pixels free — so taking the whole panel in one call and reading
both axes out of it halves the 32.5 ms above. Now wired and awaiting a run.

Implication either way: **~16.7 ms is the floor for GDI screen capture**, because
it is one display frame. That is affordable on a dedicated thread out of process
(it is a sync wait, not CPU burn — this is how screen recorders work), and it is
*not* affordable on AE's UI thread, which is consistent with the two freezes.
If one frame of latency proves too much, the upgrade path is
**Windows.Graphics.Capture** targeting the viewer HWND — which would also solve
self-capture, since it would never see our own overlay.

#### Gate status

| Spike | Verdict |
|---|---|
| A1 transform | **PASS** |
| A2 identity | not started; `app.activeViewer` / `Viewer.type` is a strong lead |
| A3a cadence | **MARGINAL** — timer fine, idle unusable, 33 Hz artefact to confirm |
| A3b readability | **PASS** — `s` readable in motion at 0.19 ms |
| A3c glued overlay | **FAIL as built** — model right, pan inference wrong architecture |
| A3d strip-measured `t` | **built, unmeasured** |
| A4 cost | not started |


### A3e — is one frame of latency good enough? (built 2026-09-09)

A3d2 left a decision open rather than a result: GDI screen capture costs one
composition sync, 16.7 ms, one display frame — and the note said "if one frame of
latency is too much, the upgrade path is Windows.Graphics.Capture." Nothing said
how much is too much, or how it would be known.

**The question is not "is 16.7 ms acceptable".** It is *how many pixels does the
overlay slip, and under which gesture* — and that is arithmetic, not opinion:

    slip = pan_velocity × latency

#### Scoping it first, because most of the fear evaporates

`t` only needs updating **while `t` is changing**: pan, zoom, panel resize, Fit.
It does not change while scrubbing the CTI — which is the dominant onion-skin
gesture. So capture latency never touches the main use case at all.

And A3c already measured a real pan velocity, without meaning to. The wheel
scroll at 36.4 s moved Δy per 200 ms of 32, 24, 18, 14, 10 px: **~160 px/s,
decelerating**. Against that:

| latency | slip |
|---|---|
| 16.7 ms (one full frame) | **2.7 px** |
| ~8 ms (mean age with a continuous 60 Hz capture thread) | **1.3 px** |

That is one to three pixels of rubber-band *during* the gesture, converging to
zero within two frames of release. Worse than A1's 1 px static budget; almost
certainly invisible as smear. A fast Hand-tool drag at ~2000 px/s would be ~30 px
— noticeable, but that is the case where the user is watching where they are
dragging *to*, not registration.

**So the prior is that GDI is good enough, and A3e exists to try to falsify it.**

#### The "one frame" claim is conditional, and the condition is the architecture

16.7 ms is a **sync wait, not CPU burn**. It is one frame only if capture runs on
its **own thread**, publishing to a slot the paint samples; the paint then reads
the newest completed sample and returns immediately. Capture *inside* the paint
puts the whole 16.7 ms in the path and the answer changes. A3e is built the first
way deliberately — it is the shipping configuration, not a stand-in for it.

#### Two arguments for WGC that are not about latency, and they are the real ones

1. **Occlusion.** `BitBlt` from the screen DC reads the *desktop*. Any window
   overlapping the viewer, AE not frontmost, a tooltip, a floating panel — and
   the detector reads someone else's pixels. The axis-agreement control makes
   that fail safe rather than lie, but "the overlay freezes whenever anything
   overlaps AE" is a product defect. WGC captures the window's own surface.
2. **Self-capture.** Handled today by never painting on the two scan lines. It
   works, but it constrains the overlay's design permanently and breaks the
   moment a skin must be drawn across that row. WGC on the AE window never sees a
   topmost overlay at all.

Those are correctness arguments, and they beat a 1.3 px latency argument.

**One thing WGC would NOT fix, and it is easy to get wrong:** WGC is paced by the
same display frames. It removes the sync wait *in the caller* and it can be
double-buffered, but it does not sample the screen more often than the screen
changes. Half a frame of quantisation slip is a property of 60 Hz, not of GDI.
See the floor the analyser now prints.

**Untested assumption, recorded before it is relied on.** `GetDC(hwnd)` returned
a blank surface, which says the viewer panel is **not separately redirected** — a
child drawn into AE's main-window surface. So WGC would have to target the
**top-level AE window** and crop to the panel's client rect. `PrintWindow(
PW_RENDERFULLCONTENT)` succeeding in A1c1 is evidence the content is renderable
from that window, but it is evidence, not a measurement.

#### The harness

`A3e_SlipProbe.cpp` → `os_A3e.exe`, and `A3e_analyse.py`.

Entirely **out of process, and needing no AE at all** — A3d proved the comp
rectangle can be found from a captured panel and that its span on the two axes
recovers the zoom, so both halves of the transform come out of the capture
itself. No AEGP, no ExtendScript, no re-entrancy, and no way to take AE down,
which two in-process A3d runs already did.

On screen: **green** = the comp rect from the newest capture (the shipping
config); **magenta** = the same from a capture deliberately held 100 ms stale.

**Magenta is a control of the *instrument*, not of the detector.** If the
analysis cannot separate a 100 ms-stale overlay from a live one, then it is not
resolving lag at all, and it could not have detected 16.7 ms being unacceptable
either — so it may not pronounce 16.7 ms acceptable. That is reported as
**INVALID**, not as a pass. A control that guards only the subject and not the
instrument is how a null result gets mistaken for a result.

#### Two measurements, and why neither alone is enough

- **`A3e_paints.txt`** — every paint, and which capture it drew from. Offline the
  capture stream is interpolated to the paint's own timestamp and the difference
  is the slip: 60 Hz, sub-pixel. But the capture stream is *itself* one sync
  behind the screen and the paint drew from that same stream, so the constant
  part cancels and this measures **the pipeline only**. It is a **lower bound**,
  and it is printed as one. A measurement that quietly omits a term it cannot see
  is exactly how 16.7 ms would get declared fine on a number that never contained
  it.
- **A screen recording** — the overlay against the actual comp, same instant,
  whole chain. **The verdict is read from this.** Standing rule, no exceptions:
  when the claim is about what the user sees, the check has to be what the user
  sees.

Samples are timestamped at the **midpoint** of the blit, not its end — the pixels
are a snapshot somewhere inside that 16.7 ms window, and stamping at the end
would build a systematic half-frame error into the very number being measured.

#### Pre-committed reading

Fixed before the first run, judged on the **video**, binned by gesture:

| result | conclusion |
|---|---|
| at rest ≤ 1 px **and** wheel ≤ 5 px **and** drag ≤ 20 px | **GDI ships.** WGC becomes a later polish item for occlusion and self-capture, not a gate |
| anything worse | **WGC required** on Windows, built before Phase 1 |
| magenta not separable from green in the moving bins | **INVALID** — fix the instrument, re-run, report nothing |

#### Offline self-test (2026-09-09) — the instrument is verified

Per [[verify-offline-before-rebuild]], run against synthetic logs with a *known*
lag before it is pointed at AE. Profile: 10 s still, 15 s at 157 px/s, 15 s at
1566 px/s, 10 s still.

| case | expectation | result |
|---|---|---|
| 16.7 ms pipeline lag | slip = velocity × measured age | wheel median **3.93 px** vs 156.6 × 0.0251 s = **3.93** |
| | | drag median **39.31 px** vs 1566 × 0.0251 s = **39.3** |
| | at rest exactly zero | **0.00 px** |
| 0 ms pipeline lag | residual quantisation only | wheel **1.32**, drag **13.16**, rest **0.00** |
| control defeated (stale channel fed the live values) | must refuse to report | **INVALID**, both moving bins named |

The recovery is exact to two decimals in both moving bins, and the zero-lag case
proves the analyser is not measuring its own interpolation error.

**And the self-test changed the analyser before any real data existed.** The
zero-lag case still showed 13.16 px in the drag bin — because the newest
published sample is on average half a capture interval old *whatever* the capture
method, and the picture kept moving. At 60 Hz that floor is
`velocity × 8.35 ms`: 1.31 px at wheel speed, **13.08 px at 1566 px/s**. So a
20 px drag budget is unreachable above roughly 2400 px/s by arithmetic alone, and
a drag-bin failure near that floor would be a fact about 60 Hz, not an argument
for WGC — which is paced by the same frames.

The analyser now prints that floor per bin and refuses to let a busted budget be
read as a verdict on the capture method without checking it. This is a change to
the instrument made *before* the first run, from arithmetic rather than from a
result — stated explicitly, because moving a budget after seeing data is the
thing this project does not do.

#### Status

Built, self-tested offline, **awaiting a real run in AE with a screen recording**.

    os_A3e.exe [comp_w] [comp_h] [seconds]        default 1920 1080 60
    py A3e_analyse.py

Hover the comp viewer for the countdown, start recording, then: hold still ~10 s,
wheel-scroll ~15 s, hand-drag hard ~15 s, hold still ~10 s.

#### A3e run 1 (2026-09-09) — the latency question is answered; a bigger one opens

60 s in AE, 1592x729 panel, comp 1920x1080, recorded at 60 fps.

##### The capture is healthy, and that was the thing in doubt

| | |
|---|---|
| captures | 3600 in 60 s |
| accepted | 3124 (86.8%) |
| cost mean | 19.2 ms, **worst 25.3 ms** |
| axes disagree | 62 (1.7%) — caught, not believed |

No stall, no drift, no thermal tail. **The dedicated capture thread does what
A3d2 predicted**: one composition sync per frame and the pixels free.

##### Pipeline slip, from the paint log — a lower bound, as designed

| bin | n | median | p95 | worst | 100 ms control p95 | quantisation floor |
|---|---|---|---|---|---|---|
| at rest | 1002 | 0.00 | 0.00 | 0.82 | 39.6 | 0.00 |
| wheel | 371 | 2.84 | 7.48 | 29.2 | 112.0 | 2.14 |
| drag | 711 | 13.73 | 42.83 | 85.0 | 216.8 | 8.74 |

The control separates cleanly in every moving bin, so **the instrument is
valid** — it had the power to detect a lag problem.

And the medians sit **on the quantisation floor**: 2.84 against 2.14, 13.73
against 8.74. That is the half-capture-interval term that no capture method
avoids, WGC included. What exceeds budget is the **p95 tail**, not the typical
case — and the tail is where the next finding lives.

##### The tail is not lag. It is blindness.

**12.6% of the run — 7.55 s across 23 runs, the longest 2.70 s — every capture
was rejected.** The overlay held its last good `t` and sat still while the
picture moved. That is not latency and must never be averaged into it.

It is also, exactly, the failure the user reported: *"what still doesn't work is
when the comp bounds are out of the view."*

**Two distinct causes, and the geometry log alone could not tell them apart.**
Both were read off the screen recording:

1. **The comp is larger than the panel.** At 43.3 s the viewer is at **69%**, so
   the comp is drawn 1325x745 in a 729-tall panel. Top and bottom edges are off
   screen, the vertical scan finds background at neither end, and the line is
   correctly rejected. The threshold is exact and known: the comp outgrows this
   panel in height at **s > 0.675**.
2. **The sample line misses the comp.** At 29.4 s the viewer is at **21.9%** — the
   comp is a 420x236 rectangle — and the single fixed sample column at panel
   x=849 falls just outside its right edge at x≈838. Nothing crosses the line, so
   there is nothing to find. Cause 2 needs no zoom limit at all; it needs only a
   small comp that is not under the line.

##### A methodological trap this ran into, worth naming

The first attempt to attribute the blind runs used the **last accepted sample**
before each one — and concluded "none of the known modes", because at 21.9% the
comp is comfortably inside the panel and at 61% it still fits.

That test cannot work. The last accepted sample is *by definition* the state in
which detection still succeeded; the state that broke it is the one the log does
not contain. **Diagnosing a blind spot from the last thing seen before going
blind will always exonerate the cause.** The video had the answer because it kept
recording through the gap.

##### What this costs, and what fixes it

Neither cause argues for Windows.Graphics.Capture. Both are properties of the
**detector**, not of how the pixels were obtained.

- **Cause 2 is cheap to kill.** Sample a *set* of rows and columns rather than
  one of each, and take the first consistent answer. The pixels are already
  captured and the scan is O(w+h) per line, so five of each costs nothing
  measurable.
- **Cause 1 needs one edge, not two.** The current scan demands both ends of a
  line be background, so it needs *both* comp edges on that axis. With `s` known
  independently — `views[i].options.zoom`, A3b-measured at 0.19 ms and readable
  in motion — a single visible edge gives `t`: `tx = x_left`, or
  `tx = x_right - s*comp_w`. That turns "either edge off ⇒ blind" into "both
  edges off ⇒ blind".
- **The residual is real.** When the comp covers the panel entirely, no edge
  exists anywhere and nothing in the picture can anchor `t`. Two options, and
  they are not equivalent: frame-to-frame correlation of the captured panel
  (anchor + delta — integrating *pixels*, so unlike A3c it cannot miss a
  gesture), or degrade honestly.

##### And a product defect the run exposed regardless

**While blind, the probe kept drawing a confident box.** The roadmap's founding
premise is that a misaligned onion skin is *worse* than none, because it lies
about where the previous drawing was. Holding the last good `t` was the right
call for a measurement spike — a jumping box would have been harder to read —
but it is the wrong behaviour for the product. **Blind must be visible as
blind.** That is the minimum correct behaviour whatever else is built.

##### Why this matters more than the latency answer

High zoom is precisely when animators want onion skinning. So cause 1 is not a
corner case; it is the main case, arriving from the direction nobody was
watching. If the fixes above do not hold at high zoom, the honest reading is
that **Option A is weaker than Option B on the merits** — B gets the transform
from AE and is never blind, at any zoom, for free.

That is now the live question, and it outranks WGC.

##### The video half: at rest confirmed, the moving bins still owed

`A3e_video.py` reads the overlay and the comp out of the *same* frame, through
the alpha gap the probe already leaves for its own detector. Time sync is derived
rather than asserted — `green_x = panel_origin_x + tx` — and it lands cleanly:
**frame 840 = probe start, panel_origin_x = 327, MAD 1.0 px.**

**At rest the overlay is exact.** 1208 frames, and the slip distribution is a
spike: 1047 frames at exactly 2 px, 188 at 0. A quantised constant like that is
an edge convention — the box is drawn 3 px thick and the detector takes the first
non-background pixel — not a tracking error. The paint log agrees independently
(at-rest median 0.00 px, worst 0.82).

**The moving bins are not measured yet,** and the reason is a control doing its
job on a bad reference. The width control compares the detected comp width
against `s·comp_w` with `s` from the log; while the user is zooming, the drawn
width changes continuously and the log's `s` is quantised, so the check misses
and the frame is refused — 1856 of them, and they are exactly the moving frames.
Left strict rather than loosened, because a wider tolerance would begin accepting
wrong edges. The fix is to stop consulting the log and adopt the probe's own
control: scan a column as well as a row and require both axes to imply the same
zoom.

So for now: **at rest, measured on the video and passing. Wheel and drag, covered
only by the paint log's lower bound.**

##### Four faults in the video tracker, all of them mine, and what each taught

Recorded because three of them produced *plausible* numbers rather than errors.

1. **No panel bound.** The background test ran off into AE's project and timeline
   panels and reported 72.3% blind against the probe's own 11.5%. Fixed with a
   24 px inset.
2. **No control.** It reported a 562 px "slip" at rest — a detector locked onto a
   panel divider, wearing a result's clothing. The probe had a width control from
   the start; the tracker had none, which is why the tracker was the thing that
   was wrong. **A measuring instrument needs the same controls as the thing it
   measures.**
3. **Colour matching cannot separate these two greens.** The authored overlay
   green (40,255,40) reaches the recording as **(94,255,65)** after OBS's colour
   conversion, and the calibration comp contains a `cal_green` solid at
   **(19,255,8)**. No tolerance admits the first and rejects the second. Replaced
   with structure: the box's edges are lines hundreds of pixels long, the marker
   is a 40 px blob. Detection went from 805 frames to 3595.
4. **A sync criterion that scored the thing it was correcting for.** Sync was
   graded on the fraction of samples agreeing within 2 px — but green and `tx`
   disagree during motion *by exactly the slip being measured*, so the score was
   really "what fraction of the run was at rest", and it refused the correct
   offset for having too few. Regraded on MAD, which is robust to that tail and
   still collapses when the alignment is genuinely wrong. The correct offset was
   being found all along and thrown away.

Fault 4 is the one worth carrying: **do not grade an instrument on a statistic
that the measurand degrades.**

### A3f — blindness, stages 1 and 2 (built 2026-09-09)

A3e run 1 left the latency question answered and a worse one open: the detector
went blind for **12.6%** of a normal session and the overlay froze while the
picture moved. A3f is the fix, and it is a **gate** — high zoom is when onion
skinning is wanted most and high zoom is where the blindness is.

#### Stage 1a — per-axis acceptance, and the correction that prompted it

Run 1 rejected the **whole frame** if either axis failed. But looking at the
recording at 43.3 s, at 69% zoom, the comp's left and right edges sat at x=493
and x=1818 — **both comfortably inside the panel**. The horizontal axis was
perfectly measurable and was discarded because the vertical one was not. That
single policy accounts for the longest blind run in the session.

So `tx` and `ty` are now accepted, held and **aged independently**. This is a
correction to the earlier A3f framing, which said the first fix was "one edge
instead of two". It is not: the first fix is **one axis instead of both**, and it
is smaller and lands harder.

#### Stage 1b — several sample lines per axis, not one

Cause 2 needs nothing cleverer than more lines: at 21.9% zoom the comp was
420x236 and the single sample column fell just outside it. Five lines per axis at
fractions 0.17 / 0.33 / 0.55 / 0.71 / 0.89. The pixels are already captured and
each scan is O(w+h), so — as A3d2 established that a full-panel blit costs the
same as a 1 px strip — **ten lines cost the same as two**.

#### The control that had to be replaced, not dropped

Accepting the axes independently **destroys** the control A3d and A3e leaned on:
that both axes must imply the same zoom. It is unavailable exactly when one axis
is missing, which is the case A3f exists to serve. Dropping a control to make a
fix fit is how a detector starts lying, so:

- **Within an axis**, the lines check each other. The comp is a rectangle, so
  every line crossing it must return the *same* pair of edges. At least
  `OS_MIN_AGREE` lines must agree within `OS_AGREE_TOL`, and the answer is the
  median of the winning cluster. A line that locked onto a layer outline or a
  stray solid is outvoted.
- **Across axes**, the old zoom check is **kept**, and still applied whenever
  both axes are present — which is most of the time.

The detector is therefore *better* guarded than in run 1, not worse: it has a
control when both axes are present **and** a control when only one is.

#### Stage 2 — blind is drawn as blind

Run 1 held the last good `t` and kept drawing a confident box. That is the
roadmap's founding failure — an onion skin that lies about where the previous
drawing was is worse than none. Now:

| state | drawn as |
|---|---|
| live fix on both axes | green, solid |
| an axis stale > 250 ms | **amber, dashed** — visibly untrusted |
| nothing measured for > 500 ms | **nothing** — the overlay hides itself |

Being wrong is no longer allowed to look like being right.

#### Offline self-test — `A3f_selftest.cpp` → `os_A3f_test.exe`, **PASSES**

It `#include`s the probe's own source, so there is one `DetectAxis` rather than a
copy free to drift from the shipping one. Synthetic panels use the **real**
colours measured off the recording (panel 13,13,13; comp 64,64,64), so `BG_TOL`
is exercised as it will be in AE rather than against an easy contrast.

| case | expected | result |
|---|---|---|
| clean comp inside the panel | the 4 / 3 crossing lines agree exactly | 300..1259 and 100..639 |
| comp taller than the panel (69%) | **x still accepted**, y refused | x n=5 at 133; y refused |
| small comp missing some lines (21.9%) | found by the lines that do cross | n=2, exact edges |
| comp covering both axes | fully blind, and says why | refused on both |
| **one contaminated line** | **outvoted** | n=0 |
| two agreeing lines | accepted | n=2, exact |
| two **disagreeing** lines | refused, not averaged | n=0 |

The last three are the broken controls, and the middle one matters most: a vote
that refused everything would "pass" by being useless, so it must also **accept**
two genuinely agreeing lines. Both directions are asserted.

#### Two things the self-test corrected, one of them in my own write-up

- **The overflow failure mode is `no_transition`, not `ends_differ`.** `Scan`
  takes its background reference *from the line's own end pixel*, so when the
  comp covers the whole line both ends are comp, they match, and nothing differs
  from them. `ends_differ` is the *other* overflow shape — the comp covering one
  end but not the other. The A3e write-up said "found background at neither end",
  which describes a case that reports differently. Both modes are now covered by
  their own assertion.
- **Not all five lines cross a comp that is fully inside the panel.** With the
  comp at 300..1259, `cols[0]`=270 and `cols[4]`=1416 are outside it. The test
  now predicts *which* lines miss rather than accepting any count — a test happy
  with any number would not notice a detector that had begun missing lines for
  the wrong reason.

#### Status

Built, self-tested offline, **awaiting a run in AE with a screen recording**. The
run matrix now has to include the two cases that broke run 1: **zoom past 100%
and pan**, and **zoom far out on a small comp and pan**.

**A3f passes** if fully-blind frames are under 1% across that matrix *and* every
remaining blind frame was shown as blind. Stage 3 — correlation for the case
where the comp covers the panel with no edge anywhere — is deliberately **not
built**: ship degrading honestly there and let usage say whether it is worth it.

#### A3f run 1 (2026-09-09) — stage 2 verified, stage 1 half right, and the ladder was in the wrong order

60 s in AE, panel **1280x567**, comp 1920x1080, recorded at 15 fps (proxy).

##### Stage 2 works, and the video says so independently

| state | video | paint log |
|---|---|---|
| green (trusted) | 56.4% | 57.1% |
| **amber (shown stale)** | **20.4%** | 20.7% |
| **hidden (shown as nothing)** | **23.2%** | 22.2% |

Three categories agreeing to within a point, measured two entirely different
ways. **Blindness is now visible as blindness** — the run-1 failure where a
confident box was drawn over a stale `t` is gone.

##### Stage 1a works and is worth what it cost

**657 captures — 18.3% of the run — were accepted on x alone.** Every one of
those was *fully blind* under run 1's reject-the-whole-frame policy. Per-axis
acceptance was the cheapest change in the ladder and it recovered the largest
single block.

(`y_only` is zero, and that is correct rather than suspicious: this panel is
2.26:1 against a 16:9 comp, so height always overflows before width. There is no
reachable state where y survives and x does not.)

##### But A3f FAILS its gate: 27.8% fully blind

| | |
|---|---|
| both axes | 1940 (53.9%) |
| x only | 657 (18.3%) |
| **fully blind** | **999 (27.8%)** — criterion was <1% |

That is higher than run 1's 12.6%, and the comparison is **not** like for like —
this matrix deliberately exercised the cases that broke run 1. The number that
matters is not the change; it is that **none of it was the unrecoverable
regime**. Zoom never exceeded 0.633 and this panel's width limit is 0.667, so the
comp never covered both axes. *Every blind frame was in principle recoverable.*

##### Why, in two parts — and the sampling half is the smaller one

A pure-geometry sweep over every reachable pan position, asking when an axis is
recoverable (worst axis shown):

| zoom | comp | 5 lines, 2 edges | grid 16, 2 edges | **grid 16, 1 edge** |
|---|---|---|---|---|
| 0.05 | 96x54 | 0.0% | 82.1% | **91.4%** |
| 0.10 | 192x108 | 0.0% | 67.5% | **92.1%** |
| 0.25 | 480x270 | 35.0% | 35.0% | **93.7%** |
| 0.50 | 960x540 | 2.0% | 2.0% | **95.2%** |
| 0.63 | 1209x680 | 0.0% | 0.0% | **86.3%** |
| 1.00 | 1920x1080 | 0.0% | 0.0% | **65.3%** |

- **Below ~20% zoom the five lines are simply too sparse.** Confirmed on the
  video: at 39 s the viewer is at **7.3%**, the comp is 140x79, no sample column
  crosses it at all and only one row does — one line cannot vote, so the axis is
  refused. A 16 px grid fixes this outright and costs nothing: the pixels are
  already captured and A3d2 established the blit, not the scan, is the price.
- **From 25% zoom upward the binding constraint is not sampling at all.** It is
  the requirement that *both* ends of a line be background. Grid 16 changes
  nothing there (35.0% → 35.0%, 2.0% → 2.0%); the one-edge solve changes
  everything (35% → 94%, 2% → 95%).

**So the ladder was in the wrong order.** "One edge instead of two" was written
as stage 3, the expensive optional rung. It is the main event, and more sample
lines — written as the cheap primary fix — only matters at extreme zoom-out.

##### And the one-edge solve cannot be done without AE

A single edge gives `t` only if `s` is known independently. The tempting no-AE
source is the last capture where an axis had both edges. Measured against this
run, it does not survive contact:

| | |
|---|---|
| median age of the last two-edge `s` at a blind frame | **1463 ms** |
| within 250 ms | 10.4% |
| **blind runs across which the zoom actually changed** | **6 of 7 (86%)** |

**Blindness is *caused* by zooming, so the zoom is precisely what changes while
you are blind.** A held `s` is wrong exactly when it is needed. This is the same
shape as the A3c lesson — an inference that works when it is not needed and fails
when it is.

So `s` must come from `views[i].options.zoom`, which A3b already measured at
**0.19 ms, readable live during a modal drag**. The PAR ambiguity from A1 is
survivable here: `sy = zoom` always, so the vertical axis is unambiguous, and the
PAR factor on x can be calibrated in any frame where x has both edges.

##### The architectural consequence, stated plainly

The probe was built to need **no AE at all**, and that was load-bearing — two
in-process A3d runs took After Effects down. The one-edge solve reverses part of
it: a component inside AE must publish the zoom to the overlay process.

It is a smaller reversal than it sounds. The **overlay stays out of process**;
what goes back in is a read A3b already proved safe and cheap, publishing one
double through shared memory or a pipe. What must not come back is the *drawing*
and the *capture* on AE's UI thread, which is what actually caused the freezes.

But it is a real decision and it belongs to the roadmap, not to a commit message.

##### Status

- Stage 2: **PASS**, verified two ways.
- Stage 1a: **PASS**, 18.3% recovered.
- Stage 1b: **INSUFFICIENT** as built — needs a dense grid, not five lines.
- **A3f gate: FAIL at 27.8% blind**, with the cause measured and the fix costed.
- Next: grid sampling (free, no AE) + the one-edge solve (needs a zoom feed).
  Projected together: **86–95% recoverable across the working range**, and still
  65% at 100% zoom.

##### A fourth instance of the same tracker fault, for the record

The first video classification reported **99.1% green, 0% amber, 0.9% hidden** —
flatly contradicting the paint log. Cause: it searched the **whole screen**, and
AE's timeline draws a long green render bar. It was measuring After Effects' UI,
not the overlay. Restricting the search to the viewer panel rect produced the
agreement tabled above.

That is the fourth time in this spike that a measuring instrument, not the thing
measured, was the wrong party — and the third time specifically from **not
bounding the search to the panel**. The rule has earned its place:
**bound the instrument to the region you are making a claim about, and check it
against an independent count before believing either.**

### A5 — the macOS capture-permission gate (not started, and it outranks A4)

**Why it jumped the queue.** The product is meant to work on macOS. Everything
A1–A3 established rests on one thing: `t` is **measured from a capture of the
viewer**, because A3c proved it cannot be inferred from input. On Windows that
capture is free of ceremony. On macOS it is not.

`CGWindowListCreateImage` and ScreenCaptureKit **both require the Screen
Recording TCC permission** — granted per-application in System Settings, and the
application being prompted is **After Effects**, not our plug-in. So the mac
product is: install the plug-in, AE raises a system dialog asking to record the
screen, and the user restarts AE. That is a far larger cost than 16.7 ms, and it
applies to *every* capture-derived route to `t`, not to one API choice.

**So the mac question is not "WGC or GDI".** It is **"is a capture-derived `t`
viable on macOS at all?"** — and it can invalidate Windows work rather than merely
delay it, which is why it goes before A4 and before any WGC code.

**What A5 must establish, on a real mac:**

- Does AE prompt for Screen Recording when a bundled AEGP calls ScreenCaptureKit
  / `CGWindowListCreateImage`, and does the grant persist across AE restarts and
  plug-in updates?
- What is the capture cost, and is it frame-paced like the Windows path?
- Is the viewer window individually capturable, or only the whole screen?
- **The product question, which is not a technical one:** is "After Effects wants
  to record your screen" an acceptable install experience for this plug-in?

**Gate:**

| result | consequence |
|---|---|
| permission acceptable and capture works | mac Option A is alive; continue |
| permission works but is unacceptable as an experience | decide now: Windows-only, or **Option B** |
| capture unavailable or unreliable | Windows-only, or **Option B** |

Note that a fail here is not automatically fatal to Option A — it is fatal to
*cross-platform* Option A, and the honest response is to decide deliberately
between shipping Windows-only and dropping to B, rather than discovering the
choice halfway through Phase 2. [[radial-menu-plugin]]'s precedent — overlay
window and local event monitor being the same shape on both platforms — does
**not** extend here: that work needed no capture and no TCC grant.

### Gate status (2026-09-09, current)

| Spike | Verdict |
|---|---|
| A1 transform | **PASS** — 14/14, worst residual 0.559 px against a 1 px criterion |
| A2 identity | not started — `app.activeViewer` / `Viewer.type` is a strong lead |
| A3a cadence | timer fine; **idle hook unusable** (9.2 s silent during a drag) |
| A3b readability | **PASS** — `s` readable in motion at 0.19 ms |
| A3c pan inference | **FAIL — architecture abandoned.** `t` is measured, not inferred |
| A3d strip-measured `t` | method works; 0 false positives in 710 samples |
| A3d2 capture cost | **16.7 ms = one composition sync.** `GetDC(hwnd)` is blank |
| **A3e slip** | **run 1 done. Latency is NOT the problem; the detector goes BLIND 12.6% of the time** |
| **A3f blindness** | **run 1: stage 2 PASSES (verified 2 ways), stage 1a PASSES (+18.3%), GATE FAILS at 27.8% blind. Fix measured: grid sampling + a ONE-EDGE solve needing AE's zoom** |
| **A5 macOS capture permission** | **not started — and it now outranks A4** |
| A4 render cost | not started |

**Order from here: A3f stage 3 (grid sampling, then the one-edge solve and the
zoom feed it needs) -> A3e run 2 -> A5 -> A2 -> A4.**

**The open decision** is whether to put a zoom-publishing component back inside
AE. The overlay stays out of process either way; what returns is a read A3b
already measured at 0.19 ms live. Without it the one-edge solve is impossible,
and without the one-edge solve A3f cannot pass above 25% zoom. WGC has dropped further down: run 1
showed the typical slip sitting on the quantisation floor that WGC shares, so its
case rests entirely on occlusion and self-capture.

**And A3f is a gate, not a chore.** High zoom is when onion skinning is wanted
most, and it is exactly where the detector is blind. If it cannot be fixed there,
Option B wins on the merits - it takes the transform from AE and is never blind,
at any zoom, for free.

### A1c — Pan by correlation (historical section below)

Now the only remaining unknown in A1. Open questions, in order:

1. **Is `zoom` live and cheap?** Does it update as the user zooms, and what does
   an `AEGP_ExecuteScript` round trip cost? If it is stale or slow, the analytic
   half is worth less than it looks. Measure before building on it.
2. **What exactly does `saveBlittedImageToPng` write** — the panel-sized blit
   including pan cropping, or the whole comp at view resolution? The answer
   decides whether pan falls straight out of it or needs a screen capture after
   all.
3. **Recover the 2-DOF translation** and check it against the A1 pass criteria
   (1 px, 12 states, one deliberately-wrong control).

#### A1c-0 — the signature is undocumented (2026-09-08)

`saveBlittedImageToPng` does **not** take a lone `File`. AE reports it requires
**four** parameters: `(boolean, File, integer, boolean)`. Nothing in the SDK or
the scripting guide documents it, and the integer is presumably an enum.

So the first version of the pan test was invalid before it ran — it called the
method with one argument. Superseded and deleted (`A1c_Blit.jsx`,
`A1c_analyse.py`).

**Map the signature, do not guess it.** `A1c0_SweepBlit.jsx` sweeps
`b1 ∈ {false,true} × n ∈ 0..7 × b2 ∈ {false,true}` — 32 calls, each writing
`A1c0_<b1>_<n>_<b2>.png`. The throws are as much the measurement as the
successes: they draw the boundary of the integer's domain, and the script records
the distinct error messages. `A1c0_analyse.py` then holds two parameters fixed and
varies the third, so each parameter's effect is isolated:

- dimensions change ⇒ a resolution/scale control
- content changes but not dimensions ⇒ a channel/overlay/alpha toggle
- nothing changes ⇒ reported as **"not proven inert"**, because the sweep held
  pan, overlays, resolution and preview mode constant

If `n = 7` is accepted, the analyser says so and warns that the domain may extend
past where the sweep stopped — an accepted upper bound is not a discovered one.

**Sweep result (2026-09-08): the method silently does nothing.** 32 combinations,
**zero throws, zero files**. And the arity claim was wrong: calling with **no**
arguments does not throw either — the "requires 4 parameters" error came from the
earlier one-argument call, not from a real contract.

The integer domain was almost certainly wrong: AE's scripting enums live around
7000–8100 (`viewer.type` 7612, `channels` 7812, `fastPreview` 8012), so sweeping
0..7 was outside it entirely.

**Parked, not chased.** It writes a PNG to disk, so it could only ever be a
calibration instrument, never a live per-frame source — and for calibration a
screen capture does the same job with no undocumented behaviour underneath.
Chasing it is how an undocumented API becomes the sunk cost that eats A1's time
box.

#### A1c-1 — marker calibration by screen capture — **TOOLING VERIFIED, AWAITING AE**

Replaces the blit approach, and is a better fit anyway: it *is* the pass criteria
the roadmap specified, measured directly, rather than a route to them.

| Tool | Role |
|---|---|
| `A1c1_MakeCalib.jsx` | builds `A1_CALIB` — 1920×1080, mid-grey ground, 5 saturated markers at exact known coords; writes `A1c1_calib.json` |
| `A1c1_Zoom.jsx` | logs the live viewer zoom, one line per capture, numbered |
| `A1c1_Capture.cpp` → `os_A1c1.exe` | captures the viewer panel's client area to a top-down 24-bit BMP + a metadata `.txt` |
| `A1c1_solve.py` | detects markers, solves the transform, judges it |

Design decisions that carry the result:

- **The ground is mid-grey, not black**, so a failed capture (which comes back
  flat black) cannot be mistaken for a successful capture of the background. The
  capture tool independently flags a uniform-colour frame and exits 3.
- **Five markers, four used.** The fit uses the corners; the centre is held out,
  so its error is an honest residual. A four-point fit to four points is exact by
  construction and proves nothing.
- **`sx` and `sy` are solved independently** though they should be equal — the
  anisotropy is then a free diagnostic for non-square pixels or bad detection.
- **Three independent checks**: held-out residual, cross-check of solved scale
  against ExtendScript's `zoom`, and a broken control (scale forced to 1.0). If
  the control is not detected, the harness reports INVALID rather than PASS.
- `PrintWindow(PW_RENDERFULLCONTENT)` first, `BitBlt` from the screen as
  fallback; which one produced the pixels is recorded, because a later analysis
  that does not know cannot interpret a blank frame.

**Solver self-tested offline on synthetic data** (per
[[verify-offline-before-rebuild]]): recovers `sx` to 1e-6 and `tx` exactly,
held-out centre 0.500 px, broken control fails by 757 px. The 0.5 px residual is
the synthetic generator rounding marker centres to integers, not solver error —
but it does mean **the 1 px criterion is tight**, and a real result near 1 px must
be attributed to the transform or the detector before it is called either way.

**First real capture (2026-09-08): the mechanism works.** Panel 1288×449,
`PrintWindow(PW_RENDERFULLCONTENT)` succeeded — the comp viewer **is** capturable,
which was the main risk in this route. All 5 markers detected, all three checks
green: held-out centre 0.426 px, zoom cross-check 0.139% apart, broken control
failing by 653 px.

**And it was rejected anyway, correctly.** `cal_red` returned **69 detected
pixels against 361** for the others — about 18% of a marker that should cover
~384 px at that zoom. A partial marker still produces a centroid; it is just a
*biased* one. Red's centre sat 1.14 px off in x and 1.26 px off in y relative to
the markers sharing its comp coordinates — most of A1's entire 1 px budget,
inside a corner the fit depends on, hidden in a result that otherwise passed.

Cause: `addSolid` leaves the last-created layer selected, and AE draws selection
handles and a bounding box **over** it in the viewer. `cal_red` was created last.

Two fixes, and only the second one matters:

1. `MakeCalib` now deselects every layer and hides rulers/guides after
   `openInViewer`.
2. **The solver now checks marker integrity itself** — each marker's area against
   the median, rejecting anything under half or over double — and reports
   `NOT ANALYSED` rather than solving. Re-run against the existing capture 1: it
   is now correctly refused. The first fix stops this instance; the second stops
   the class, and the class is the dangerous part, because the failure mode is a
   *plausible wrong answer* rather than an error.

Also made PAR-aware in the same pass, before it could produce a confusing false
failure: AE applies pixel aspect on X only (drawn width is `w*PAR*zoom`, height
`h*zoom`), so the expected `sx/sy` is **PAR, not 1**, and the zoom cross-check
compares against **`sy`**, which PAR does not touch. `MakeCalib` now prompts for
the PAR so the non-square state is reachable without editing the script.

#### Run 1 of the 15-state matrix (2026-09-09) — model confirmed, coverage failed

**The transform model is right.** Six captures solved, across two zooms, two
panel sizes, centred and panned:

| capture | panel | sx | sy | residual |
|---|---|---|---|---|
| 1 | 1288×449 | 0.408125 | 0.407895 | **0.000 px** |
| 2 | 1288×449 (panned) | 0.408750 | 0.407895 | **0.000 px** |
| 3 | 1288×449 | 0.500000 | 0.500000 | **0.373 px** |
| 7 | 924×449 | 0.408125 | 0.407895 | **0.000 px** |
| 9 | 924×449 | 0.500000 | 0.500000 | **0.373 px** |
| 13 | 1288×449 | 0.408125 | 0.407895 | **0.000 px** |

Broken control detected on every one (551–652 px). `screen = s·comp + t` is the
correct model, and the marker-integrity fix held — no partial markers anywhere.

**Nine captures were unusable, and the design was at fault, not the operator.**
The markers span 760 comp-pixels vertically. At 100% zoom that needs a panel over
760 px tall; the viewer was 449. Captures 4,5,6,8,10,11,12 were **impossible as
specified**. Fixed: `MakeCalib` now asks for the comp size and lays the markers
out proportionally, so the 100% states use a 640×360 comp — still scale 1.0,
which is the thing being measured.

**The PAR states did not test PAR.** Capture 13 measured `sx/sy = 1.0006` on a
comp that really was PAR 2. **AE's viewer has a Pixel Aspect Ratio Correction
toggle that defaults to OFF, and `ViewOptions` does not expose it.** So this is a
*second* piece of transform-affecting viewer state that cannot be read, alongside
pan. Not fatal — it is a binary, and with correction off `sx = sy = zoom` — but
it is a real gap and it belongs in A1's result, not in a footnote.

**The zoom "disagreement" was bookkeeping, not physics.** Two zoom readings were
logged against fifteen captures, so capture 2 was judged against a reading taken
eight minutes later. Where the pairing was valid (capture 1) the two sources
agreed to 0.107%.

Fixed by making the slip impossible rather than asking for care: **`os_A1c1.exe`
now refuses to capture unless the zoom-log line count equals the next capture
index**, and writes nothing when it refuses. Verified — it correctly rejected a
16th capture against 2 readings, leaving the file count unchanged.

#### Run 2 (2026-09-09) — **A1 PASSES**

14 captures, 14 usable, 14 zoom readings in enforced lockstep.

```
usable captures : 14
worst residual  : 0.559 px   (criterion 1.0 px)
broken control  : detected everywhere
zoom cross-check: all agree   (on 14 of 14 captures)
measured PAR    : 1 on captures 1-12, 2 on captures 13-14
A1 PASSES.
```

Coverage: fit / 50% / 100% zoom, centred and panned hard, two panel sizes
(1288×697 and 924×697), square and PAR 2. Residuals were 0.000 px on ten of the
fourteen; the worst was 0.559 px.

**PAR correction is real and measurable.** With the viewer toggle enabled,
captures 13–14 measured `sx/sy` of 2.003 and 2.000 against a PAR 2 comp — exactly
as predicted. So `sx = zoom·PAR` when correction is on and `sx = zoom` when it is
off, and since `ViewOptions` does not expose the toggle, **the plug-in must infer
it rather than read it.** A known, bounded unknown.

**So the transform is: `screen = s·comp + t`, with `s` from
`views[i].options.zoom` (times PAR when correction is on) and `t` the only thing
that must be recovered by other means.**

##### Two harness bugs this run exposed, both mine

- **`calib.json` is global, comp geometry is per-capture.** Set B used a 640×360
  comp but the file had been overwritten by the final 1920×1080 build, so those
  four captures were solved against markers 3× too large and reported a 66% zoom
  disagreement. The residuals were unaffected — a uniform scale error cancels in
  the held-out prediction — which is exactly why it survived to be caught by the
  cross-check rather than the primary measurement. Fixed: `A1c1_Zoom.jsx` now
  logs comp width/height/PAR with every reading, and the solver prefers that over
  the global file. Run 2's data was recovered with an explicitly-labelled
  `A1c1_geometry.json`, whose inference was **falsifiable and verified**: applying
  it left every residual unchanged and made all 14 cross-checks agree.
- **The broken control was degenerate at 100% zoom.** Forcing the scale to 1.0 is
  the *correct* transform when zoom is 1.0, so the control stopped being a control
  on precisely the four captures the new coverage added — and the harness
  correctly reported INVALID rather than passing. Replaced with two controls that
  are wrong everywhere: a fixed +3 px translation and a 5% scale error.
  **Generalisable: a control must be wrong at every point in the matrix, or it
  stops guarding exactly where coverage grows.**

##### Gate status

| Spike | Verdict |
|---|---|
| A1 transform | **PASS** |
| A2 identity | not started — but A1b's `app.activeViewer` / `Viewer.type` is a strong lead |
| A3 sync | not started — **the gate to bet against** |
| A4 cost | not started |

A1 is a **static** result: the transform is correct when nothing is moving.

#### A1c-1 (superseded design) — the blit pan test

The three-run design below is still the right measurement; it just has to wait
until a working argument tuple is known, since a pan test run through a
misunderstood argument proves nothing.

The design, so it cannot be read wrong:

- **Runs 1 vs 2 differ only in pan.** Byte-identical blits ⇒ the blit carries no
  pan information (whole-comp hypothesis) and A1c needs a screen capture after
  all. Different blits ⇒ the blit encodes the pan crop (panel hypothesis) and pan
  falls out of a correlation with no screen capture anywhere in the loop.
- **Run 3 changes the zoom**, which tests Q1 *and* gives an independent second
  read on Q2: under whole-comp the blit dimensions must equal
  `round(comp_wh * zoom)`; under panel they stay pinned to the panel size. The
  two reads must agree — if they disagree, neither hypothesis holds.
- The analyser **refuses to answer Q2** if runs 1 and 2 turn out to have been at
  different zooms, rather than reporting a difference that pan did not cause.
