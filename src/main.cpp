// Xteink X3 カスタムファームウェア — フェーズ3: TXT読書画面と基本の読書中メニュー
//
// やること:
//   1. SDカードアクセス(FileBrowserService、SDCardManagerのラッパー)
//   2. ホーム画面(直近の本 + 2x2グリッド)
//   3. フォルダ画面(SDカードの実際のファイル/フォルダ一覧、ページング)
//   4. TXT読書画面(ページング、閉じる確認メニュー、進捗のSD保存)
//   5. ホーム⇔フォルダ⇔読書の画面遷移
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
#include "core/RtcService.h"
#include "core/SettingsService.h"
#include "core/TxtReaderService.h"
#include "gfx/CjkFontImpl.h"
#include "gfx/MiniFontImpl.h"
#include "gfx/XteinkBinFontImpl.h"
#include "screens/FolderScreen.h"
#include "screens/HistoryScreen.h"
#include "screens/HomeScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/TxtReaderScreen.h"

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
RtcService rtc;

int lastKnownBatteryPercent = -1;
bool lastKnownBatteryCharging = false;
unsigned long lastBatteryCheckMs = 0;
constexpr unsigned long kBatteryCheckIntervalMs = 30000;

// ステータスバー時刻表示(設定でON/OFF)は変化が緩やかなため、バッテリー同様
// 一定間隔でのみRTCを読み直す。
String lastClockText;
unsigned long lastClockCheckMs = 0;
constexpr unsigned long kClockCheckIntervalMs = 20000;

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

// UP/DOWN(側面ボタン)は短押しでその画面本来の意味(フォーカス移動等)、
// 長押し(kLongPressMs以上)でCONFIRM/BACKのショートカットとして働く
// (底面のCONFIRM/BACKまで手を伸ばさなくても側面ボタンだけで操作できるように、
// というフィードバックを受けて追加。各画面のhandleButton()自体は変更せず、
// 長押しを検出した時点でCONFIRM/BACKのボタンコードに置き換えて渡すだけなので、
// FolderScreenのようにUP/DOWNへ独自の意味(ディレクトリ階層移動)を割り当てている
// 画面とも衝突しない)。長押し判定はリリース時にgetHeldTime()を見て行うため、
// 短押し自体の反応が遅延することはない(ダブルクリック方式だと2回目の入力を
// 待つ必要があり反応が遅れるため、こちらを採用した)。
constexpr unsigned long kLongPressMs = 500;

// X3の実機は縦持ちで使う機器だが、E-inkパネルはネイティブでは792x528(横長)の
// フレームバッファしか持たない。UI層は常にこの528x792(縦長)の論理サイズで描画し、
// FrameBufferOps側で物理座標(792x528)への90度回転変換を行う(実機を時計回りに
// 90度回すと正しい向きになることを確認済み)。
constexpr uint16_t LOGICAL_WIDTH = EInkDisplay::X3_DISPLAY_HEIGHT;   // 528
constexpr uint16_t LOGICAL_HEIGHT = EInkDisplay::X3_DISPLAY_WIDTH;   // 792

// SettingRow等の本文サイズ(5x7ドットの2倍角 = 10x14px相当がデフォルト。設定画面の
// 「TEXT SIZE」でランタイム変更できる)。
MiniFontImpl font(2);

// Markdown見出し(#〜######)用のASCIIフォールバックフォント。本文(font)より
// 拡大率を1段階大きくするだけの単純な区別(scaleは4が上限、詳細はMiniFontImpl参照)。
// CJKフォントで読んでいる場合はcjkHeadingFontが優先され、こちらは使われない。
MiniFontImpl miniHeadingFont(3);

// TXT読書画面の本文用フォント(設定画面「BOOK FONT」で.cpfont/.binを個別に選べる、
// applyReaderBodyFontSettings()参照)。既定(未設定時)はcrosspoint-jp
// (https://github.com/zrn-ns/crosspoint-jp)のSDカードフォント形式(.cpfont)、
// kDefaultReaderBodyFontPath(SettingsService.h)を読み込む。見つからない場合は
// MiniFontImpl(ASCII専用、非ASCIIは豆腐表示)にフォールバックする。設定画面の
// 「SYSTEM FONT」(UIチローム全体)とは独立している。
CjkFontImpl cjkFont;
// 同上、BOOK FONTで.bin(XteinkBinFontImpl)が選ばれた場合用。
XteinkBinFontImpl readerBodyBinFont;

