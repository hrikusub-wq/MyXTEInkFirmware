#pragma once
#include "Rect.h"
#include "Widget.h"

// ホーム画面の4分割グリッドで使うボタン。アイコン(今はプレースホルダー図形)+
// ラベルの縦積みレイアウト。選択中は背景反転(黒背景・白文字)になる。
class HomeGridButton : public Widget {
 public:
  HomeGridButton() : bounds_{}, label_("") {}
  HomeGridButton(Rect bounds, const char* label) : bounds_(bounds), label_(label) {}

  void setLabel(const char* label) { label_ = label; }
  void setSelected(bool selected) { selected_ = selected; }
  void setBounds(const Rect& bounds) { bounds_ = bounds; }
  bool isSelected() const { return selected_; }
  const Rect& bounds() const { return bounds_; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const char* label_;
  bool selected_ = false;
};
