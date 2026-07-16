#pragma once
#include <Arduino.h>

#include "../gfx/Font.h"

// 画面を構成する部品(ステータスバー、設定行など)の共通インターフェース。
// レイアウト計算(文字列幅の取得など)は必ずFont経由で行い、pxのハードコードで
// 文字列の折り返し・寄せを決め打ちしないこと(将来の可変幅CJKフォント対応のため)。
class Widget {
 public:
  virtual ~Widget() = default;

  virtual void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const = 0;
};
