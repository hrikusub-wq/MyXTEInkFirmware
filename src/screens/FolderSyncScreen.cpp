#include "FolderSyncScreen.h"

#include <InputManager.h>

FolderSyncScreen::FolderSyncScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble)
    : ble_(ble),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  (void)font;
  footerItems_[0] = {PhysicalButton::kBack, "SETTINGS"};
  footer_.setItems(footerItems_, 1);
}

void FolderSyncScreen::onEnter() {
  totalCount_ = 0;
  syncedCount_ = 0;
  cachedOperationKind_ = OperationKind::kNone;
  cachedFileName_ = "";
  cachedReceivedBytes_ = 0;

  if (!ble_.isConnected()) {
    uiState_ = UiState::kNotConnected;
    return;
  }

  ble_.sendSyncRequest();
  uiState_ = UiState::kWaitingForCount;
}

bool FolderSyncScreen::pollUpdates() {
  if (uiState_ == UiState::kNotConnected) return false;

  BleTransferService::ErrorCode err;
  if (ble_.consumeError(err)) {
    lastError_ = err;
    uiState_ = UiState::kError;
    return true;
  }

  if (uiState_ == UiState::kWaitingForCount) {
    int count = 0;
    if (ble_.consumeSyncCountReceived(count)) {
      totalCount_ = count;
      syncedCount_ = 0;
      uiState_ = (count > 0) ? UiState::kSyncing : UiState::kUpToDate;
      return true;
    }
    return false;
  }

  if (uiState_ == UiState::kSyncing) {
    bool changed = false;

    String deletedPath;
    if (ble_.consumeDeletedFile(deletedPath)) {
      cachedOperationKind_ = OperationKind::kDelete;
      cachedFileName_ = deletedPath;
      cachedReceivedBytes_ = 0;
      syncedCount_++;
      changed = true;
    } else {
      if (ble_.receivedBytes() != cachedReceivedBytes_ || ble_.currentFileName() != cachedFileName_) {
        cachedOperationKind_ = OperationKind::kTransfer;
        cachedReceivedBytes_ = ble_.receivedBytes();
        cachedFileName_ = ble_.currentFileName();
        changed = true;
      }
      if (ble_.consumeFileDone()) {
        syncedCount_++;
        cachedReceivedBytes_ = 0;
        cachedFileName_ = "";
        changed = true;
      }
    }

    if (syncedCount_ >= totalCount_) {
      uiState_ = UiState::kCompleted;
    }
    return changed;
  }

  return false;
}

void FolderSyncScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {

  const int lineH = font.lineHeight();
  const int textX = 16;
  int y = 24;

  switch (uiState_) {
    case UiState::kNotConnected:
      font.drawText(fb, fbWidth, fbHeight, textX, y, "PC APP NOT CONNECTED");
      y += lineH + 4;
      font.drawText(fb, fbWidth, fbHeight, textX, y, "CONNECT VIA BLUETOOTH");
      y += lineH + 4;
      font.drawText(fb, fbWidth, fbHeight, textX, y, "SCREEN FIRST");
      break;
    case UiState::kWaitingForCount:
      font.drawText(fb, fbWidth, fbHeight, textX, y, "CHECKING...");
      break;
    case UiState::kUpToDate:
      font.drawText(fb, fbWidth, fbHeight, textX, y, "ALREADY UP TO DATE");
      break;
    case UiState::kSyncing: {
      char buf[32];
      snprintf(buf, sizeof(buf), "SYNCING %d / %d", syncedCount_ + 1, totalCount_);
      font.drawText(fb, fbWidth, fbHeight, textX, y, buf);
      y += lineH + 12;
      if (cachedFileName_.length() > 0) {
        const char* opLabel = (cachedOperationKind_ == OperationKind::kDelete) ? "DELETING" : "SENDING";
        font.drawText(fb, fbWidth, fbHeight, textX, y, opLabel);
        y += lineH + 4;
        font.drawText(fb, fbWidth, fbHeight, textX, y, cachedFileName_.c_str());
        if (cachedOperationKind_ == OperationKind::kTransfer) {
          y += lineH + 4;
          char progressBuf[32];
          snprintf(progressBuf, sizeof(progressBuf), "%lu BYTES", static_cast<unsigned long>(cachedReceivedBytes_));
          font.drawText(fb, fbWidth, fbHeight, textX, y, progressBuf);
        }
      }
      break;
    }
    case UiState::kCompleted:
      font.drawText(fb, fbWidth, fbHeight, textX, y, "SYNC COMPLETE");
      break;
    case UiState::kError: {
      font.drawText(fb, fbWidth, fbHeight, textX, y, "SYNC ERROR");
      y += lineH + 12;
      char buf[32];
      snprintf(buf, sizeof(buf), "CODE: %s", BleTransferService::errorCodeLabel(lastError_));
      font.drawText(fb, fbWidth, fbHeight, textX, y, buf);
      break;
    }
  }

  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction FolderSyncScreen::handleButton(uint8_t buttonIndex) {
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }
  return ScreenAction::kNone;
}
