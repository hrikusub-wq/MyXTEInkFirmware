#pragma once
#include <Arduino.h>

// フレームバッファ(1bit/px・行方向詰め、1バイト=横8px・MSBが左端、0=黒/1=白)への
// ピクセル単位・矩形単位の基本操作。MiniFontやUIコンポーネントの選択状態表示
// (反転・枠線)など、複数箇所から共通で使う処理をここに集約する。
//
// 座標系の注意: 引数のx/y/fbWidth/fbHeightはすべて「論理座標系」(縦長、
// 528x792)。実機のE-inkパネルはネイティブでは792x528(横長)だが、実装(.cpp)側で
// 論理→物理への90度回転変換を行うため、呼び出し側はパネルが横長であることを
// 意識しなくてよい。
namespace FrameBufferOps {

// 1ピクセルを黒にする。画面外は無視。
void setBlackPixel(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y);

// 1ピクセルを白にする。画面外は無視。
void setWhitePixel(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y);

// 1ピクセルを反転する(黒<->白)。画面外は無視。
void togglePixel(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y);

// 矩形範囲を塗りつぶす。black=trueなら黒、falseなら白。
void fillRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, int w, int h, bool black);

// 矩形範囲の全ピクセルを反転する(選択行の黒背景反転などに使う)。
void invertRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, int w, int h);

// 矩形の枠線を描く(角丸は簡略化して直角枠のみ)。thicknessは線の太さ(px)。
void drawRectOutline(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                     int x, int y, int w, int h, int thickness = 1);

// 水平線を1本引く(ステータスバー・フッターの区切り線などに使う)。
void drawHLine(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y, int w);

}  // namespace FrameBufferOps
