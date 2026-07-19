#include "HomeGridButton.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kIconPx = static_cast<int>(IconSize::kLarge);
constexpr int kIconTopMargin = 12;
constexpr int kIconLabelGap = 16;  // アイコン下の余白(以前は8。もう少し空けたいというフィードバックを受けて拡大)
}  // namespace

void HomeGridButton::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  // 選択時の背景(薄いグレーのドットパターン)はアイコン・文字より先に敷く。
  // drawIcon()/font.drawText()はどちらも「黒画素だけを描く透過描画」のため
  // (Icon.cpp/各フォント実装参照)、先に背景を敷いてから上描きしても欠けたり
  // 汚れたりしない(SettingRow::render()と同じ手法。以前は黒背景反転だったが、
  // 選択の主張が強すぎるというフィードバックを受けてグレーに変更した)。
  if (selected_) {
    FrameBufferOps::fillRectLightGrayDither(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  }

  const int iconX = bounds_.x + (bounds_.w - kIconPx) / 2;
  const int iconY = bounds_.y + kIconTopMargin;

  if (hasIcon_) {
    drawIcon(fb, fbWidth, fbHeight, iconX, iconY, icon_, IconSize::kLarge);
  }

  const int labelWidth = font.measureText(label_);
  const int labelX = bounds_.x + (bounds_.w - labelWidth) / 2;
  const int labelY = iconY + kIconPx + kIconLabelGap;
  font.drawText(fb, fbWidth, fbHeight, labelX, labelY, label_);
}
