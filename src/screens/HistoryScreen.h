#pragma once
#include <algorithm>
#include <vector>

#include "../core/TxtReaderService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"

// 最近開いた本の履歴一覧。ホーム画面のBACKボタンから開く
// (詳細はREADME「履歴画面について」を参照)。
//
// - LEFT/RIGHT: フォーカス移動
// - CONFIRM: フォーカス中の本を読書画面で開く
// - BACK: ホーム画面に戻る
//
// データはTxtReaderService::readHistory()から取得する(最近開いた順、最大
// TxtReaderService::kMaxHistoryEntries件)。ページングはせず、件数の上限を
// 画面に収まる行数として扱う。
class HistoryScreen : public Screen {
 public:
  HistoryScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font);

  // ホーム画面から遷移するたびmain.cpp側が呼び、最新の履歴をSDから読み込む。
  void reload();

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // システムフォントが変更されたときにmain.cpp側から呼ぶ(FolderScreen等と同様)。
  void relayout(const Font& font);

  // handleButton()がScreenAction::kOpenFileを返したとき、main.cpp側がこれを使う。
  const String& pendingOpenFilePath() const { return pendingOpenPath_; }

 private:
  static constexpr int kMaxRows = TxtReaderService::kMaxHistoryEntries;
  static constexpr int kFooterHeight = 32;
  // 「リストの視認性を上げてほしい」というフィードバックを受けて拡大(以前は10、
  // FolderScreen.hのkRowPaddingコメント参照)。
  static constexpr int kRowPadding = 30;

  void layoutRows(const Font& font);
  void refreshRowLabels();

  uint16_t fbWidth_;
  uint16_t fbHeight_;

  std::vector<BookHistoryEntry> entries_;
  int focusIndex_ = 0;
  String pendingOpenPath_;

  FooterGuide footer_;
  FooterGuideItem footerItems_[4];

  SettingRow rows_[kMaxRows];
  String rowLabels_[kMaxRows];
  String rowValues_[kMaxRows];
};