// Markdown見出し(H1)用の既定フォント(設定画面「MARKDOWN」のHEADING 1が未設定=
// DEFAULTの場合のフォールバック)。crosspoint-jpの.cpfontではなく、XTEink Web Font
// Maker(https://github.com/lakafior/XTEink-Web-Font-Maker、純正Xteinkファームウェア
// 向けのカスタムフォント変換ツールが使う.bin形式)で生成済みのフォントファイルを使う。
// ユーザーが既にSDカードへ配置していたファイルを流用でき、追加のダウンロードが
// 不要だったため採用した(詳細はREADME「Markdown対応について」を参照)。幅・高さは
// ファイル名から自動解析する(XteinkBinFontImpl::parseDimensions()、下記
// tryBeginBinFont()参照)。
constexpr const char* CJK_HEADING_FONT_PATH = "/fonts/Noto Sans JP 24pt.32×46.bin";

// Markdown見出しH1/H2/H3用フォント(H4〜H6はH3を流用、TxtReaderService参照)。
// H2/H3は未設定なら1つ上のレベルにカスケードする(applyMarkdownFontSettings()参照)。
XteinkBinFontImpl headingFont1;
XteinkBinFontImpl headingFont2;
XteinkBinFontImpl headingFont3;

// Markdownの箇条書き(LIST)・太字/強調(BOLD)用フォント。既定値は無く、設定画面
// 「MARKDOWN」で明示的に.binを選んだ場合のみ使われる(未設定なら本文と同じフォントで
// 代用、README「Markdown対応について」参照)。
XteinkBinFontImpl mdListFont;
XteinkBinFontImpl mdBoldFont;

// 設定画面の「SYSTEM FONT」で選ばれたUIチローム用フォント。cjkFontとは別インスタンス
// (読書本文用フォントと異なるファイルを選べるようにするため、それぞれ独立して開く)。
CjkFontImpl systemCjkFont;
// 同上、SYSTEM FONTで.bin(XteinkBinFontImpl)が選ばれた場合用。
XteinkBinFontImpl systemBinFont;
AppSettings appSettings;

// path(ファイル名部分に"WxH"または"W×H"を含む)から幅・高さを解析してimplを開く。
// 解析やbegin()に失敗した場合はfalseを返し、implはready()=false のままになる
// (呼び出し側がフォールバックする)。
bool tryBeginBinFont(XteinkBinFontImpl& impl, const char* path) {
  int w = 0, h = 0;
  return XteinkBinFontImpl::parseDimensions(String(path), w, h) && impl.begin(path, w, h);
}

// 全画面のrender()に渡す「現在のシステムフォント」。デフォルトはMiniFontImpl(font)。
// 設定でCJKフォントが選ばれ読み込みに成功した場合のみsystemCjkFontを指す
// (applySystemFontSettings()参照)。
const Font* activeFont = &font;

HomeScreen homeScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
FolderScreen folderScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, fileBrowser);
TxtReaderScreen readerScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, appSettings);
SettingsScreen settingsScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, rtc, battery, fileBrowser, appSettings);
HistoryScreen historyScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font);

// 画面はまだホーム/フォルダ/読書/設定/履歴の5つしかないため、専用のScreenManager
// (スタック)は作らずここで素直に切り替える。画面が増えて管理が煩雑になったら
// 導入を検討する。
enum class ActiveScreen { kHome, kFolder, kReader, kSettings, kHistory };
ActiveScreen activeScreen = ActiveScreen::kHome;

Screen& currentScreen() {
  switch (activeScreen) {
    case ActiveScreen::kFolder: return folderScreen;
    case ActiveScreen::kReader: return readerScreen;
    case ActiveScreen::kSettings: return settingsScreen;
    case ActiveScreen::kHistory: return historyScreen;
    case ActiveScreen::kHome:
    default: return homeScreen;
  }
}

