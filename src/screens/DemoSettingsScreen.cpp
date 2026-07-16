#include "DemoSettingsScreen.h"

#include <InputManager.h>

DemoSettingsScreen::DemoSettingsScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font)
    : statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      rows_{
          SettingRow(RowBounds(fbWidth, font, 0), "WI-FI", "OFF"),
          SettingRow(RowBounds(fbWidth, font, 1), "SLEEP TIMER", "5 MIN"),
          SettingRow(RowBounds(fbWidth, font, 2), "POWER OFF TIMER", "30 MIN"),
          SettingRow(RowBounds(fbWidth, font, 3), "SERIAL NUMBER", "X3-0001"),
      },
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  for (auto& row : rows_) {
    row.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  }
  footerItems_[0] = {"UP/DN", "MOVE"};
  footerItems_[1] = {"OK", "SELECT"};
  footerItems_[2] = {"BACK", "EXIT"};
  footer_.setItems(footerItems_, 3);

  // 実際の時刻・バッテリー取得はまだ未実装(フェーズ1はダミー値でよい)。
  // バッテリーはBQ27220(I2C)から読む自作ドライバが別途必要(README参照)。
  statusBar_.setLeftText("12:34");
  statusBar_.setRightText("87%");

  updateFocus();
}

void DemoSettingsScreen::updateFocus() {
  for (int i = 0; i < kRowCount; i++) {
    rows_[i].setSelected(i == focusIndex_);
  }
}

void DemoSettingsScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  statusBar_.render(fb, fbWidth, fbHeight, font);
  for (auto& row : rows_) {
    row.render(fb, fbWidth, fbHeight, font);
  }
  footer_.render(fb, fbWidth, fbHeight, font);
}

bool DemoSettingsScreen::handleButton(uint8_t buttonIndex) {
  // 本来はハードウェアのボタン列挙(InputManager)にUI層が直接依存しない方が
  // 望ましいが、フェーズ1では画面遷移層をまだ作っていないため簡潔さを優先し、
  // InputManagerの定数をそのまま使っている。フェーズ2以降で画面が増えたら
  // 「論理入力(上/下/決定/戻る)」への変換層を検討する。
  if (buttonIndex == InputManager::BTN_UP) {
    focusIndex_ = (focusIndex_ + kRowCount - 1) % kRowCount;
    updateFocus();
    return true;
  }
  if (buttonIndex == InputManager::BTN_DOWN) {
    focusIndex_ = (focusIndex_ + 1) % kRowCount;
    updateFocus();
    return true;
  }
  return false;
}
