#pragma once
#include <vector>

#include "../core/FileBrowserService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"
#include "../ui/StatusBar.h"

// SDカードのファイル/フォルダ一覧を表示する画面。
//
// - UP/DOWN: リスト内のフォーカス移動(ページをまたいで移動する)
// - RIGHT/CONFIRM: フォーカス中がディレクトリならその中に入る
// - LEFT: 親ディレクトリに戻る(ルートにいるときは何もしない)
// - BACK: ホーム画面に戻る(呼び出し側がScreenAction::kNavigateBackを見て切り替える)
//
// ファイル/フォルダの一覧表示にはSettingRowを流用している(左:名前、右:種別/サイズ)。
class FolderScreen : public Screen {
 public:
  FolderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, FileBrowserService& fileBrowser);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // 画面を開くたびにルートディレクトリから見せたい場合に呼ぶ。
  void resetToRoot();

  // main.cpp側がBatteryServiceから読み取った最新の残量・充電状態をここで反映する。
  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }
  void setBatteryCharging(bool charging) { statusBar_.setBatteryCharging(charging); }

 private:
  static constexpr int kMaxVisibleRows = 24;  // 1ページに表示できる行数の上限(配列確保用)
  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;
  static constexpr int kRowPadding = 10;

  static int RowHeight(const Font& font) { return font.lineHeight() + kRowPadding; }

  void reloadCurrentDirectory();
  void layoutRows(uint16_t fbWidth, uint16_t fbHeight, const Font& font);
  // focusIndex_を含むページの範囲をentries_からrows_[0..rowsPerPage_)に反映する。
  // UP/DOWNでのフォーカス移動時、ページをまたいだ場合の表示切り替えもここで行う。
  void reloadRowWindowForFocus();
  void updateFooter();
  void enterSelectedIfDirectory();
  void goToParent();

  FileBrowserService& fileBrowser_;
  String currentPath_ = "/";
  std::vector<DirEntry> entries_;
  int focusIndex_ = 0;  // entries_内のグローバルインデックス
  int rowsPerPage_ = 1;

  StatusBar statusBar_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[4];
  char pageLabel_[16] = "1/1";

  SettingRow rows_[kMaxVisibleRows];
  String rowLabels_[kMaxVisibleRows];
  String rowValues_[kMaxVisibleRows];
  int visibleRowCount_ = 0;
};
