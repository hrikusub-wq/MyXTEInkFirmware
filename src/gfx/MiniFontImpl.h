#pragma once
#include "Font.h"

// MiniFont(ASCII専用・固定5x7ドット)をFontインターフェースの実装にしたもの。
// フェーズ5でSDカードから読み込む可変幅CJKフォントに差し替える際は、この
// クラスと同じFontインターフェースを実装した別クラスをUIコンポーネントに渡すだけでよい。
class MiniFontImpl : public Font {
 public:
  // scaleはMiniFontの拡大率(1=5x7px、6=30x42pxなど)。
  explicit MiniFontImpl(int scale) : scale_(scale < 1 ? 1 : scale) {}

  int measureText(const char* utf8Text) const override;
  int lineHeight() const override;
  void drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, const char* utf8Text) const override;

 private:
  int scale_;
};
