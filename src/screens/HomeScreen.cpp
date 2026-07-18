#include "HomeScreen.h"

#include <InputManager.h>

#include "../gfx/FrameBufferOps.h"

namespace {
struct HomeButtonDef {
  HomeScreen::GridButton id;
  IconId icon;
  const char* label;
};

// ホーム画面のボタン定義。今後ボタン(=他機能への入り口)を追加する場合は、
// HomeScreen.hのGridButton enumに値を足した上で、ここに同じ順序で1行追記し、
// kButtonCountも更新すればよい(レイアウト計算・フォーカス移動は自動で追従する。
// クラスコメント参照)。
constexpr HomeButtonDef kButtonDefs[] = {
    {HomeScreen::GridButton::kRead, IconId::kImportContacts, "READ"},
    {HomeScreen::GridButton::kSettings, IconId::kSettings, "SETTINGS"},
    {HomeScreen::GridButton::kFolder, IconId::kFolder, "FOLDER"},
};
}  // namespace

HomeScreen::HomeScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font)
    : statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  static_assert(sizeof(kButtonDefs) / sizeof(kButtonDefs[0]) == kButtonCount,
                "kButtonDefs[]の要素数とkButtonCountを一致させてください");
  (void)font;  // ボタン行の高さは固定px(kButtonRowHeight)で決め打ちのため未使用
  statusBar_.setBatteryPercent(87);

  // UP/DOWN(グリッドの縦移動)は側面ボタンのためフッターには表示できない。
  // LEFT/RIGHTは同じ"MOVE"だと見分けがつかないため、矢印アイコンで向きを示す。
  footerItems_[0] = {PhysicalButton::kLeft, "", IconId::kChevronBackward, true};
  footerItems_[1] = {PhysicalButton::kRight, "", IconId::kChevronForward, true};
  footerItems_[2] = {PhysicalButton::kConfirm, "OPEN", IconId::kCheck, true};
  footer_.setItems(footerItems_, 3);

  // 列数kColsPerRowで折り返す「アプリグリッド」配置。フッターのすぐ上を最終行と
  // して、行が増えるほど上へ積み上がっていく(クラスコメント参照)。
  const int rowBottomMost = static_cast<int>(fbHeight) - kFooterHeight - kButtonRowBottomMargin;
  const int cellW = (static_cast<int>(fbWidth) - kGridMargin * (kColsPerRow + 1)) / kColsPerRow;

  for (int i = 0; i < kButtonCount; i++) {
    const int row = i / kColsPerRow;
    const int col = i % kColsPerRow;
    const int rowFromBottom = kRowCount - 1 - row;  // 最終行(画面下)ほど0に近い
    const int x = kGridMargin + col * (cellW + kGridMargin);
    const int y = rowBottomMost - (rowFromBottom + 1) * kButtonRowHeight - rowFromBottom * kGridMargin;
    buttons_[i].setBounds(Rect{x, y, cellW, kButtonRowHeight});
    buttons_[i].setLabel(kButtonDefs[i].label);
    buttons_[i].setIcon(kButtonDefs[i].icon);
  }

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
  // RIGHT/DOWNは「次へ」、LEFT/UPは「前へ」。行の境界を無視し、グリッド全体を
  // 1本の輪(row-major順、A B C / D E F ならA→B→C→D→E→F→A→…)とみなして
  // 循環移動する(側面ボタン(UP/DOWN)だけでも底面ボタン(LEFT/RIGHT)と同じ操作が
  // できるように、というフィードバックを受けた仕様。例: Cの次はD、Aの前はF)。
  // 端で止まらず必ず隣へ移動する。
  if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
    focusIndex_ = (focusIndex_ + 1) % kButtonCount;
    updateFocus();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
    focusIndex_ = (focusIndex_ + kButtonCount - 1) % kButtonCount;
    updateFocus();
    return ScreenAction::kRedraw;
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
