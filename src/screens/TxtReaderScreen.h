#pragma once
#include "../core/SettingsService.h"
#include "../core/TxtReaderService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/StatusBar.h"

// TXT/Markdownファイルを表示する読書画面。表示モードは2種類(TxtReaderService::
// ReadMode参照):
//   - PAGED(既定): 1画面分の内容を「ページ」としてめくる従来方式。
//   - SCROLL: 数行ずつ連続して読み進める方式。E-inkは画面更新が遅く真のピクセル
//     単位スクロールは実用的でないため、「少しだけ進めて全体を再描画する」ことで
//     連続して読んでいる感覚を出す(前の内容の大半が画面上に残ったまま数行だけ
//     送られる)。
//
// - CONFIRM: (オーバーレイ非表示時)「読書設定」オーバーレイを開く。中はSettingsScreen
//   と同じ操作体系(LEFT/RIGHT・UP/DOWNどちらでもフォーカス移動、決定はCONFIRM)。
//     - MODE: CONFIRMで表示方式を横(PAGE)⇔縦(SCROLL)に切り替える(即時反映)
//     - SCROLL LINES: CONFIRMでTIMEZONEと同様の編集画面に入り、LEFT/RIGHTで
//       1〜9の範囲で調整、CONFIRMで保存、BACKで破棄
//   オーバーレイ自体はBACKで閉じて読書に戻る。
// - LEFT/UP: 前のページ(PAGED) / 数行戻る(SCROLL)
// - RIGHT/DOWN: 次のページ(PAGED) / 数行進む(SCROLL)
// - BACK: (オーバーレイ非表示時)「閉じてホームへ」の確認オーバーレイを開く/閉じる
//   オーバーレイ表示中のCONFIRM=閉じてホームへ戻る、BACK=キャンセルして読書に戻る
//
// ページ内容の折り返し・進捗保存・Markdown記法の解析(見出し・箇条書き・コード
// ブロック)はTxtReaderService(core/)に委譲する。このクラスは、その解析結果
// (TxtReaderService::RenderLine)を実際の見た目に変換する描画側の責務を持つ:
// 見出し行はheadingFontForLevel()(H1〜H3+の最大3段階)で描画し、見出し記号(#)は
// 描画時に読み飛ばし、Markdownファイルでは太字/斜体の*記号も描画時に取り除く
// (コードブロック内の行は除く)。拡張子が.md/.markdownのファイルをopenFile()した
// 場合のみMarkdownモードになる(.txtは従来通りプレーンテキストとして扱う)。
class TxtReaderScreen : public Screen {
 public:
  // settingsはSCROLL LINES(settings.scrollStepLines)の読み書きに使う
  // (呼び出し側=main.cppが所有するAppSettingsへの参照を保持し続ける)。
  TxtReaderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, AppSettings& settings);

  // pathのTXT/Markdownファイルを開いて表示状態を初期化する。失敗時はfalseを返す
  // (画面遷移は呼び出し側=main.cppが成功時のみ行う)。
  bool openFile(const String& path);

  // 本文の描画・折り返し測定に使うフォントを差し替える(CjkFontImpl等)。
  // nullptrを渡すとコンストラクタで渡したチロームフォント(MiniFontImpl)に戻る
  // (ASCII専用フォントでも豆腐グリフとして表示だけは継続できるフォールバック)。
  // 呼び出し側(main.cpp)は、フォントの読み込みに成功した場合のみ呼ぶこと。
  // 内部でレイアウト(1ページの行数・行の幅)をこのフォントのlineHeight()に
  // 合わせて再計算する。
  void setContentFont(const Font* contentFont);

  // Markdownの見出し行(#〜######)の描画に使うフォント。レベルごとに最大3段階まで
  // (H1=headingFont1、H2=headingFont2、H3〜H6=headingFont3)使い分けられる。
  // nullptrならcontentFont()に戻る(見出し用の別フォントを用意していない場合、
  // 本文と同じ大きさで表示されるだけで、見出し記号の除去や空行の扱いは変わらない)。
  void setHeadingFont1(const Font* headingFont);
  void setHeadingFont2(const Font* headingFont);
  void setHeadingFont3(const Font* headingFont);

  // Markdownの箇条書き行(行頭"- "/"* "/"+ ")の描画に使うフォント。nullptrなら
  // contentFont()に戻る(headingFontと同じ考え方)。
  void setListFont(const Font* listFont);

  // Markdownの太字/強調(*text*・**text**)部分の描画に使うフォント。行内の一部
  // だけに適用される点がheading/listと異なる(render()参照)。nullptrなら太字の
  // 視覚的な区別をせず、記号だけ取り除いて周囲と同じフォントで描画する
  // (従来の挙動、TxtReaderService側の折り返し計算には影響しない)。
  void setBoldFont(const Font* boldFont);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }
  void setBatteryCharging(bool charging) { statusBar_.setBatteryCharging(charging); }

 private:
  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;
  static constexpr int kContentMargin = 16;
  // READING SETTINGSオーバーレイの項目数(0=MODE、1=SCROLL LINES)。
  static constexpr int kReadingSettingsItemCount = 2;

  void layout(const Font& contentFont);
  void updateStatusAndFooter();
  void toggleReadMode();
  void enterScrollLinesEdit();
  void adjustScrollLinesDraft(int delta);
  void commitScrollLinesEdit();
  void drawCloseOverlay(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawReadingSettingsOverlay(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  void drawScrollLinesEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;
  const Font& contentFont() const { return contentFont_ ? *contentFont_ : *font_; }
  const Font& headingFont1() const { return headingFont1_ ? *headingFont1_ : contentFont(); }
  const Font& headingFont2() const { return headingFont2_ ? *headingFont2_ : contentFont(); }
  const Font& headingFont3() const { return headingFont3_ ? *headingFont3_ : contentFont(); }
  // headingLevel(1〜6)に対応する見出しフォントを返す(H3以上はheadingFont3())。
  const Font& headingFontForLevel(uint8_t level) const {
    if (level <= 1) return headingFont1();
    if (level == 2) return headingFont2();
    return headingFont3();
  }
  const Font& listFont() const { return listFont_ ? *listFont_ : contentFont(); }

  const Font* font_;
  const Font* contentFont_ = nullptr;
  const Font* headingFont1_ = nullptr;
  const Font* headingFont2_ = nullptr;
  const Font* headingFont3_ = nullptr;
  const Font* listFont_ = nullptr;
  const Font* boldFont_ = nullptr;  // nullptr可: 太字の視覚区別なし(render()参照)
  AppSettings& settings_;
  uint16_t fbWidth_;
  uint16_t fbHeight_;
  TxtReaderService reader_;
  StatusBar statusBar_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[4];
  char pageLabel_[16] = "1/1";
  String titleText_;
  bool markdownMode_ = false;

  int contentTop_ = 0;
  int viewportWidthPx_ = 0;
  int viewportHeightPx_ = 1;

  bool showCloseOverlay_ = false;

  bool showReadingSettings_ = false;
  int readingSettingsFocus_ = 0;  // 0=MODE, 1=SCROLL LINES

  bool editingScrollLines_ = false;
  uint8_t scrollLinesDraft_ = 3;
};
