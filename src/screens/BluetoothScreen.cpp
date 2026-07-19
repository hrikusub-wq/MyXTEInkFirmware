#include "BluetoothScreen.h"

#include <InputManager.h>

BluetoothScreen::BluetoothScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble)
    : ble_(ble),
      statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  (void)font;
  statusBar_.setLeftText("BLUETOOTH");
  footerItems_[0] = {PhysicalButton::kBack, "SETTINGS"};
  footer_.setItems(footerItems_, 1);
}

bool BluetoothScreen::pollUpdates() {
  bool changed = false;

  BleTransferService::ErrorCode err;
  if (ble_.consumeError(err)) {
    changed = true;
  }
  // DONEはBluetoothScreen(単発転送)では特別な画面遷移をせず、次のS:が来るか
  // BACKで抜けるまで「受信中」表示のまま待つ(consumeするだけで状態は見ない)。
  ble_.consumeFileDone();

  if (ble_.isConnected() != cachedConnected_) {
    cachedConnected_ = ble_.isConnected();
    changed = true;
  }
  if (ble_.state() != cachedState_) {
    cachedState_ = ble_.state();
    changed = true;
  }
  if (ble_.currentFileName() != cachedFileName_) {
    cachedFileName_ = ble_.currentFileName();
    changed = true;
  }
  if (ble_.receivedBytes() != cachedReceivedBytes_) {
    cachedReceivedBytes_ = ble_.receivedBytes();
    changed = true;
  }

  return changed;
}

void BluetoothScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  statusBar_.render(fb, fbWidth, fbHeight, font);

  const int lineH = font.lineHeight();
  const int textX = 16;
  int y = kStatusBarHeight + 24;

  if (ble_.state() == BleTransferService::State::kError) {
    font.drawText(fb, fbWidth, fbHeight, textX, y, "ERROR");
    y += lineH + 12;
    char buf[32];
    snprintf(buf, sizeof(buf), "CODE: %s", BleTransferService::errorCodeLabel(ble_.lastError()));
    font.drawText(fb, fbWidth, fbHeight, textX, y, buf);
  } else if (ble_.state() == BleTransferService::State::kReceiving) {
    font.drawText(fb, fbWidth, fbHeight, textX, y, "RECEIVING FILE");
    y += lineH + 12;
    font.drawText(fb, fbWidth, fbHeight, textX, y, ble_.currentFileName().c_str());
    y += lineH + 4;
    char buf[48];
    const uint32_t total = ble_.currentFileSize();
    const int percent = (total > 0) ? static_cast<int>((ble_.receivedBytes() * 100ULL) / total) : 0;
    snprintf(buf, sizeof(buf), "%lu / %lu BYTES (%d%%)", static_cast<unsigned long>(ble_.receivedBytes()),
             static_cast<unsigned long>(total), percent);
    font.drawText(fb, fbWidth, fbHeight, textX, y, buf);
  } else if (ble_.isConnected()) {
    font.drawText(fb, fbWidth, fbHeight, textX, y, "CONNECTED");
    y += lineH + 12;
    font.drawText(fb, fbWidth, fbHeight, textX, y, "WAITING FOR TRANSFER");
  } else {
    font.drawText(fb, fbWidth, fbHeight, textX, y, "WAITING FOR CONNECTION");
    y += lineH + 12;
    font.drawText(fb, fbWidth, fbHeight, textX, y, "DEVICE NAME:");
    y += lineH + 4;
    font.drawText(fb, fbWidth, fbHeight, textX, y, ble_.deviceName().c_str());
  }

  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction BluetoothScreen::handleButton(uint8_t buttonIndex) {
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }
  return ScreenAction::kNone;
}
