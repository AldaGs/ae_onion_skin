# Onion Skin — how to use it, and the one constraint

## The constraint

**The effect's input must carry alpha.**

Onion Skin builds its ghosts from the frames either side of the current one. If
those frames are fully opaque, each ghost covers the last and the current frame
covers them all — the result is your input, unchanged. Not faint. Identical.

That is not a bug and it is not fixable from inside the effect: an opaque pixel
carries no information about what is behind it, and there is nothing to recover.

## Two placements

### On an adjustment layer — for character animation

Put an adjustment layer above the drawing layers and apply Onion Skin to it.

This is the placement that ghosts **every kind of animation**, including layer
Position, Rotation and Scale, because an adjustment layer sees comp space *after*
transforms are applied.

**Keep the background above the onion-skin layer, or out of the comp.** An
adjustment layer receives the composite of everything below it, so a background
solid below it makes the ghosts vanish.

Working arrangements:

```
  Drawing layers          Drawing layers            Drawing layers
  ONION SKIN (adj)        ONION SKIN (adj)          ONION SKIN (adj)
  (nothing below)         BG as a guide layer       BG in a parent comp
```

Broken arrangement:

```
  ONION SKIN (adj)
  Drawing layers
  BG solid  <-- opaque, and now the ghosts are gone
```

### On the drawing layer — for art that animates in its own space

Apply Onion Skin directly to a layer whose **content** changes frame to frame:
shape paths, puppet pins, source text, a precomped animation.

A layer's own alpha is never contaminated by what is below it, so the background
can be anywhere.

**Caveat:** AE applies layer transforms *after* effects, so a layer animated only
by Position keyframes shows its content unchanging to the effect, and the ghosts
land on top of each other invisibly. For transform animation, use the adjustment
layer.

## Controls

| Control | What it does |
|---|---|
| **Enable** | Off is an exact pass-through — the image is bit-identical |
| **Previous / Next Frames** | How many ghosts each way, 0–12 |
| **Frame Step** | Spacing. 2 ghosts every other frame — the usual want on 2s |
| **Strength** | Overall ghost opacity. 0 is an exact pass-through |
| **Falloff** | How fast ghosts fade with distance. 100 = all equal |
| **Tint Amount** | 100 = flat coloured ghost, 0 = the drawing's own colours faded |
| **Past / Future Colour** | The two directions of time, independently |
| **Open Onion Skin Panel** | Opens the control panel |
| **Debug Log** | Writes `%TEMP%\onionskin_fx.txt`. Off unless asked for |

Ghosts thin out naturally at the ends of the timeline — there are no frames past
the end, so those skins simply do not appear.

## Retired

**Source Layer 1/2/3** existed in v1.0–v1.3 to lift the alpha constraint by
naming the drawing layer explicitly. They were retired in v1.4 because a
checked-out layer param arrives at the layer's own dimensions and carries none of
its comp transform — so it ghosts a layer's *artwork* but not its *animated
position*, which for character animation is exactly the thing you need to see.

Their disk IDs remain reserved and the params are still created, invisible.
Removing them outright would renumber every parameter after them and silently
mis-map saved projects.
