#include "Icon.h"

#include "FrameBufferOps.h"

void drawIcon(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, IconId id, IconSize size) {
  const int px = static_cast<int>(size);
  const int widthBytes = (px + 7) / 8;
  const uint8_t* bitmap = (size == IconSize::kSmall)
                               ? IconAssets::ICON_BITMAP_24[static_cast<int>(id)]
                               : IconAssets::ICON_BITMAP_40[static_cast<int>(id)];

  for (int row = 0; row < px; row++) {
    for (int col = 0; col < px; col++) {
      const uint8_t byteVal = pgm_read_byte(&bitmap[row * widthBytes + (col >> 3)]);
      const bool isBlack = !(byteVal & (0x80 >> (col & 7)));
      if (isBlack) {
        FrameBufferOps::setBlackPixel(fb, fbWidth, fbHeight, x + col, y + row);
      }
    }
  }
}
