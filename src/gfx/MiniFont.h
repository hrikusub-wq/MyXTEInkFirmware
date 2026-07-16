#pragma once
#include <Arduino.h>

// フェーズ0用の最小ビットマップフォント (5x7ドット、大文字英数字と一部記号のみ)。
// 本格的なフォントシステム(SDカードからのCJKフォント読み込み)はフェーズ5で実装予定。
// フレームバッファは1bit/px・行方向詰め(1バイト=横8px、MSBが左端)、0=黒/1=白。
namespace MiniFont {

constexpr int GLYPH_W = 5;  // グリフの幅(ドット)
constexpr int GLYPH_H = 7;  // グリフの高さ(ドット)
constexpr int ADVANCE = 6;  // 次の文字までの送り幅(ドット、字間1px込み)

// 1文字を描画する。scaleで拡大(1=5x7px、4=20x28px)。
// fbWidth/fbHeightは画面の実サイズ(はみ出しはクリップされる)。
void drawChar(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, char c, int scale);

// 文字列を描画する。小文字は大文字として描画される。
void drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, const char* text, int scale);

// 描画した場合の文字列のピクセル幅を返す(中央寄せ用)
int textWidth(const char* text, int scale);

}  // namespace MiniFont
