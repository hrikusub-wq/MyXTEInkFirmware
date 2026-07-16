#include "FooterGuide.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
constexpr int kIconPx = static_cast<int>(IconSize::kSmall);
constexpr int kIconTextGap = 4;
constexpr int kSlotCount = 4;  // BACK/CONFIRM/LEFT/RIGHT (底面の物理ボタンのみ)
}  // namespace

void FooterGuide::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  // 上端に区切り線
  FrameBufferOps::drawHLine(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w);

  const int slotWidth = bounds_.w / kSlotCount;
  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;
  const int iconY = bounds_.y + (bounds_.h - kIconPx) / 2;

  for (size_t i = 0; i < count_; i++) {
    const FooterGuideItem& item = items_[i];
    const int slotX = bounds_.x + static_cast<int>(item.button) * slotWidth;

    int contentWidth = 0;
    if (item.hasIcon) {
      contentWidth = kIconPx;
      if (item.description[0] != '\0') {
        contentWidth += kIconTextGap + font.measureText(item.description);
      }
    } else {
      contentWidth = font.measureText(item.description);
    }
    int cursorX = slotX + (slotWidth - contentWidth) / 2;

    if (item.hasIcon) {
      drawIcon(fb, fbWidth, fbHeight, cursorX, iconY, item.icon, IconSize::kSmall);
      cursorX += kIconPx + kIconTextGap;
      if (item.description[0] != '\0') {
        font.drawText(fb, fbWidth, fbHeight, cursorX, textY, item.description);
      }
    } else {
      font.drawText(fb, fbWidth, fbHeight, cursorX, textY, item.description);
    }
  }

  if (trailingText_ != nullptr) {
    const int w = font.measureText(trailingText_);
    font.drawText(fb, fbWidth, fbHeight, bounds_.x + bounds_.w - kPadding - w, textY, trailingText_);
  }
}