// 設定画面で選ばれたシステムフォント(MiniFont or SD上の.cpfont)を実際に適用する。
// フォントが変わるとUIチロームの行の高さ等も変わるため、フォント依存のレイアウトを
// 持つ画面(FolderScreen・SettingsScreen自身)を再計算させる。
// HomeScreen/TxtReaderScreenのチローム部分は固定pxレイアウトでフォント非依存のため
// 再計算不要(README「CJKフォントについて」参照)。
void applySystemFontSettings() {
  font.setScale(appSettings.miniFontScale);

  if (appSettings.systemFontKind == SystemFontKind::kCjkFont && appSettings.cjkFontPath[0] != '\0' &&
      systemCjkFont.begin(appSettings.cjkFontPath)) {
    activeFont = &systemCjkFont;
    Serial.printf("[X3FW] system font: %s\n", appSettings.cjkFontPath);
  } else if (appSettings.systemFontKind == SystemFontKind::kBinFont && appSettings.binFontPath[0] != '\0' &&
             tryBeginBinFont(systemBinFont, appSettings.binFontPath)) {
    activeFont = &systemBinFont;
    Serial.printf("[X3FW] system font: %s (bin)\n", appSettings.binFontPath);
  } else {
    activeFont = &font;
    Serial.printf("[X3FW] system font: MiniFont (scale=%d)\n", font.scale());
  }

  folderScreen.relayout(*activeFont);
  settingsScreen.relayout(*activeFont);
  historyScreen.relayout(*activeFont);
}

// 読書画面のMarkdown表示用フォント(見出しH1/H2/H3・リスト・太字)を設定に応じて
// 反映する。H1は未設定(DEFAULT)でも常に既定フォント(CJK_HEADING_FONT_PATH、失敗時は
// ASCIIフォールバック)を使う。H2/H3は未設定なら1つ上のレベルの解決済みフォントに
// カスケードする(H3未設定→H2、H2未設定→H1、H1も未設定ならASCII)ため、レベルを
// 個別に用意しなくても「見出しは全部同じ大きさ」という従来の見た目を維持できる。
// LIST/BOLDは未設定なら本文と同じフォントで代用する(nullptrを渡す。
// TxtReaderScreen::listFont()/render()参照)。
void applyMarkdownFontSettings() {
  const char* h1Path =
      (appSettings.mdHeading1FontPath[0] != '\0') ? appSettings.mdHeading1FontPath : CJK_HEADING_FONT_PATH;
  const Font* h1Resolved;
  if (tryBeginBinFont(headingFont1, h1Path)) {
    h1Resolved = &headingFont1;
    Serial.printf("[X3FW] markdown H1 font ready: %s\n", h1Path);
  } else {
    h1Resolved = &miniHeadingFont;
    Serial.printf("[X3FW] markdown H1 font not found at %s (using ASCII fallback)\n", h1Path);
  }
  readerScreen.setHeadingFont1(h1Resolved);

  const Font* h2Resolved = h1Resolved;
  if (appSettings.mdHeading2FontPath[0] != '\0' && tryBeginBinFont(headingFont2, appSettings.mdHeading2FontPath)) {
    h2Resolved = &headingFont2;
    Serial.printf("[X3FW] markdown H2 font ready: %s\n", appSettings.mdHeading2FontPath);
  }
  readerScreen.setHeadingFont2(h2Resolved);

  const Font* h3Resolved = h2Resolved;
  if (appSettings.mdHeading3FontPath[0] != '\0' && tryBeginBinFont(headingFont3, appSettings.mdHeading3FontPath)) {
    h3Resolved = &headingFont3;
    Serial.printf("[X3FW] markdown H3 font ready: %s\n", appSettings.mdHeading3FontPath);
  }
  readerScreen.setHeadingFont3(h3Resolved);

  if (appSettings.mdListFontPath[0] != '\0' && tryBeginBinFont(mdListFont, appSettings.mdListFontPath)) {
    readerScreen.setListFont(&mdListFont);
    Serial.printf("[X3FW] markdown list font ready: %s\n", appSettings.mdListFontPath);
  } else {
    readerScreen.setListFont(nullptr);
  }

  if (appSettings.mdBoldFontPath[0] != '\0' && tryBeginBinFont(mdBoldFont, appSettings.mdBoldFontPath)) {
    readerScreen.setBoldFont(&mdBoldFont);
    Serial.printf("[X3FW] markdown bold font ready: %s\n", appSettings.mdBoldFontPath);
  } else {
    readerScreen.setBoldFont(nullptr);
  }
}

