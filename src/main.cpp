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
#include "core/BleTransferService.h"
#include "core/FileBrowserService.h"
#include "core/PowerManager.h"
#include "core/RtcService.h"
#include "core/SettingsService.h"
#include "core/TxtReaderService.h"
#include "gfx/CjkFontImpl.h"
#include "gfx/MiniFontImpl.h"
#include "gfx/XteinkBinFontImpl.h"
#include "screens/BluetoothScreen.h"
#include "screens/FolderScreen.h"
#include "screens/FolderSyncScreen.h"
#include "screens/HistoryScreen.h"
#include "screens/HomeScreen.h"
#include "screens/LiveTextScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/StandbyScreen.h"
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
BleTransferService bleTransfer(fileBrowser);

// Bluetooth/FolderSync画面が開いている間だけ、BLE側の状態変化(接続/切断・
// 受信進捗等)をポーリングして再描画するための間隔。ボタン入力を伴わない
// 非同期イベントのため、バッテリー残量・時計と同じ「一定間隔でチェックし、
// 変化があった場合のみ再描画する」方式にする(kBatteryCheckIntervalMs等参照)。
unsigned long lastBleUiCheckMs = 0;
constexpr unsigned long kBleUiCheckIntervalMs = 500;

// Liveテキストモードのファイル更新ポーリング間隔。実際のリロード要否・
// デバウンスはLiveTextScreen::pollForUpdate()内部で判定するため、ここでは
// BLE状態と同じ間隔でチェックを呼ぶだけでよい。
unsigned long lastLiveTextCheckMs = 0;

// FolderScreenでのLEFT/RIGHT長押し中、E-inkのリフレッシュ待ちのたびにボタンを
// 押し直さなくても自動的にフォーカスが進み続けるようにする(参考実装
// crosspoint-readerのButtonNavigator::onContinuous()と同じ考え方)。isPressed()は
// 押されている「間」ずっとtrueを返すレベル判定なので、リフレッシュでloop()が
// ブロックされていた間の一瞬の押下エッジ(wasPressed())を取りこぼす問題を
// 回避できる: 押しっぱなしにしている限り、リフレッシュ明けに再開した時点でも
// まだ「押されている」ことが分かる。UP/DOWN(側面ボタン)は既に長押し=CONFIRM/
// BACKショートカットという別の意味を持つため対象外とし、FolderScreen内で
// 現在何の長押し意味も持たないLEFT/RIGHTだけに限定する。
unsigned long lastContinuousNavMs = 0;
constexpr unsigned long kContinuousNavStartMs = 400;     // 長押しとみなすまでの時間
constexpr unsigned long kContinuousNavIntervalMs = 100;  // 連続発火の最小間隔

// InputManagerのgetHeldTime()はボタン個別ではなく「直近に何らかのボタンが
// 押されてから離されるまでの経過時間」というグローバルな値であり、isPressed()も
// 直近のupdate()呼び出し時点のスナップショットでしかない。LEFT/RIGHTの押下や
// この連続ナビ自体がFAST_REFRESHの描画ブロッキング(数百ms)を挟むと、その間
// update()が呼ばれないため、ブロッキング明け直後の1周期はisPressed()がまだ
// 「実際にはもう離されているのに古いtrueのまま」になりうる。さらに
// InputManager側のデバウンス(5ms)は「状態が変化した」と検知したupdate()呼び出し
// そのものでは確定させず、次のupdate()呼び出しで確定させる作りのため、
// ブロッキング明け直後の1周期だけでは離されたことがまだ反映されないことがある。
// これにより、単発タップでも1回余分に連続ナビが誤発火する不具合が実機で見つかった
// (LEFT/RIGHTの押下エッジ処理直後の1周期をスキップするだけでは、次の1周期分の
// 誤発火を防ぎきれなかった)。
// 対策: LEFT/RIGHTの押下エッジを処理した直後、および連続ナビ自体がリフレッシュを
// 行った直後は、次の1周期分だけ判定を丸ごと見送る(このフラグで持ち越す)。
bool skipContinuousNavCheckOnce = false;

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
// 長押し(appSettings.longPressMs以上)でCONFIRM/BACKのショートカットとして働く
// (底面のCONFIRM/BACKまで手を伸ばさなくても側面ボタンだけで操作できるように、
// というフィードバックを受けて追加。各画面のhandleButton()自体は変更せず、
// 長押しを検出した時点でCONFIRM/BACKのボタンコードに置き換えて渡すだけなので、
// FolderScreenのようにUP/DOWNへ独自の意味(ディレクトリ階層移動)を割り当てている
// 画面とも衝突しない)。長押し判定はリリース時にgetHeldTime()を見て行うため、
// 短押し自体の反応が遅延することはない(ダブルクリック方式だと2回目の入力を
// 待つ必要があり反応が遅れるため、こちらを採用した)。判定時間自体は固定値では
// なくSettingsScreenの「LONG PRESS」でユーザーが調整できる(appSettings.longPressMs、
// 範囲200〜1500ms)。

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
constexpr const char* CJK_HEADING_FONT_PATH = "/System/fonts/Noto Sans JP 24pt.32×46.bin";

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
BluetoothScreen bluetoothScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, bleTransfer);
FolderSyncScreen folderSyncScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, bleTransfer);
LiveTextScreen liveTextScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, bleTransfer);
StandbyScreen standbyScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, fileBrowser, display, appSettings);

