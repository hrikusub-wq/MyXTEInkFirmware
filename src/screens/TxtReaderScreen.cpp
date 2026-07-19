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
  // 表示できない。RIGHTは未使用のためフッターに項目を出さない(空スロットになる)。
  footerItems_[0] = {PhysicalButton::kBack, "CLOSE"};
  // LEFTは短押し=ブックマーク追加、長押し=ブックマーク一覧(クラスコメント参照)。
  footerItems_[1] = {PhysicalButton::kLeft, "", IconId::kBookmark, true};
  // CONFIRMは読書設定オーバーレイを開く(表示方式の横(PAGE)/縦(SCROLL)切り替え等)。
  footerItems_[2] = {PhysicalButton::kConfirm, "SETTINGS"};
  footer_.setItems(footerItems_, 3);
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
  showReadingSettings_ = false;
  editingScrollLines_ = false;
  showBookmarkList_ = false;
  showBookmarkToast_ = false;
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

void TxtReaderScreen::enterBookmarkList() {
  bookmarkListFocus_ = 0;
  showBookmarkList_ = true;
}

void TxtReaderScreen::addBookmark() {
  if (!reader_.addBookmark()) return;
  showBookmarkToast_ = true;
}

void TxtReaderScreen::openBookmarkList() { enterBookmarkList(); }

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

  if (editingScrollLines_) {
    drawScrollLinesEdit(fb, fbWidth, fbHeight, font);
  } else if (showBookmarkList_) {
    drawBookmarkList(fb, fbWidth, fbHeight, font);
  } else if (showReadingSettings_) {
    drawReadingSettingsOverlay(fb, fbWidth, fbHeight, font);
  } else if (showBookmarkToast_) {
    // 「BOOKMARKED」の一瞬だけの確認表示。他のオーバーレイと同時には出さない
    // (オーバーレイを開いた時点でhandleButton()冒頭がトーストを消すため)。
    const int lineH = font.lineHeight();
    const int textW = font.measureText("BOOKMARKED");
    const int boxW = textW + 24;
    const int boxH = lineH + 16;
    const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
    const int boxY = contentTop_ + 8;
    FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);
    FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);
    font.drawText(fb, fbWidth, fbHeight, boxX + 12, boxY + 8, "BOOKMARKED");
  }
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

  std::vector<Bookmark> bookmarks;
  reader_.readBookmarks(bookmarks);
  char bookmarkCountBuf[8];
  snprintf(bookmarkCountBuf, sizeof(bookmarkCountBuf), "%u", static_cast<unsigned>(bookmarks.size()));
  SettingRow bookmarksRow(Rect{boxX + 16, listTop + rowH * 2, boxW - 32, rowH}, "BOOKMARKS", bookmarkCountBuf);
  bookmarksRow.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  bookmarksRow.setSelected(readingSettingsFocus_ == 2);
  bookmarksRow.render(fb, fbWidth, fbHeight, font);
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

void TxtReaderScreen::drawBookmarkList(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  std::vector<Bookmark> bookmarks;
  reader_.readBookmarks(bookmarks);

  const int lineH = font.lineHeight();
  const int rowH = lineH + 10;
  const int boxW = static_cast<int>(fbWidth) - 64;
  // 一覧が空でも最低1行分の高さは確保する("NO BOOKMARKS"表示用)。
  const int rowCount = bookmarks.empty() ? 1 : static_cast<int>(bookmarks.size());
  const int boxH = lineH + 16 + rowH * rowCount + 12;
  const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
  const int boxY = (static_cast<int>(fbHeight) - boxH) / 2;

  FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);

  const int textY = boxY + 16;
  font.drawText(fb, fbWidth, fbHeight, boxX + 16, textY, "BOOKMARKS");

  const int listTop = textY + lineH + 12;
  if (bookmarks.empty()) {
    font.drawText(fb, fbWidth, fbHeight, boxX + 16, listTop, "NO BOOKMARKS");
    return;
  }

  for (size_t i = 0; i < bookmarks.size(); i++) {
    char valueBuf[8];
    snprintf(valueBuf, sizeof(valueBuf), "%d%%", bookmarks[i].percent);
    // previewが返す一時Stringはこのループの間だけ生存させればよい(SettingRowは
    // ポインタで保持するため、render()呼び出しをまたいで生存させる必要はない)。
    SettingRow row(Rect{boxX + 16, listTop + static_cast<int>(i) * rowH, boxW - 32, rowH},
                  bookmarks[i].preview.c_str(), valueBuf);
    row.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
    row.setSelected(static_cast<int>(i) == bookmarkListFocus_);
    row.render(fb, fbWidth, fbHeight, font);
  }
}

ScreenAction TxtReaderScreen::handleButton(uint8_t buttonIndex) {
  showBookmarkToast_ = false;  // 次の操作が来たらブックマーク追加のトーストを消す

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

  if (showBookmarkList_) {
    std::vector<Bookmark> bookmarks;
    reader_.readBookmarks(bookmarks);
    const int total = static_cast<int>(bookmarks.size());

    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
      if (total > 0) bookmarkListFocus_ = (bookmarkListFocus_ + total - 1) % total;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
      if (total > 0) bookmarkListFocus_ = (bookmarkListFocus_ + 1) % total;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      if (total > 0 && reader_.jumpToBookmark(bookmarkListFocus_)) {
        // ジャンプしたら一覧・読書設定オーバーレイとも閉じて読書画面に戻る。
        showBookmarkList_ = false;
        showReadingSettings_ = false;
        updateStatusAndFooter();
      }
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      showBookmarkList_ = false;
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
      } else if (readingSettingsFocus_ == 1) {
        enterScrollLinesEdit();
      } else {
        enterBookmarkList();
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

  // LEFTは通常時はmain.cpp側で短押し/長押し(ブックマーク追加/一覧)として横取り
  // されるためここには届かない。RIGHTは未使用(クラスコメント参照)。ページ送り/
  // スクロールはUP/DOWNのみに一本化している。
  if (buttonIndex == InputManager::BTN_UP) {
    const bool moved = scrolling ? reader_.scrollBackward() : reader_.prevPage();
    if (!moved) return ScreenAction::kNone;
    updateStatusAndFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_DOWN) {
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
    reader_.close();
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
