# Changelog

## 0.3.0 — Wrongs (new name, new panel)
- Renamed from "Unofficial Rings" to **Wrongs**. Only the display and bundle name changed. Plug-in codes (`aumf` / `Ring` / `Ptdt`),
  parameter IDs and the saved-state format are unchanged. Delete the old `Unofficial Rings.component` / `.vst3` before installing.
- New panel in the Anamnesis "gold sections" look (cosmetic only; no control renamed, reordered, re-ranged or removed). Fixed 680 x 596.
- Fixed: Model / Synth FX now swap with Mode using the value the panel is told, not a stored value that may not be updated yet.
- Added RingsCheck (host-less check of controls, saved projects, panel fit, title pixels, drawing cost) and panel pictures to the build workflow;
  added an AU identity check to the macOS build.
- Status: CI green (macOS build + RingsCheck, 134 checks). Owner tested on the Intel Mac in Logic and reported "works".

## 0.2.0
- Pitch Snap (Off / Steps / Semitones) on Transpose + FM.
