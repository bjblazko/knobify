# 0007: Battery indicator

## Status

Accepted — 2026-09-13. Placement and coloring superseded by
[ADR 0008](0008-braun-design-system-and-screen-redesign.md) (hidden in
normal use, red at ≤20%, always shown on the lock screen). Calibration
and "no charging detection" amended 2026-09-13 — see *Amendment* below.

## Context

`device.md` documents an optional 3.7V LiPo battery via a PH1.25
connector with onboard charging circuitry, and GPIO1 as "Battery ADC" —
but flags the whole pinout as "not independently verified against the
physical board yet." No code anywhere read this pin; the README listed
a battery indicator as explicitly out of scope.

Before writing any UI, a throwaway diagnostic sketch was flashed to
read GPIO1 directly over serial (with the battery attached):

- GPIO1 returns a stable, plausible reading (~2400mV, ±40-50mV noise
  over 40s) — not floating-pin noise, not clipped at 0 or full-scale.
- No charging signal exists: the same ~2400mV reading held steady
  across a USB unplug/replug cycle, `device.md` documents no separate
  charge-status pin, and the board has no visible charging LED.
- The true battery-voltage-to-ADC-mV relationship (divider ratio, if
  any) is still unconfirmed — this single probe only established that
  the pin returns *something plausible*, not its exact calibration.

## Decision

### Ship without charging detection

The indicator shows only charge level (percent -> color), never a
charging state. Faking it with a voltage-trend heuristic was
considered and rejected — with no dedicated signal, it would be wrong
often enough to be misleading rather than merely absent. Revisit only
if a real signal turns up (an undocumented GPIO, or a longer-window
voltage-trend approach with actual validation).

### Hardware read / percent-mapping / rendering are three separate layers

Following this project's existing hardware/logic split
([ADR 0003](0003-testing-strategy.md)):

- `lib/drivers-battery/BatteryAdcDriver.h` — thin wrapper around
  `analogReadMilliVolts(GPIO1)`, no logic.
- `lib/power/BatteryMonitor.h` — pure, host-tested logic (like
  `IdleTimer`/`LockController`): millivolts in, 0-100% and a 5-bucket
  `Level` enum out. `kEmptyMilliVolts`/`kFullMilliVolts` are explicitly
  flagged as placeholders pending a multimeter cross-check of the
  divider ratio.
- `lib/ui/BatteryIndicator.h` — LVGL-facing, not host-tested, renders
  `BatteryMonitor`'s level as an icon/color.

### Rendered once on LVGL's top layer, not per-screen

`ScreenManager::render()` fully deletes and recreates its screen object
on every navigation change (see `ScreenManager.cpp`), so a per-screen
approach would mean touching every render branch. `LockOverlay` already
solved exactly this by living on `lv_layer_top()`, which LVGL always
draws above the active screen. `BatteryIndicator` uses the same trick:
one label, created once in `main.cpp::setup()`, visible on every screen
automatically — including the lock screen, since it's constructed
*after* `LockOverlay::begin()` and later-created top-layer children
z-order above earlier ones.

### Placement: matched to the back/scan button's row, not a corner

First attempt placed the icon at `LV_ALIGN_TOP_RIGHT` (a screen corner);
it looked correct in a serial-dumped screenshot but was invisible on the
real device, clipped by the round bezel. Corrected to sit on the same row
as the back/scan button (`LV_ALIGN_TOP_MID`, vertically centered on that
button's midline, offset sideways to avoid overlapping it) — confirmed
visible on the physical device afterward. See
[`ux-guidelines.md` §7](../design/ux-guidelines.md#7-layout--widget-principles)
for the general corner-avoidance rule this follows.

## Consequences

- Percent readout is only as accurate as the placeholder calibration
  constants in `BatteryMonitor.h` — recalibrate `kEmptyMilliVolts`/
  `kFullMilliVolts` once real empty/full battery voltages are
  cross-checked with a multimeter at the battery terminals.
- No charging state shown; if that's revisited later, `BatteryMonitor`
  has room for an `isCharging()`-style addition without touching
  `BatteryIndicator`'s rendering split.
- `BatteryAdcDriver`/`BatteryIndicator` are not host-tested (Arduino/
  LVGL-only, like the other driver/UI-layer classes) — only
  `BatteryMonitor`'s percent/level logic has unit test coverage
  (`test/test_power/test_power.cpp`).

## Amendment (2026-09-13): calibration and charging state

GPIO1 sits behind a 2:1 divider. `BatteryAdcDriver` now returns the
undivided rail voltage (the scale the factory firmware logs as
`Battery : %u mv`). From user-provided real-device readings, a full
cell is 4100mV (`kFullMilliVolts`) and anything above 4500mV means the
board is on USB and charging — confirmed live at ~4740mV on USB. The
original probe's "no jump on unplug" did not hold up. The lock screen
shows "Charging" instead of a percentage in that state, since the
reading then reflects the charger rail rather than the cell.
`kEmptyMilliVolts` (3400) is still an estimate.
