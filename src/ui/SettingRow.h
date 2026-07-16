#pragma once
#include "Rect.h"
#include "Widget.h"

// 2カラム設定行(左:ラベル、右:現在値)。設定画面・読書中メニューの両方で使う
// 共通コンポーネント。選択中は枠線または背景反転で強調する(SelectionStyleで切替)。
class SettingRow : public Widget {
 public:
  enum class SelectionStyle {
    kOutline,  // 角丸枠線(実装簡略化のため直角枠)
    kInvert,   // 背景反転(黒地に白文字)
  };

  SettingRow(Rect bounds, const char* label, const char* value)
      : bounds_(bounds), label_(label), value_(value) {}

  void setLabel(const char* label) { label_ = label; }
  void setValue(const char* value) { value_ = value; }
  void setSelected(bool selected) { selected_ = selected; }
  void setSelectionStyle(SelectionStyle style) { style_ = style; }
  bool isSelected() const { return selected_; }
  const Rect& bounds() const { return bounds_; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const char* label_;
  const char* value_;
  bool selected_ = false;
  SelectionStyle style_ = SelectionStyle::kOutline;
};
