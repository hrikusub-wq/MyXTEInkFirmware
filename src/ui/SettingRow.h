#pragma once
#include "Rect.h"
#include "Widget.h"
#include "../gfx/Icon.h"

// 2カラム設定行(左:ラベル、右:現在値)。設定画面・読書中メニュー・フォルダ画面の
// 一覧表示で使う共通コンポーネント。選択中は枠線または薄いグレーのドット背景で
// 強調する(SelectionStyleで切替)。setIcon()を呼んだ場合、ラベルの前に24pxアイコンを
// 表示する(フォルダ画面のファイル/フォルダ種別アイコン用。呼ばなければ従来通り
// アイコンなし)。
class SettingRow : public Widget {
 public:
  enum class SelectionStyle {
    kOutline,        // 角丸枠線(実装簡略化のため直角枠)
    kGrayHighlight,  // 薄いグレーのドットパターン背景(黒文字はそのまま)。以前は
                     // 黒背景反転だったが、選択の主張が強すぎたためグレーに変更した
                     // (FrameBufferOps::fillRectLightGrayDither()参照)。
  };

  // デフォルト構築(可変長リストでの配列確保用)。setBounds/setLabel/setValueで
  // 後から内容を設定する。
  SettingRow() : bounds_{}, label_(""), value_("") {}
  SettingRow(Rect bounds, const char* label, const char* value)
      : bounds_(bounds), label_(label), value_(value) {}

  void setLabel(const char* label) { label_ = label; }
  void setValue(const char* value) { value_ = value; }
  void setIcon(IconId icon) { icon_ = icon; hasIcon_ = true; }
  void clearIcon() { hasIcon_ = false; }
  void setSelected(bool selected) { selected_ = selected; }
  void setSelectionStyle(SelectionStyle style) { style_ = style; }
  // 可変長リスト(フォルダ画面など)で行を使い回す際に、位置・高さを再設定する用。
  void setBounds(const Rect& bounds) { bounds_ = bounds; }
  bool isSelected() const { return selected_; }
  const Rect& bounds() const { return bounds_; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

  // アイコン付き行の描画サイズ(SettingRow.cpp参照)。一覧画面側がkIconPxを含めた
  // 行の高さ(font.lineHeight()とkIconPxの大きい方+パディング)を計算する際に
  // 参照できるよう公開している(FolderScreen::RowHeight()等参照)。
  static constexpr int kIconPx = static_cast<int>(IconSize::kLarge);

 private:
  Rect bounds_;
  const char* label_;
  const char* value_;
  IconId icon_ = IconId::kFolder;
  bool hasIcon_ = false;
  bool selected_ = false;
  SelectionStyle style_ = SelectionStyle::kOutline;
};
