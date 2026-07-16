#pragma once
#include "Rect.h"
#include "Widget.h"

// 画面上部固定のステータスバー。左側と右側に任意テキストを表示する汎用コンポーネント。
// 設定画面では左に時刻、フォルダ画面では左にパンくずパスを表示する、といった
// 使い分けを呼び出し側が行う想定(このクラス自体は文字列の意味を知らない)。
class StatusBar : public Widget {
 public:
  explicit StatusBar(Rect bounds) : bounds_(bounds) {}

  void setLeftText(const char* text) { leftText_ = text; }
  void setRightText(const char* text) { rightText_ = text; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const char* leftText_ = "";
  const char* rightText_ = "";
};
