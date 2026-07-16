#include "HomeGridButton.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kIconMargin = 12;   // ボタン矩形の縁からアイコン枠までの余白
constexpr int kIconLabelGap = 8;  // アイコンとラベルの間隔
}  // namespace

void HomeGridButton::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int iconSize = bounds_.w - kIconMargin * 2;
  const int iconX = bounds_.x + kIconMargin;
  const int iconY = bounds_.y + kIconMargin;

  // アイコンはプレースホルダーとして矩形の枠だけ描く(実アイコンは今後差し替え)
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, iconX, iconY, iconSize, iconSize, 2);

  const int labelWidth = font.measureText(label_);
  const int labelX = bounds_.x + (bounds_.w - labelWidth) / 2;
  const int labelY = iconY + iconSize + kIconLabelGap;
  font.drawText(fb, fbWidth, fbHeight, labelX, labelY, label_);

  if (selected_) {
    FrameBufferOps::invertRect(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  }
}
