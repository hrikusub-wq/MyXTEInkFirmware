#include "BatteryDateOverlay.h"
#include "../gfx/FrameBufferOps.h"
#include "../gfx/Icon.h"
#include <stdio.h>
#include <string.h>

namespace BatteryDateOverlay {

int drawBatteryAndDate(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font,
                       int x, int y, int percent, bool charging, const RtcDateTime* date,
                       bool drawBackgroundBox, bool rightAlign) {
  if (percent < 0) return 0; // 未取得

  char batBuf[16];
  snprintf(batBuf, sizeof(batBuf), "%d%%", percent);
  int batW = font.measureText(batBuf);
  
  char dateBuf[16] = {0};
  int dateW = 0;
  if (date) {
    snprintf(dateBuf, sizeof(dateBuf), "%04d.%02d.%02d", date->year, date->month, date->day);
    dateW = font.measureText(dateBuf);
  }

  const int iconSize = static_cast<int>(IconSize::kSmall); // 24px
  const int iconMargin = 4;
  const int elementMargin = 12; // dateとbatteryの間
  
  const int lineH = font.lineHeight();
  const int totalH = (iconSize > lineH) ? iconSize : lineH;

  // 全体の幅を計算
  int totalW = 0;
  if (date) {
    totalW += dateW + elementMargin;
  }
  if (charging) {
    totalW += iconSize + iconMargin;
  }
  totalW += batW;

  int currentX = rightAlign ? (x - totalW) : x;

  // 背景を描画する場合
  if (drawBackgroundBox) {
    const int padding = 4;
    // 角丸半径は8px
    FrameBufferOps::fillRoundRectDither(fb, fbWidth, fbHeight,
                                        currentX - padding, y - padding,
                                        totalW + padding * 2, totalH + padding * 2, 8, false);
  }
  
  // 日付
  if (date) {
    int textY = y + (totalH - lineH) / 2;
    font.drawText(fb, fbWidth, fbHeight, currentX, textY, dateBuf);
    currentX += dateW + elementMargin;
  }

  // 雷マーク
  if (charging) {
    int iconY = y + (totalH - iconSize) / 2;
    drawIcon(fb, fbWidth, fbHeight, currentX, iconY, IconId::kBatteryCharging, IconSize::kSmall);
    currentX += iconSize + iconMargin;
  }

  // バッテリー%
  int textY = y + (totalH - lineH) / 2;
  font.drawText(fb, fbWidth, fbHeight, currentX, textY, batBuf);

  return totalH;
}

}  // namespace BatteryDateOverlay
