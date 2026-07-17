#include "TxtReaderScreen.h"

#include <InputManager.h>

#include "../gfx/FrameBufferOps.h"
#include "../ui/SettingRow.h"

namespace {
// Markdownモードの太字/斜体記号(*)を取り除きながら、*で囲まれた区間だけboldFont
// (nullptrなら周囲と同じフォント)で描画する。単純に「*の連続がトグルスイッチ」
// として動作するだけで、*と**(斜体/太字)は区別しない(従来のstripAsterisks()と
// 同じ簡略化方針)。ページインデックスのバイトオフセット計算には影響しない
// (TxtReaderService::appendWrapped()は記号込みの生の行幅で折り返し判定するため)。
//
// 重要: "**"(連続する2文字)は1回のトグルとして扱う。1文字ずつ独立にトグルすると
// "**bold**"のような標準的な太字記法で、開始直後の"**"だけで2回トグルして
// オン→オフと打ち消し合ってしまい、常に非太字で描画されるバグになる
// (単独の"*italic*"は1文字ずつのトグルでも問題なかったため見過ごされていた)。
void drawMarkdownRuns(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y, const String& text,
                      const Font& normalFont, const Font* boldFont) {
  int cursorX = x;
  bool bold = false;
  String segment;

  auto flush = [&]() {
    if (segment.length() == 0) return;
    const Font& f = (bold && boldFont) ? *boldFont : normalFont;
    f.drawText(fb, fbWidth, fbHeight, cursorX, y, segment.c_str());
    cursorX += f.measureText(segment.c_str());
    segment = "";
  };

  size_t i = 0;
  const size_t len = text.length();
  while (i < len) {
    if (text[i] == '*') {
      flush();
      bold = !bold;
      // "**"は1つのトグルとして両方まとめて読み飛ばす。単独の"*"は1文字だけ進める。
      i += (i + 1 < len && text[i + 1] == '*') ? 2 : 1;
      continue;
    }
    segment += text[i];
    i++;
  }
  flush();
}
}  // namespace

TxtReaderScreen::TxtReaderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, AppSettings& settings)
    : font_(&font),
      settings_(settings),
      fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  // UP/DOWN(前/次ページ、または数行戻る/進む)は側面ボタンのためフッターには
  // 表示できない。
  footerItems_[0] = {PhysicalButton::kBack, "CLOSE"};
  footerItems_[1] = {PhysicalButton::kLeft, "", IconId::kChevronBackward, true};
  footerItems_[2] = {PhysicalButton::kRight, "", IconId::kChevronForward, true};
  // CONFIRMは読書設定オーバーレイを開く(表示方式の横(PAGE)/縦(SCROLL)切り替え等)。
  footerItems_[3] = {PhysicalButton::kConfirm, "SETTINGS"};
  footer_.setItems(footerItems_, 4);
  footer_.setTrailingText(pageLabel_);

  layout(font);
}

void TxtReaderScreen::layout(const Font& /*contentFont*/) {
  // ページに収まる行数(旧linesPerPage_)は本文フォント基準の固定値では決められない
  // (見出し等、行ごとにフォントの高さが異なるため。TxtReaderService::open()の
  // viewportHeightPxコメント参照)。ここでは純粋にピクセル領域の高さだけを渡し、
  // ページ区切りの計算自体はTxtReaderService側で行ごとの実際のlineHeight()を
  // 積算しながら行う。
  contentTop_ = kStatusBarHeight + kContentMargin;
  viewportWidthPx_ = static_cast<int>(fbWidth_) - kContentMargin * 2;
  const int contentBottom = static_cast<int>(fbHeight_) - kFooterHeight - kContentMargin;
  viewportHeightPx_ = contentBottom - contentTop_;
  if (viewportHeightPx_ < 1) viewportHeightPx_ = 1;
}

void TxtReaderScreen::setContentFont(const Font* contentFont) {
  contentFont_ = contentFont;
  layout(contentFont ? *contentFont : *font_);
}

void TxtReaderScreen::setHeadingFont1(const Font* headingFont) { headingFont1_ = headingFont; }

void TxtReaderScreen::setHeadingFont2(const Font* headingFont) { headingFont2_ = headingFont; }

void TxtReaderScreen::setHeadingFont3(const Font* headingFont) { headingFont3_ = headingFont; }

void TxtReaderScreen::setListFont(const Font* listFont) { listFont_ = listFont; }

void TxtReaderScreen::setBoldFont(const Font* boldFont) { boldFont_ = boldFont; }