// 読書画面の本文フォント(設定画面「BOOK FONT」)を反映する。kCjkFont+パス未設定は
// kDefaultReaderBodyFontPathへフォールバックする(設定を一度も変更していない
// ユーザーの動作を変えないため)。いずれの経路でも読み込みに失敗したら最終的に
// MiniFontImpl(ASCII、setContentFont(nullptr))へフォールバックする。
void applyReaderBodyFontSettings() {
  if (appSettings.readerBodyFontKind == SystemFontKind::kBinFont && appSettings.readerBodyBinFontPath[0] != '\0' &&
      tryBeginBinFont(readerBodyBinFont, appSettings.readerBodyBinFontPath)) {
    readerScreen.setContentFont(&readerBodyBinFont);
    Serial.printf("[X3FW] reader body font: %s (bin)\n", appSettings.readerBodyBinFontPath);
    return;
  }

  if (appSettings.readerBodyFontKind == SystemFontKind::kCjkFont) {
    const char* path =
        (appSettings.readerBodyCjkFontPath[0] != '\0') ? appSettings.readerBodyCjkFontPath : kDefaultReaderBodyFontPath;
    if (cjkFont.begin(path)) {
      readerScreen.setContentFont(&cjkFont);
      Serial.printf("[X3FW] reader body font: %s\n", path);
      return;
    }
    Serial.printf("[X3FW] reader body font not found at %s (falling back to ASCII)\n", path);
  }

  readerScreen.setContentFont(nullptr);
  Serial.println("[X3FW] reader body font: MiniFont (ASCII)");
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
  currentScreen().render(fb, LOGICAL_WIDTH, LOGICAL_HEIGHT, *activeFont);
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
  readerScreen.setBatteryPercent(percent);
  readerScreen.setBatteryCharging(charging);
  settingsScreen.setBatteryPercent(percent);
  settingsScreen.setBatteryCharging(charging);
  historyScreen.setBatteryPercent(percent);
  historyScreen.setBatteryCharging(charging);
}

