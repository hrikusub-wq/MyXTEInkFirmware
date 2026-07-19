#pragma once
#include "Rect.h"
#include "Widget.h"
#include "../gfx/Icon.h"

// ホーム画面の4分割グリッドで使うボタン。アイコン(40px)+ラベルの縦積みレイアウト。
// 選択中は薄いグレーのドットパターン背景になる(SettingRowと同じ手法)。setIcon()を
// 呼ばない場合はアイコンを描画しない(空きスロット用)。
class HomeGridButton : public Widget {
 public:
  HomeGridButton() : bounds_{}, label_("") {}
  HomeGridButton(Rect bounds, const char* label) : bounds_(bounds), label_(label) {}

  void setLabel(const char* label) { label_ = label; }
  void setIcon(IconId icon) { icon_ = icon; hasIcon_ = true; }
  void setSelected(bool selected) { selected_ = selected; }
  void setBounds(const Rect& bounds) { bounds_ = bounds; }
  bool isSelected() const { return selected_; }
  const Rect& bounds() const { return bounds_; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const char* label_;
  IconId icon_ = IconId::kFolder;
  bool hasIcon_ = false;
  bool selected_ = false;
};
