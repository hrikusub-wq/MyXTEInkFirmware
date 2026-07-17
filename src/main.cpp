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
//   GPIO 13 = バッテリー電源ラッチMOSFET制御(要最優先初期化、下記参照)
//   GPIO 1  = ボタンADC1 (4ボタン抵抗ラダー)
//   GPIO 2  = ボタンADC2 (2ボタン抵抗ラダー)
//   GPIO 3  = 電源ボタン (アクティブLOW)

#include <Arduino.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <esp_system.h>

#include "core/BatteryService.h"
#include "core/FileBrowserService.h"
#include "gfx/MiniFontImpl.h"
#include "screens/FolderScreen.h"
#include "screens/HomeScreen.h"

namespace {

// GPIO13はバッテリーからの給電を自己保持する電源ラッチMOSFETのゲート制御ピン
// (当初「SDカード電源制御」と誤解していたが、crosspoint-readerのソース調査で
// 判明。SDカードとは無関係)。USB給電中はUSB自体がVCCを供給するためGPIO13の
// 状態に関わらず動作するが、USBを抜いてバッテリー単独駆動に切り替わった瞬間、
// このラッチがHIGHで確実に保持されていないと電源が落ちる(実機でモザイク状の
// 画面表示のままフリーズし、リセットしても復帰しない不具合として確認)。
// open-drainではハイインピーダンスになり確実な駆動ができないため、必ず
// push-pull出力(OUTPUT)でHIGHを駆動すること。他の何よりも先に初期化する。
constexpr int8_t BATTERY_LATCH_PIN = 13;

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
bool lastKnownBatteryCharging = false;
unsigned long lastBatteryCheckMs = 0;
constexpr unsigned long kBatteryCheckIntervalMs = 30000;

// USB(給電+シリアル)の抜き差しは電源経路の切り替えを伴い、その瞬間に電圧が
// 不安定になることがある。この間にE-inkへSPI通信を行うとデータが化けて
// 画面にノイズが焼き付いたまま戻らなくなる不具合を実機で確認したため、
// USB接続状態が変化してから一定時間はE-ink描画そのものを止めて電源が
// 落ち着くのを待つ。ボタン入力自体は通常通り処理し、状態(フォーカス位置等)は
// 更新され続ける(描画だけが遅延する)。
bool lastUsbConnected = true;
unsigned long powerUnstableUntilMs = 0;
constexpr unsigned long kPowerSettleMs = 3000;
// 不安定期間中にボタン操作等で再描画が抑制された場合、期間が明けたら
// 最新状態を1回だけ強制的に描画する。
bool pendingRedrawAfterSettle = false;

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

// millis()のオーバーフローを考慮した「まだ不安定期間内か」の判定。
bool isPowerUnstable() {
  return static_cast<long>(powerUnstableUntilMs - millis()) > 0;
}

// USB(Serial)の接続/切断を検知したら、不安定期間の開始時刻を更新する。
void checkUsbConnectionChange() {
  const bool usbConnected = static_cast<bool>(Serial);
  if (usbConnected != lastUsbConnected) {
    lastUsbConnected = usbConnected;
    powerUnstableUntilMs = millis() + kPowerSettleMs;
    if (usbConnected) Serial.println("[X3FW] USB connected, pausing display updates briefly");
  }
}

void renderAndRefresh(EInkDisplay::RefreshMode mode) {
  display.clearScreen();
  uint8_t* fb = display.getFrameBuffer();
  currentScreen().render(fb, LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
  display.displayBuffer(mode);
}

// 電源が不安定な可能性がある間はE-ink描画をスキップする(画面の状態自体は
// 呼び出し側で更新済みのため、安定後の次の描画で反映される)。
void safeRenderAndRefresh(EInkDisplay::RefreshMode mode) {
  if (isPowerUnstable()) {
    pendingRedrawAfterSettle = true;
    return;
  }
  renderAndRefresh(mode);
}

// 両画面のStatusBarに残量・充電状態を反映する(画面切り替え時にどちらも最新値であるように)。
void applyBatteryState(int percent, bool charging) {
  if (percent < 0) return;
  lastKnownBatteryPercent = percent;
  lastKnownBatteryCharging = charging;
  homeScreen.setBatteryPercent(percent);
  homeScreen.setBatteryCharging(charging);
  folderScreen.setBatteryPercent(percent);
  folderScreen.setBatteryCharging(charging);
}

void logResetReason() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const char* reasonStr = "UNKNOWN";
  switch (reason) {
    case ESP_RST_POWERON: reasonStr = "POWERON"; break;
    case ESP_RST_EXT: reasonStr = "EXT"; break;
    case ESP_RST_SW: reasonStr = "SW"; break;
    case ESP_RST_PANIC: reasonStr = "PANIC"; break;
    case ESP_RST_INT_WDT: reasonStr = "INT_WDT"; break;
    case ESP_RST_TASK_WDT: reasonStr = "TASK_WDT"; break;
    case ESP_RST_WDT: reasonStr = "WDT"; break;
    case ESP_RST_DEEPSLEEP: reasonStr = "DEEPSLEEP"; break;
    case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT"; break;
    case ESP_RST_SDIO: reasonStr = "SDIO"; break;
    default: break;
  }
  Serial.printf("[X3FW] reset reason: %s (%d)\n", reasonStr, static_cast<int>(reason));
}

}  // namespace

