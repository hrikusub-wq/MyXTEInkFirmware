#pragma once
#include "../ui/FooterGuide.h"
#include "../ui/HomeGridButton.h"
#include "../ui/Screen.h"
#include "../ui/StatusBar.h"

// ホーム画面。上部に直近の本(プレースホルダー表示)、下部に2x2グリッド
// (読書へ/フォルダ/設定/空き)を表示する。
//
// 現時点ではTXT読書機能もEPUB対応もないため、上部の本情報は完全にダミー表示。
// 実データ(実際に開いていたファイル・進捗)との連携はフェーズ3(TXT読書画面)以降で行う。
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

  // main.cpp側がBatteryServiceから読み取った最新の残量をここで反映する。
  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }

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
};
