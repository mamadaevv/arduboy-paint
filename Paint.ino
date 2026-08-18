#include <Arduboy2.h>

Arduboy2 arduboy;

// ===== состояние экрана =====
enum Screen { SPLASH, MENU, SOON, SIZE_SELECT, HELP, PAINT };
Screen screen = SPLASH;

// ===== настройки сетки (меняются в рантайме) =====
uint8_t cell = 4;        // текущий размер пикселя/кисти
uint8_t gw, gh;          // число клеток (пересчитывается при входе в Paint)
uint8_t grid[256];       // битовая сетка (максимум при cell=2: 64*32=2048 бит)

// меню / выбор
uint8_t menuSel = 0;                 // 0 Play, 1 Gallery, 2 Paint
uint8_t sizeOpts[4] = {2, 4, 8, 16};
uint8_t sizeSel = 1;                 // индекс (default 4)
const uint8_t HELP_LINES = 7;
const char* helpLines[HELP_LINES] = {
  "Arrows: move",
  "A: draw/erase",
  "A hold: drag",
  "B tap: undo",
  "B hold: clear",
  "",
  "A+B hold: exit",
};
int helpOff = 0;
unsigned long splashStart = 0;
unsigned long soonStart = 0;

// ===== Paint: состояние =====
const uint16_t HIST_MAX = 32;
uint16_t history[HIST_MAX];
uint16_t histHead = 0;

int16_t cx, cy, prevCx, prevCy;
unsigned long lastMove = 0;
const unsigned long REPEAT = 180;
unsigned long bPressStart = 0;
bool bHandled = false;
unsigned long abStart = 0;
bool awaitRelease = false;   // после смены экрана ждём отпускания всех кнопок

// смена экрана с защитой от залипа
void goScreen(Screen s) {
  screen = s;
  awaitRelease = true;
}

// ===== сетка =====
bool getCell(uint8_t gx, uint8_t gy) {
  uint16_t i = gx + (uint16_t)gy * gw;
  return (grid[i >> 3] >> (i & 7)) & 1;
}
void setCell(uint8_t gx, uint8_t gy, bool v) {
  uint16_t i = gx + (uint16_t)gy * gw;
  if (v) grid[i >> 3] |= (1 << (i & 7));
  else   grid[i >> 3] &= ~(1 << (i & 7));
}
void pushChange(uint8_t gx, uint8_t gy, bool oldVal) {
  if (histHead >= HIST_MAX) return;
  uint16_t idx = gx + (uint16_t)gy * gw;
  history[histHead++] = (idx << 1) | (oldVal ? 1 : 0);
}
void drawRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t py = y; py < y + h; py++) {
    if (py < 0 || py > 63) continue;
    for (int16_t px = x; px < x + w; px++) {
      if (px < 0 || px > 127) continue;
      uint8_t gx = px / cell, gy = py / cell;
      arduboy.drawPixel(px, py, getCell(gx, gy) ? WHITE : BLACK);
    }
  }
}

// ===== вход в режимы =====
void enterPaint() {
  cell = sizeOpts[sizeSel];
  gw = 128 / cell;
  gh = 64 / cell;
  for (uint16_t i = 0; i < sizeof(grid); i++) grid[i] = 0;
  histHead = 0;
  cx = (gw / 2) * cell;
  cy = (gh / 2) * cell;
  prevCx = cx; prevCy = cy;
  lastMove = millis();
  abStart = 0;
  arduboy.clear();
  goScreen(PAINT);
}

void setup() {
  arduboy.boot();
  arduboy.display();
  arduboy.bootLogo();
  arduboy.setFrameRate(60);
  splashStart = millis();
}