void setup() {
  // バッテリー電源ラッチを他の何よりも先に確実にHIGH固定する(詳細は上記コメント参照)。
  pinMode(BATTERY_LATCH_PIN, OUTPUT);
  digitalWrite(BATTERY_LATCH_PIN, HIGH);

  Serial.begin(115200);
  delay(300);  // USB CDCの接続待ち(接続されていなくても先へ進む)
  lastUsbConnected = static_cast<bool>(Serial);
  Serial.println("[X3FW] boot: Xteink X3 custom firmware (phase 2)");
  logResetReason();

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
    const bool charging = battery.isCharging();
    if (percent >= 0) {
      applyBatteryState(percent, charging);
      Serial.printf("[X3FW] battery: %d%% (%dmV) charging=%d (current=%dmA)\n",
                    percent, battery.readMillivolts(), charging, battery.readCurrentMilliamps());
    } else {
      Serial.println("[X3FW] battery: read failed");
    }
    uint16_t rawStatus = 0;
    if (battery.readRawBatteryStatus(rawStatus)) {
      // BatteryStatus()のビット位置はライブラリ未検証(README参照)。充電判定には
      // 電流の符号(BatteryService::isCharging())を使っており、これはデバッグ用。
      Serial.printf("[X3FW] battery raw BatteryStatus=0x%04X\n", rawStatus);
    }
  } else {
    Serial.println("[X3FW] battery gauge (BQ27220) not detected");
  }

  renderAndRefresh(EInkDisplay::FULL_REFRESH);
  Serial.println("[X3FW] initial frame drawn");
}

void loop() {
  checkUsbConnectionChange();

  if (pendingRedrawAfterSettle && !isPowerUnstable()) {
    pendingRedrawAfterSettle = false;
    renderAndRefresh(EInkDisplay::FULL_REFRESH);
  }

  input.update();

  for (uint8_t b = 0; b <= InputManager::BTN_POWER; b++) {
    if (!input.wasPressed(b)) continue;

    if (Serial) Serial.printf("[X3FW] button pressed: %s (index=%u)\n", InputManager::getButtonName(b), b);

    const ScreenAction action = currentScreen().handleButton(b);

    if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kHome) {
      activeScreen = ActiveScreen::kFolder;
      folderScreen.resetToRoot();
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kNavigateBack && activeScreen == ActiveScreen::kFolder) {
      activeScreen = ActiveScreen::kHome;
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kRedraw) {
      // 部分更新(FAST_REFRESH)でちらつきを抑えて書き換える
      safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
    }
  }

  // バッテリー残量・充電状態は緩やかにしか変化しないため、一定間隔でのみ読み直す。
  // 変化した場合のみ再描画し、無駄な部分更新を避ける。
  if (millis() - lastBatteryCheckMs >= kBatteryCheckIntervalMs) {
    lastBatteryCheckMs = millis();
    const int percent = battery.readPercent();
    const bool charging = battery.isCharging();
    if (percent >= 0 && (percent != lastKnownBatteryPercent || charging != lastKnownBatteryCharging)) {
      applyBatteryState(percent, charging);
      if (Serial) Serial.printf("[X3FW] battery: %d%% charging=%d\n", percent, charging);
      safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
    }
  }

  delay(10);
}
