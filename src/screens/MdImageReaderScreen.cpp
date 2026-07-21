#include "MdImageReaderScreen.h"

#include <InputManager.h>

#include "../gfx/FrameBufferOps.h"
#include "../ui/SettingRow.h"

MdImageReaderScreen::MdImageReaderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font)
    : font_(&font),
      fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  // UP/DOWN(前/次ページ)は側面ボタンのためフッターには表示できない
  // (TxtReaderScreenと同じ理由)。RIGHT/CONFIRMは未使用のためフッターに項目を
  // 出さない(空スロットになる)。
  footerItems_[0] = {PhysicalButton::kBack, "CLOSE"};
  // LEFTは短押し=ブックマーク追加、長押し=ブックマーク一覧(TxtReaderScreenと同じ、
  // main.cpp側でisOverlayShown()を見て判定する)。
  footerItems_[1] = {PhysicalButton::kLeft, "", IconId::kBookmark, true};
  footer_.setItems(footerItems_, 2);
  footer_.setTrailingText(pageLabel_);
}

bool MdImageReaderScreen::openFile(const String& path) {
  if (!reader_.open(path)) return false;

  const int lastSlash = path.lastIndexOf('/');
  titleText_ = (lastSlash >= 0) ? path.substring(lastSlash + 1) : path;
  showBookmarkList_ = false;
  showBookmarkToast_ = false;
  updateFooter();
  return true;
}

void MdImageReaderScreen::updateFooter() {
  snprintf(pageLabel_, sizeof(pageLabel_), "%d/%d", reader_.currentPage() + 1, reader_.totalPages());
}

void MdImageReaderScreen::addBookmark() {
  if (!reader_.addBookmark()) return;
  showBookmarkToast_ = true;
}

void MdImageReaderScreen::openBookmarkList() {
  bookmarkListFocus_ = 0;
  showBookmarkList_ = true;
}

void MdImageReaderScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  if (!reader_.isOpen() || !reader_.renderCurrentPage(fb, fbWidth, fbHeight, kContentMargin, kContentMargin)) {
    font.drawText(fb, fbWidth, fbHeight, kContentMargin, kContentMargin, "(EMPTY)");
  }

  footer_.render(fb, fbWidth, fbHeight, font);

  if (showBookmarkList_) {
    drawBookmarkList(fb, fbWidth, fbHeight, font);
  } else if (showBookmarkToast_) {
    // 「BOOKMARKED」の一瞬だけの確認表示(TxtReaderScreenと同じ見た目)。
    const int lineH = font.lineHeight();
    const int textW = font.measureText("BOOKMARKED");
    const int boxW = textW + 24;
    const int boxH = lineH + 16;
    const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
    const int boxY = kContentMargin + 8;
    FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);
    FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);
    font.drawText(fb, fbWidth, fbHeight, boxX + 12, boxY + 8, "BOOKMARKED");
  }
}

void MdImageReaderScreen::drawBookmarkList(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  std::vector<MdImageReaderService::Bookmark> bookmarks;
  reader_.readBookmarks(bookmarks);

  const int lineH = font.lineHeight();
  const int rowH = lineH + 10;
  const int boxW = static_cast<int>(fbWidth) - 64;
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
    SettingRow row(Rect{boxX + 16, listTop + static_cast<int>(i) * rowH, boxW - 32, rowH},
                  bookmarks[i].preview.c_str(), valueBuf);
    row.setSelectionStyle(SettingRow::SelectionStyle::kGrayHighlight);
    row.setSelected(static_cast<int>(i) == bookmarkListFocus_);
    row.render(fb, fbWidth, fbHeight, font);
  }
}

ScreenAction MdImageReaderScreen::handleButton(uint8_t buttonIndex) {
  showBookmarkToast_ = false;  // 次の操作が来たらブックマーク追加のトーストを消す

  if (showBookmarkList_) {
    std::vector<MdImageReaderService::Bookmark> bookmarks;
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
        showBookmarkList_ = false;
        updateFooter();
      }
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      showBookmarkList_ = false;
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  // LEFTは通常時はmain.cpp側で短押し/長押し(ブックマーク追加/一覧)として横取り
  // されるためここには届かない(TxtReaderScreenと同じ)。RIGHT/CONFIRMは未使用。
  if (buttonIndex == InputManager::BTN_UP) {
    if (!reader_.prevPage()) return ScreenAction::kNone;
    updateFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_DOWN) {
    if (!reader_.nextPage()) return ScreenAction::kNone;
    updateFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    reader_.close();
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
