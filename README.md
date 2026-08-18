# Arduboy Paint (Arduino Nano / ATmega328p)

Pixel-paint sketch for the DIY Arduboy built on an **Arduino Nano (ATmega328p)**
with an I²C SSD1306 128×64 display, running the `harbaum/Arduboy2` Nano port.

Self-contained: only depends on `Arduboy2` (the Nano port). No external sound/math
libraries, so it compiles cleanly for the 328p.

## Controls

| Input | Action |
|-------|--------|
| **← → ↑ ↓** | Move cursor (one grid cell per tap; hold = repeat every ~180 ms) |
| **A** (tap) | Toggle cell `CELL`×`CELL` (paint if empty, erase if filled) |
| **A** (hold) | Drag-paint/erase — repeats the first tap's action along the path |
| **B** (tap) | Undo last A action, one cell at a time (LIFO, 64 steps) |
| **B** (hold ≥500 ms) | Clear the entire canvas (resets undo history) |

The cursor is a `CELL`×`CELL` checkerboard fill (no border). When idle for ≥1 s it
animates (the checkerboard pattern inverts every 250 ms).

## Settings

Edit at the top of `Paint.ino`:

```cpp
const uint8_t CELL = 8;   // grid/brush size in pixels (2, 4, 8, 16 ...)
```

All logic (grid, cursor, bounds, drawing) is derived from `CELL`. Screen is 128×64,
so use a value that divides both. **Note:** cursor bounds use `128-CELL` / `64-CELL`
(not `127`/`63`) so the bottom-right cell stays reachable.

## Hardware

- Arduino Nano, ATmega328p (CH340)
- I²C SSD1306 128×64 OLED, address 0x3C (SCL→A5, SDA→A4)
- Buttons wired for the SLIMBOY layout (see the Obsidian note)

## Build & flash

```powershell
$bin = 'C:\Users\risow\bin\arduino-cli.exe'
$bp = 'D:\temp\paint_build'

# compile (LTO must be disabled for the 328p link)
& $bin compile --fqbn arduino:avr:nano:cpu=atmega328old `
    --build-property 'build.extra_flags=-fno-lto' `
    --build-path $bp .\Paint.ino

# flash (replace COM8 with the port shown by: arduino-cli board list)
& $bin upload -p COM8 --fqbn arduino:avr:nano:cpu=atmega328old --input-dir $bp
```

> **Gotcha:** `arduino:avr@1.8.8` LTO breaks the `Arduboy2::begin()` link under the
> 328p — always build with `-fno-lto`. The display must be SSD1306 @ 0x3C; the CH340
> port often changes after re-plugging, so always check `board list` first.

## Fast boot

`setup()` uses `arduboy.boot()` + `arduboy.bootLogo()` instead of `arduboy.begin()`,
so it skips `systemButtons()` (the B-at-boot sound menu), `audio.begin()`, and
`waitNoButtons()`. The logo animation is kept.

## Sizes (atmega328old, -fno-lto, CELL=8)

- Flash: ~6342 B (20% of 30720)
- RAM: ~1260 B (61% of 2048, ~788 B free)

## Source

`Paint.ino` — single self-contained sketch.
