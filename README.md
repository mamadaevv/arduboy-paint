# Arduboy Paint (Arduino Nano / ATmega328p)

Pixel-paint sketch for the DIY Arduboy built on an **Arduino Nano (ATmega328p)**
with an I²C SSD1306 128×64 display, running the `harbaum/Arduboy2` Nano port.

> Self-contained — only depends on `Arduboy2`. No extra libraries.

## Screens (state machine)
```
SPLASH ──(release any key)──▶ MENU ──▶ SIZE_SELECT ──▶ HELP ──▶ PAINT
            │                      │                      │
            └──────────────────────┴──────────────────────┘  (B = back)
```
- **SPLASH** — "Pixel Pic", "press any key" blinks, waits for button release
- **MENU** — `> Play / Gallery / Paint` (Play & Gallery are "Coming soon" stubs)
- **SIZE_SELECT** — pixel size **2 / 4 / 8 / 16 px** in two columns; A=OK, B=Back
- **HELP** — scrollable text (4 visible rows, ←↑↓→ scroll by 1 row, A=OK, B=Back)
- **PAINT** — the drawing canvas

## Paint controls
| Input | Action |
|-------|--------|
| ← → ↑ ↓ | move brush (grid step, key-repeat on hold) |
| **A** (tap) | toggle draw/erase on cell |
| **A** (hold) | drag-paint (repeats the first tap's action) |
| **B** (tap) | **undo** last A-action (LIFO, 32 steps) |
| **B** (hold ≥500 ms) | **clear all** |
| **A + B** (hold ≥500 ms) | **exit to menu** |

## Build & flash
```powershell
# On Windows (Matebook), COM8 = Nano (CH340). FQBN must be atmega328old + -fno-lto
$bin = 'C:\Users\risow\bin\arduino-cli.exe'
$sketch = 'D:\Users\risow\Documents\Arduino\arduboy-games-nano\Paint'
$bp = 'D:\temp\paint_build'
& $bin compile --fqbn arduino:avr:nano:cpu=atmega328old --build-property 'build.extra_flags=-fno-lto' --build-path $bp $sketch
& $bin upload -p COM8 --fqbn arduino:avr:nano:cpu=atmega328old --input-dir $bp
```

## Hardware
See `README` in the parent vault / Obsidian note
`Устройства/Arduino Nano — Paint (Arduboy-порт).md`.

| Element | Nano pin |
|---------|----------|
| OLED I²C SSD1306 (0x3C) SCL/SDA | A5 / A4 |
| UP / DOWN / LEFT / RIGHT / A / B | A3 / D2 / A1 / D3 / D4 / A2 |
| Buzzer | D9 ↔ D11 (no GND) |

## Gotchas
- **`-fno-lto` is mandatory** — LTO breaks `Arduboy2::begin()` linking on 328p.
- Display must be **SSD1306 @ 0x3C**; SH1106 / 0x3D needs a `lcdBootProgram` edit.
- CH340 changes COM port after USB replug — always check `arduino-cli board list`.
- RAM is tight (~79%): `grid[256]` (bit-packed, max for 2px mode) + `history[32]`.
- Help scroll is hand-rolled: `helpOff` (px) shifts rows; out-of-area rows are simply not drawn.

## Size
Flash ~8982 B (29%), RAM ~1609 B (78%).
