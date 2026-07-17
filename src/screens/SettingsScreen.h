#pragma once
#include <vector>

#include "../core/BatteryService.h"
#include "../core/FileBrowserService.h"
#include "../core/RtcService.h"
#include "../core/SettingsService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"
#include "../ui/StatusBar.h"

// 設定画面。項目は固定のリストで、LEFT/RIGHT・UP/DOWNのどちらでもフォーカス移動
// できる(冗長だが同じ意味。TxtReaderScreenのLEFT/UP=前ページ・RIGHT/DOWN=次ページと
// 同じ考え方)。値の変更・決定は必ずCONFIRM経由に統一している(以前はLEFT/RIGHT=
// フォーカス移動・UP/DOWN=値変更だったが、UP/DOWNもフォーカス移動として使いたい
// というフィードバックを受けて、値変更はCONFIRMに一本化した)。
//
// - TIME: CONFIRMで時刻編集モードに入る(LEFT/RIGHTで年/月/日/時/分を選択、
//   UP/DOWNで増減、CONFIRMで保存してRTCへ書き込む、BACKでキャンセル)。
//   表示・編集する値はタイムゾーンオフセット適用後のローカル時刻。
// - TIMEZONE: CONFIRMでタイムゾーン編集モード(別ウィンドウ)に入る。
//   LEFT/RIGHT(またはUP/DOWN)で-9〜+9の範囲で1時間刻みに変更、CONFIRMで保存、
//   BACKでキャンセル。RTCの生値は変更せず、表示・TIME編集時の変換にのみ使う。
// - CLOCK IN STATUS BAR: CONFIRMでON/OFF切り替え
// - SYSTEM FONT / BOOK FONT: CONFIRMで一覧から選ぶ別ウィンドウ(フォントピッカー、
//   LEFT/RIGHT・UP/DOWNどちらでもフォーカス移動)を開く。一覧は"/fonts"直下の
//   MINIFONT(内蔵)・*.cpfont(CjkFontImpl)・*.bin(XteinkBinFontImpl)を並べたもので、
//   両項目で共有する(FontTarget参照)。SYSTEM FONTはUIチローム全体、BOOK FONTは
//   読書画面の本文のみに反映される、互いに独立した設定(main.cpp側が
//   consumeFontSettingsChanged()を見て実際のフォント差し替え・他画面の再レイアウトを
//   行う)。BOOK FONTを一度も変更していない場合の既定はkDefaultReaderBodyFontPathの
//   .cpfont(従来のハードコードされた読書本文フォントと同じ)。
// - TEXT SIZE: CONFIRMを押すたびにMiniFontImplの拡大率を1→2→3→4→1…と循環させる
// - MARKDOWN: CONFIRMで見出し(HEADING 1/2/3)・箇条書き(LIST)・太字(BOLD)それぞれの
//   .binフォントを個別に選べるサブメニューを開く(2段階: ロール一覧→各ロールの
//   フォントピッカー)。CjkFontImplは含めない(いずれもXteinkBinFontImplの
//   「役割ごとに固定サイズグリフを使い分ける」という用途に絞っているため)。
//   見出しはH1〜H3の3段階まで個別サイズに対応(H4〜H6はH3のフォントを流用、
//   main.cpp側のheadingFontForLevel()相当の仕組み参照)。HEADING 1の選択肢0番目は
//   「DEFAULT」(main.cpp側の既定見出しフォントにフォールバック)、HEADING 2/3の
//   0番目は「OFF」(1つ上のレベルのフォントにカスケード)、LIST/BOLDの0番目も
//   「OFF」(本文と同じフォントで代用、太字は記号除去のみ)。
// - BATTERY: 読み取り専用(残量%・電圧)
// - CLEAR CACHE: CONFIRMで確認オーバーレイ、再度CONFIRMで/.reader_cache/を削除
// - BACK: ホーム画面に戻る
//
// 設定はSettingsService経由でSDへ即時保存する(TxtReaderServiceの進捗保存と同じ
// 「変更のたびに保存」方式)。
class SettingsScreen : public Screen {
 public:
  SettingsScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, RtcService& rtc, BatteryService& battery,
                 FileBrowserService& fileBrowser, AppSettings& settings);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // システムフォントが変更されたときにmain.cpp側から呼ぶ(FolderScreenと同様、
  // 自分自身の行の高さもフォントのlineHeight()に依存するため)。
  void relayout(const Font& font);

  // SDカードの"/fonts"を再スキャンして選択可能な.cpfont一覧を更新する。
  // コンストラクタはグローバルオブジェクトとしてsetup()より前(SDカード初期化前)に
  // 呼ばれるため、SDアクセスを伴うこの処理はコンストラクタに含めず、main.cpp側が
  // fileBrowser.begin()成功後に明示的に呼ぶ。
  void reloadAvailableFonts();

  // SYSTEM FONT/TEXT SIZEが変更された直後に一度だけtrueを返し、以後falseに戻る
  // (main.cpp側が実際のフォント差し替え・他画面のrelayout()を行うためのフラグ)。
  bool consumeFontSettingsChanged();

  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }
  void setBatteryCharging(bool charging) { statusBar_.setBatteryCharging(charging); }

 private:
  enum class ItemKind { kClock, kTimezone, kToggle, kFontCycle, kScaleCycle, kMarkdownMenu, kReadOnly, kAction };

  // SYSTEM FONT/BOOK FONTはフォントピッカーの仕組み(一覧・選択状態)を共有するが、
  // 反映先のAppSettingsフィールドが異なるため、どちらの操作かを区別するのに使う。
  enum class FontTarget { kSystem = 0, kReaderBody = 1 };
  static constexpr int kFontTargetCount = 2;

  // MARKDOWNサブメニューで個別設定できるフォントの役割。見出しはH1〜H3の3段階まで
  // (H4〜H6はH3を流用、TxtReaderService::headingFontForLevel()参照)。
  enum class MarkdownRole { kHeading1 = 0, kHeading2 = 1, kHeading3 = 2, kList = 3, kBold = 4 };
  static constexpr int kMarkdownRoleCount = 5;

  // "/fonts"直下の*.binのうち、ファイル名から幅・高さを解析できたものだけを一覧化する。
  struct BinFontEntry {
    String name;  // ファイル名のみ("/fonts/"は含まない)
    int width = 0;
    int height = 0;
  };

  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;
  static constexpr int kRowPadding = 10;
  static constexpr int kItemCount = 9;

  static ItemKind kindForIndex(int index);
  static const char* labelForIndex(int index);
  static const char* markdownRoleLabel(MarkdownRole role);
  // kFontCycle行(SYSTEM FONT/BOOK FONT)のうち、indexがどちらの対象かを返す。
  static FontTarget fontTargetForIndex(int index);

  void layoutRows(const Font& font);
  void refreshFontList();
  void refreshRowValues();
  void scanForCurrentFontSelection();
  void scanFontSelectionForTarget(FontTarget target, SystemFontKind kind, const char* cjkPath, const char* binPath);
  void scanMarkdownRoleSelections();
  String fontLabelFor(int selectionIndex) const;
  String markdownRoleFontLabelFor(MarkdownRole role, int selectionIndex) const;
  void commitFontSelection(FontTarget target);
  void commitMarkdownRoleSelection(MarkdownRole role);
  void applyScaleDelta(int delta);
  void enterClockEdit();
  void adjustClockField(int delta);
  void commitClockEdit();
  void enterFontPicker(FontTarget target);
  void enterMarkdownMenu();
  void enterMarkdownFontPicker(MarkdownRole role);
  void enterTimezoneEdit();
  void adjustTimezoneDraft(int delta);
  void commitTimezoneEdit();
  void clearReaderCache();
  void drawClockEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawFontPicker(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawMarkdownMenu(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawMarkdownFontPicker(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawTimezoneEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawClearCacheOverlay(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;

  RtcService& rtc_;
  BatteryService& battery_;
  FileBrowserService& fileBrowser_;
  AppSettings& settings_;

  uint16_t fbWidth_;
  uint16_t fbHeight_;

  std::vector<String> availableCjkFonts_;      // "/fonts"直下の*.cpfont(ファイル名のみ)
  std::vector<BinFontEntry> availableBinFonts_;  // "/fonts"直下の*.bin(解析済み幅高さ付き)
  // fontSelectionIndex_[target]: 0=MiniFont、1..M=cpfont、M+1..M+N=bin
  int fontSelectionIndex_[kFontTargetCount] = {0, 0};
  bool fontSettingsChanged_ = false;

  int focusIndex_ = 0;

  bool editingClock_ = false;
  RtcDateTime clockDraft_;
  int clockFieldIndex_ = 0;  // 0=year,1=month,2=day,3=hour,4=minute

  bool editingFont_ = false;
  FontTarget fontPickerTarget_ = FontTarget::kSystem;
  int fontPickerFocusIndex_ = 0;

  // mdRoleSelectionIndex_[role]: 0=OFF(HEADING1のみ「DEFAULT」の意味)、
  // 1..N=availableBinFonts_[i-1]
  int mdRoleSelectionIndex_[kMarkdownRoleCount] = {0, 0, 0, 0, 0};
  bool editingMarkdownMenu_ = false;
  int markdownMenuFocusIndex_ = 0;  // MarkdownRole参照

  bool editingMarkdownFontPicker_ = false;
  MarkdownRole markdownFontPickerRole_ = MarkdownRole::kHeading1;
  int markdownFontPickerFocusIndex_ = 0;

  bool editingTimezone_ = false;
  int8_t timezoneDraft_ = 0;

  bool showClearCacheConfirm_ = false;

  StatusBar statusBar_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[4];

  SettingRow rows_[kItemCount];
  String rowValues_[kItemCount];
};
