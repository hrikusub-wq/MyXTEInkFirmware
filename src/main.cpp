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
#include "core/MdImageReaderService.h"
#include "core/SettingsService.h"
#include "core/TxtReaderService.h"
#include "gfx/CjkFontImpl.h"
#include "gfx/FrameBufferOps.h"
#include "gfx/MiniFontImpl.h"
#include "gfx/Wallpaper.h"
#include "gfx/XteinkBinFontImpl.h"
#include "screens/BluetoothScreen.h"
#include "screens/FolderScreen.h"
#include "screens/FolderSyncScreen.h"
#include "screens/HistoryScreen.h"
#include "screens/HomeScreen.h"
#include "screens/LiveTextScreen.h"
#include "screens/MdImageReaderScreen.h"
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

// 方向ボタン(LEFT/RIGHT/UP/DOWN)長押し中、E-inkのリフレッシュ待ちのたびに
// ボタンを押し直さなくても自動的にフォーカス/ページが進み続けるようにする
// (参考実装crosspoint-readerのButtonNavigator::onContinuous()と同じ考え方)。
// isPressed()は押されている「間」ずっとtrueを返すレベル判定なので、リフレッシュで
// loop()がブロックされていた間の一瞬の押下エッジ(wasPressed())を取りこぼす問題を
// 回避できる: 押しっぱなしにしている限り、リフレッシュ明けに再開した時点でも
// まだ「押されている」ことが分かる。以前はFolderScreen+LEFT/RIGHTのみだったが、
// 「もっと軽い操作感にしてほしい・サイドボタン(UP/DOWN)での連打が遅い」という
// フィードバックを受け、全画面・4方向ボタンに拡張した(UP/DOWNはこれと同時に
// 長押しCONFIRM/BACKショートカットを廃止し純粋な移動キーへ変更したため、
// 対象にできるようになった。main.cppのボタン処理forループ参照)。
unsigned long lastContinuousNavMs = 0;
constexpr unsigned long kContinuousNavStartMs = 400;     // 長押しとみなすまでの時間
constexpr unsigned long kContinuousNavIntervalMs = 100;  // 連続発火の最小間隔

// フォーカス移動等の単純な部分更新(FAST_REFRESH)は数百msブロッキングし、その間
// input.update()が呼ばれない(E-inkの物理特性上、書き込み中はSPIバスも占有されて
// おりボタンを読みに行けない)。そのため素早く連打すると、最初の1回だけが処理され
// 残りのタップは「ブロック中に押して離された」ことがまるごと見えなくなり
// 取りこぼされていた(フォルダ探索で連打しても思った通りに進まない不具合)。
// 各画面のhandleButton()はフォーカス位置の更新自体をレンダリングとは独立に即座に
// 行っているため、実際のE-ink書き込みだけを短時間デバウンスし、無操作期間
// (kListRedrawDebounceMs)が空いてから最新の状態を1回だけ描画するようにした。
// これによりloop()自体はブロックされたままにならず、連打の全タップを取りこぼさず
// モデル(フォーカス位置)に反映できる(CrossPointJPのような「連打しても正しく
// 選択できる」挙動を狙った対策。画面遷移相当のFULL_REFRESHは元々頻度が低く
// 連打の対象にならないため対象外、下記kRedraw分岐のコメント参照)。
bool pendingListRedraw = false;
unsigned long pendingListRedrawFirstMs = 0;
unsigned long lastListInputMs = 0;
constexpr unsigned long kListRedrawDebounceMs = 60;  // これだけ操作が無ければ描画を実行する
constexpr unsigned long kListRedrawMaxWaitMs = 220;  // 連打が続いても最悪ここまで待てば描画する

