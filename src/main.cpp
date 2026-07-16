// Xteink X3 カスタムファームウェア — フェーズ0: Hello World
//
// やること:
//   1. E-inkディスプレイの初期化と "HELLO, X3" の描画 (全体更新)
//   2. 物理ボタン入力の読み取りとシリアルログ出力
//   3. 押されたボタン名を画面にも表示 (高速部分更新の動作確認を兼ねる)
//
// Xteink X3 ピン割り当て (公式ファームウェアのGPIO解析結果に基づく):
//   GPIO 8  = SPI SCLK (EPD/SD共有)     GPIO 4  = EPD DC
//   GPIO 10 = SPI MOSI (EPD/SD共有)     GPIO 5  = EPD RST
//   GPIO 21 = EPD CS                    GPIO 6  = EPD BUSY
//   GPIO 1  = ボタンADC1 (4ボタン抵抗ラダー)
//   GPIO 2  = ボタンADC2 (2ボタン抵抗ラダー)
//   GPIO 3  = 電源ボタン (アクティブLOW)

#include <Arduino.h>
#include <EInkDisplay.h>
#include <InputManager.h>

#include "gfx/MiniFont.h"

namespace {

constexpr int8_t EPD_SCLK = 8;
constexpr int8_t EPD_MOSI = 10;
constexpr int8_t EPD_CS = 21;
constexpr int8_t EPD_DC = 4;
constexpr int8_t EPD_RST = 5;
constexpr int8_t EPD_BUSY = 6;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager input;

// 画面全体を描き直す。lastButtonが空でなければ押されたボタン名も表示する。
void renderScreen(const char* lastButton) {
  display.clearScreen();  // 全面白で塗り直し

  uint8_t* fb = display.getFrameBuffer();
  const uint16_t w = display.getDisplayWidth();
  const uint16_t h = display.getDisplayHeight();

  constexpr int TITLE_SCALE = 6;  // 5x7 -> 30x42px
  const char* title = "HELLO, X3";
  MiniFont::drawText(fb, w, h,
                     (w - MiniFont::textWidth(title, TITLE_SCALE)) / 2,
                     h / 2 - (MiniFont::GLYPH_H * TITLE_SCALE) / 2 - 40,
                     title, TITLE_SCALE);

  constexpr int INFO_SCALE = 3;
  if (lastButton[0] != '\0') {
    char line[32];
    snprintf(line, sizeof(line), "BTN: %s", lastButton);
    MiniFont::drawText(fb, w, h,
                       (w - MiniFont::textWidth(line, INFO_SCALE)) / 2,
                       h / 2 + 60, line, INFO_SCALE);
  } else {
    const char* hint = "PRESS ANY BUTTON";
    MiniFont::drawText(fb, w, h,
                       (w - MiniFont::textWidth(hint, INFO_SCALE)) / 2,
                       h / 2 + 60, hint, INFO_SCALE);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // USB CDCの接続待ち(接続されていなくても先へ進む)
  Serial.println("[X3FW] boot: Xteink X3 custom firmware (phase 0)");

  input.begin();

  display.setDisplayX3();  // 792x528のX3パネルモード(begin前に呼ぶ必要がある)
  display.begin();
  Serial.printf("[X3FW] display ready: %ux%u\n",
                display.getDisplayWidth(), display.getDisplayHeight());

  renderScreen("");
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.println("[X3FW] initial frame drawn");
}

void loop() {
  input.update();

  for (uint8_t b = 0; b <= InputManager::BTN_POWER; b++) {
    if (!input.wasPressed(b)) continue;

    const char* name = InputManager::getButtonName(b);
    Serial.printf("[X3FW] button pressed: %s (index=%u)\n", name, b);

    renderScreen(name);
    // 部分更新(FAST_REFRESH)でちらつきを抑えて書き換える
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
  }

  delay(10);
}
