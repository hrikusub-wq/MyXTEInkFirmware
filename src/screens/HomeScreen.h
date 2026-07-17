#pragma once
#include "../ui/FooterGuide.h"
#include "../ui/HomeGridButton.h"
#include "../ui/Screen.h"
#include "../ui/StatusBar.h"

// ホーム画面。上部に直近の本(TXT読書画面での進捗があれば実データ、なければ
// プレースホルダー表示)、下部に2x2グリッド(読書へ/フォルダ/設定/空き)を表示する。
//
// EPUB対応はまだないため、上部の本情報が示せるのはTXTファイルの進捗のみ。
class HomeScreen : public Screen {
 public:
  // グリッドボタンのどれが選ばれたかをmain.cpp(画面遷移の管理者)に伝えるための識別子。
  enum class GridButton {
    kRead,
    kFolder,
    kSettings,
    kEmpty,
  };

  HomeScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // ScreenActionがkNavigateForwardを返した直後に、main.cpp側が
  // 「どのグリッドボタンが選ばれて遷移が要求されたか」を知るために呼ぶ。
  GridButton lastActivatedButton() const { return static_cast<GridButton>(focusIndex_); }

  // main.cpp側がBatteryServiceから読み取った最新の残量・充電状態をここで反映する。
  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }
  void setBatteryCharging(bool charging) { statusBar_.setBatteryCharging(charging); }

  // 直近に開いていた本を反映する(起動時・読書画面を閉じたときにmain.cppが呼ぶ)。
  // pathが空のままなら「本がまだない」プレースホルダー表示が続く。
  void setLastBook(const String& path, int percent);

  // GridButton::kReadがCONFIRMされScreenAction::kOpenFileが返ったとき、
  // main.cpp側がここから開くべきファイルパスを取得する。
  const String& lastBookPath() const { return lastBookPath_; }

  // ステータスバー左側の時刻表示(設定でON/OFF切り替え可能)。空文字列で非表示。
  // main.cpp側がRtcServiceから読み取った"HH:MM"を定期的に渡す。
  void setClockText(const char* text);

 private:
  static constexpr int kGridCols = 2;
  static constexpr int kGridRows = 2;
  static constexpr int kButtonCount = kGridCols * kGridRows;
  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;
  static constexpr int kBookAreaHeight = 220;  // 上部の本プレースホルダー領域の高さ
  static constexpr int kGridMargin = 16;

  void updateFocus();
  void drawBookPlaceholder(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;

  StatusBar statusBar_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[3];
  HomeGridButton buttons_[kButtonCount];
  int focusIndex_ = static_cast<int>(GridButton::kFolder);

  String lastBookPath_;
  String lastBookTitle_;
  int lastBookPercent_ = 0;
  String clockText_;
};