void loop() {
  if (!arduboy.nextFrame()) return;
  arduboy.pollButtons();
  if (arduboy.buttonsState() == 0) awaitRelease = false;  // все кнопки отпущены -> снимаем гвард

  switch (screen) {

    // ---------- СПЛЭШ ----------
    case SPLASH: {
      bool anyReleased =
          arduboy.justReleased(UP_BUTTON) || arduboy.justReleased(DOWN_BUTTON) ||
          arduboy.justReleased(LEFT_BUTTON) || arduboy.justReleased(RIGHT_BUTTON) ||
          arduboy.justReleased(A_BUTTON) || arduboy.justReleased(B_BUTTON);
      // уходим ТОЛЬКО по отпусканию кнопки (без таймаута — не уходит сам)
      if (anyReleased && !awaitRelease) {
        goScreen(MENU);
        menuSel = 0;
      }
      break;
    }

    // ---------- МЕНЮ ----------
    case MENU: {
      if (arduboy.justPressed(UP_BUTTON))   menuSel = (menuSel + 2) % 3;
      if (arduboy.justPressed(DOWN_BUTTON)) menuSel = (menuSel + 1) % 3;
      if (arduboy.justReleased(A_BUTTON) && !awaitRelease) {
        if (menuSel == 2) { goScreen(SIZE_SELECT); sizeSel = 1; }
        else { goScreen(SOON); soonStart = millis(); }
      }
      break;
    }

    // ---------- ЗАГЛУШКА ----------
    case SOON: {
      if (millis() - soonStart > 1500 ||
          arduboy.justPressed(UP_BUTTON) || arduboy.justPressed(DOWN_BUTTON) ||
          arduboy.justPressed(LEFT_BUTTON) || arduboy.justPressed(RIGHT_BUTTON) ||
          arduboy.justPressed(A_BUTTON) || arduboy.justPressed(B_BUTTON)) {
        screen = MENU;
      }
      break;
    }

    // ---------- ВЫБОР РАЗМЕРА ----------
    case SIZE_SELECT: {
      if (arduboy.justPressed(UP_BUTTON))   sizeSel = (sizeSel + 3) % 4;
      if (arduboy.justPressed(DOWN_BUTTON)) sizeSel = (sizeSel + 1) % 4;
      if (arduboy.justPressed(LEFT_BUTTON)) sizeSel = (sizeSel + 2) % 4;
      if (arduboy.justPressed(RIGHT_BUTTON)) sizeSel = (sizeSel + 2) % 4;
      if (arduboy.justPressed(B_BUTTON)) { goScreen(MENU); }
      else if (arduboy.justReleased(A_BUTTON) && !awaitRelease) { goScreen(HELP); helpOff = 0; }
      break;
    }

    // ---------- СПРАВКА ----------
    case HELP: {
      const int ROW_H = 11;               // строка 8px + 3px между -> 11/22/33/44
      const int HELP_MIN = 0;
      const int HELP_MAX = (HELP_LINES - 1) * ROW_H - 3 * ROW_H;  // ровно 4 видимые строки
      if (arduboy.justPressed(UP_BUTTON))   helpOff -= ROW_H;
      if (arduboy.justPressed(DOWN_BUTTON)) helpOff += ROW_H;
      if (helpOff < HELP_MIN) helpOff = HELP_MIN;
      if (helpOff > HELP_MAX) helpOff = HELP_MAX;
      if (arduboy.justPressed(B_BUTTON)) { goScreen(SIZE_SELECT); }
      else if (arduboy.justReleased(A_BUTTON) && !awaitRelease) { enterPaint(); }
      break;
    }

    // ---------- РИСОВАНИЕ ----------
    case PAINT: {
      // выход: A+B удержание >= 500 мс
      bool ab = arduboy.pressed(A_BUTTON) && arduboy.pressed(B_BUTTON);
      if (ab) {
        if (abStart == 0) abStart = millis();
        if (millis() - abStart >= 500) { goScreen(MENU); menuSel = 0; break; }
      } else {
        abStart = 0;
      }

      // движение (пропускаем, если обе кнопки зажаты = выход)
      if (!ab) {
        bool up = arduboy.pressed(UP_BUTTON);
        bool down = arduboy.pressed(DOWN_BUTTON);
        bool left = arduboy.pressed(LEFT_BUTTON);
        bool right = arduboy.pressed(RIGHT_BUTTON);
        bool dirJust = arduboy.justPressed(UP_BUTTON) || arduboy.justPressed(DOWN_BUTTON) ||
                       arduboy.justPressed(LEFT_BUTTON) || arduboy.justPressed(RIGHT_BUTTON);
        if ((up || down || left || right) && (dirJust || millis() - lastMove >= REPEAT)) {
          if (left)  cx -= cell;
          if (right) cx += cell;
          if (up)    cy -= cell;
          if (down)  cy += cell;
          if (cx < 0) cx = 0;
          if (cx > 128 - cell) cx = 128 - cell;
          if (cy < 0) cy = 0;
          if (cy > 64 - cell) cy = 64 - cell;
          lastMove = millis();
        }

        // A — рисование (пропускаем при выходе)
        static bool aTarget = true;
        if (arduboy.justPressed(A_BUTTON)) {
          uint8_t gx = cx / cell, gy = cy / cell;
          bool old = getCell(gx, gy);
          aTarget = !old;
          if (aTarget != old) { setCell(gx, gy, aTarget); pushChange(gx, gy, old); }
        }
        if (arduboy.pressed(A_BUTTON)) {
          uint8_t gx = cx / cell, gy = cy / cell;
          bool old = getCell(gx, gy);
          if (aTarget != old) { setCell(gx, gy, aTarget); pushChange(gx, gy, old); }
        }

        // B — tap undo / hold clear (пропускаем при выходе)
        if (arduboy.justPressed(B_BUTTON)) { bPressStart = millis(); bHandled = false; }
        if (arduboy.pressed(B_BUTTON) && !bHandled) {
          if (millis() - bPressStart >= 500) {
            for (uint16_t i = 0; i < sizeof(grid); i++) grid[i] = 0;
            histHead = 0;
            arduboy.clear();
            bHandled = true;
          }
        }
        if (arduboy.justReleased(B_BUTTON) && !bHandled) {
          if (histHead > 0) {
            uint16_t rec = history[--histHead];
            bool oldVal = rec & 1;
            uint16_t idx = rec >> 1;
            uint8_t gx = idx % gw;
            uint8_t gy = idx / gw;
            setCell(gx, gy, oldVal);
            drawRegion(gx * cell, gy * cell, cell, cell);
          }
          bHandled = true;
        }
      }

      // отрисовка (инкрементально)
      drawRegion(prevCx - cell, prevCy - cell, cell * 3, cell * 3);
      uint8_t phase = 0;
      if (millis() - lastMove >= 1000) phase = (millis() / 250) & 1;
      for (uint8_t i = 0; i < cell; i++)
        for (uint8_t j = 0; j < cell; j++) {
          bool on = (((i + j + phase) & 1) == 1);
          arduboy.drawPixel(cx + i, cy + j, on ? BLACK : WHITE);
        }
      prevCx = cx; prevCy = cy;
      break;
    }
  }

  // ----- отрисовка экранов (кроме PAINT — там инкрементально) -----
  if (screen == SPLASH) {
    arduboy.clear();
    arduboy.setTextSize(2);
    arduboy.setCursor(18, 18);
    arduboy.print(F("Pixel Pic"));
    arduboy.setTextSize(1);
    // фраза мигает каждые ~400 мс
    if ((millis() / 400) & 1) {
      arduboy.setCursor(30, 44);
      arduboy.print(F("press any key"));
    }
  } else if (screen == MENU) {
    arduboy.clear();
    arduboy.setTextSize(2);
    arduboy.setCursor(18, 4);
    arduboy.print(F("Pixel Pic"));
    arduboy.setTextSize(1);
    const char* items[3] = {"Play", "Gallery", "Paint"};
    for (uint8_t i = 0; i < 3; i++) {
      arduboy.setCursor(24, 26 + i * 11);
      if (i == menuSel) arduboy.print(F("> ")); else arduboy.print(F("  "));
      arduboy.print(items[i]);
    }
  } else if (screen == SOON) {
    arduboy.clear();
    arduboy.setTextSize(1);
    arduboy.setCursor(32, 26);
    arduboy.print(F("Coming soon"));
  } else if (screen == SIZE_SELECT) {
    arduboy.clear();
    arduboy.setTextSize(1);
    arduboy.setCursor(8, 4);
    arduboy.print(F("Pixel size:"));
    // две колонки: [2px, 4px] | [8px, 16px]
    const uint8_t colX[2] = {20, 72};
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t col = i / 2;
      uint8_t row = i % 2;
      int16_t x = colX[col];
      int16_t y = 22 + row * 14;
      arduboy.setCursor(x, y);
      if (i == sizeSel) arduboy.print(F("> ")); else arduboy.print(F("  "));
      arduboy.print(sizeOpts[i]);
      arduboy.print(F("px"));
    }
    arduboy.setCursor(8, 56);
    arduboy.print(F("A:OK  B:Back"));
  } else if (screen == HELP) {
    arduboy.clear();
    arduboy.setTextSize(1);
    arduboy.setCursor(8, 2);
    arduboy.print(F("Help: Paint Mode"));
    const int ROW_H = 11;          // строка 8px + 3px между -> 11/22/33/44
    const int TOP = 11;            // верхний край первой строки
    const int BOT = 52;            // нижний край области (для стрелки)
    // аффорданс скролла: треугольники остриём к краям области (x=120)
    if (helpOff > 0) {             // стрелка вверх, остриё в y=11
      for (uint8_t k = 0; k < 4; k++) {
        arduboy.drawPixel(120 - k, TOP + k);
        arduboy.drawPixel(120 + k, TOP + k);
      }
    }
    if (helpOff < (HELP_LINES - 1) * ROW_H - 3 * ROW_H) { // стрелка вниз, остриё в y=52
      for (uint8_t k = 0; k < 4; k++) {
        arduboy.drawPixel(120 - k, BOT - k);
        arduboy.drawPixel(120 + k, BOT - k);
      }
    }
    // ровно 4 видимые строки на фиксированных y; меняется только содержимое
    uint8_t startIdx = helpOff / ROW_H;
    for (uint8_t r = 0; r < 4; r++) {
      uint8_t i = startIdx + r;
      if (i >= HELP_LINES) break;
      arduboy.setCursor(8, TOP + r * ROW_H);
      arduboy.print(helpLines[i]);
    }
    arduboy.setCursor(8, 56);
    arduboy.print(F("A:OK  B:Back"));
  }

  arduboy.display();
}
