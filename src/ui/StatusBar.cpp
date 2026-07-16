#include "StatusBar.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
constexpr int kIconPx = static_cast<int>(IconSize::kSmall);
constexpr int kIconTextGap = 4;

IconId batteryIconFor(int percent) {
  if (percent >= 60) return IconId::kBatteryFull;
  if (percent >= 20) return IconId::kBatteryHalf;
  return IconId::kBatteryLow;  // 低残量は視認性を優先してアラート形状のアイコンにする
}
}  // namespace

void StatusBar::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;

  font.drawText(fb, fbWidth, fbHeight, bounds_.x + kPadding, textY, leftText_);

  char percentBuf[8];
  snprintf(percentBuf, sizeof(percentBuf), "%d%%", batteryPercent_);
  const int percentWidth = font.measureText(percentBuf);
  const int totalWidth = kIconPx + kIconTextGap + percentWidth;

  const int iconX = bounds_.x + bounds_.w - kPadding - totalWidth;
  const int iconY = bounds_.y + (bounds_.h - kIconPx) / 2;
  drawIcon(fb, fbWidth, fbHeight, iconX, iconY, batteryIconFor(batteryPercent_), IconSize::kSmall);
  font.drawText(fb, fbWidth, fbHeight, iconX + kIconPx + kIconTextGap, textY, percentBuf);

  // 下端に区切り線
  FrameBufferOps::drawHLine(fb, fbWidth, fbHeight, bounds_.x, bounds_.y + bounds_.h - 1, bounds_.w);
}
