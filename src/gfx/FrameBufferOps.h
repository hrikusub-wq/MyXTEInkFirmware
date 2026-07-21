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

// 矩形範囲を塗りつぶす。black=trueなら黒、falseなら白。
void fillRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, int w, int h, bool black);

// 角丸矩形を塗りつぶす。rは角の半径(px)。
void fillRoundRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                   int x, int y, int w, int h, int radius, bool black);

// 指定した矩形(角丸)を50%の市松模様で半透明に塗りつぶす。
// (背景の壁紙の上にUI要素を重ねる際の視認性確保用。既存の画素を半分残し、半分を白または黒で上書きする)
void fillRoundRectDither(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                         int x, int y, int w, int h, int radius, bool black);

// 矩形範囲に薄いドットパターン(4px中1px黒)を敷き、疑似的な「グレー」背景を作る。
// パネルは1bpp(白黒2値)のため真のグレー階調は表現できないが、市松状のドットで
// 明るいグレーに見せる。文字は呼び出し側がこの後に描画すること(setBlackPixel()は
// 「黒にする」だけの操作なので、先に背景を敷いてから文字を上描きすれば文字の
// 黒画素がドットパターンより優先される=文字は欠けずそのまま読める)。
void fillRectLightGrayDither(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                             int x, int y, int w, int h);

// 矩形の枠線を描く(角丸は簡略化して直角枠のみ)。thicknessは線の太さ(px)。
void drawRectOutline(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                     int x, int y, int w, int h, int thickness = 1);

// 矩形範囲の全画素を白黒反転する(既に描画済みのアイコン・文字ごと反転できる)。
// 選択中のグリッドボタンを「白抜き」で強調する用途などに使う。
void invertRect(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, int w, int h);

// 水平線を1本引く(ステータスバー・フッターの区切り線などに使う)。
void drawHLine(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y, int w);

// SD等から読んだ1行分の1bppビットマップ(0=黒/1=白、MSBが左端、行方向バイト詰め、
// この関数自体は1行分=widthPxピクセルのみを扱う)を、(x, y)を左上として1行だけ
// 描画する。黒だけでなく白も明示的に描く点がdrawIcon()(gfx/Icon.cpp、PROGMEM上の
// アイコン用、白は暗黙のフレームバッファ地色に任せている)と異なる: この関数は
// ページ画像全体を白紙の上に上書きする用途(MdImageReaderService参照)のため、
// 事前の白クリアを省略できるよう白画素も自分で描く。
// rowBytesはceil(widthPx/8)バイト以上の長さを持つこと。
void blitBitmapRow(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                    int x, int y, const uint8_t* rowBytes, int widthPx);

}  // namespace FrameBufferOps
