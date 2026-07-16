#include "FooterGuide.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
constexpr int kItemGap = 24;
}  // namespace

void FooterGuide::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  // 上端に区切り線
  FrameBufferOps::drawHLine(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w);

  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;
  int cursorX = bounds_.x + kPadding;

  char buf[48];
  for (size_t i = 0; i < count_; i++) {
    snprintf(buf, sizeof(buf), "%s:%s", items_[i].buttonLabel, items_[i].description);
    font.drawText(fb, fbWidth, fbHeight, cursorX, textY, buf);
    cursorX += font.measureText(buf) + kItemGap;
  }
}
