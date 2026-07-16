#include "FolderScreen.h"

#include <InputManager.h>

namespace {

void formatSize(uint32_t bytes, char* buf, size_t bufSize) {
  if (bytes < 1024) {
    snprintf(buf, bufSize, "%luB", static_cast<unsigned long>(bytes));
  } else if (bytes < 1024UL * 1024UL) {
    snprintf(buf, bufSize, "%luKB", static_cast<unsigned long>(bytes / 1024));
  } else {
    snprintf(buf, bufSize, "%luMB", static_cast<unsigned long>(bytes / (1024UL * 1024UL)));
  }
}

}  // namespace

FolderScreen::FolderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, FileBrowserService& fileBrowser)
    : fileBrowser_(fileBrowser),
      statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  footerItems_[0] = {"UP/DN", "MOVE"};
  footerItems_[1] = {"L/R", "IN/OUT"};
  footerItems_[2] = {"PG", pageLabel_};
  footer_.setItems(footerItems_, 3);

  layoutRows(fbWidth, fbHeight, font);
  reloadCurrentDirectory();
}

void FolderScreen::layoutRows(uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  const int rowH = RowHeight(font);
  const int listHeight = static_cast<int>(fbHeight) - kStatusBarHeight - kFooterHeight;
  int n = listHeight / rowH;
  if (n < 1) n = 1;
  if (n > kMaxVisibleRows) n = kMaxVisibleRows;
  rowsPerPage_ = n;

  for (int i = 0; i < kMaxVisibleRows; i++) {
    rows_[i].setBounds(Rect{0, kStatusBarHeight + i * rowH, static_cast<int>(fbWidth), rowH});
    rows_[i].setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  }
}

void FolderScreen::resetToRoot() {
  currentPath_ = "/";
  reloadCurrentDirectory();
}

void FolderScreen::reloadCurrentDirectory() {
  entries_ = fileBrowser_.listDirectory(currentPath_.c_str());
  focusIndex_ = 0;
  reloadRowWindowForFocus();
}

void FolderScreen::reloadRowWindowForFocus() {
  const int pageIndex = focusIndex_ / rowsPerPage_;
  const int startIdx = pageIndex * rowsPerPage_;
  const int total = static_cast<int>(entries_.size());
  int count = 0;
  for (int i = startIdx; i < total && count < rowsPerPage_; i++, count++) {
    const DirEntry& entry = entries_[i];
    rowLabels_[count] = entry.isDirectory ? ("[" + entry.name + "]") : entry.name;
    if (entry.isDirectory) {
      rowValues_[count] = "<DIR>";
    } else {
      char sizeBuf[16];
      formatSize(entry.size, sizeBuf, sizeof(sizeBuf));
      rowValues_[count] = sizeBuf;
    }
    rows_[count].setLabel(rowLabels_[count].c_str());
    rows_[count].setValue(rowValues_[count].c_str());
    rows_[count].setSelected(i == focusIndex_);
  }
  visibleRowCount_ = count;
  updateFooter();
}

void FolderScreen::updateFooter() {
  statusBar_.setLeftText(currentPath_.c_str());

  const int total = static_cast<int>(entries_.size());
  const int totalPages = total == 0 ? 1 : (total + rowsPerPage_ - 1) / rowsPerPage_;
  const int currentPage = (focusIndex_ / rowsPerPage_) + 1;
  snprintf(pageLabel_, sizeof(pageLabel_), "%d/%d", currentPage, totalPages);
}

void FolderScreen::enterSelectedIfDirectory() {
  if (focusIndex_ < 0 || focusIndex_ >= static_cast<int>(entries_.size())) return;
  const DirEntry& entry = entries_[focusIndex_];
  if (!entry.isDirectory) return;  // ファイルを開く処理はフェーズ3(TXT読書画面)以降

  currentPath_ = (currentPath_ == "/") ? ("/" + entry.name) : (currentPath_ + "/" + entry.name);
  reloadCurrentDirectory();
}

void FolderScreen::goToParent() {
  if (currentPath_ == "/") return;

  const int lastSlash = currentPath_.lastIndexOf('/');
  currentPath_ = (lastSlash <= 0) ? String("/") : currentPath_.substring(0, lastSlash);
  reloadCurrentDirectory();
}

void FolderScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  statusBar_.render(fb, fbWidth, fbHeight, font);

  if (entries_.empty()) {
    font.drawText(fb, fbWidth, fbHeight, 16, kStatusBarHeight + 20, "(EMPTY)");
  } else {
    for (int i = 0; i < visibleRowCount_; i++) {
      rows_[i].render(fb, fbWidth, fbHeight, font);
    }
  }

  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction FolderScreen::handleButton(uint8_t buttonIndex) {
  const int total = static_cast<int>(entries_.size());

  if (buttonIndex == InputManager::BTN_DOWN) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + 1) % total;
    reloadRowWindowForFocus();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_UP) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + total - 1) % total;
    reloadRowWindowForFocus();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_CONFIRM) {
    if (total == 0) return ScreenAction::kNone;
    enterSelectedIfDirectory();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_LEFT) {
    if (currentPath_ == "/") return ScreenAction::kNone;
    goToParent();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
