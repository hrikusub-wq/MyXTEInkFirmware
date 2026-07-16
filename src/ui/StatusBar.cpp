#include "StatusBar.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
}  // namespace

void StatusBar::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;

  font.drawText(fb, fbWidth, fbHeight, bounds_.x + kPadding, textY, leftText_);

  const int rightWidth = font.measureText(rightText_);
  font.drawText(fb, fbWidth, fbHeight, bounds_.x + bounds_.w - kPadding - rightWidth, textY, rightText_);

  // 下端に区切り線
  FrameBufferOps::drawHLine(fb, fbWidth, fbHeight, bounds_.x, bounds_.y + bounds_.h - 1, bounds_.w);
}
