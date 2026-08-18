#include <Arduboy2.h>

Arduboy2 arduboy;

// ===== НАСТРОЙКИ (менять здесь при написании кода) =====
const uint8_t CELL = 8;          // размер сетки/кисти в пикселях (2,4,8,16 ...)
// =======================================================

const uint8_t GW = 128 / CELL;   // число клеток по X
const uint8_t GH = 64 / CELL;    // число клеток по Y
uint8_t grid[GW * GH / 8 + 1];   // битовая упаковка сетки

const uint16_t HIST_MAX = 64;
uint16_t history[HIST_MAX];      // запись: (индекс<<1)|oldVal
uint16_t histHead = 0;

int16_t cx = (GW / 2) * CELL;    // старт в центре сетки
int16_t cy = (GH / 2) * CELL;
int16_t prevCx = (GW / 2) * CELL;
int16_t prevCy = (GH / 2) * CELL;

unsigned long lastMove = 0;
const unsigned long REPEAT = 180; // мс между шагами при удержании
unsigned long bPressStart = 0;    // время нажатия B (для tap/hold)
bool bHandled = false;            // уже обработали тап/холд B в этой сессии нажатия

bool getCell(uint8_t gx, uint8_t gy) {
  uint16_t i = gx + (uint16_t)gy * GW;
  return (grid[i >> 3] >> (i & 7)) & 1;
}
void setCell(uint8_t gx, uint8_t gy, bool v) {
  uint16_t i = gx + (uint16_t)gy * GW;
  if (v) grid[i >> 3] |= (1 << (i & 7));
  else   grid[i >> 3] &= ~(1 << (i & 7));
}

// записать изменение клетки в историю (для undo)
void pushChange(uint8_t gx, uint8_t gy, bool oldVal) {
  if (histHead >= HIST_MAX) return;
  uint16_t idx = gx + (uint16_t)gy * GW;
  history[histHead++] = (idx << 1) | (oldVal ? 1 : 0);
}

// восстановить прямоугольник (курсор + запас) по сетке
void drawRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t py = y; py < y + h; py++) {
    if (py < 0 || py > 63) continue;
    for (int16_t px = x; px < x + w; px++) {
      if (px < 0 || px > 127) continue;
      uint8_t gx = px / CELL, gy = py / CELL;
      arduboy.drawPixel(px, py, getCell(gx, gy) ? WHITE : BLACK);
    }
  }
}

void setup() {
  arduboy.boot();              // голый hardware-init (быстро, без меню/audio)
  arduboy.display();           // чистый экран
  arduboy.bootLogo();          // анимация логотипа (как в begin(), но без лишнего)
  arduboy.setFrameRate(60);
  for (uint16_t i = 0; i < sizeof(grid); i++) grid[i] = 0;
  arduboy.clear();
  arduboy.display();
}

void loop() {
  if (!arduboy.nextFrame()) return;
  arduboy.pollButtons();

  // движение: тап = 1 клетка, удержание = повтор 1 клетка каждые REPEAT мс
  bool up = arduboy.pressed(UP_BUTTON);
  bool down = arduboy.pressed(DOWN_BUTTON);
  bool left = arduboy.pressed(LEFT_BUTTON);
  bool right = arduboy.pressed(RIGHT_BUTTON);
  bool dirJust = arduboy.justPressed(UP_BUTTON) || arduboy.justPressed(DOWN_BUTTON) ||
                 arduboy.justPressed(LEFT_BUTTON) || arduboy.justPressed(RIGHT_BUTTON);
  if ((up || down || left || right) && (dirJust || millis() - lastMove >= REPEAT)) {
    if (left)  cx -= CELL;
    if (right) cx += CELL;
    if (up)    cy -= CELL;
    if (down)  cy += CELL;
    if (cx < 0) cx = 0;
    if (cx > 128 - CELL) cx = 128 - CELL;
    if (cy < 0) cy = 0;
    if (cy > 64 - CELL) cy = 64 - CELL;
    lastMove = millis();
  }

  // A — tap: тоггл клетки; удержание (drag): повторяет действие первого тапа
  static bool aTarget = true;
  if (arduboy.justPressed(A_BUTTON)) {
    uint8_t gx = cx / CELL, gy = cy / CELL;
    bool old = getCell(gx, gy);
    aTarget = !old;
    if (aTarget != old) { setCell(gx, gy, aTarget); pushChange(gx, gy, old); }
  }
  if (arduboy.pressed(A_BUTTON)) {
    uint8_t gx = cx / CELL, gy = cy / CELL;
    bool old = getCell(gx, gy);
    if (aTarget != old) { setCell(gx, gy, aTarget); pushChange(gx, gy, old); }
  }

  // B: tap = undo последнего действия A (по одной клетке, LIFO);
  //    удержание (>=500 мс) = стереть весь холст.
  if (arduboy.justPressed(B_BUTTON)) {
    bPressStart = millis();
    bHandled = false;
  }
  if (arduboy.pressed(B_BUTTON) && !bHandled) {
    if (millis() - bPressStart >= 500) {
      // долгое удержание -> очистить холст
      for (uint16_t i = 0; i < sizeof(grid); i++) grid[i] = 0;
      histHead = 0;                 // история тоже сбрасывается
      arduboy.clear();
      bHandled = true;
    }
  }
  if (arduboy.justReleased(B_BUTTON) && !bHandled) {
    // короткий тап -> undo
    if (histHead > 0) {
      uint16_t rec = history[--histHead];
      bool oldVal = rec & 1;
      uint16_t idx = rec >> 1;
      uint8_t gx = idx % GW;
      uint8_t gy = idx / GW;
      setCell(gx, gy, oldVal);
      drawRegion(gx * CELL, gy * CELL, CELL, CELL);
    }
    bHandled = true;
  }

  // убрать старый курсор
  drawRegion(prevCx - CELL, prevCy - CELL, CELL * 3, CELL * 3);

  // шахматка курсора (без обводок); при простое >=1 сек — анимация инверсией
  uint8_t phase = 0;
  if (millis() - lastMove >= 1000)
    phase = (millis() / 250) & 1;
  for (uint8_t i = 0; i < CELL; i++)
    for (uint8_t j = 0; j < CELL; j++) {
      bool on = (((i + j + phase) & 1) == 1);
      arduboy.drawPixel(cx + i, cy + j, on ? BLACK : WHITE);
    }

  prevCx = cx;
  prevCy = cy;

  arduboy.display();
}