// ホーム画面のステータスバーに時刻("HH:MM")を反映する(設定でONの場合のみ)。
// 変化した場合のみホーム画面が表示中なら再描画する(バッテリー同様、無駄な
// 部分更新を避ける)。
void updateStatusBarClock() {
  String newText;
  if (appSettings.showClockInStatusBar && rtc.ready()) {
    RtcDateTime dt;
    if (rtc.readDateTime(dt)) {
      const RtcDateTime local = addHoursToDateTime(dt, appSettings.timezoneOffsetHours);
      char buf[8];
      snprintf(buf, sizeof(buf), "%02u:%02u", local.hour, local.minute);
      newText = buf;
    }
  }

  if (newText == lastClockText) return;
  lastClockText = newText;
  homeScreen.setClockText(lastClockText.c_str());
  if (activeScreen == ActiveScreen::kHome) safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
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
  Serial.println("[X3FW] boot: Xteink X3 custom firmware (phase 3)");
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
    String lastBookPath;
    int lastBookPercent = 0;
    if (TxtReaderService::readLastBook(lastBookPath, lastBookPercent)) {
      homeScreen.setLastBook(lastBookPath, lastBookPercent);
      Serial.printf("[X3FW] last book: %s (%d%%)\n", lastBookPath.c_str(), lastBookPercent);
    }

    if (SettingsService::load(appSettings)) {
      Serial.println("[X3FW] settings loaded");
    } else {
      Serial.println("[X3FW] no saved settings, using defaults");
    }
    settingsScreen.reloadAvailableFonts();
    applySystemFontSettings();
    // 読書本文フォント(設定「BOOK FONT」)。
    applyReaderBodyFontSettings();
    // Markdown見出し/リスト/太字用フォント(設定「MARKDOWN」)。本文用フォントの
    // 読み込み可否とは独立に(ASCII本文+.bin見出し、という組み合わせも成立するため)。
    applyMarkdownFontSettings();
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

  if (rtc.begin()) {
    Serial.printf("[X3FW] RTC (DS3231) ready, lostPower=%d\n", rtc.lostPower());
    RtcDateTime dt;
    if (rtc.readDateTime(dt)) {
      Serial.printf("[X3FW] RTC time: %04u-%02u-%02u %02u:%02u:%02u\n",
                    dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
      Serial.println("[X3FW] RTC time read failed");
    }
  } else {
    Serial.println("[X3FW] RTC (DS3231) not detected");
  }

  // 初回の時刻表示は直接セットする(updateStatusBarClock()はloop()での定期更新用で、
  // 変化時に再描画も行ってしまうため、これから直後にFULL_REFRESHする起動時には使わない)。
  if (appSettings.showClockInStatusBar && rtc.ready()) {
    RtcDateTime dt;
    if (rtc.readDateTime(dt)) {
      const RtcDateTime local = addHoursToDateTime(dt, appSettings.timezoneOffsetHours);
      char buf[8];
      snprintf(buf, sizeof(buf), "%02u:%02u", local.hour, local.minute);
      lastClockText = buf;
      homeScreen.setClockText(buf);
    }
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
    uint8_t effectiveButton = b;

    if (b == InputManager::BTN_UP || b == InputManager::BTN_DOWN) {
      // 短押し/長押しの判定はボタンを離した瞬間にしかできないため、この2つだけ
      // wasPressed()ではなくwasReleased()を見る(反応が「押した瞬間」から
      // 「離した瞬間」にずれるだけで、短押し自体に待ち時間は発生しない)。
      if (!input.wasReleased(b)) continue;
      if (input.getHeldTime() >= kLongPressMs) {
        effectiveButton = (b == InputManager::BTN_UP) ? InputManager::BTN_CONFIRM : InputManager::BTN_BACK;
      }
    } else {
      if (!input.wasPressed(b)) continue;
    }

    if (Serial) {
      if (effectiveButton != b) {
        Serial.printf("[X3FW] button long-press: %s -> %s\n", InputManager::getButtonName(b),
                      InputManager::getButtonName(effectiveButton));
      } else {
        Serial.printf("[X3FW] button pressed: %s (index=%u)\n", InputManager::getButtonName(effectiveButton),
                      effectiveButton);
      }
    }

    const ScreenAction action = currentScreen().handleButton(effectiveButton);

    if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kHome) {
      if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kSettings) {
        activeScreen = ActiveScreen::kSettings;
      } else {
        activeScreen = ActiveScreen::kFolder;
        folderScreen.resetToRoot();
      }
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kOpenHistory && activeScreen == ActiveScreen::kHome) {
      activeScreen = ActiveScreen::kHistory;
      historyScreen.reload();
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kNavigateBack) {
      if (activeScreen == ActiveScreen::kFolder || activeScreen == ActiveScreen::kSettings ||
          activeScreen == ActiveScreen::kHistory) {
        activeScreen = ActiveScreen::kHome;
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      } else if (activeScreen == ActiveScreen::kReader) {
        activeScreen = ActiveScreen::kHome;
        // 閉じた本の最新の進捗をホーム画面のプレースホルダーに反映する。
        String lastBookPath;
        int lastBookPercent = 0;
        if (TxtReaderService::readLastBook(lastBookPath, lastBookPercent)) {
          homeScreen.setLastBook(lastBookPath, lastBookPercent);
        }
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      }
    } else if (action == ScreenAction::kOpenFile) {
      String pathToOpen;
      if (activeScreen == ActiveScreen::kHome) {
        pathToOpen = homeScreen.lastBookPath();
      } else if (activeScreen == ActiveScreen::kFolder) {
        pathToOpen = folderScreen.pendingOpenFilePath();
      } else if (activeScreen == ActiveScreen::kHistory) {
        pathToOpen = historyScreen.pendingOpenFilePath();
      }
      if (pathToOpen.length() > 0 && readerScreen.openFile(pathToOpen)) {
        activeScreen = ActiveScreen::kReader;
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      }
    } else if (action == ScreenAction::kRedraw) {
      if (activeScreen == ActiveScreen::kSettings && settingsScreen.consumeFontSettingsChanged()) {
        // フォント変更は全画面のレイアウト・見た目に影響するため、部分更新ではなく
        // FULL_REFRESHで確実に描き直す。SYSTEM FONT・BOOK FONT・MARKDOWN(見出し/
        // リスト/太字)のいずれの変更でもこのフラグが立つため、まとめて反映する。
        applySystemFontSettings();
        applyReaderBodyFontSettings();
        applyMarkdownFontSettings();
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      } else {
        // 部分更新(FAST_REFRESH)でちらつきを抑えて書き換える
        safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
      }
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

  if (millis() - lastClockCheckMs >= kClockCheckIntervalMs) {
    lastClockCheckMs = millis();
    updateStatusBarClock();
  }

  delay(10);
}