// 画面はまだホーム/フォルダ/読書/設定/履歴/Bluetooth/フォルダ同期/Liveテキスト/
// 待機の9つしかないため、専用のScreenManager(スタック)は作らずここで素直に
// 切り替える。画面が増えて管理が煩雑になったら導入を検討する。
enum class ActiveScreen {
  kHome,
  kFolder,
  kReader,
  kSettings,
  kHistory,
  kBluetooth,
  kFolderSync,
  kLiveText,
  kStandby
};
ActiveScreen activeScreen = ActiveScreen::kHome;
// kBluetoothはHome/Settingsのどちらからでも開けるため、BACKで戻る先を覚えておく
// (trueならHomeから、falseならSettingsから開いた)。
bool bluetoothEnteredFromHome = false;
// kFolderも同様に2箇所から開ける(Home「FOLDER」="/User"、Settings「SYSTEM」=
// "/System")。trueならSettingsから開いた(BACKでSettingsへ戻す)、falseなら
// Homeから開いた(BACKでHomeへ戻す)。
bool folderEnteredFromSettings = false;

Screen& currentScreen() {
  switch (activeScreen) {
    case ActiveScreen::kFolder: return folderScreen;
    case ActiveScreen::kReader: return readerScreen;
    case ActiveScreen::kSettings: return settingsScreen;
    case ActiveScreen::kHistory: return historyScreen;
    case ActiveScreen::kBluetooth: return bluetoothScreen;
    case ActiveScreen::kFolderSync: return folderSyncScreen;
    case ActiveScreen::kLiveText: return liveTextScreen;
    case ActiveScreen::kStandby: return standbyScreen;
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
    liveTextScreen.setContentFont(&readerBodyBinFont);
    Serial.printf("[X3FW] reader body font: %s (bin)\n", appSettings.readerBodyBinFontPath);
    return;
  }

  if (appSettings.readerBodyFontKind == SystemFontKind::kCjkFont) {
    const char* path =
        (appSettings.readerBodyCjkFontPath[0] != '\0') ? appSettings.readerBodyCjkFontPath : kDefaultReaderBodyFontPath;
    if (cjkFont.begin(path)) {
      readerScreen.setContentFont(&cjkFont);
      liveTextScreen.setContentFont(&cjkFont);
      Serial.printf("[X3FW] reader body font: %s\n", path);
      return;
    }
    Serial.printf("[X3FW] reader body font not found at %s (falling back to ASCII)\n", path);
  }

  readerScreen.setContentFont(nullptr);
  liveTextScreen.setContentFont(nullptr);
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
  bluetoothScreen.setBatteryPercent(percent);
  bluetoothScreen.setBatteryCharging(charging);
  folderSyncScreen.setBatteryPercent(percent);
  folderSyncScreen.setBatteryCharging(charging);
  liveTextScreen.setBatteryPercent(percent);
  liveTextScreen.setBatteryCharging(charging);
  standbyScreen.setBatteryPercent(percent);
  standbyScreen.setBatteryCharging(charging);
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
  // ディープスリープからの復帰(または万一ホールドが残っているケース)に備え、
  // HIGH再設定の直後にホールドを解除する(PowerManager.h参照。解除してから
  // 再設定すると、解除の瞬間にピンの電圧が不定になりうるため順序が重要)。
  PowerManager::releaseGpioHoldOnBoot(BATTERY_LATCH_PIN);

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

  // BLEのGATTサーバー構築はSDアクセスを伴わないため初期化順序に制約はないが、
  // アドバタイズ自体はBluetoothScreen/FolderSyncScreenを開いている間だけ行う
  // (startAdvertising()/stopAdvertising()、main.cppのloop()側の画面遷移分岐参照)。
  if (bleTransfer.begin()) {
    Serial.printf("[X3FW] BLE ready: %s\n", bleTransfer.deviceName().c_str());
  } else {
    Serial.println("[X3FW] BLE init failed");
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
  // ヒープ逼迫の切り分け用の一時的な診断ログ(起動完了時点のベースライン空きヒープ)。
  Serial.printf("[X3FW] heap: setup complete, free=%u\n", ESP.getFreeHeap());
}

void loop() {
  checkUsbConnectionChange();

  // BLEコールバックが溜めた受信データ・コマンドをここで処理する(SD書き込みは
  // 必ずloop()側で行い、BLEスタックのタスクとSPIバスを取り合わないようにする。
  // BleTransferService.hのクラスコメント参照)。画面がBluetooth/FolderSync以外でも
  // アドバタイズしていなければ何も届かないため、常時呼んでおいて問題ない。
  bleTransfer.update();

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
      if (input.getHeldTime() >= appSettings.longPressMs) {
        effectiveButton = (b == InputManager::BTN_UP) ? InputManager::BTN_CONFIRM : InputManager::BTN_BACK;
      }
    } else if (b == InputManager::BTN_LEFT && activeScreen == ActiveScreen::kReader &&
               !readerScreen.isOverlayShown()) {
      // 読書画面(オーバーレイ非表示時)ではLEFTを「短押し=ブックマーク追加、
      // 長押し=ブックマーク一覧を開く」に割り当てる(TxtReaderScreen.hのクラス
      // コメント参照)。UP/DOWNと同じ考え方でwasReleased()+getHeldTime()を使う。
      // 通常のhandleButton()ディスパッチは経由せず、ここで直接呼んで次のボタンへ
      // 進む(オーバーレイ表示中はこの分岐に入らないため、LEFTは下のelse節経由で
      // 通常通りhandleButton()に渡り、フォーカス移動等に使われる)。
      if (!input.wasReleased(b)) continue;
      if (input.getHeldTime() >= appSettings.longPressMs) {
        readerScreen.openBookmarkList();
        if (Serial) Serial.println("[X3FW] bookmark list opened (LEFT long-press)");
      } else {
        readerScreen.addBookmark();
        if (Serial) Serial.println("[X3FW] bookmark added (LEFT short-press)");
      }
      safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
      continue;
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

    // 待機画面で画像表示中にCONFIRMされると、通常のScreenAction経由ではなく
    // このフラグでディープスリープ突入が要求される(PowerManager::
    // enterDeepSleepStandby()は戻らない関数のため、StandbyScreen側では完結
    // させられない。StandbyScreen.h参照)。
    if (activeScreen == ActiveScreen::kStandby && standbyScreen.consumeSleepRequested()) {
      if (Serial) Serial.println("[X3FW] entering deep sleep standby");
      // E-inkパネル自体もアナログ電源・クロックを落とし、コントローラICを
      // 低消費電力モードへ入れておく(EInkDisplay::deepSleep()参照)。これを
      // 呼ばないと、ESP32コア自体はディープスリープに入っていてもパネルの
      // コントローラが通常のアクティブ状態のまま給電され続け、バッテリーを
      // 消費し続けてしまう(実機で「2時間で12%減る」異常消費として確認された
      // 不具合、真のディープスリープなら数十µAのはずが数十mA相当だった)。
      display.deepSleep();
      PowerManager::enterDeepSleepStandby(BATTERY_LATCH_PIN, InputManager::POWER_BUTTON_PIN);
    }

    if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kHome) {
      if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kSettings) {
        activeScreen = ActiveScreen::kSettings;
      } else if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kBluetooth) {
        activeScreen = ActiveScreen::kBluetooth;
        bluetoothEnteredFromHome = true;
        bleTransfer.clearError();  // 前回のエラー表示を持ち越さない
        bleTransfer.startAdvertising();
      } else if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kStandby) {
        activeScreen = ActiveScreen::kStandby;
        // ヒープ逼迫の切り分け用の一時的な診断ログ(待機画面での写真表示後に
        // ヒープが逼迫しファイルを開けなくなる不具合の調査のため)。
        if (Serial) Serial.printf("[X3FW] heap: entering standby, free=%u\n", ESP.getFreeHeap());
        standbyScreen.onEnter();
      } else if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kLiveText) {
        activeScreen = ActiveScreen::kLiveText;
        // LiveTextは常に単一の固定パス(LiveTextScreen::kDefaultPath、PC側アプリの
        // 保存先と一致)だけを扱う一時ファイルとして割り切った設計にしたため、
        // 「最後に開いたパス」を覚えておく必要はなく、無条件にこのパスを開く。
        liveTextScreen.openFile(LiveTextScreen::kDefaultPath);
      } else {
        activeScreen = ActiveScreen::kFolder;
        folderEnteredFromSettings = false;
        folderScreen.setRoot("/User");
        folderScreen.resetToRoot();
      }
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kSettings) {
      if (settingsScreen.lastNavigateTarget() == SettingsScreen::NavigateTarget::kBluetooth) {
        activeScreen = ActiveScreen::kBluetooth;
        bluetoothEnteredFromHome = false;
        bleTransfer.clearError();  // 前回のエラー表示を持ち越さない
        bleTransfer.startAdvertising();
      } else if (settingsScreen.lastNavigateTarget() == SettingsScreen::NavigateTarget::kSystemFolder) {
        activeScreen = ActiveScreen::kFolder;
        folderEnteredFromSettings = true;
        folderScreen.setRoot("/System");
        folderScreen.resetToRoot();
      } else {
        activeScreen = ActiveScreen::kFolderSync;
        bleTransfer.clearError();  // 前回のエラー表示を持ち越さない
        folderSyncScreen.onEnter();
      }
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kOpenHistory && activeScreen == ActiveScreen::kHome) {
      activeScreen = ActiveScreen::kHistory;
      historyScreen.reload();
      safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (action == ScreenAction::kNavigateBack) {
      if (activeScreen == ActiveScreen::kFolder) {
        // Home「FOLDER」("/User")・Settings「SYSTEM」("/System")のどちらから
        // 開いたかで戻り先が変わる(folderEnteredFromSettings参照)。
        activeScreen = folderEnteredFromSettings ? ActiveScreen::kSettings : ActiveScreen::kHome;
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      } else if (activeScreen == ActiveScreen::kSettings || activeScreen == ActiveScreen::kHistory ||
                 activeScreen == ActiveScreen::kStandby) {
        // ヒープ逼迫の切り分け用の一時的な診断ログ(entering standbyのログと対にして、
        // 待機画面滞在中にヒープがどれだけ減った/戻ったかを見る)。
        if (Serial && activeScreen == ActiveScreen::kStandby) {
          Serial.printf("[X3FW] heap: leaving standby, free=%u\n", ESP.getFreeHeap());
        }
        activeScreen = ActiveScreen::kHome;
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      } else if (activeScreen == ActiveScreen::kLiveText) {
        liveTextScreen.closeFile();
        activeScreen = ActiveScreen::kHome;
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      } else if (activeScreen == ActiveScreen::kBluetooth || activeScreen == ActiveScreen::kFolderSync) {
        // kFolderSyncは常にSettingsScreenから開くため設定画面へ戻す。kBluetoothは
        // HomeScreenからも開けるため、開いた場所(bluetoothEnteredFromHome)へ戻す。
        if (activeScreen == ActiveScreen::kBluetooth) bleTransfer.stopAdvertising();
        activeScreen = (activeScreen == ActiveScreen::kBluetooth && bluetoothEnteredFromHome)
                           ? ActiveScreen::kHome
                           : ActiveScreen::kSettings;
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
      } else if (activeScreen == ActiveScreen::kStandby && standbyScreen.isShowingImage()) {
        // 一覧表示から画像表示への遷移。4階調グレースケール表示は「まず白黒
        // ベース画像を実際にパネルへ書き込み、続けてグレー階調を上乗せする」
        // 2段階のディスプレイ書き込みが必須(EInkDisplay/README.md参照)で、
        // 通常のrender()→displayBuffer()1回きりのパイプラインでは実現できない
        // ため、safeRenderAndRefresh()を経由せずStandbyScreen側で完結する
        // showImageGrayscale()を直接呼ぶ(電源不安定期間中はsafeRenderAndRefresh()
        // と同じ理由で保留する)。
        if (isPowerUnstable()) {
          pendingRedrawAfterSettle = true;
        } else {
          standbyScreen.showImageGrayscale();
        }
      } else {
        // 部分更新(FAST_REFRESH)でちらつきを抑えて書き換える
        safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
      }
    }
  }

  // FolderScreenでのLEFT/RIGHT長押し中の自動連続ナビゲーション(lastContinuousNavMs・
  // skipContinuousNavCheckOnce宣言部のコメント参照)。上のボタン処理forループとは
  // 独立に、押されている「レベル」を直接見る。
  //
  // 重要な注意点: InputManager::isPressed()/getHeldTime()は「直近のupdate()時点の
  // スナップショット」であり、update()は毎loop()の先頭で1回しか呼ばれない。上の
  // forループでLEFT/RIGHTの押下エッジ(wasPressed)がまさに処理された場合、そこから
  // safeRenderAndRefresh()が数百ms(FAST_REFRESH)ブロッキングするが、その間
  // update()は呼ばれないため、ブロック終了直後にここでisPressed()/getHeldTime()を
  // 見ると「ブロックしていた時間がそのまま経過時間に加算された、古いスナップ
  // ショット」を見てしまう。さらにInputManager側のデバウンスは「状態が変化した」
  // ことを検知したupdate()呼び出しそのものでは確定させず、次のupdate()呼び出しで
  // 確定させる作りのため、ブロッキング明け直後の1周期だけをスキップしても、
  // その次の1周期でまだ「離されたことが反映されていない」ことがあり、実機で
  // 「1回のタップで2回進む」不具合として残っていた。
  // 対策: LEFT/RIGHTの押下エッジを処理した直後、および連続ナビ自体がリフレッシュを
  // 行った直後は、次の1周期分だけ判定を丸ごと見送る(skipContinuousNavCheckOnceで
  // 持ち越す)。
  const bool leftRightJustPressedThisLoop =
      input.wasPressed(InputManager::BTN_LEFT) || input.wasPressed(InputManager::BTN_RIGHT);
  const bool skipThisTick = leftRightJustPressedThisLoop || skipContinuousNavCheckOnce;
  skipContinuousNavCheckOnce = leftRightJustPressedThisLoop;

  if (activeScreen == ActiveScreen::kFolder && !skipThisTick) {
    uint8_t continuousBtn = 0xFF;
    if (input.isPressed(InputManager::BTN_RIGHT)) {
      continuousBtn = InputManager::BTN_RIGHT;
    } else if (input.isPressed(InputManager::BTN_LEFT)) {
      continuousBtn = InputManager::BTN_LEFT;
    }
    if (continuousBtn != 0xFF && input.getHeldTime() >= kContinuousNavStartMs &&
        millis() - lastContinuousNavMs >= kContinuousNavIntervalMs) {
      lastContinuousNavMs = millis();
      const ScreenAction action = folderScreen.handleButton(continuousBtn);
      if (action == ScreenAction::kRedraw) {
        safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
        skipContinuousNavCheckOnce = true;  // このリフレッシュ直後の1周期も同様に見送る
      }
    } else if (continuousBtn == 0xFF) {
      lastContinuousNavMs = 0;  // 離されたら次の長押し判定に備えてリセット
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
      // StandbyScreenが画像表示中は再描画をスキップする。render()は画像表示中、
      // 初回描画後は何もせずreturnするだけの実装のため、ここでrenderAndRefresh()を
      // 呼ぶとdisplay.clearScreen()でframebufferが白紙化されたまま(画像が
      // 再描画されないまま)パネルに送られ、写真が消えて真っ白になってしまう
      // (実機で確認した不具合)。ホーム等へ戻れば最新のバッテリー状態は
      // 反映済みなので、表示だけ次の機会まで遅延させても実害はない。
      const bool standbyShowingImage = activeScreen == ActiveScreen::kStandby && standbyScreen.isShowingImage();
      if (!standbyShowingImage) {
        safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
      }
    }
  }

  if (millis() - lastClockCheckMs >= kClockCheckIntervalMs) {
    lastClockCheckMs = millis();
    updateStatusBarClock();
  }

  // Bluetooth/FolderSync画面はボタン入力を伴わずに状態が変わる(接続/切断・
  // ファイル受信の進捗)ため、バッテリー・時計と同様に一定間隔でポーリングし、
  // 変化があった場合のみ再描画する。
  if ((activeScreen == ActiveScreen::kBluetooth || activeScreen == ActiveScreen::kFolderSync) &&
      millis() - lastBleUiCheckMs >= kBleUiCheckIntervalMs) {
    lastBleUiCheckMs = millis();
    const bool changed = (activeScreen == ActiveScreen::kFolderSync) ? folderSyncScreen.pollUpdates()
                                                                     : bluetoothScreen.pollUpdates();
    if (changed) safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
  }

  if (activeScreen == ActiveScreen::kLiveText && millis() - lastLiveTextCheckMs >= kBleUiCheckIntervalMs) {
    lastLiveTextCheckMs = millis();
    if (liveTextScreen.pollForUpdate()) {
      const EInkDisplay::RefreshMode mode =
          liveTextScreen.consumeNeedsFullRefresh() ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH;
      safeRenderAndRefresh(mode);
    }
  }

  delay(10);
}
