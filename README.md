# keybum
Keyboard emulator on STM32F411

## What it does
The board enumerates as a USB HID **boot keyboard**. When PA0 is pulled low
(button press, active-low with internal pull-up), the device types out the
string defined by `KEYBUM_TYPE_STRING` in `Core/Src/main.c` (default
`"Hello from STM32 keybum!\n"`) with randomized human-like timing — variable
per-key hold times, randomized inter-key gaps, and an occasional longer
pause after a space.

## Hardware
- STM32F411CEU6 on a generic "Black Pill" / WeAct-style board
- USB OTG FS on PA11 (D-) / PA12 (D+)
- 25 MHz HSE → 96 MHz SYSCLK → 48 MHz USB clock
- PA0: trigger button to GND (uses internal pull-up)

Identical board / pinout / clocks to the `mouseum` project — same `.ioc` apart
from project name. Only the firmware is different.

## How it differs from `mouseum`
- USB HID **report descriptor** is a 6-key boot keyboard (8-byte reports)
  instead of a 3-button mouse (4-byte reports). The descriptor lives in
  `Middlewares/USB_Device_Library/Class/HID/`. `mouseum` references the
  upstream `usbd_hid.c` from the STM32Cube repo (which ships with a mouse
  descriptor); `keybum` ships its own modified copy of the same file so the
  upstream stays untouched and the mouse project keeps working.
- USB Product string is `"STM32 Keyboard Emulator"` and the PID is bumped by
  one (`22316` vs mouseum's `22315`) so the host can tell the two devices
  apart in its driver cache.
- `main.c` replaces the Bezier-curve mouse mover with an ASCII→HID keycode
  mapper plus a typing routine.

## Customizing the typed text
Edit `KEYBUM_TYPE_STRING` near the top of `Core/Src/main.c` (or `-D` it from
the toolchain) and reflash. The mapper supports printable US ASCII plus
`\n` (Enter) and `\t` (Tab); other characters are skipped.

> **Layout note:** HID keyboards send key *positions*, not characters. The
> host's active keyboard layout decides what glyph appears. The mapper here
> assumes the host is set to US QWERTY; with another layout configured on
> the host, punctuation will land on different keys.

## Building
1. Open the project in STM32CubeIDE and import `keybum.ioc` if needed.
2. Build the `Debug` configuration. The linked references in `.project` /
   `.cproject` resolve via the `STM32CUBE_REPO` path variable, which points
   at `<USER_HOME>/STM32Cube/Repository/` on this machine — same as
   `mouseum`.
3. Flash with ST-Link via the included `keybum Debug.launch`.
