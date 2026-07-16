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

#include "core/BatteryService.h"
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
BatteryService battery;

int lastKnownBatteryPercent = -1;
unsigned long lastBatteryCheckMs = 0;
constexpr unsigned long kBatteryCheckIntervalMs = 30000;

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

// 両画面のStatusBarに残量を反映する(画面切り替え時にどちらも最新値であるように)。
void applyBatteryPercent(int percent) {
  if (percent < 0) return;
  lastKnownBatteryPercent = percent;
  homeScreen.setBatteryPercent(percent);
  folderScreen.setBatteryPercent(percent);
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

  if (battery.begin()) {
    Serial.println("[X3FW] battery gauge (BQ27220) ready");
    const int percent = battery.readPercent();
    if (percent >= 0) {
      applyBatteryPercent(percent);
      Serial.printf("[X3FW] battery: %d%% (%dmV)\n", percent, battery.readMillivolts());
    } else {
      Serial.println("[X3FW] battery: read failed");
    }
    uint16_t rawStatus = 0;
    if (battery.readRawBatteryStatus(rawStatus)) {
      // BatteryStatus()のビット位置はライブラリ未検証(README参照)。ここでは
      // 生の値をログに出すのみで、充電判定には使わない。
      Serial.printf("[X3FW] battery raw BatteryStatus=0x%04X\n", rawStatus);
    }
  } else {
    Serial.println("[X3FW] battery gauge (BQ27220) not detected");
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

  // バッテリー残量は緩やかにしか変化しないため、一定間隔でのみ読み直す。
  // 値が変化した場合のみ再描画し、無駄な部分更新を避ける。
  if (millis() - lastBatteryCheckMs >= kBatteryCheckIntervalMs) {
    lastBatteryCheckMs = millis();
    const int percent = battery.readPercent();
    if (percent >= 0 && percent != lastKnownBatteryPercent) {
      applyBatteryPercent(percent);
      Serial.printf("[X3FW] battery: %d%%\n", percent);
      renderAndRefresh(EInkDisplay::FAST_REFRESH);
    }
  }

  delay(10);
}
