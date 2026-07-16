#include "SettingRow.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
}  // namespace

void SettingRow::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;

  font.drawText(fb, fbWidth, fbHeight, bounds_.x + kPadding, textY, label_);

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
