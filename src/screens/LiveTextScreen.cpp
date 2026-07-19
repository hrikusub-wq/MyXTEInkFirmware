#include "LiveTextScreen.h"

#include <InputManager.h>

LiveTextScreen::LiveTextScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble)
    : ble_(ble),
      font_(&font),
      fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  footerItems_[0] = {PhysicalButton::kBack, "CLOSE"};
  footer_.setItems(footerItems_, 1);
  footer_.setTrailingText(pageLabel_);

  layout();
}

void LiveTextScreen::layout() {
  contentTop_ = kContentMargin;
  viewportWidthPx_ = static_cast<int>(fbWidth_) - kContentMargin * 2;
  const int contentBottom = static_cast<int>(fbHeight_) - kFooterHeight - kContentMargin;
  viewportHeightPx_ = contentBottom - contentTop_;
  if (viewportHeightPx_ < 1) viewportHeightPx_ = 1;
}

void LiveTextScreen::setContentFont(const Font* contentFont) {
  contentFont_ = contentFont;
  layout();
}

void LiveTextScreen::openFile(const String& absPath) {
  currentPath_ = absPath;
  const int lastSlash = absPath.lastIndexOf('/');
  titleText_ = (lastSlash >= 0) ? absPath.substring(lastSlash + 1) : absPath;

  reader_.close();
  // まだPCから一度も送られていない場合はopen()が失敗するが、それでも画面には
  // 入る(render()が「まだ受信していません」の空表示を出す)。
  reader_.open(currentPath_, contentFont(), viewportWidthPx_, viewportHeightPx_);
  // ファイルハンドルは開いたままにせず、表示に必要な内容(ページインデックス・
  // currentLines())を読み込んだらすぐ閉じる。SDカードは同じファイルを読み書き
  // 両方のハンドルで同時に開けないため、開いたままだとPC側からの新しい書き込み
  // ("P:...")が毎回IOエラーで失敗してしまう(実機で確認した不具合)。
  reader_.closeFileHandle();
  pendingReloadDueMs_ = 0;
  reloadCount_ = 0;
  needsFullRefreshPending_ = false;
  updateStatusAndFooter();

  ble_.beginLiveWatch();
}

void LiveTextScreen::closeFile() {
  ble_.endLiveWatch();
  reader_.close();
  pendingReloadDueMs_ = 0;
  // 常に一時ファイルとして扱う設計(クラスコメント参照)。閉じるたびにSDから
  // 削除し、次にホーム画面「LIVE TEXT」から開いたときはPC側の次の保存を
  // 待つ空の状態から始まる。
  SdMan.remove(currentPath_.c_str());
}

void LiveTextScreen::updateStatusAndFooter() {
  snprintf(pageLabel_, sizeof(pageLabel_), "%d/%d", reader_.currentPage() + 1, reader_.totalPages());
}

bool LiveTextScreen::reloadFromDisk() {
  // reader_.open()は内部でclose()を呼ぶため、ここで明示的に呼ぶ必要はない。
  const bool ok = reader_.open(currentPath_, contentFont(), viewportWidthPx_, viewportHeightPx_);
  // openFile()と同じ理由でファイルハンドルは保持しない(上記コメント参照)。
  reader_.closeFileHandle();

  if (!ok) {
    // PC側がまだ書き込み中(ファイルハンドルを保持したまま)のタイミングと
    // 重なった等、一時的にreader_.open()が失敗した可能性がある。ここで
    // 諦めると内容が古いまま(または「まだ受信していません」表示のまま)固定
    // されてしまい、ユーザーが手動で画面を出入りし直すまで直らない不具合に
    // なっていた。短時間後に自動的に再試行する(kReloadRetryMs参照)。
    pendingReloadDueMs_ = millis() + kReloadRetryMs;
    return false;
  }

  updateStatusAndFooter();
  reloadCount_++;
  needsFullRefreshPending_ = (reloadCount_ % kFullRefreshEvery == 0);
  return true;
}

bool LiveTextScreen::pollForUpdate() {
  if (ble_.consumeLiveTextUpdated()) {
    // 短時間の連続更新をまとめるため、締め切りを都度延長するだけにする
    // (即座にリロードしない)。
    pendingReloadDueMs_ = millis() + kLiveDebounceMs;
  }
  if (pendingReloadDueMs_ != 0 && millis() >= pendingReloadDueMs_) {
    pendingReloadDueMs_ = 0;
    // reloadFromDisk()が失敗した場合はpendingReloadDueMs_を自分で再セットして
    // いるので、ここではその戻り値をそのまま返す(失敗時は再描画せず、次回の
    // 自動再試行を静かに待つ)。
    return reloadFromDisk();
  }
  return false;
}

bool LiveTextScreen::consumeNeedsFullRefresh() {
  const bool v = needsFullRefreshPending_;
  needsFullRefreshPending_ = false;
  return v;
}

void LiveTextScreen::drawContent(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight) const {
  const Font& body = contentFont();
  if (!reader_.isOpen()) {
    body.drawText(fb, fbWidth, fbHeight, kContentMargin, contentTop_, "(NO LIVE TEXT RECEIVED YET)");
  } else if (reader_.currentLines().empty()) {
    body.drawText(fb, fbWidth, fbHeight, kContentMargin, contentTop_, "(EMPTY)");
  } else {
    int y = contentTop_;
    for (const auto& line : reader_.currentLines()) {
      body.drawText(fb, fbWidth, fbHeight, kContentMargin, y, line.text.c_str());
      y += body.lineHeight();
    }
  }
}

void LiveTextScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  // statusBar rendering removed
  drawContent(fb, fbWidth, fbHeight);
  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction LiveTextScreen::handleButton(uint8_t buttonIndex) {
  if (buttonIndex == InputManager::BTN_UP) {
    // ページ送りは実際にファイルへシークして読み込むため、操作の間だけ
    // ファイルハンドルを開き直す(openFile()と同じ理由でアイドル中は閉じておく)。
    if (!reader_.reopenFileHandle()) return ScreenAction::kNone;
    const bool moved = reader_.prevPage();
    reader_.closeFileHandle();
    if (!moved) return ScreenAction::kNone;
    updateStatusAndFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_DOWN) {
    if (!reader_.reopenFileHandle()) return ScreenAction::kNone;
    const bool moved = reader_.nextPage();
    reader_.closeFileHandle();
    if (!moved) return ScreenAction::kNone;
    updateStatusAndFooter();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
