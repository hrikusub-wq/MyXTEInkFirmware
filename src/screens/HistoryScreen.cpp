#include "HistoryScreen.h"

#include <InputManager.h>

HistoryScreen::HistoryScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font)
    : fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {

  // UP/DOWNは側面ボタンのためフッターには表示できない。
  footerItems_[0] = {PhysicalButton::kBack, "HOME"};
  footerItems_[1] = {PhysicalButton::kConfirm, "OPEN", IconId::kCheck, true};
  footerItems_[2] = {PhysicalButton::kLeft, "", IconId::kChevronBackward, true};
  footerItems_[3] = {PhysicalButton::kRight, "", IconId::kChevronForward, true};
  footer_.setItems(footerItems_, 4);

  layoutRows(font);
}

void HistoryScreen::layoutRows(const Font& font) {
  // アイコン(SettingRow::kIconPx)が小さいフォントサイズ設定でもはみ出さないよう、
  // 行高さはフォントの行高さとアイコンサイズの大きい方を基準にする
  // (FolderScreen::RowHeight()と同じ考え方)。
  const int rowH = std::max(font.lineHeight(), SettingRow::kIconPx) + kRowPadding;
  for (int i = 0; i < kMaxRows; i++) {
    rows_[i].setBounds(Rect{0, i * rowH, static_cast<int>(fbWidth_), rowH});
    rows_[i].setSelectionStyle(SettingRow::SelectionStyle::kGrayHighlight);
  }
}

void HistoryScreen::relayout(const Font& font) {
  layoutRows(font);
}

void HistoryScreen::reload() {
  TxtReaderService::readHistory(entries_);
  if (focusIndex_ >= static_cast<int>(entries_.size())) focusIndex_ = 0;
  refreshRowLabels();
}

void HistoryScreen::refreshRowLabels() {
  const int total = static_cast<int>(entries_.size());
  for (int i = 0; i < kMaxRows; i++) {
    if (i < total) {
      const int lastSlash = entries_[i].path.lastIndexOf('/');
      rowLabels_[i] = (lastSlash >= 0) ? entries_[i].path.substring(lastSlash + 1) : entries_[i].path;
      char buf[8];
      snprintf(buf, sizeof(buf), "%d%%", entries_[i].percent);
      rowValues_[i] = buf;
      rows_[i].setLabel(rowLabels_[i].c_str());
      rows_[i].setValue(rowValues_[i].c_str());
      rows_[i].setIcon(IconId::kBook);
    }
    rows_[i].setSelected(i == focusIndex_ && i < total);
  }
}

void HistoryScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  const int total = static_cast<int>(entries_.size());
  if (total == 0) {
    font.drawText(fb, fbWidth, fbHeight, 16, 20, "(NO HISTORY YET)");
  } else {
    for (int i = 0; i < total; i++) rows_[i].render(fb, fbWidth, fbHeight, font);
  }

  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction HistoryScreen::handleButton(uint8_t buttonIndex) {
  const int total = static_cast<int>(entries_.size());

  if (buttonIndex == InputManager::BTN_RIGHT) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + 1) % total;
    refreshRowLabels();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_LEFT) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + total - 1) % total;
    refreshRowLabels();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    if (total == 0 || focusIndex_ < 0 || focusIndex_ >= total) return ScreenAction::kNone;
    pendingOpenPath_ = entries_[focusIndex_].path;
    return ScreenAction::kOpenFile;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
