#pragma once
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"
#include "../ui/StatusBar.h"

// フェーズ1の動作確認用テスト画面。SettingRowを4行並べ、UP/DOWNでフォーカスが
// 移動し選択行の見た目が変わることを実機で確認するために作った。実際の設定項目・
// 値変更ロジックはフェーズ4で本実装する(ここではダミーの表示のみ)。
class DemoSettingsScreen : public Screen {
 public:
  DemoSettingsScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  bool handleButton(uint8_t buttonIndex) override;

 private:
  static constexpr int kRowCount = 4;
  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;
  static constexpr int kRowPadding = 12;  // font.lineHeight()に足す行の上下余白

  static int RowHeight(const Font& font) { return font.lineHeight() + kRowPadding; }
  static Rect RowBounds(uint16_t fbWidth, const Font& font, int index) {
    const int h = RowHeight(font);
    return Rect{0, kStatusBarHeight + h * index, static_cast<int>(fbWidth), h};
  }

  void updateFocus();

  StatusBar statusBar_;
  SettingRow rows_[kRowCount];
  FooterGuide footer_;
  FooterGuideItem footerItems_[3];
  int focusIndex_ = 0;
};