// InputManagerのgetHeldTime()はボタン個別ではなく「直近に何らかのボタンが
// 押されてから離されるまでの経過時間」というグローバルな値であり、isPressed()も
// 直近のupdate()呼び出し時点のスナップショットでしかない。何らかのブロッキング
// 描画(FAST_REFRESH/FULL_REFRESH、数百ms)を挟むと、その間update()が呼ばれない
// ため、ブロック終了直後にisPressed()/getHeldTime()を見ると「ブロックしていた
// 時間がそのまま経過時間に加算された、古いスナップショット」を見てしまう。
//
// さらに厄介なのは、InputManager側のデバウンス(DEBOUNCE_DELAY=5ms)は「状態が
// 変化した」ことを検知したupdate()呼び出しそのものでは確定させず、次のupdate()
// 呼び出しで確定させる作りな点(InputManager.cpp参照): ブロック明け最初の
// update()は「実世界の状態が変わった」ことを検知してdebounceタイマーをリセット
// するだけで終わり、currentStateはまだ古いまま。その次のupdate()(loop()の
// delay(10)を挟むためブロック明けから最短でも約20ms後)でようやくdebounceの
// 猶予(5ms)を満たしcurrentStateが実際の状態に更新される。つまり「ブロック
// 明け直後の1周期だけスキップする」だけでは足りず、実機で「単押しなのに2回分
// 進む」不具合として残っていた(1周期分のスキップだと、まだ確定していない
// 古いスナップショットのままgetHeldTime()だけが大きくなった状態を拾ってしまう)。
//
// 対策: 個別の呼び出し箇所ごとにフラグを立てる方式(以前の実装、漏れが起きやすい)
// ではなく、`renderAndRefresh()`が実際にブロッキング描画を行うたびに
// `lastBlockingRefreshMs`を更新するようにし、そこから`kPostBlockSettleMs`
// (デバウンス確認に必要な最短時間に余裕を持たせた値)が経過するまでは、
// どのブロッキング描画がきっかけであっても連続ナビの判定自体を一律で見送る。
unsigned long lastBlockingRefreshMs = 0;
constexpr unsigned long kPostBlockSettleMs = 40;

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

// UP/DOWN(側面ボタン)は以前、短押しでその画面本来の意味(フォーカス移動等)、
// 長押し(appSettings.longPressMs以上)でCONFIRM/BACKのショートカットとして働いて
// いた(片手操作用のフィードバックで追加した機能)。しかし「サイドボタンでの
// 連打が遅い」というフィードバックを受け、UP/DOWNへ他の方向ボタンと同じ
// 押しっぱなし連続ナビ(kContinuousNavStartMs等参照)を適用することにした際、
// 同じ「長押し」という操作を連続ナビのCONFIRM/BACKショートカットと取り合って
// しまう(特にDOWNは「連続スクロールしながら長押しを続けると、離した瞬間に
// BACKでフォルダごと抜けてしまう」という分かりにくい組み合わせになる)ため、
// UP/DOWNは純粋な移動キーに戻し、このショートカット自体を廃止した。
// appSettings.longPressMsは引き続きTxtReaderScreen読書中のLEFT(短押し=
// ブックマーク追加、長押し=一覧表示)の判定に使われている(main.cppのボタン
// 処理forループ参照)。

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

HomeScreen homeScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, battery, rtc, appSettings);
FolderScreen folderScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, fileBrowser);
TxtReaderScreen readerScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, appSettings);
MdImageReaderScreen mdImageReaderScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
SettingsScreen settingsScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, rtc, battery, fileBrowser, appSettings);
HistoryScreen historyScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font);
BluetoothScreen bluetoothScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, bleTransfer);
FolderSyncScreen folderSyncScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, bleTransfer);
LiveTextScreen liveTextScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, bleTransfer);
StandbyScreen standbyScreen(LOGICAL_WIDTH, LOGICAL_HEIGHT, font, fileBrowser, display, appSettings, battery, rtc);

