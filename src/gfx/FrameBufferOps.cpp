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

void fillRoundRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                   int x, int y, int w, int h, int r, bool black) {
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;

  // 中央の十字部分(2つの矩形)
  fillRect(fb, fbWidth, fbHeight, x + r, y, w - 2 * r, h, black);
  fillRect(fb, fbWidth, fbHeight, x, y + r, r, h - 2 * r, black);
  fillRect(fb, fbWidth, fbHeight, x + w - r, y + r, r, h - 2 * r, black);

  // 4つの角(円の一部)
  for (int cy = 0; cy < r; cy++) {
    for (int cx = 0; cx < r; cx++) {
      int dx = r - 1 - cx;
      int dy = r - 1 - cy;
      if (dx * dx + dy * dy <= r * r) {
        // 左上
        if (black) setBlackPixel(fb, fbWidth, fbHeight, x + cx, y + cy);
        else       setWhitePixel(fb, fbWidth, fbHeight, x + cx, y + cy);
        // 右上
        if (black) setBlackPixel(fb, fbWidth, fbHeight, x + w - 1 - cx, y + cy);
        else       setWhitePixel(fb, fbWidth, fbHeight, x + w - 1 - cx, y + cy);
        // 左下
        if (black) setBlackPixel(fb, fbWidth, fbHeight, x + cx, y + h - 1 - cy);
        else       setWhitePixel(fb, fbWidth, fbHeight, x + cx, y + h - 1 - cy);
        // 右下
        if (black) setBlackPixel(fb, fbWidth, fbHeight, x + w - 1 - cx, y + h - 1 - cy);
        else       setWhitePixel(fb, fbWidth, fbHeight, x + w - 1 - cx, y + h - 1 - cy);
      }
    }
  }
}

void fillRoundRectDither(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                         int x, int y, int w, int h, int r, bool black) {
  if (r <= 0) {
    for (int yy = y; yy < y + h; yy++) {
      for (int xx = x; xx < x + w; xx++) {
        if (((xx + yy) & 1) == 0) {
          if (black) setBlackPixel(fb, fbWidth, fbHeight, xx, yy);
          else       setWhitePixel(fb, fbWidth, fbHeight, xx, yy);
        }
      }
    }
    return;
  }
  
  // r>0 の場合(角丸の切り落とし判定付き)
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      if (((xx + yy) & 1) != 0) continue;
      
      bool inside = true;
      if (xx < x + r && yy < y + r) {
        int dx = (x + r) - xx;
        int dy = (y + r) - yy;
        if (dx * dx + dy * dy > r * r) inside = false;
      } else if (xx >= x + w - r && yy < y + r) {
        int dx = xx - (x + w - 1 - r);
        int dy = (y + r) - yy;
        if (dx * dx + dy * dy > r * r) inside = false;
      } else if (xx < x + r && yy >= y + h - r) {
        int dx = (x + r) - xx;
        int dy = yy - (y + h - 1 - r);
        if (dx * dx + dy * dy > r * r) inside = false;
      } else if (xx >= x + w - r && yy >= y + h - r) {
        int dx = xx - (x + w - 1 - r);
        int dy = yy - (y + h - 1 - r);
        if (dx * dx + dy * dy > r * r) inside = false;
      }
      
      if (inside) {
        if (black) setBlackPixel(fb, fbWidth, fbHeight, xx, yy);
        else       setWhitePixel(fb, fbWidth, fbHeight, xx, yy);
      }
    }
  }
}

void fillRectLightGrayDither(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                             int x, int y, int w, int h) {
  // 2x2のパターンのうち1マスだけ黒にする(4px中1px=約25%被覆率)。50%の市松模様
  // だと選択行の背景が主張しすぎて上に乗る文字が読みにくくなるため、控えめな
  // 薄いグレーに見える密度にしている。
  for (int yy = y; yy < y + h; yy++) {
    if ((yy & 1) != 0) continue;
    for (int xx = x; xx < x + w; xx++) {
      if ((xx & 1) != 0) continue;
      setBlackPixel(fb, fbWidth, fbHeight, xx, yy);
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

void blitBitmapRow(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                    int x, int y, const uint8_t* rowBytes, int widthPx) {
  for (int col = 0; col < widthPx; col++) {
    const uint8_t byteVal = rowBytes[col >> 3];
    const bool isWhite = (byteVal & (0x80 >> (col & 7))) != 0;
    if (isWhite) {
      setWhitePixel(fb, fbWidth, fbHeight, x + col, y);
    } else {
      setBlackPixel(fb, fbWidth, fbHeight, x + col, y);
    }
  }
}

void invertRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, int w, int h) {
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const int lx = x + col;
      const int ly = y + row;
      if (lx < 0 || ly < 0 || lx >= fbWidth || ly >= fbHeight) continue;
      int px, py;
      logicalToPhysical(lx, ly, &px, &py);
      fb[static_cast<uint32_t>(py) * widthBytesOf(kPhysicalWidth) + (px >> 3)] ^= (0x80 >> (px & 7));
    }
  }
}

}  // namespace FrameBufferOps