bool TxtReaderScreen::openFile(const String& path) {
  String lower = path;
  lower.toLowerCase();
  markdownMode_ = lower.endsWith(".md") || lower.endsWith(".markdown");

  const Font* h1Arg = markdownMode_ ? &headingFont1() : nullptr;
  const Font* h2Arg = markdownMode_ ? &headingFont2() : nullptr;
  const Font* h3Arg = markdownMode_ ? &headingFont3() : nullptr;
  const Font* listFontArg = markdownMode_ ? &listFont() : nullptr;
  if (!reader_.open(path, contentFont(), viewportWidthPx_, viewportHeightPx_, markdownMode_, h1Arg, h2Arg, h3Arg,
                    listFontArg)) {
    return false;
  }

  const int lastSlash = path.lastIndexOf('/');
  titleText_ = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
  showCloseOverlay_ = false;
  showReadingSettings_ = false;
  editingScrollLines_ = false;
  updateStatusAndFooter();
  return true;
}

void TxtReaderScreen::updateStatusAndFooter() {
  statusBar_.setLeftText(titleText_.c_str());
  const bool scrolling = reader_.readMode() == TxtReaderService::ReadMode::kScroll;
  if (scrolling) {
    // SCROLL中はページ番号という概念が無いため、ファイル内の読了位置(%)を出す。
    snprintf(pageLabel_, sizeof(pageLabel_), "%d%%", reader_.progressPercent());
  } else {
    snprintf(pageLabel_, sizeof(pageLabel_), "%d/%d", reader_.currentPage() + 1, reader_.totalPages());
  }
}

void TxtReaderScreen::toggleReadMode() {
  const bool scrolling = reader_.readMode() == TxtReaderService::ReadMode::kScroll;
  reader_.setReadMode(scrolling ? TxtReaderService::ReadMode::kPaged : TxtReaderService::ReadMode::kScroll);
  updateStatusAndFooter();
}

void TxtReaderScreen::enterScrollLinesEdit() {
  scrollLinesDraft_ = settings_.scrollStepLines;
  editingScrollLines_ = true;
}

void TxtReaderScreen::adjustScrollLinesDraft(int delta) {
  int v = static_cast<int>(scrollLinesDraft_) + delta;
  if (v < 1) v = 1;
  if (v > 9) v = 9;
  scrollLinesDraft_ = static_cast<uint8_t>(v);
}

void TxtReaderScreen::commitScrollLinesEdit() {
  settings_.scrollStepLines = scrollLinesDraft_;
  SettingsService::save(settings_);
}

void TxtReaderScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  statusBar_.render(fb, fbWidth, fbHeight, font);

  const Font& body = contentFont();
  const Font& list = listFont();
  if (!reader_.isOpen() || reader_.currentLines().empty()) {
    body.drawText(fb, fbWidth, fbHeight, kContentMargin, contentTop_, "(EMPTY)");
  } else {
    int y = contentTop_;
    for (const auto& line : reader_.currentLines()) {
      const Font& lineFont =
          (line.headingLevel > 0) ? headingFontForLevel(line.headingLevel) : (line.isListItem ? list : body);

      String display = line.text;
      if (line.skipPrefixChars > 0 && line.skipPrefixChars <= display.length()) {
        display = display.substring(line.skipPrefixChars);
      }

      if (markdownMode_ && !line.rawContent) {
        drawMarkdownRuns(fb, fbWidth, fbHeight, kContentMargin, y, display, lineFont, boldFont_);
      } else {
        lineFont.drawText(fb, fbWidth, fbHeight, kContentMargin, y, display.c_str());
      }
      y += lineFont.lineHeight();
    }
  }

  footer_.render(fb, fbWidth, fbHeight, font);

  if (showCloseOverlay_) {
    drawCloseOverlay(fb, fbWidth, fbHeight, font);
  } else if (editingScrollLines_) {
    drawScrollLinesEdit(fb, fbWidth, fbHeight, font);
  } else if (showReadingSettings_) {
    drawReadingSettingsOverlay(fb, fbWidth, fbHeight, font);
  }
}

void TxtReaderScreen::drawCloseOverlay(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int boxW = static_cast<int>(fbWidth) - 64;
  const int lineH = font.lineHeight();
  const int boxH = lineH * 4 + 40;
  const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
  const int boxY = (static_cast<int>(fbHeight) - boxH) / 2;

  FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);  // 背景を白でクリア
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);

  const int textY = boxY + 16;
  font.drawText(fb, fbWidth, fbHeight, boxX + 16, textY, "CLOSE READER?");

  SettingRow closeRow(Rect{boxX + 16, textY + lineH + 12, boxW - 32, lineH + 10}, "CLOSE", "");
  closeRow.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  closeRow.setSelected(true);
  closeRow.render(fb, fbWidth, fbHeight, font);

  font.drawText(fb, fbWidth, fbHeight, boxX + 16, boxY + boxH - lineH - 12, "CONFIRM=OK  BACK=CANCEL");
}

