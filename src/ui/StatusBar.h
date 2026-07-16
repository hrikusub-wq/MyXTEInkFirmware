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

  // 残量(0-100)を設定する。表示アイコンはこの値に応じて自動選択される。
  // 充電中かどうかの判定はopen-x4-sdkのBatteryMonitorにAPIがなく(X4専用の
  // ADC分圧方式でX3のBQ27220 I2Cには非対応)、今回は未対応。
  void setBatteryPercent(int percent) { batteryPercent_ = percent; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const char* leftText_ = "";
  int batteryPercent_ = 100;
};
