// Xteink X3 カスタムファームウェア — フェーズ1: 共通UIコンポーネント
//
// やること:
//   1. Font抽象インターフェース経由でのテキスト描画(将来のCJKフォント差し替えに備える)
//   2. 共通UIコンポーネント(StatusBar/SettingRow/HomeGridButton/FooterGuide)の実装
//   3. 動作確認: SettingRowを4行並べたテスト画面で、UP/DOWNボタンによる
//      フォーカス移動と選択行の見た目変化を実機で確認する
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

#include "gfx/MiniFontImpl.h"
#include "screens/DemoSettingsScreen.h"

namespace {

constexpr int8_t EPD_SCLK = 8;
constexpr int8_t EPD_MOSI = 10;
constexpr int8_t EPD_CS = 21;
constexpr int8_t EPD_DC = 4;
constexpr int8_t EPD_RST = 5;
constexpr int8_t EPD_BUSY = 6;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager input;

// X3の実機は縦持ちで使う機器だが、E-inkパネルはネイティブでは792x528(横長)の
// フレームバッファしか持たない。UI層は常にこの528x792(縦長)の論理サイズで描画し、
// FrameBufferOps側で物理座標(792x528)への90度回転変換を行う(実機を時計回りに
// 90度回すと正しい向きになることを確認済み)。
constexpr uint16_t LOGICAL_WIDTH = EInkDisplay::X3_DISPLAY_HEIGHT;   // 528
constexpr uint16_t LOGICAL_HEIGHT = EInkDisplay::X3_DISPLAY_WIDTH;   // 792

// SettingRow等の本文サイズ(5x7ドットの2倍角 = 10x14px相当)
MiniFontImpl font(2);

DemoSettingsScreen screen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font);

void renderAndRefresh(EInkDisplay::RefreshMode mode) {
  display.clearScreen();
  uint8_t* fb = display.getFrameBuffer();
  screen.render(fb, LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
  display.displayBuffer(mode);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // USB CDCの接続待ち(接続されていなくても先へ進む)
  Serial.println("[X3FW] boot: Xteink X3 custom firmware (phase 1)");

  input.begin();

  display.setDisplayX3();  // 792x528のX3パネルモード(begin前に呼ぶ必要がある)
  display.begin();
  Serial.printf("[X3FW] display ready: %ux%u\n",
                display.getDisplayWidth(), display.getDisplayHeight());

  renderAndRefresh(EInkDisplay::FULL_REFRESH);
  Serial.println("[X3FW] initial frame drawn");
}

void loop() {
  input.update();

  for (uint8_t b = 0; b <= InputManager::BTN_POWER; b++) {
    if (!input.wasPressed(b)) continue;

    Serial.printf("[X3FW] button pressed: %s (index=%u)\n", InputManager::getButtonName(b), b);

    if (screen.handleButton(b)) {
      // 部分更新(FAST_REFRESH)でちらつきを抑えて書き換える
      renderAndRefresh(EInkDisplay::FAST_REFRESH);
    }
  }

  delay(10);
}
