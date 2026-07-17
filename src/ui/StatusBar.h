#pragma once
#include "Rect.h"
#include "Widget.h"
#include "../gfx/Icon.h"

// 画面上部固定のステータスバー。左側に任意テキスト(時刻・パンくずパスなど、
// 使い分けは呼び出し側が行う)、右側にバッテリー残量(アイコン+%数値)を表示する。
class StatusBar : public Widget {
 public:
  explicit StatusBar(Rect bounds) : bounds_(bounds) {}

  void setLeftText(const char* text) { leftText_ = text; }

  // 残量(0-100)を設定する。充電中でなければこの値に応じてアイコンを自動選択する。
  void setBatteryPercent(int percent) { batteryPercent_ = percent; }

  // 充電中かどうか。trueの間は残量に関わらずbattery_chargingアイコンを表示する。
  void setBatteryCharging(bool charging) { batteryCharging_ = charging; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const char* leftText_ = "";
  int batteryPercent_ = 100;
  bool batteryCharging_ = false;
};