// 画面はまだホーム/フォルダ/読書/設定/履歴/Bluetooth/フォルダ同期/Liveテキスト/
// 待機の9つしかないため、専用のScreenManager(スタック)は作らずここで素直に
// 切り替える。画面が増えて管理が煩雑になったら導入を検討する。
enum class ActiveScreen {
  kHome,
  kFolder,
  kReader,
  kMdImageReader,
  kSettings,
  kHistory,
  kBluetooth,
  kFolderSync,
  kLiveText,
  kStandby
};
ActiveScreen activeScreen = ActiveScreen::kHome;
// kBluetoothはHome/Settingsのどちらからでも開けるため、BACKで戻る先を覚えておく
ActiveScreen bluetoothReturnScreen = ActiveScreen::kHome;
ActiveScreen liveTextReturnScreen = ActiveScreen::kHome;
unsigned long lastAutoStandbyActivityMs = 0;
unsigned long lastStandbyImageActivityMs = 0;
constexpr unsigned long kStandbyAutoSleepMs = 2UL * 60UL * 1000UL;  // 2分
// kFolderも同様に2箇所から開ける(Home「FOLDER」="/User"、Settings「SYSTEM」=
// "/System")。trueならSettingsから開いた(BACKでSettingsへ戻す)、falseなら
// Homeから開いた(BACKでHomeへ戻す)。
bool folderEnteredFromSettings = false;

