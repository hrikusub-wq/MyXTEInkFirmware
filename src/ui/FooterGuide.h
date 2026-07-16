#pragma once
#include "Rect.h"
#include "Widget.h"
#include "../gfx/Icon.h"

// X3実機の物理ボタンのうち、画面下部(底面)に横一列で並んでいるのは
// BACK/CONFIRM/LEFT/RIGHTの4つのみ(実機写真で確認済み)。UP/DOWNは左右側面に
// あるため画面下部では位置合わせできず、フッターガイドの対象外としている。
// FooterGuideItemのbuttonでこの位置を指定すると、対応するボタンの真上
// (画面をこの4等分したスロットの中央)にアイコン/説明が配置される。
enum class PhysicalButton {
  kBack = 0,
  kConfirm = 1,
  kLeft = 2,
  kRight = 3,
};

// フッターガイドの1項目。iconに値があれば「アイコン+description」、なければ
// description文字列のみを表示する。
struct FooterGuideItem {
  PhysicalButton button;
  const char* description;
  IconId icon = IconId::kFolder;
  bool hasIcon = false;
};

// 画面下部固定のフッターガイド。底面の物理ボタン(BACK/CONFIRM/LEFT/RIGHT)の
// 操作案内を、対応するボタンの真上(横一列4等分スロットの中央)に表示する。
// ページ番号などボタン位置に対応しない補足情報は setTrailingText() で右端に
// 固定表示する。
class FooterGuide : public Widget {
 public:
  explicit FooterGuide(Rect bounds) : bounds_(bounds) {}

  void setItems(const FooterGuideItem* items, size_t count) {
    items_ = items;
    count_ = count;
  }
  void setTrailingText(const char* text) { trailingText_ = text; }

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const override;

 private:
  Rect bounds_;
  const FooterGuideItem* items_ = nullptr;
  size_t count_ = 0;
  const char* trailingText_ = nullptr;
};
