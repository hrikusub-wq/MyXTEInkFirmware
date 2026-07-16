// Xteink X3 カスタムファームウェア — フェーズ2: ホーム画面とフォルダ画面
//
// やること:
//   1. SDカードアクセス(FileBrowserService、SDCardManagerのラッパー)
//   2. ホーム画面(本のプレースホルダー + 2x2グリッド)
//   3. フォルダ画面(SDカードの実際のファイル/フォルダ一覧、ページング)
//   4. ホーム⇔フォルダの画面遷移
//
// Xteink X3 ピン割り当て (公式ファームウェアのGPIO解析結果に基づく):
//   GPIO 8  = SPI SCLK (EPD/SD共有)     GPIO 4  = EPD DC
//   GPIO 10 = SPI MOSI (EPD/SD共有)     GPIO 5  = EPD RST
//   GPIO 21 = EPD CS                    GPIO 6  = EPD BUSY
//   GPIO 12 = SDカード CS
//   GPIO 1  = ボタンADC1 (4ボタン抵抗ラダー)
//   GPIO 2  = ボタンADC2 (2ボタン抵抗ラダー)
//   GPIO 3  = 電源ボタン (アクティブLOW)

#include <Arduino.h>
#include <EInkDisplay.h>
#include <InputManager.h>

#include "core/FileBrowserService.h"
#include "gfx/MiniFontImpl.h"
#include "screens/FolderScreen.h"
#include "screens/HomeScreen.h"

namespace {

constexpr int8_t EPD_SCLK = 8;
constexpr int8_t EPD_MOSI = 10;
constexpr int8_t EPD_CS = 21;
constexpr int8_t EPD_DC = 4;
constexpr int8_t EPD_RST = 5;
constexpr int8_t EPD_BUSY = 6;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager input;
FileBrowserService fileBrowser;

// X3の実機は縦持ちで使う機器だが、E-inkパネルはネイティブでは792x528(横長)の
// フレームバッファしか持たない。UI層は常にこの528x792(縦長)の論理サイズで描画し、
// FrameBufferOps側で物理座標(792x528)への90度回転変換を行う(実機を時計回りに
// 90度回すと正しい向きになることを確認済み)。
constexpr uint16_t LOGICAL_WIDTH = EInkDisplay::X3_DISPLAY_HEIGHT;   // 528
constexpr uint16_t LOGICAL_HEIGHT = EInkDisplay::X3_DISPLAY_WIDTH;   // 792

// SettingRow等の本文サイズ(5x7ドットの2倍角 = 10x14px相当)
MiniFontImpl font(2);

HomeScreen homeScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
FolderScreen folderScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, fileBrowser);

// 画面はまだホーム/フォルダの2つしかないため、専用のScreenManager(スタック)は
// 作らずここで素直に切り替える。画面が増えて管理が煩雑になったら導入を検討する。
enum class ActiveScreen { kHome, kFolder };
ActiveScreen activeScreen = ActiveScreen::kHome;

Screen& currentScreen() {
  return (activeScreen == ActiveScreen::kHome) ? static_cast<Screen&>(homeScreen)
                                                : static_cast<Screen&>(folderScreen);
}

void renderAndRefresh(EInkDisplay::RefreshMode mode) {
  display.clearScreen();
  uint8_t* fb = display.getFrameBuffer();
  currentScreen().render(fb, LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
  display.displayBuffer(mode);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // USB CDCの接続待ち(接続されていなくても先へ進む)
  Serial.println("[X3FW] boot: Xteink X3 custom firmware (phase 2)");

  input.begin();

  display.setDisplayX3();  // 792x528のX3パネルモード(begin前に呼ぶ必要がある)
  display.begin();
  Serial.printf("[X3FW] display ready: %ux%u\n",
                display.getDisplayWidth(), display.getDisplayHeight());

  // SDカードはEPDとSPIバスを共有しているため、EPD側のSPI初期化が終わった後に
  // 初期化する(先にSDを初期化するとバスがまだ整っておらず検出に失敗した)。
  if (fileBrowser.begin()) {
    Serial.println("[X3FW] SD card ready");
  } else {
    Serial.println("[X3FW] SD card not detected (folder screen will show empty)");
  }

  renderAndRefresh(EInkDisplay::FULL_REFRESH);
  Serial.println("[X3FW] initial frame drawn");
}

void loop() {
  input.update();

  for (uint8_t b = 0; b <= InputManager::BTN_POWER; b++) {
    if (!input.wasPressed(b)) continue;

    Serial.printf("[X3FW] button pressed: %s (index=%u)\n", InputManager::getButtonName(b), b);

    const ScreenAction action = currentScreen().handleButton(b);

    if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kHome) {
      activeScreen = ActiveScreen::kFolder;
      folderScreen.resetToRoot();
      renderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kNavigateBack && activeScreen == ActiveScreen::kFolder) {
      activeScreen = ActiveScreen::kHome;
      renderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kRedraw) {
      // 部分更新(FAST_REFRESH)でちらつきを抑えて書き換える
      renderAndRefresh(EInkDisplay::FAST_REFRESH);
    }
  }

  delay(10);
}