Screen& currentScreen() {
  switch (activeScreen) {
    case ActiveScreen::kFolder: return folderScreen;
    case ActiveScreen::kReader: return readerScreen;
    case ActiveScreen::kMdImageReader: return mdImageReaderScreen;
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

// pathが.md/.markdownで、対応する画像キャッシュ(PC側コンパニオンツールが
// 事前レンダリングした"/User/.md_cache/<name>/index.bin")が有効かどうかを
// 判定する。kOpenFile分岐がこれを見て、MdImageReaderScreen/TxtReaderScreenの
// どちらを開くか決める(無効ならTxtReaderScreenの生テキスト表示へ自動的に
// フォールバックする)。
bool shouldOpenAsMdImage(const String& path) {
  String lower = path;
  lower.toLowerCase();
  if (!lower.endsWith(".md") && !lower.endsWith(".markdown")) return false;
  return MdImageReaderService::hasValidImageCache(path);
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

  if (Serial) {
    Serial.printf("[X3FW] render: free_heap=%lu\n", (unsigned long)ESP.getFreeHeap());
  }

  display.displayBuffer(mode);
  // このブロッキング呼び出しが実際に終わった時刻を記録する(lastBlockingRefreshMs
  // 宣言部のコメント参照)。個別の呼び出し箇所ごとにフラグを立てる方式だと
  // 対策漏れが起きやすいため、実際にブロッキング描画が起きる場所はここ1箇所に
  // 集約し、ここで一律に記録することで連続ナビ側の判定を安全にする。
  lastBlockingRefreshMs = millis();
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
  lastAutoStandbyActivityMs = millis();
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
    initWallpaper();
    
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

  // begin()はデバイス名の計算だけを行う軽量な処理で、NimBLEDevice::init()自体は
  // 呼ばない(ヒープを恒常的に数十KB規模で消費するため、Bluetooth/FolderSync/
  // LiveTextのいずれかの画面を実際に開くまで遅延させる。BleTransferService::
  // ensureStackReady()参照)。
  bleTransfer.begin();
  Serial.printf("[X3FW] BLE device name: %s (stack init deferred until first use)\n",
                bleTransfer.deviceName().c_str());

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

  if (appSettings.rtcEnabled) {
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
  } else {
    Serial.println("[X3FW] RTC disabled by settings");
  }


  renderAndRefresh(EInkDisplay::FULL_REFRESH);
  Serial.println("[X3FW] initial frame drawn");
  // ヒープ逼迫の切り分け用の一時的な診断ログ(起動完了時点のベースライン空きヒープ)。
  Serial.printf("[X3FW] heap: setup complete, free=%u\n", ESP.getFreeHeap());
}

void enterStandbyDeepSleep() {
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

void loop() {

  // スタンバイで写真が表示されたまま2分操作が無ければ、CONFIRMを押したのと同じ
  // 扱いで自動的にディープスリープへ移行する(E-inkは電源を切っても表示を保持
  // するため、画面の見た目は変わらない)。AutoStandby・電源キー長押しは写真の
  // 自動表示までしか行わないため、これが無いと「表示したまま朝までCPUがフル
  // 稼働し続ける」放置シナリオが再発する(バッテリー消費相談の原因調査を参照)。
  if (activeScreen == ActiveScreen::kStandby && standbyScreen.isShowingImage() &&
      millis() - lastStandbyImageActivityMs >= kStandbyAutoSleepMs) {
    enterStandbyDeepSleep();
  }

  // 5分無操作でAutoStandby (ホーム画面限定)
  if (activeScreen == ActiveScreen::kHome) {
    if (millis() - lastAutoStandbyActivityMs > 5 * 60 * 1000) {
      if (Serial) Serial.println("[X3FW] AutoStandby triggered due to 5 min inactivity");
      if (standbyScreen.enterQuickRandom()) {
        activeScreen = ActiveScreen::kStandby;
        // showImage()を経由させる(LOADING表示→デコード→turnOffScreen=trueで
        // パネルのアナログ電源を明示的に落とす、までの一連の処理)。ここを
        // safeRenderAndRefresh()直呼びにすると、そのフォールバック経路の
        // render()ではturnOffScreen=trueが実行されず、パネルへの通電が
        // 落ちないまま(実機で「2時間で12%消費」と確認された不具合と同じ経路)
        // 表示され続けてしまう。
        if (isPowerUnstable()) {
          pendingRedrawAfterSettle = true;
        } else {
          standbyScreen.showImage();
          lastStandbyImageActivityMs = millis();
        }
      }
      lastAutoStandbyActivityMs = millis();
    }
  }

  checkUsbConnectionChange();

  // BLEコールバックが溜めた受信データ・コマンドをここで処理する(SD書き込みは
  // 必ずloop()側で行い、BLEスタックのタスクとSPIバスを取り合わないようにする。
  // BleTransferService.hのクラスコメント参照)。画面がBluetooth/FolderSync以外でも
  // アドバタイズしていなければ何も届かないため、常時呼んでおいて問題ない。
  bleTransfer.update();

  if (pendingRedrawAfterSettle && !isPowerUnstable()) {
    pendingRedrawAfterSettle = false;
    // StandbyScreenが画像表示中はrenderAndRefresh()を呼んではいけない
    // (kBatteryCheckIntervalMs分岐の同種のコメント参照)。render()は画像表示中、
    // 初回描画後は何もせずreturnするだけの実装のため、renderAndRefresh()内の
    // display.clearScreen()で白紙化されたフレームバッファがそのままパネルに
    // 送られ、表示中の写真が消えてしまう(USB接続状態の変化(電源不安定判定)が
    // この保留を経由して起きた場合に発生する不具合として実機で確認された)。
    const bool standbyShowingImage = activeScreen == ActiveScreen::kStandby && standbyScreen.isShowingImage();
    if (!standbyShowingImage) {
      renderAndRefresh(EInkDisplay::FULL_REFRESH);
    } else if (!standbyScreen.isImageDrawn()) {
      // mode_がkShowingImageになった直後、電源不安定でshowImage()自体の呼び出しを
      // 保留していたケース。isShowingImage()だけでは「未描画のまま保留」と
      // 「既に描画済み」を区別できず、後者だと誤判定してshowImage()が永久に
      // 呼ばれなくなる(画面が真っ白/前の画面のまま固まる)不具合があったため、
      // isImageDrawn()も合わせて見て、未描画なら保留していた描画をここで行う。
      standbyScreen.showImage();
    } else if (Serial) {
      // 切り分け用の診断ログ(待機画面の写真表示中に電源不安定判定の保留描画が
      // スキップされたことを確認するため)。
      Serial.println("[X3FW] standby: skipped pendingRedrawAfterSettle while showing image");
    }
  }

  input.update();

  for (uint8_t b = 0; b <= InputManager::BTN_POWER; b++) {
    if (b == InputManager::BTN_POWER) {
      if (!input.wasReleased(b)) continue;
      if (input.getHeldTime() >= appSettings.longPressMs) {
        if (activeScreen == ActiveScreen::kStandby) {
          activeScreen = ActiveScreen::kHome;
          safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
        } else {
          // Bluetooth/FolderSync画面から離れる場合はアドバタイズを止める
          // (通常のBACK処理と同様、動いたまま他画面へ遷移しないようにする)。
          if (activeScreen == ActiveScreen::kBluetooth || activeScreen == ActiveScreen::kFolderSync) {
            bleTransfer.stopAdvertising();
          }
          if (standbyScreen.enterQuickRandom()) {
            activeScreen = ActiveScreen::kStandby;
            // showImage()を経由させる(AutoStandby側の同種のコメント参照。
            // safeRenderAndRefresh()直呼びだとパネルの電源断が実行されない)。
            if (isPowerUnstable()) {
              pendingRedrawAfterSettle = true;
            } else {
              standbyScreen.showImage();
              lastStandbyImageActivityMs = millis();
            }
          }
          // /System/standbyに画像が1枚も無い場合は何もしない(現在の画面のまま)。
        }
      } else {
        if (activeScreen == ActiveScreen::kStandby) {
          activeScreen = ActiveScreen::kHome;
          safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
        } else if (activeScreen != ActiveScreen::kBluetooth) {
          bluetoothReturnScreen = activeScreen;
          activeScreen = ActiveScreen::kBluetooth;
          bleTransfer.clearError();
          bleTransfer.startAdvertising();
          safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
        }
      }
      lastAutoStandbyActivityMs = millis();
      continue;
    }

    const bool onReaderNoOverlay = activeScreen == ActiveScreen::kReader && !readerScreen.isOverlayShown();
    const bool onMdImageReaderNoOverlay =
        activeScreen == ActiveScreen::kMdImageReader && !mdImageReaderScreen.isOverlayShown();
    if (b == InputManager::BTN_LEFT && (onReaderNoOverlay || onMdImageReaderNoOverlay)) {
      // 読書画面(オーバーレイ非表示時)ではLEFTを「短押し=ブックマーク追加、
      // 長押し=ブックマーク一覧を開く」に割り当てる(TxtReaderScreen.hのクラス
      // コメント参照。MdImageReaderScreenも同じ割り当て)。通常のhandleButton()
      // ディスパッチは経由せず、ここで直接呼んで次のボタンへ進む(オーバーレイ
      // 表示中はこの分岐に入らないため、LEFTは下のelse節経由で通常通り
      // handleButton()に渡り、フォーカス移動等に使われる)。UP/DOWNはかつて同じ
      // 短押し/長押しの仕組みでCONFIRM/BACKのショートカットとして働いていたが、
      // 「サイドボタンでの連打が遅い」というフィードバックを受けて純粋な移動キーに
      // 戻したため、この特殊扱いはLEFTのブックマーク機能にのみ残っている。
      if (!input.wasReleased(b)) continue;
      const bool longPress = input.getHeldTime() >= appSettings.longPressMs;
      if (onReaderNoOverlay) {
        if (longPress) {
          readerScreen.openBookmarkList();
          if (Serial) Serial.println("[X3FW] bookmark list opened (LEFT long-press)");
        } else {
          readerScreen.addBookmark();
          if (Serial) Serial.println("[X3FW] bookmark added (LEFT short-press)");
        }
      } else {
        if (longPress) {
          mdImageReaderScreen.openBookmarkList();
          if (Serial) Serial.println("[X3FW] md image bookmark list opened (LEFT long-press)");
        } else {
          mdImageReaderScreen.addBookmark();
          if (Serial) Serial.println("[X3FW] md image bookmark added (LEFT short-press)");
        }
      }
      safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
      continue;
    }
    if (!input.wasPressed(b)) continue;

    if (Serial) Serial.printf("[X3FW] button pressed: %s (index=%u)\n", InputManager::getButtonName(b), b);

    lastListInputMs = millis();  // 下記kRedrawデバウンスの「無操作期間」判定用
    lastAutoStandbyActivityMs = millis(); // 無操作タイマーリセット
    lastStandbyImageActivityMs = millis(); // スタンバイ画像表示中のタイムアウトリセット

    const ScreenAction action = currentScreen().handleButton(b);

    // kRedraw以外のアクションは画面遷移等それ自体が同期的にFULL_REFRESHするため、
    // 保留中だった部分更新(pendingListRedraw)はもう意味が無く破棄する(そのまま
    // 残すと、画面遷移後に無関係な1回分のFAST_REFRESHが余分に走ってしまう)。
    if (action != ScreenAction::kRedraw && action != ScreenAction::kNone) {
      pendingListRedraw = false;
    }

    // 待機画面で画像表示中にCONFIRMされると、通常のScreenAction経由ではなく
    // このフラグでディープスリープ突入が要求される(PowerManager::
    // enterDeepSleepStandby()は戻らない関数のため、StandbyScreen側では完結
    // させられない。StandbyScreen.h参照)。
    if (activeScreen == ActiveScreen::kStandby && standbyScreen.consumeSleepRequested()) {
      enterStandbyDeepSleep();
    }

    if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kHome) {
      if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kSettings) {
        activeScreen = ActiveScreen::kSettings;
      } else if (homeScreen.lastActivatedButton() == HomeScreen::GridButton::kBluetooth) {
        bluetoothReturnScreen = ActiveScreen::kHome;
        activeScreen = ActiveScreen::kBluetooth;
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
        liveTextReturnScreen = ActiveScreen::kHome;
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
        bluetoothReturnScreen = ActiveScreen::kSettings;
        activeScreen = ActiveScreen::kBluetooth;
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
    } else if (action == ScreenAction::kNavigateForward && activeScreen == ActiveScreen::kBluetooth) {
      activeScreen = ActiveScreen::kLiveText;
      liveTextReturnScreen = ActiveScreen::kBluetooth;
      liveTextScreen.openFile(LiveTextScreen::kDefaultPath);
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
        activeScreen = liveTextReturnScreen;
        safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
      } else if (activeScreen == ActiveScreen::kBluetooth || activeScreen == ActiveScreen::kFolderSync) {
        if (activeScreen == ActiveScreen::kBluetooth) bleTransfer.stopAdvertising();
        activeScreen = (activeScreen == ActiveScreen::kBluetooth)
                           ? bluetoothReturnScreen
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
      } else if (activeScreen == ActiveScreen::kMdImageReader) {
        activeScreen = ActiveScreen::kHome;
        // MdImageReaderServiceもTxtReaderServiceと同じlast_book.txtを更新するため、
        // 既存のreadLastBook()がそのまま使える(HomeScreen側の変更は不要)。
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
      if (pathToOpen.length() > 0) {
        bool opened = false;
        // .md/.markdownで有効な画像キャッシュ(PC側で事前レンダリング済み)が
        // あれば画像ページ表示画面を優先する。無い場合、または万一openFile()自体が
        // 失敗した場合(ページファイル破損等)は、常にTxtReaderScreen(生テキスト
        // 表示)へフォールバックする。
        if (shouldOpenAsMdImage(pathToOpen)) {
          opened = mdImageReaderScreen.openFile(pathToOpen);
          if (opened) activeScreen = ActiveScreen::kMdImageReader;
        }
        if (!opened) {
          opened = readerScreen.openFile(pathToOpen);
          if (opened) activeScreen = ActiveScreen::kReader;
        }
        if (opened) {
          safeRenderAndRefresh(EInkDisplay::FULL_REFRESH);
        } else {
          // TxtReaderService::open()はヒープ逼迫時にstd::bad_allocを捕まえてfalseを
          // 返す(デバイス全体のクラッシュを避けるための既存の設計、TxtReaderService.cpp
          // 参照)。以前はここで何もしなかったため、ユーザーからは「CONFIRMを押しても
          // 何も起きない」ように見えていた。開けなかったことを画面上でも分かるように
          // し、原因の切り分け用にヒープ残量もログへ残す。
          if (Serial) {
            Serial.printf("[X3FW] openFile failed for \"%s\" (free heap=%u)\n", pathToOpen.c_str(),
                          ESP.getFreeHeap());
          }
          safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
        }
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
        // 一覧表示から画像表示への遷移。デコード前に「LOADING...」を一度表示して
        // から本番の画像を表示する2段階の書き込みが必要で、通常のrender()→
        // displayBuffer()1回きりのパイプラインでは実現できないため、
        // safeRenderAndRefresh()を経由せずStandbyScreen側で完結するshowImage()を
        // 直接呼ぶ(電源不安定期間中はsafeRenderAndRefresh()と同じ理由で保留する)。
        if (isPowerUnstable()) {
          pendingRedrawAfterSettle = true;
        } else {
          standbyScreen.showImage();
          lastStandbyImageActivityMs = millis();
        }
      } else {
        // 部分更新(FAST_REFRESH)は同期的に呼ぶと連打時に後続のタップを取りこぼす
        // (lastContinuousNavMs宣言部近くのpendingListRedrawコメント参照)ため、
        // ここでは即座に描画せず保留する。実際の描画はループ末尾のデバウンス
        // チェックが行う。
        if (!pendingListRedraw) pendingListRedrawFirstMs = millis();
        pendingListRedraw = true;
      }
    }
  }

  // 保留中の部分更新(pendingListRedraw)を、短い無操作期間が空いた時点、または
  // 連打が続いても最悪kListRedrawMaxWaitMs経過した時点で実際に描画する
  // (kListRedrawDebounceMs宣言部のコメント参照)。for文の外に置くことで、
  // ボタン入力が無いloop()の周回でも(=連打が止まった直後も)判定が効く。
  if (pendingListRedraw) {
    const unsigned long now = millis();
    const bool quiet = (now - lastListInputMs) >= kListRedrawDebounceMs;
    const bool waitedTooLong = (now - pendingListRedrawFirstMs) >= kListRedrawMaxWaitMs;
    if (quiet || waitedTooLong) {
      pendingListRedraw = false;
      // StandbyScreenが画像表示中はrenderAndRefresh()を呼んではいけない
      // (kBatteryCheckIntervalMs分岐・pendingRedrawAfterSettle分岐の同種の
      // コメント参照)。直前の方向キー操作で立ったpendingListRedrawが、
      // StandbyScreenの一覧でCONFIRMを押して画像表示へ遷移した後まで消えずに
      // 残ることがある(一覧画面のCONFIRM自体もkRedrawを返すため、680行目付近の
      // 「kRedraw以外なら破棄する」処理では消えない)。画像表示への遷移
      // (showImage())は数百ms〜数秒ブロッキングするため、ループに戻ってきた
      // 時点でこのデバウンス条件(quiet/waitedTooLong)を無条件に満たしてしまい、
      // 白紙化されたフレームバッファがdisplayBuffer(FAST_REFRESH)(電源オフ
      // 引数なし)でそのまま送られ、写真が白紙で上書きされた上にパネルの電源も
      // 再びオンのまま放置される不具合があった(実機で確認、真因はここだった)。
      const bool standbyShowingImage = activeScreen == ActiveScreen::kStandby && standbyScreen.isShowingImage();
      if (!standbyShowingImage) {
        // このFAST_REFRESH自体が数百msブロッキングする(renderAndRefresh()が
        // lastBlockingRefreshMsを更新する、下の連続ナビブロックのコメント参照)。
        safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
      }
    }
  }

  // LEFT/RIGHT/UP/DOWN押しっぱなし中の自動連続ナビゲーション(lastContinuousNavMs・
  // lastBlockingRefreshMs宣言部のコメント参照)。上のボタン処理forループとは
  // 独立に、押されている「レベル」を直接見る。以前はFolderScreen+LEFT/RIGHTのみ
  // だったが、「もっと軽い操作感にしてほしい・サイドボタンでの連打が遅い」という
  // フィードバックを受け、全画面・4方向ボタンすべてに拡張した(UP/DOWNは同じ
  // フィードバックを受けて長押しCONFIRM/BACKショートカットを廃止し純粋な移動
  // キーに戻したため、この対象にできるようになった)。各画面のhandleButton()は
  // 4方向ボタンに対してkRedrawまたはkNone以外を返さないことを確認済み(画面遷移
  // 系のアクションは方向ボタンからは発生しない)。
  //
  // ブロッキング描画直後は古いスナップショットを見てしまう問題(lastBlockingRefreshMs
  // 宣言部のコメント参照)への対策として、直近のブロッキング描画からkPostBlockSettleMs
  // 以内は判定自体を丸ごと見送る。
  const bool recentlyBlocked = (millis() - lastBlockingRefreshMs) < kPostBlockSettleMs;

  // TxtReaderScreen読書中(オーバーレイ非表示)のLEFTだけは、上のボタン処理
  // forループで短押し=ブックマーク追加・長押し=一覧表示という別の意味を持つ
  // (main.cppの該当分岐参照)。この連続ナビがLEFTも対象にしてしまうと、保持中に
  // 「前のページに戻る」が連続発火しつつ同時にブックマーク長押し判定も進むという
  // 二重の意味になってしまうため、この状態でのLEFTだけ対象から除外する。
  const bool leftReservedForBookmark =
      (activeScreen == ActiveScreen::kReader && !readerScreen.isOverlayShown()) ||
      (activeScreen == ActiveScreen::kMdImageReader && !mdImageReaderScreen.isOverlayShown());

  if (!recentlyBlocked) {
    uint8_t continuousBtn = 0xFF;
    if (input.isPressed(InputManager::BTN_RIGHT)) {
      continuousBtn = InputManager::BTN_RIGHT;
    } else if (!leftReservedForBookmark && input.isPressed(InputManager::BTN_LEFT)) {
      continuousBtn = InputManager::BTN_LEFT;
    } else if (input.isPressed(InputManager::BTN_DOWN)) {
      continuousBtn = InputManager::BTN_DOWN;
    } else if (input.isPressed(InputManager::BTN_UP)) {
      continuousBtn = InputManager::BTN_UP;
    }
    if (continuousBtn != 0xFF && input.getHeldTime() >= kContinuousNavStartMs &&
        millis() - lastContinuousNavMs >= kContinuousNavIntervalMs) {
      lastContinuousNavMs = millis();
      const ScreenAction action = currentScreen().handleButton(continuousBtn);
      if (action == ScreenAction::kRedraw) safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
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
  if (appSettings.rtcEnabled && activeScreen == ActiveScreen::kHome) {
    if (millis() - lastClockCheckMs >= kClockCheckIntervalMs) {
      lastClockCheckMs = millis();
      RtcDateTime dt;
      if (rtc.readDateTime(dt)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02u:%02u", dt.hour, dt.minute);
        if (lastClockText != buf) {
          lastClockText = buf;
          safeRenderAndRefresh(EInkDisplay::FAST_REFRESH);
        }
      }
    }
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
