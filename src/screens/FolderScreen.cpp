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

// フェーズ6時点で読書画面が対応しているのはTXT/Markdownのみ(epubは将来のフェーズ)。
bool isReadableFile(const DirEntry& entry) {
  if (entry.isDirectory) return false;
  String lowerName = entry.name;
  lowerName.toLowerCase();
  return lowerName.endsWith(".txt") || lowerName.endsWith(".md") || lowerName.endsWith(".markdown");
}

IconId iconForEntry(const DirEntry& entry) {
  if (entry.isDirectory) return IconId::kFolder;

  String lowerName = entry.name;
  lowerName.toLowerCase();
  if (lowerName.endsWith(".txt")) return IconId::kDescription;
  if (lowerName.endsWith(".md") || lowerName.endsWith(".markdown")) return IconId::kMarkdown;
  if (lowerName.endsWith(".epub")) return IconId::kImportContacts;
  // .binは現状XTEinkToolkit等で変換済みのカスタムフォントファイル専用として扱う。
  // 将来.bin拡張子がフォント以外(テーマファイル等)にも使われるようになったら、
  // 拡張子だけでなくファイル先頭のマジックナンバーで判定する仕組みへの
  // 拡張を検討すること。
  if (lowerName.endsWith(".bin")) return IconId::kFontDownload;
  return IconId::kBook;  // 拡張子不明のファイルは汎用の本アイコンで代用
}

}  // namespace

FolderScreen::FolderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, FileBrowserService& fileBrowser)
    : fileBrowser_(fileBrowser),
      fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  statusBar_.setBatteryPercent(87);

  // UP/DOWN(リストのフォーカス移動、LEFT/RIGHTと同じ意味)は側面ボタンのため
  // フッターには表示できない。
  footerItems_[0] = {PhysicalButton::kBack, "HOME"};
  footerItems_[1] = {PhysicalButton::kConfirm, "", IconId::kCheck, true};
  footerItems_[2] = {PhysicalButton::kLeft, "", IconId::kChevronBackward, true};
  footerItems_[3] = {PhysicalButton::kRight, "", IconId::kChevronForward, true};
  footer_.setItems(footerItems_, 4);
  footer_.setTrailingText(pageLabel_);

  layoutRows(font);
  reloadCurrentDirectory();
}

void FolderScreen::layoutRows(const Font& font) {
  const int rowH = RowHeight(font);
  const int listHeight = static_cast<int>(fbHeight_) - kStatusBarHeight - kFooterHeight;
  int n = listHeight / rowH;
  if (n < 1) n = 1;
  if (n > kMaxVisibleRows) n = kMaxVisibleRows;
  rowsPerPage_ = n;

  for (int i = 0; i < kMaxVisibleRows; i++) {
    rows_[i].setBounds(Rect{0, kStatusBarHeight + i * rowH, static_cast<int>(fbWidth_), rowH});
    rows_[i].setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  }
}

void FolderScreen::relayout(const Font& font) {
  layoutRows(font);
  // 1ページの行数(rowsPerPage_)が変わった可能性があるため、現在のフォーカス位置を
  // 基準に表示ウィンドウ(rows_)とページ番号表示を作り直す。
  reloadRowWindowForFocus();
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
    rowLabels_[count] = entry.name;
    if (entry.isDirectory) {
      rowValues_[count] = "<DIR>";
    } else {
      char sizeBuf[16];
      formatSize(entry.size, sizeBuf, sizeof(sizeBuf));
      rowValues_[count] = sizeBuf;
    }
    rows_[count].setLabel(rowLabels_[count].c_str());
    rows_[count].setValue(rowValues_[count].c_str());
    rows_[count].setIcon(iconForEntry(entry));
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

  // BACKはルートでは「ホームへ戻る」、それ以外では「親ディレクトリへ戻る」という
  // 意味に変わる(handleButton()参照)ため、フッターの表示もそれに合わせる
  // (どちらも文字列リテラルなのでポインタを直接差し替えるだけでよい)。
  footerItems_[0].description = (currentPath_ == "/") ? "HOME" : "UP";
}

void FolderScreen::enterSelectedIfDirectory() {
  if (focusIndex_ < 0 || focusIndex_ >= static_cast<int>(entries_.size())) return;
  const DirEntry& entry = entries_[focusIndex_];
  if (!entry.isDirectory) return;

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

  // フォーカス移動はLEFT/RIGHT・UP/DOWNのどちらでも同じ意味(他のリスト画面と統一)。
  // 「入る/開く」はCONFIRM(またはUP長押しのショートカット)のみに限定し、UP単体の
  // 短押しが誤って選択操作にならないようにする。
  if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + 1) % total;
    reloadRowWindowForFocus();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + total - 1) % total;
    reloadRowWindowForFocus();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    if (total == 0 || focusIndex_ < 0 || focusIndex_ >= total) return ScreenAction::kNone;
    const DirEntry& entry = entries_[focusIndex_];
    if (entry.isDirectory) {
      enterSelectedIfDirectory();
      return ScreenAction::kRedraw;
    }
    if (isReadableFile(entry)) {
      pendingOpenPath_ = (currentPath_ == "/") ? ("/" + entry.name) : (currentPath_ + "/" + entry.name);
      return ScreenAction::kOpenFile;
    }
    return ScreenAction::kNone;  // TXT/Markdown以外はまだ読書画面が対応していない
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    // ルートでなければ親ディレクトリへ1段戻るだけ、ルートまで戻ったらホームへ抜ける
    // (DOWN長押しのショートカットからもここに来る)。
    if (currentPath_ != "/") {
      goToParent();
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
