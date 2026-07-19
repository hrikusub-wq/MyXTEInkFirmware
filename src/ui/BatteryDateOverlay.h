#pragma once

#include <stdint.h>
#include "../gfx/Font.h"
#include "../core/RtcService.h"

namespace BatteryDateOverlay {

// 待機画面・ホーム画面で共有するバッテリー＆日付描画。
// percent: バッテリー残量(0-100)。負の値の場合は描画しない(未取得など)
// charging: 充電中かどうか
// date: RTC時刻。nullptrまたは無効な場合は描画しない
// drawBackgroundBox: trueの場合、文字の下に白い背景(fillRect, false)を敷いて写真と重なっても読めるようにする。
// x, y: 描画の基準座標。rightAlign=trueの場合はxを右端の座標として扱う。
// 戻り値: 描画した領域の高さ
int drawBatteryAndDate(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font,
                       int x, int y, int percent, bool charging, const RtcDateTime* date,
                       bool drawBackgroundBox, bool rightAlign = false);

}  // namespace BatteryDateOverlay
