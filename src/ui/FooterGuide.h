#pragma once
#include "Rect.h"
#include "Widget.h"

// フッターガイドの1項目(ボタン名+機能説明)。例: {"BACK", "戻る"}。
struct FooterGuideItem {
  const char* buttonLabel;
  const char* description;
};

// 画面下部固定のフッターガイド。物理ボタンの操作案内を横並びで表示する。
// ページネーション表示("1 / 2"など)がある画面は、その文字列も1項目として渡せばよい。
class FooterGuide : public Widget {
 public:
  explicit FooterGuide(Rect bounds) : bounds_(bounds) {}

  void setItems(const FooterGuideItem* items, size_t count) {
    items_ = items;
    count_ = count;
  }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const FooterGuideItem* items_ = nullptr;
  size_t count_ = 0;
};
