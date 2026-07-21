#include "BluetoothScreen.h"

#include <InputManager.h>

BluetoothScreen::BluetoothScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble)
    : ble_(ble),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  (void)font;
  footerItems_[0] = {PhysicalButton::kBack, "BACK"};
  footerItems_[1] = {PhysicalButton::kConfirm, "LIVE TEXT"};
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
  // 受信バイト数の変化は意図的に再描画トリガーに含めない(下記render()のコメント参照)。

  return changed;
}

void BluetoothScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {

  const int lineH = font.lineHeight();
  const int textX = 16;
  int y = 24;

  if (ble_.state() == BleTransferService::State::kError) {
    font.drawText(fb, fbWidth, fbHeight, textX, y, "ERROR");
    y += lineH + 12;
    char buf[32];
    snprintf(buf, sizeof(buf), "CODE: %s", BleTransferService::errorCodeLabel(ble_.lastError()));
    font.drawText(fb, fbWidth, fbHeight, textX, y, buf);
  } else if (ble_.state() == BleTransferService::State::kReceiving) {
    // バイト単位の進捗はあえて表示しない: E-inkとSDカードはSPIバスを共有して
    // いるため、進捗が変わるたびに再描画すると転送そのものが遅くなる
    // (実機で確認済み)。転送状況はPC側アプリで確認する運用とし、この画面は
    // ファイル名の表示のみ(受信開始・別ファイルへの切り替わり時にのみ更新)。
    font.drawText(fb, fbWidth, fbHeight, textX, y, "RECEIVING FILE");
    y += lineH + 12;
    font.drawText(fb, fbWidth, fbHeight, textX, y, ble_.currentFileName().c_str());
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

  bool showLiveText = ble_.isConnected() && 
                      ble_.state() != BleTransferService::State::kReceiving && 
                      ble_.state() != BleTransferService::State::kError;
  footer_.setItems(footerItems_, showLiveText ? 2 : 1);

  footer_.render(fb, fbWidth, fbHeight, font);
}

ScreenAction BluetoothScreen::handleButton(uint8_t buttonIndex) {
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    bool canOpenLiveText = ble_.isConnected() && 
                           ble_.state() != BleTransferService::State::kReceiving && 
                           ble_.state() != BleTransferService::State::kError;
    if (canOpenLiveText) {
      return ScreenAction::kNavigateForward;
    }
  }
  return ScreenAction::kNone;
}
