#include "SettingRow.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
constexpr int kIconPx = static_cast<int>(IconSize::kSmall);
constexpr int kIconLabelGap = 6;
}  // namespace

void SettingRow::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;
  int labelX = bounds_.x + kPadding;

  if (hasIcon_) {
    const int iconY = bounds_.y + (bounds_.h - kIconPx) / 2;
    drawIcon(fb, fbWidth, fbHeight, labelX, iconY, icon_, IconSize::kSmall);
    labelX += kIconPx + kIconLabelGap;
  }

  const int valueWidth = font.measureText(value_);
  const int valueX = bounds_.x + bounds_.w - kPadding - valueWidth;

  // ラベルと値がぶつからないよう、ラベル側の使える幅を「値の左端 - ラベルの左端」に
  // 制限する。フォント次第(特に24pt級の.binフォントのように1文字が非常に幅広い場合)では
  // 元のラベルがこの幅に収まらないことがあるため、その場合は末尾を"..."に置き換えて
  // 収まる長さまで切り詰める(measureText()はフォント実装に関わらず必ず使えるため、
  // px単位の決め打ちをせずフォント非依存に判定できる)。
  const int availableLabelWidth = valueX - labelX - kPadding;
  String label(label_);
  if (availableLabelWidth > 0 && font.measureText(label.c_str()) > availableLabelWidth) {
    const char* kEllipsis = "...";
    while (label.length() > 0 && font.measureText((label + kEllipsis).c_str()) > availableLabelWidth) {
      label.remove(label.length() - 1);
    }
    label += kEllipsis;
  }

  font.drawText(fb, fbWidth, fbHeight, labelX, textY, label.c_str());
  font.drawText(fb, fbWidth, fbHeight, valueX, textY, value_);

  if (!selected_) return;

  if (style_ == SelectionStyle::kOutline) {
    FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  } else {
    // 白背景に黒文字で描いた内容を丸ごと反転させ、黒背景に白文字にする。
    FrameBufferOps::invertRect(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  }
}
