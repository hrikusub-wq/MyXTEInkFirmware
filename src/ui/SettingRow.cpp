#include "SettingRow.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
constexpr int kIconPx = static_cast<int>(IconSize::kSmall);
constexpr int kIconLabelGap = 6;
}  // namespace

void SettingRow::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;
  int labelX = bounds_.x + kPadding;

  if (hasIcon_) {
    const int iconY = bounds_.y + (bounds_.h - kIconPx) / 2;
    drawIcon(fb, fbWidth, fbHeight, labelX, iconY, icon_, IconSize::kSmall);
    labelX += kIconPx + kIconLabelGap;
  }

  font.drawText(fb, fbWidth, fbHeight, labelX, textY, label_);

  const int valueWidth = font.measureText(value_);
  font.drawText(fb, fbWidth, fbHeight, bounds_.x + bounds_.w - kPadding - valueWidth, textY, value_);

  if (!selected_) return;

  if (style_ == SelectionStyle::kOutline) {
    FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  } else {
    // 白背景に黒文字で描いた内容を丸ごと反転させ、黒背景に白文字にする。
    FrameBufferOps::invertRect(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  }
}
