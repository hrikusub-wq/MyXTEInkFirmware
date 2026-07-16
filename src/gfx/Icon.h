#pragma once
#include <Arduino.h>

#include "../generated/icons_generated.h"

// scripts/convert_icons.pyでSVG(Material Symbols由来)から事前にラスタライズ・
// 1bpp化したアイコンをフレームバッファに描画する。MiniFontと同じビットマップ
// 転送ロジック(1バイト=横8px、MSBが左端、0=黒/1=白)を再利用しており、実機では
// SVGパース・ベクター描画・実行時スケーリングを一切行わない。
using IconId = IconAssets::IconId;

// 24px(リスト/フッター用)と40px(ホームグリッド用)の2サイズのみ用意している。
// 変換時にそれぞれ別のビットマップとしてラスタライズ済みのため、このenum以外の
// サイズは存在しない(実行時に拡大縮小はしない)。
enum class IconSize {
  kSmall = 24,
  kLarge = 40,
};

void drawIcon(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
              int x, int y, IconId id, IconSize size);
