#include "SettingRow.h"

#include "../gfx/FrameBufferOps.h"

namespace {
constexpr int kPadding = 8;
// アイコンサイズはSettingRow::kIconPx(ヘッダで公開、IconSize::kLarge=40px)を使う。
// 以前はIconSize::kSmall(24px)だったが、「リストの視認性を上げてほしい」という
// フィードバックを受けてアイコンを大きくした(行の高さ側は各画面のkRowPadding
// 拡大とSettingRow::kIconPxを考慮したRowHeight()計算で追従させている)。
constexpr int kIconLabelGap = 10;  // アイコンが大きくなった分、ラベルとの間隔も拡大
}  // namespace

void SettingRow::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  // kGrayHighlight選択時の背景(薄いグレーのドットパターン)は文字より先に敷く。各フォント
  // 実装のdrawText()は"文字の黒画素だけをsetBlackPixel()で描く透過描画"のため
  // (MiniFont::drawChar()/CjkFontImpl::drawText()/XteinkBinFontImpl::drawText()参照)、
  // 先に背景を敷いてから文字を上描きしても文字が欠けたり汚れたりしない。
  if (selected_ && style_ == SelectionStyle::kGrayHighlight) {
    FrameBufferOps::fillRectLightGrayDither(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  }

  const int textY = bounds_.y + (bounds_.h - font.lineHeight()) / 2;
  int labelX = bounds_.x + kPadding;

  if (hasIcon_) {
    const int iconY = bounds_.y + (bounds_.h - kIconPx) / 2;
    drawIcon(fb, fbWidth, fbHeight, labelX, iconY, icon_, IconSize::kLarge);
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

  // kGrayHighlightの背景は関数冒頭で(文字より先に)描画済みなので、ここではkOutlineのみ扱う。
  if (selected_ && style_ == SelectionStyle::kOutline) {
    FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, bounds_.x, bounds_.y, bounds_.w, bounds_.h);
  }
}
