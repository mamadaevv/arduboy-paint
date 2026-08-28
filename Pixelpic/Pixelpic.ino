#include <Arduboy2.h>

// ===== состояние экрана (сцены) — объявлено ДО всего, как в оригинале =====
enum Screen { SPLASH_INTRO, SPLASH, MENU, SOON, SIZE_SELECT, HELP, PAINT, PLAY, WIN, SPLASH_EXIT, MENU_EXIT };
Screen screen = SPLASH_INTRO;
bool navLock = false;
void goScreen(Screen s) { screen = s; navLock = true; }
#define NAV(b) (arduboy.justPressed(b) && !navLock)

Arduboy2 arduboy;
// ЗВУК: пассивный бузер D9<->D11 (без GND). tone() на D9, D11=LOW как return-путь.
#define BUZ1 9
#define BUZ2 11
void beep(uint16_t f, uint16_t ms) { tone(BUZ1, f); delay(ms); noTone(BUZ1); }
// Стартовая мелодия: бодрый мотив (ноты C5..C6, длительности мс) — в PROGMEM
void startupMelody() {
  static const uint16_t notes[] PROGMEM = {
    523, 659, 784, 1047, 784, 659, 880, 698, 523, 784, 1047, 880, 784, 659, 587, 523
  };
  static const uint16_t durs[] PROGMEM = {
    160, 160, 160, 320, 200, 200, 200, 200, 160, 160, 240, 160, 160, 160, 160, 480
  };
  for (uint8_t i = 0; i < 15; i++) { beep(pgm_read_word(&notes[i]), pgm_read_word(&durs[i])); delay(30); }
}
void winMelody() {
  static const uint16_t notes[] PROGMEM = {659, 784, 1047, 1319};
  static const uint16_t durs[]  PROGMEM = {160, 160, 160, 500};
  for (uint8_t i = 0; i < 4; i++) { beep(pgm_read_word(&notes[i]), pgm_read_word(&durs[i])); delay(30); }
}

// ===== геометрия интерфейса (константы вместо магии) =====
const int LOGO_X = 10;
const int LOGO_SPLASH_Y = 18;
const int LOGO_MENU_Y   = 4;
const int ROW_H = 11;
const int HELP_TOP = 11;
const int HELP_BOT = 52;
const int HELP_VISIBLE = 4;

uint8_t cell = 4;
uint8_t gw, gh;
uint8_t grid[256];
uint8_t target[256];      // целевая фигура (PLAY)
uint16_t dirtyCount = 0;

uint8_t menuSel = 0;
uint8_t sizeOpts[4] = {2, 4, 8, 16};
uint8_t sizeSel = 1;
const uint8_t HELP_LINES = 7;
const char* helpLines[HELP_LINES] = {
  "Arrows: move", "A: draw/erase", "A hold: drag", "B tap: undo",
  "B hold: clear", "", "A+B hold: exit",
};
int helpOff = 0;
const int HELP_MIN = 0;
const int HELP_MAX = (HELP_LINES - 1) * ROW_H - HELP_VISIBLE * ROW_H;

unsigned long transStart = 0;
const unsigned long TRANS_MS = 350;
int transShift = 0;
unsigned long introStart = 0;
const unsigned long INTRO_MS = 1500;

const uint16_t HIST_MAX = 32;
uint16_t history[HIST_MAX];
uint16_t histHead = 0;

int16_t cx, cy, prevCx, prevCy;
unsigned long lastMove = 0;
const unsigned long REPEAT = 180;
unsigned long bPressStart = 0;
bool bHandled = false;
unsigned long abStart = 0;
unsigned long winStart = 0;
bool isPlay = false;
bool pendingPlay = false;   // true, если из SIZE_SELECT надо войти в PLAY
bool bootMelodyPlayed = false;

