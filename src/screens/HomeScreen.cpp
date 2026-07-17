#include "HomeScreen.h"

#include <InputManager.h>

#include "../gfx/FrameBufferOps.h"

HomeScreen::HomeScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font)
    : statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  statusBar_.setBatteryPercent(87);

  // UP/DOWN(グリッドの縦移動)は側面ボタンのためフッターには表示できない。
  // LEFT/RIGHTは同じ"MOVE"だと見分けがつかないため、矢印アイコンで向きを示す。
  footerItems_[0] = {PhysicalButton::kLeft, "", IconId::kChevronBackward, true};
  footerItems_[1] = {PhysicalButton::kRight, "", IconId::kChevronForward, true};
  footerItems_[2] = {PhysicalButton::kConfirm, "OPEN", IconId::kCheck, true};
  footer_.setItems(footerItems_, 3);

  const int gridTop = kStatusBarHeight + kBookAreaHeight;
  const int gridBottom = static_cast<int>(fbHeight) - kFooterHeight;
  const int cellW = (static_cast<int>(fbWidth) - kGridMargin * (kGridCols + 1)) / kGridCols;
  const int cellH = (gridBottom - gridTop - kGridMargin * (kGridRows + 1)) / kGridRows;

  const char* labels[kButtonCount] = {"READ", "FOLDER", "SETTINGS", "----"};
  for (int row = 0; row < kGridRows; row++) {
    for (int col = 0; col < kGridCols; col++) {
      const int i = row * kGridCols + col;
      const int x = kGridMargin + col * (cellW + kGridMargin);
      const int y = gridTop + kGridMargin + row * (cellH + kGridMargin);
      buttons_[i].setBounds(Rect{x, y, cellW, cellH});
      buttons_[i].setLabel(labels[i]);
    }
  }
  // kEmpty(4番目、"----")は未使用のためアイコンを設定しない
  buttons_[static_cast<int>(GridButton::kRead)].setIcon(IconId::kImportContacts);
  buttons_[static_cast<int>(GridButton::kFolder)].setIcon(IconId::kFolder);
  buttons_[static_cast<int>(GridButton::kSettings)].setIcon(IconId::kSettings);

  updateFocus();
}

void HomeScreen::setClockText(const char* text) {
  clockText_ = text;
  statusBar_.setLeftText(clockText_.c_str());
}

void HomeScreen::setLastBook(const String& path, int percent) {
  lastBookPath_ = path;
  lastBookPercent_ = percent;
  const int lastSlash = path.lastIndexOf('/');
  lastBookTitle_ = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
}

void HomeScreen::updateFocus() {
  for (int i = 0; i < kButtonCount; i++) {
    buttons_[i].setSelected(i == focusIndex_);
  }
}

void HomeScreen::drawBookPlaceholder(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  (void)fbHeight;
  const int margin = 16;
  const int iconW = 90;
  const int iconH = kBookAreaHeight - margin * 2;
  const int iconX = margin;
  const int iconY = kStatusBarHeight + margin;

  // 本のプレースホルダーアイコン: 表紙の矩形+背表紙の縦線
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, iconX, iconY, iconW, iconH, 2);
  FrameBufferOps::fillRect(fb, fbWidth, fbHeight, iconX + 12, iconY, 2, iconH, true);

  const int textX = iconX + iconW + margin;
  const int textY0 = iconY;
  const int lineH = font.lineHeight();

  if (lastBookPath_.length() > 0) {
    char progressBuf[24];
    snprintf(progressBuf, sizeof(progressBuf), "%d%% READ", lastBookPercent_);
    font.drawText(fb, fbWidth, fbHeight, textX, textY0, lastBookTitle_.c_str());
    font.drawText(fb, fbWidth, fbHeight, textX, textY0 + lineH + 8, progressBuf);
    font.drawText(fb, fbWidth, fbHeight, textX, textY0 + (lineH + 8) * 2, "PRESS OPEN TO");
    font.drawText(fb, fbWidth, fbHeight, textX, textY0 + (lineH + 8) * 3 + 12, "CONTINUE READING");
  } else {
    font.drawText(fb, fbWidth, fbHeight, textX, textY0, "NO BOOK YET");
    font.drawText(fb, fbWidth, fbHeight, textX, textY0 + lineH + 8, "OPEN A FILE FROM");
    font.drawText(fb, fbWidth, fbHeight, textX, textY0 + (lineH + 8) * 2, "FOLDER TO START");
    font.drawText(fb, fbWidth, fbHeight, textX, textY0 + (lineH + 8) * 3 + 12, "0% - -- LEFT");
  }
}

void HomeScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  statusBar_.render(fb, fbWidth, fbHeight, font);
  drawBookPlaceholder(fb, fbWidth, fbHeight, font);
  for (auto& button : buttons_) {
    button.render(fb, fbWidth, fbHeight, font);
  }
  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction HomeScreen::handleButton(uint8_t buttonIndex) {
  const int row = focusIndex_ / kGridCols;
  const int col = focusIndex_ % kGridCols;

  if (buttonIndex == InputManager::BTN_RIGHT) {
    if (col + 1 < kGridCols) {
      focusIndex_ += 1;
      updateFocus();
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }
  if (buttonIndex == InputManager::BTN_LEFT) {
    if (col - 1 >= 0) {
      focusIndex_ -= 1;
      updateFocus();
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }
  if (buttonIndex == InputManager::BTN_DOWN) {
    if (row + 1 < kGridRows) {
      focusIndex_ += kGridCols;
      updateFocus();
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }
  if (buttonIndex == InputManager::BTN_UP) {
    if (row - 1 >= 0) {
      focusIndex_ -= kGridCols;
      updateFocus();
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    // フォルダ・設定はどちらもkNavigateForwardを返す。main.cpp側がlastActivatedButton()
    // を見てどちらの画面に遷移するか判断する。
    if (lastActivatedButton() == GridButton::kFolder || lastActivatedButton() == GridButton::kSettings) {
      return ScreenAction::kNavigateForward;
    }
    // READは直近に開いていた本があるときだけ機能する(なければ何もしない)。
    if (lastActivatedButton() == GridButton::kRead && lastBookPath_.length() > 0) {
      return ScreenAction::kOpenFile;
    }
    return ScreenAction::kNone;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kOpenHistory;
  }

  return ScreenAction::kNone;
}