void TxtReaderScreen::drawReadingSettingsOverlay(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                                                 const Font& font) const {
  const int lineH = font.lineHeight();
  const int rowH = lineH + 10;
  const int boxW = static_cast<int>(fbWidth) - 64;
  const int boxH = lineH + 16 + rowH * kReadingSettingsItemCount + 12;
  const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
  const int boxY = (static_cast<int>(fbHeight) - boxH) / 2;

  FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);  // 背景を白でクリア
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);

  const int textY = boxY + 16;
  font.drawText(fb, fbWidth, fbHeight, boxX + 16, textY, "READING SETTINGS");

  // 「横(PAGE)」「縦(SCROLL)」という表記はユーザー指定。UIチロームフォント
  // (このoverlay描画に使うfont)がMiniFontImpl(ASCII専用)のままだと漢字部分が
  // 豆腐表示になる可能性があるが、括弧内の英語表記は読める(SettingsScreenの
  // 「SYSTEM FONT」でCJKフォントを選べば漢字も正しく表示される)。
  const bool scrolling = reader_.readMode() == TxtReaderService::ReadMode::kScroll;
  const int listTop = textY + lineH + 12;

  SettingRow modeRow(Rect{boxX + 16, listTop, boxW - 32, rowH}, "MODE", scrolling ? "縦(SCROLL)" : "横(PAGE)");
  modeRow.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  modeRow.setSelected(readingSettingsFocus_ == 0);
  modeRow.render(fb, fbWidth, fbHeight, font);

  char scrollLinesBuf[4];
  snprintf(scrollLinesBuf, sizeof(scrollLinesBuf), "%u", settings_.scrollStepLines);
  SettingRow scrollLinesRow(Rect{boxX + 16, listTop + rowH, boxW - 32, rowH}, "SCROLL LINES", scrollLinesBuf);
  scrollLinesRow.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  scrollLinesRow.setSelected(readingSettingsFocus_ == 1);
  scrollLinesRow.render(fb, fbWidth, fbHeight, font);
}

void TxtReaderScreen::drawScrollLinesEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int boxW = static_cast<int>(fbWidth) - 64;
  const int lineH = font.lineHeight();
  const int boxH = lineH * 2 + 32;
  const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
  const int boxY = (static_cast<int>(fbHeight) - boxH) / 2;

  FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);

  const int titleY = boxY + 16;
  font.drawText(fb, fbWidth, fbHeight, boxX + 16, titleY, "SCROLL LINES");

  char buf[4];
  snprintf(buf, sizeof(buf), "%u", scrollLinesDraft_);
  font.drawText(fb, fbWidth, fbHeight, boxX + 16, titleY + lineH + 16, buf);
}

ScreenAction TxtReaderScreen::handleButton(uint8_t buttonIndex) {
  if (showCloseOverlay_) {
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      showCloseOverlay_ = false;
      reader_.close();
      return ScreenAction::kNavigateBack;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      showCloseOverlay_ = false;
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingScrollLines_) {
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_DOWN) {
      adjustScrollLinesDraft(-1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_UP) {
      adjustScrollLinesDraft(1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      commitScrollLinesEdit();
      editingScrollLines_ = false;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingScrollLines_ = false;  // 破棄(保存しない)
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (showReadingSettings_) {
    // SettingsScreenと同じ操作体系: LEFT/RIGHT・UP/DOWNどちらでもフォーカス移動、
    // 決定はCONFIRM。
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
      readingSettingsFocus_ = (readingSettingsFocus_ + kReadingSettingsItemCount - 1) % kReadingSettingsItemCount;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
      readingSettingsFocus_ = (readingSettingsFocus_ + 1) % kReadingSettingsItemCount;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      if (readingSettingsFocus_ == 0) {
        toggleReadMode();
      } else {
        enterScrollLinesEdit();
      }
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      showReadingSettings_ = false;
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  const bool scrolling = reader_.readMode() == TxtReaderService::ReadMode::kScroll;

  if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
    const bool moved = scrolling ? reader_.scrollBackward() : reader_.prevPage();
    if (!moved) return ScreenAction::kNone;
    updateStatusAndFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
    const bool moved = scrolling ? reader_.scrollForward(settings_.scrollStepLines) : reader_.nextPage();
    if (!moved) return ScreenAction::kNone;
    updateStatusAndFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    // 読書中(オーバーレイ非表示時)のCONFIRMは「読書設定」オーバーレイを開く
    // (いわゆる「選択ボタン」での切り替え)。
    readingSettingsFocus_ = 0;
    showReadingSettings_ = true;
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    showCloseOverlay_ = true;
    return ScreenAction::kRedraw;
  }

  return ScreenAction::kNone;
}