bool getCellBuf(uint8_t* buf, uint8_t gx, uint8_t gy) {
  uint16_t i = gx + (uint16_t)gy * gw;
  return (buf[i >> 3] >> (i & 7)) & 1;
}
void setCellBuf(uint8_t* buf, uint8_t gx, uint8_t gy, bool v) {
  uint16_t i = gx + (uint16_t)gy * gw;
  if (v) buf[i >> 3] |= (1 << (i & 7));
  else   buf[i >> 3] &= ~(1 << (i & 7));
}
bool getCell(uint8_t gx, uint8_t gy) { return getCellBuf(grid, gx, gy); }
void setCell(uint8_t gx, uint8_t gy, bool v) {
  uint16_t i = gx + (uint16_t)gy * gw;
  bool old = (grid[i >> 3] >> (i & 7)) & 1;
  if (v == old) return;
  if (v) { grid[i >> 3] |= (1 << (i & 7)); dirtyCount++; }
  else   { grid[i >> 3] &= ~(1 << (i & 7)); dirtyCount--; }
}
void pushChange(uint8_t gx, uint8_t gy, bool oldVal) {
  if (histHead >= HIST_MAX) return;
  uint16_t idx = gx + (uint16_t)gy * gw;
  history[histHead++] = (idx << 1) | (oldVal ? 1 : 0);
}
bool isCanvasEmpty() { return dirtyCount == 0; }
void clearGrid() {
  for (uint16_t i = 0; i < sizeof(grid); i++) grid[i] = 0;
  dirtyCount = 0;
}
bool matchTarget() {
  for (uint16_t i = 0; i < sizeof(grid); i++) if (grid[i] != target[i]) return false;
  return true;
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
// Обводка цели: для каждой закрашенной клетки рисуем границы, где сосед — пустой
void drawTargetOutline() {
  for (uint8_t gy = 0; gy < gh; gy++) {
    for (uint8_t gx = 0; gx < gw; gx++) {
      if (!getCellBuf(target, gx, gy)) continue;
      int16_t x0 = gx * cell, y0 = gy * cell;
      bool up = (gy > 0) && getCellBuf(target, gx, gy - 1);
      bool dn = (gy < gh - 1) && getCellBuf(target, gx, gy + 1);
      bool lf = (gx > 0) && getCellBuf(target, gx - 1, gy);
      bool rt = (gx < gw - 1) && getCellBuf(target, gx + 1, gy);
      if (!up) for (uint8_t i = 0; i < cell; i++) arduboy.drawPixel(x0 + i, y0, WHITE);
      if (!dn) for (uint8_t i = 0; i < cell; i++) arduboy.drawPixel(x0 + i, y0 + cell - 1, WHITE);
      if (!lf) for (uint8_t i = 0; i < cell; i++) arduboy.drawPixel(x0, y0 + i, WHITE);
      if (!rt) for (uint8_t i = 0; i < cell; i++) arduboy.drawPixel(x0 + cell - 1, y0 + i, WHITE);
    }
  }
}
void drawLogo(int y) {
  arduboy.setTextSize(2);
  arduboy.setCursor(LOGO_X, y);
  arduboy.print(F("Pixel Pic"));
}
void drawMenuItems() {
  arduboy.setTextSize(1);
  const char* items[3] = {"Play", "Paint", "Help"};
  for (uint8_t i = 0; i < 3; i++) {
    arduboy.setCursor(24, 26 + i * ROW_H);
    if (i == menuSel) arduboy.print(F("> ")); else arduboy.print(F("  "));
    arduboy.print(items[i]);
  }
}
void drawPressAnyKey() {
  arduboy.setTextSize(1);
  if ((millis() / 400) & 1) {
    arduboy.setCursor(22, 44);
    arduboy.print(F("press any key"));
  }
}

void enterPaint() {
  cell = sizeOpts[sizeSel];
  gw = 128 / cell;
  gh = 64 / cell;
  clearGrid();
  histHead = 0;
  isPlay = false;
  cx = (gw / 2) * cell;
  cy = (gh / 2) * cell;
  prevCx = cx; prevCy = cy;
  lastMove = millis();
  abStart = 0;
  arduboy.clear();
  goScreen(PAINT);
}
void enterPlay(uint8_t c) {
  cell = c;                   // выбранный размер клетки
  gw = 128 / cell;
  gh = 64 / cell;
  clearGrid();
  for (uint16_t i = 0; i < sizeof(target); i++) target[i] = 0;
  histHead = 0;
  isPlay = true;
  // цель: одна клетка по центру
  uint8_t tx = gw / 2, ty = gh / 2;
  setCellBuf(target, tx, ty, true);
  cx = tx * cell;
  cy = ty * cell;
  prevCx = cx; prevCy = cy;
  lastMove = millis();
  abStart = 0;
  winStart = 0;
  arduboy.clear();
  goScreen(PLAY);
}

// Общая логика рисования для PAINT и PLAY
void paintLoop() {
  bool ab = arduboy.pressed(A_BUTTON) && arduboy.pressed(B_BUTTON);
  if (ab) {
    if (abStart == 0) abStart = millis();
    if (millis() - abStart >= 500) { goScreen(MENU); menuSel = 0; return; }
  } else { abStart = 0; }
  if (!ab) {
    bool up = arduboy.pressed(UP_BUTTON), down = arduboy.pressed(DOWN_BUTTON),
         left = arduboy.pressed(LEFT_BUTTON), right = arduboy.pressed(RIGHT_BUTTON);
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
    if (!navLock) {
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
    }
    if (arduboy.justPressed(B_BUTTON)) { bPressStart = millis(); bHandled = false; }
    if (arduboy.pressed(B_BUTTON) && !bHandled) {
      if (millis() - bPressStart >= 500) { clearGrid(); histHead = 0; arduboy.clear(); bHandled = true; }
    }
    if (arduboy.justReleased(B_BUTTON) && !bHandled) {
      if (isCanvasEmpty()) { goScreen(MENU); return; }
      if (histHead > 0) {
        uint16_t rec = history[--histHead];
        bool oldVal = rec & 1;
        uint16_t idx = rec >> 1;
        uint8_t gx = idx % gw, gy = idx / gw;
        setCell(gx, gy, oldVal);
        drawRegion(gx * cell, gy * cell, cell, cell);
      }
      bHandled = true;
    }
  }
  // проверка победы (только в PLAY): удержание совпадения 1с -> сцена WIN
  if (isPlay && matchTarget()) {
    if (winStart == 0) winStart = millis();
    else if (millis() - winStart >= 1000) { goScreen(WIN); return; }
  } else {
    winStart = 0;
  }

  drawRegion(prevCx - cell, prevCy - cell, cell * 3, cell * 3);
  uint8_t phase = 0;
  if (millis() - lastMove >= 1000) phase = (millis() / 250) & 1;
  for (uint8_t i = 0; i < cell; i++)
    for (uint8_t j = 0; j < cell; j++) {
      bool on = (((i + j + phase) & 1) == 1);
      arduboy.drawPixel(cx + i, cy + j, on ? BLACK : WHITE);
    }
  prevCx = cx; prevCy = cy;
}

void setup() {
  pinMode(BUZ1, OUTPUT);
  pinMode(BUZ2, OUTPUT);
  digitalWrite(BUZ1, LOW);
  digitalWrite(BUZ2, LOW);
  arduboy.boot();
  arduboy.display();
  arduboy.bootLogo();
  // мелодия перенесена на сплэш (играет ВМЕСТЕ с лого "Pixel Pic")
  arduboy.setFrameRate(60);
  introStart = millis();
}

void loop() {
  if (!arduboy.nextFrame()) { return; }
  arduboy.pollButtons();
  if (arduboy.buttonsState() == 0) navLock = false;

  switch (screen) {
    case SPLASH_INTRO: {
      unsigned long t = millis() - introStart;
      if (t >= INTRO_MS) { goScreen(SPLASH); break; }
      unsigned long period = 300 - (t * 180) / INTRO_MS;
      unsigned long onDur  = 40  + (t * 220) / INTRO_MS;
      bool show = (t % period) < onDur;
      arduboy.clear();
      if (show) {
        drawLogo(LOGO_SPLASH_Y);
        if (!bootMelodyPlayed) {        // мелодия вместе с лого
          bootMelodyPlayed = true;
          arduboy.display();            // показать лого до мелодии
          startupMelody();
        }
      }
      break;
    }
    case SPLASH: {
      if (NAV(UP_BUTTON) || NAV(DOWN_BUTTON) || NAV(LEFT_BUTTON) ||
          NAV(RIGHT_BUTTON) || NAV(A_BUTTON) || NAV(B_BUTTON)) {
        transStart = millis(); transShift = 0; menuSel = 0;
        screen = SPLASH_EXIT; navLock = true;
      }
      break;
    }
    case SPLASH_EXIT: {
      unsigned long t = millis() - transStart;
      if (t >= TRANS_MS) { goScreen(MENU); break; }
      transShift = (t * (LOGO_SPLASH_Y - LOGO_MENU_Y)) / TRANS_MS;
      if (transShift > (LOGO_SPLASH_Y - LOGO_MENU_Y)) transShift = (LOGO_SPLASH_Y - LOGO_MENU_Y);
      break;
    }
    case MENU: {
      if (arduboy.justPressed(UP_BUTTON))   menuSel = (menuSel + 2) % 3;
      if (arduboy.justPressed(DOWN_BUTTON)) menuSel = (menuSel + 1) % 3;
      if (NAV(A_BUTTON)) {
        if (menuSel == 1) { goScreen(SIZE_SELECT); sizeSel = 1; }
        else if (menuSel == 2) { goScreen(HELP); helpOff = 0; }
        else { pendingPlay = true; goScreen(SIZE_SELECT); sizeSel = 3; }  // Play -> выбор размера
      } else if (NAV(B_BUTTON)) {
        transStart = millis(); transShift = 0;
        screen = MENU_EXIT; navLock = true;
      }
      break;
    }
    case MENU_EXIT: {
      unsigned long t = millis() - transStart;
      if (t >= TRANS_MS) { goScreen(SPLASH); break; }
      transShift = (t * (LOGO_SPLASH_Y - LOGO_MENU_Y)) / TRANS_MS;
      if (transShift > (LOGO_SPLASH_Y - LOGO_MENU_Y)) transShift = (LOGO_SPLASH_Y - LOGO_MENU_Y);
      break;
    }
    case SOON: {
      if (NAV(UP_BUTTON) || NAV(DOWN_BUTTON) || NAV(LEFT_BUTTON) ||
          NAV(RIGHT_BUTTON) || NAV(A_BUTTON) || NAV(B_BUTTON)) { goScreen(MENU); }
      break;
    }
    case SIZE_SELECT: {
      if (arduboy.justPressed(UP_BUTTON))   sizeSel = (sizeSel + 3) % 4;
      if (arduboy.justPressed(DOWN_BUTTON)) sizeSel = (sizeSel + 1) % 4;
      if (arduboy.justPressed(LEFT_BUTTON)) sizeSel = (sizeSel + 2) % 4;
      if (arduboy.justPressed(RIGHT_BUTTON)) sizeSel = (sizeSel + 2) % 4;
      if (NAV(B_BUTTON)) { pendingPlay = false; goScreen(MENU); }
      else if (NAV(A_BUTTON)) {
        if (pendingPlay) { pendingPlay = false; enterPlay(sizeOpts[sizeSel]); }
        else { enterPaint(); }
      }
      break;
    }
    case HELP: {
      if (arduboy.justPressed(UP_BUTTON))   helpOff -= ROW_H;
      if (arduboy.justPressed(DOWN_BUTTON)) helpOff += ROW_H;
      if (helpOff < HELP_MIN) helpOff = HELP_MIN;
      if (helpOff > HELP_MAX) helpOff = HELP_MAX;
      if (NAV(B_BUTTON)) { goScreen(MENU); }
      else if (NAV(A_BUTTON)) { enterPaint(); }
      break;
    }
    case PAINT: {
      paintLoop();
      break;
    }
    case PLAY: {
      paintLoop();
      drawTargetOutline();
      break;
    }
    case WIN: {
      // кадр: надпись YOU / WIN в две строки по центру + лёгкая анимация
      arduboy.clear();
      uint8_t pulse = (millis() / 250) & 1;
      int y1 = 14 + (pulse ? 0 : 1);   // лёгкий пульс по Y
      int y2 = 38 + (pulse ? 1 : 0);
      arduboy.setTextSize(3);            // крупно
      arduboy.setCursor(40, y1);        // "YOU" по центру (128/2 - ~24)
      arduboy.print(F("YOU"));
      arduboy.setCursor(44, y2);        // "WIN" по центру
      arduboy.print(F("WIN"));
      // тонкая рамка по периметру (статично, без шума)
      for (uint8_t i = 0; i < 128; i++) { arduboy.drawPixel(i, 0, WHITE); arduboy.drawPixel(i, 63, WHITE); }
      for (uint8_t i = 0; i < 64; i++)  { arduboy.drawPixel(0, i, WHITE); arduboy.drawPixel(127, i, WHITE); }
      // мелодия сразу при входе (кадр уже нарисован)
      static bool melodyPlayed = false;
      if (!melodyPlayed) {
        melodyPlayed = true;
        arduboy.display();
        winMelody();
      }
      if (NAV(UP_BUTTON) || NAV(DOWN_BUTTON) || NAV(LEFT_BUTTON) ||
          NAV(RIGHT_BUTTON) || NAV(A_BUTTON) || NAV(B_BUTTON)) {
        melodyPlayed = false;
        goScreen(MENU);
      }
      break;
    }
  }

  if (screen == SPLASH || screen == SPLASH_EXIT) {
    arduboy.clear();
    bool anim = (screen == SPLASH_EXIT);
    int logoY = anim ? (LOGO_SPLASH_Y - transShift) : LOGO_SPLASH_Y;
    if (logoY < LOGO_MENU_Y) logoY = LOGO_MENU_Y;
    drawLogo(logoY);
    if (!anim) drawPressAnyKey();
    else if (transShift >= (LOGO_SPLASH_Y - LOGO_MENU_Y)) drawMenuItems();
  } else if (screen == MENU || screen == MENU_EXIT) {
    bool anim = (screen == MENU_EXIT);
    int logoY = anim ? (LOGO_MENU_Y + transShift) : LOGO_MENU_Y;
    if (logoY > LOGO_SPLASH_Y) logoY = LOGO_SPLASH_Y;
    arduboy.clear();
    drawLogo(logoY);
    if (!anim) drawMenuItems();
    else if (transShift >= (LOGO_SPLASH_Y - LOGO_MENU_Y)) drawPressAnyKey();
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
    const uint8_t colX[2] = {20, 72};
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t col = i / 2, row = i % 2;
      int16_t x = colX[col], y = 22 + row * 14;
      arduboy.setCursor(x, y);
      if (i == sizeSel) arduboy.print(F("> ")); else arduboy.print(F("  "));
      arduboy.print(sizeOpts[i]);
      arduboy.print(F("px"));
    }
  } else if (screen == HELP) {
    arduboy.clear();
    arduboy.setTextSize(1);
    arduboy.setCursor(8, 2);
    arduboy.print(F("Help: Paint Mode"));
    uint8_t startIdx = helpOff / ROW_H;
    for (uint8_t r = 0; r < HELP_VISIBLE; r++) {
      uint8_t i = startIdx + r;
      if (i >= HELP_LINES) break;
      arduboy.setCursor(8, HELP_TOP + r * ROW_H);
      arduboy.print(helpLines[i]);
    }
  }

  arduboy.display();
}
