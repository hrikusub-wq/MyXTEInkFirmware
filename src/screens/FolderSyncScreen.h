#pragma once
#include "../core/BleTransferService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/StatusBar.h"

// ローカルフォルダ同期画面。SettingsScreenの「FOLDER SYNC」から開く。
//
// 画面に入った瞬間(onEnter(), main.cpp側がHistoryScreen::reload()と同じ考え方で
// 遷移のたびに呼ぶ)にBLE接続中かどうかを見て、接続中ならStatus "SYNC:REQUEST"を
// 送信し応答("Y:<count>")を待つ。未接続ならその旨を表示して待つだけで何もしない。
//
// 応答を受け取ったら、0件なら「最新の状態です」、n件なら以下の2種類の操作を
// BleTransferServiceのポーリングで追跡し進捗を表示する:
//   - ファイル転送("F:"始まり、既存のS/データ/Eフローと共有する状態機械)
//   - ファイル削除("X:"始まり、consumeDeletedFile()で1回で完結)
// 全完了で完了表示にし、BACKでSettingsScreenへ戻る。
//
// 制約(既知の設計上の限界): BLEのキャラクタリスティック構成上デバイス→PCの
// 大容量アップロード経路が無いため、あくまでPC→SDの一方向ミラーである。
// SDカードを手動編集すると次回同期で検知されない。
//
// BACKのみ有効(旧CloudSyncScreen/BluetoothScreenと同様)。
class FolderSyncScreen : public Screen {
 public:
  FolderSyncScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble);

  // main.cpp側がこの画面へ遷移するたびに呼ぶ。
  void onEnter();

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // main.cppのloop()から一定間隔で呼ぶ。状態が変化していればtrueを返す。
  bool pollUpdates();

  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }
  void setBatteryCharging(bool charging) { statusBar_.setBatteryCharging(charging); }

 private:
  enum class UiState { kNotConnected, kWaitingForCount, kUpToDate, kSyncing, kCompleted, kError };
  // 現在同期中の操作が転送か削除かの表示切り替え用。
  enum class OperationKind { kNone, kTransfer, kDelete };

  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;

  BleTransferService& ble_;

  UiState uiState_ = UiState::kNotConnected;
  int totalCount_ = 0;
  int syncedCount_ = 0;
  BleTransferService::ErrorCode lastError_ = BleTransferService::ErrorCode::kNone;

  // pollUpdates()での同期中の進捗変化検出用キャッシュ。
  OperationKind cachedOperationKind_ = OperationKind::kNone;
  String cachedFileName_;
  uint32_t cachedReceivedBytes_ = 0;

  StatusBar statusBar_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[1];
};
