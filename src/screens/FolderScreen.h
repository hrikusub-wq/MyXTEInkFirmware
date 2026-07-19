#pragma once
#include <algorithm>
#include <vector>

#include "../core/FileBrowserService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"

// SDカードのファイル/フォルダ一覧を表示する画面。setRoot()で設定したルートに
// 応じて2箇所から使い回される: ホーム画面「FOLDER」("/User"、日常使うユーザー
// ファイル)、設定画面「SYSTEM」("/System"、フォント等の機材データ。ほぼ開かない
// 想定の読み取り目的)。
//
// - LEFT/RIGHT・UP/DOWN: リスト内のフォーカス移動(どれも同じ意味、ページをまたいで
//   移動する。他のリスト画面(SettingsScreen/HistoryScreen)と統一している)。
//   LEFT/RIGHTは長押し中、一定間隔で自動的に連続してフォーカス移動する
//   (main.cpp参照。E-inkのリフレッシュ待ちで都度ボタンを押し直す手間を省く)。
// - CONFIRM: フォーカス中がディレクトリならその中に入る、TXTファイルなら開く
// - BACK: ルート(setRoot()で設定したパス)でなければ親ディレクトリへ1段戻る、
//   ルートなら抜ける(呼び出し側がScreenAction::kNavigateBackを見て、Home/
//   Settingsのどちらから開かれたかに応じて戻り先を切り替える)
//
// 一覧には読める拡張子(txt/md/markdown)のファイルとディレクトリのみ表示する
// (reloadCurrentDirectory()参照。読めないファイルは一覧から除外する)。
//
// (UP単体を「ディレクトリに入る」に割り当てていた時期があったが、UP長押しの
// CONFIRMショートカット機能と意味が重複し「側面ボタンを押すと選択になってしまう」
// 状態になっていたため、CONFIRM系の操作は物理CONFIRMボタン(+その長押し
// ショートカット)だけに一本化した。「親ディレクトリへ戻る」も同様の理由でDOWN単体
// ではなくBACK(+その長押しショートカット)に統合している)
//
// ファイル/フォルダの一覧表示にはSettingRowを流用している(左:名前、右:種別/サイズ)。
class FolderScreen : public Screen {
 public:
  FolderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, FileBrowserService& fileBrowser);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // 画面を開くたびに(setRoot()で設定した)ルートディレクトリから見せたい場合に呼ぶ。
  void resetToRoot();

  // このFolderScreenインスタンスが「ルート」とみなすパスを設定する。BACKで
  // それより上には遡らず、ここでScreenAction::kNavigateBackを返す(main.cpp側が
  // どの画面に戻るかを判定する)。ホーム画面の「FOLDER」("/User")、設定画面の
  // 「SYSTEM」("/System")の2箇所から同じFolderScreenインスタンスを使い回すために
  // 導入した(resetToRoot()呼び出し前に呼ぶこと)。
  void setRoot(const String& rootPath) { rootPath_ = rootPath; }

  // システムフォントが変更されたときにmain.cpp側から呼ぶ。行の高さはフォントの
  // lineHeight()に依存するため、1ページの行数・行の配置を新しいフォントで
  // 再計算する(コンストラクタ時のlayoutRows()と同じ処理をやり直す)。
  void relayout(const Font& font);

  // handleButton()がScreenAction::kOpenFileを返したとき、main.cpp側がこれを使って
  // 開くべきファイルの絶対パスを取得する。
  const String& pendingOpenFilePath() const { return pendingOpenPath_; }

 private:
  static constexpr int kMaxVisibleRows = 24;  // 1ページに表示できる行数の上限(配列確保用)
  static constexpr int kFooterHeight = 32;
  // 「リストの視認性を上げてほしい」というフィードバックを受けて拡大(以前は10)。
  static constexpr int kRowPadding = 30;

  // アイコン(SettingRow::kIconPx、40px)と本文フォントの行高さのうち大きい方を
  // 基準にする。フォントサイズが小さい設定(TEXT SIZE)でもアイコンが行からはみ
  // 出さないようにするため(std::maxのため<algorithm>が必要、FolderScreen.cppで
  // 既にインクルード済み)。
  static int RowHeight(const Font& font) {
    return std::max(font.lineHeight(), SettingRow::kIconPx) + kRowPadding;
  }

  void reloadCurrentDirectory();
  void layoutRows(const Font& font);
  // focusIndex_を含むページの範囲をentries_からrows_[0..rowsPerPage_)に反映する。
  // UP/DOWNでのフォーカス移動時、ページをまたいだ場合の表示切り替えもここで行う。
  void reloadRowWindowForFocus();
  void updateFooter();
  void enterSelectedIfDirectory();
  void goToParent();

  FileBrowserService& fileBrowser_;
  uint16_t fbWidth_;
  uint16_t fbHeight_;
  String rootPath_ = "/";  // setRoot()で変更する。BACKで遡れる上限。
  String currentPath_ = "/";
  String pendingOpenPath_;
  std::vector<DirEntry> entries_;
  int focusIndex_ = 0;  // entries_内のグローバルインデックス
  int rowsPerPage_ = 1;

  FooterGuide footer_;
  FooterGuideItem footerItems_[4];
  char pageLabel_[16] = "1/1";

  SettingRow rows_[kMaxVisibleRows];
  String rowLabels_[kMaxVisibleRows];
  String rowValues_[kMaxVisibleRows];
  int visibleRowCount_ = 0;
};
