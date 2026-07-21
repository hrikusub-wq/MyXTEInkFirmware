#pragma once
#include "../core/BleTransferService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"

// Bluetooth(BLE)画面。SettingsScreenの「BLUETOOTH」から開く。
//
// 画面を開いている間だけBLEアドバタイズを有効にする(常時ONにはしない。
// main.cpp側がこの画面への出入りに合わせてBleTransferService::startAdvertising()/
// stopAdvertising()を呼ぶ、SettingsScreenの各種オーバーレイと違いこの画面自身は
// アドバタイズのON/OFFを持たない)。表示内容はBleTransferServiceの状態をそのまま
// 反映するだけの「ビューア」で、この画面自身は受信ロジックを持たない(実際の
// 受信処理はBleTransferService::update()がmain.cppのloop()から毎回呼ばれて行う)。
//
// - 未接続: デバイス名+待受中である旨
// - 接続済み(転送なし): 接続済みである旨
// - ファイル受信中: ファイル名・進捗
// - エラー: エラー内容(BACKで抜けると次回入り直したときにクリアされる。
//   main.cpp側がBleTransferService::clearError()を呼ぶ)
//
// BACKのみ有効(footerItems_はSettingsScreenの例にならう)。
class BluetoothScreen : public Screen {
 public:
  BluetoothScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // main.cppのloop()から一定間隔で呼ぶ。BLE状態が変化していればtrueを返す
  // (呼び出し側はtrueのときだけ再描画すればよい)。
  bool pollUpdates();

  // setBatteryPercent/setBatteryCharging removed

 private:
  static constexpr int kFooterHeight = 32;

  BleTransferService& ble_;

  // pollUpdates()での変化検出用キャッシュ。受信バイト数はここで追跡しない
  // (PC側アプリで進捗を確認する運用のため。E-ink/SDはSPIバスを共有しており
  // 受信バイト数の変化ごとに再描画すると転送そのものが遅くなるため、あえて
  // 追跡・表示しない設計にしている)。
  bool cachedConnected_ = false;
  BleTransferService::State cachedState_ = BleTransferService::State::kIdle;
  String cachedFileName_;

  FooterGuide footer_;
  FooterGuideItem footerItems_[2];
};
