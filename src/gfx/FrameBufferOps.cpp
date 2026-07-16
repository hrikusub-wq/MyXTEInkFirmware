#include "FrameBufferOps.h"

// X3のE-inkパネルはネイティブでは792x528(横長)のフレームバッファしか持たないが、
// 実機は縦持ちで使う機器のため、UI層(MiniFont・各種UIコンポーネント)は常に
// 528x792(縦長)の論理座標で描画する。ここで論理座標を物理座標に変換してから
// 実際のビット操作を行うことで、上位のコードは回転を一切意識しなくてよい。
//
// 変換方向: 実機を時計回りに90度回転させると正しい向きで読めることを実機で
// 確認済み(= 論理コンテンツを物理フレームバッファへ格納する際は時計回りCW90回転)。
namespace FrameBufferOps {

namespace {
constexpr int kPhysicalWidth = 792;
constexpr int kPhysicalHeight = 528;

inline uint32_t widthBytesOf(int physWidth) {
  return (static_cast<uint32_t>(physWidth) + 7) / 8;
}

// 論理座標(縦長: 0<=lx<528, 0<=ly<792)を物理座標(横長: 0<=px<792, 0<=py<528)に変換する。
inline void logicalToPhysical(int lx, int ly, int* px, int* py) {
  *px = ly;
  *py = (kPhysicalHeight - 1) - lx;
}
}  // namespace

void setBlackPixel(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y) {
  if (x < 0 || y < 0 || x >= fbWidth || y >= fbHeight) return;
  int px, py;
  logicalToPhysical(x, y, &px, &py);
  fb[static_cast<uint32_t>(py) * widthBytesOf(kPhysicalWidth) + (px >> 3)] &= ~(0x80 >> (px & 7));
}

void setWhitePixel(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y) {
  if (x < 0 || y < 0 || x >= fbWidth || y >= fbHeight) return;
  int px, py;
  logicalToPhysical(x, y, &px, &py);
  fb[static_cast<uint32_t>(py) * widthBytesOf(kPhysicalWidth) + (px >> 3)] |= (0x80 >> (px & 7));
}

void togglePixel(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y) {
  if (x < 0 || y < 0 || x >= fbWidth || y >= fbHeight) return;
  int px, py;
  logicalToPhysical(x, y, &px, &py);
  fb[static_cast<uint32_t>(py) * widthBytesOf(kPhysicalWidth) + (px >> 3)] ^= (0x80 >> (px & 7));
}

void fillRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, int w, int h, bool black) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      if (black) {
        setBlackPixel(fb, fbWidth, fbHeight, xx, yy);
      } else {
        setWhitePixel(fb, fbWidth, fbHeight, xx, yy);
      }
    }
  }
}

void invertRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      togglePixel(fb, fbWidth, fbHeight, xx, yy);
    }
  }
}

void drawRectOutline(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                     int x, int y, int w, int h, int thickness) {
  if (thickness < 1) thickness = 1;
  fillRect(fb, fbWidth, fbHeight, x, y, w, thickness, true);                          // 上辺
  fillRect(fb, fbWidth, fbHeight, x, y + h - thickness, w, thickness, true);          // 下辺
  fillRect(fb, fbWidth, fbHeight, x, y, thickness, h, true);                          // 左辺
  fillRect(fb, fbWidth, fbHeight, x + w - thickness, y, thickness, h, true);          // 右辺
}

void drawHLine(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y, int w) {
  fillRect(fb, fbWidth, fbHeight, x, y, w, 1, true);
}

}  // namespace FrameBufferOps
