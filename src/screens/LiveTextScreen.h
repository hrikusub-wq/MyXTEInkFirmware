#pragma once
#include <SDCardManager.h>

#include "../core/BleTransferService.h"
#include "../core/TxtReaderService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/StatusBar.h"

// PC側からのBLE経由での上書きに追従して自動的に再表示する、常に単一の固定
// パス(kDefaultPath)だけを扱う一時ファイルビューア。ホーム画面の「LIVE TEXT」
// ボタンからのみ開く(main.cpp参照)。BACKで閉じると同時にSDから削除する
// (closeFile()参照)ため、真に「その場限りの下書き」用途に割り切っている。
// 恒久的に残したいファイルはPC側で保存する想定(gui.pyのLiveTextタブ)。
//
// 描画・ページング自体はTxtReaderServiceをそのまま使い、TxtReaderScreenとの差分は
// 「ファイル更新の検知」「BACKで確認なしに即終了・削除する」の2点。Markdown・
// ブックマーク・スクロールモード・読書設定など読書画面の付加機能は持たない
// (シンプルな常時ビューアとして割り切る)。
//
// E-inkの物理的な書き換え速度(FAST_REFRESHでも数百ms級)は「完全なリアルタイム」の
// 対極にあるため、PCが"P:"で書き込むたびに即座に再読み込みはせず、以下の
// 緩和策を組み込んでいる:
//   - デバウンス: 短時間の連続更新(エディタのオートセーブ等)をまとめ、最後の
//     1回だけリロードする(kLiveDebounceMs)
//   - 定期FULL_REFRESH: 一定回数リロードするごとにFULL_REFRESHを挟み、
//     FAST_REFRESHの繰り返しによる残像を除去する(consumeNeedsFullRefresh())
// 「保存するたびに1〜数秒遅延して反映される疑似リアルタイム」であり、真の
// キーストローク単位の同期はE-inkである以上不可能なことを利用側は理解しておくこと。
//
// ボタン割り当て:
// - UP: 前のページ
// - DOWN: 次のページ
// - CONFIRM: 未使用
// - BACK: 監視を終了し、確認なしで即座に戻る(SDからも削除する)
class LiveTextScreen : public Screen {
 public:
  // LiveTextが常に開く唯一の絶対パス。PC側アプリ(gui.py、
  // LIVE_TEXT_DEVICE_PATH)の保存先と完全一致させること(両リポジトリに
  // またがる結合点)。ドット始まりなのでFolderScreenの一覧からは自動的に
  // 非表示になる(FileBrowserService::listDirectory()の隠しファイルフィルタ、
  // "/User"配下に置くことでローカルフォルダ同期のスキャン対象パスとも
  // 揃えている)。
  static constexpr const char* kDefaultPath = "/User/.livetext_scratch.txt";

  LiveTextScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BleTransferService& ble);

  // 本文の描画・折り返し測定に使うフォントを差し替える(CjkFontImpl等)。TxtReaderScreen::
  // setContentFont()と同じ考え方(main.cppのapplyReaderBodyFontSettings()が読書画面と
  // 同じフォントをこちらにも設定する)。nullptrを渡すとコンストラクタで渡した
  // チロームフォント(MiniFontImpl、ASCII専用)に戻る。
  void setContentFont(const Font* contentFont);

  // absPathを開き、BLE側の監視対象として登録する(ble_.beginLiveWatch())。
  // ファイルがまだ存在しない場合(PCがこれから初めて書き込む場合)でも失敗せず
  // 画面には入り、その旨の空表示になる(render()参照)。main.cppはホーム画面の
  // 「LIVE TEXT」ボタンからkDefaultPathを渡して呼ぶ(それ以外の呼び出し元は無い)。
  void openFile(const String& absPath);

  // BACK時にmain.cppが呼ぶ。BLE側の監視を解除しファイルを閉じたうえ、SDから
  // 削除する(常に一時ファイルとして扱う設計、クラスコメント参照)。
  void closeFile();

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // main.cppのloop()から一定間隔で呼ぶ。デバウンス済みの更新が確定し実際に
  // リロードした場合にtrueを返す(呼び出し側が再描画する)。
  bool pollForUpdate();

  // 直近のpollForUpdate()=trueが、定期的な残像除去のためFULL_REFRESHを使うべき
  // タイミングだったかどうかを1回だけ消費する(main.cpp側がリフレッシュモードの
  // 判定に使う)。
  bool consumeNeedsFullRefresh();

  void setBatteryPercent(int percent) { statusBar_.setBatteryPercent(percent); }
  void setBatteryCharging(bool charging) { statusBar_.setBatteryCharging(charging); }

 private:
  static constexpr int kStatusBarHeight = 32;
  static constexpr int kFooterHeight = 32;
  static constexpr int kContentMargin = 16;
  // 短時間の連続更新をまとめるためのデバウンス時間。PCエディタのオートセーブが
  // 短時間に連発しても、この間隔の間は最後の1回分だけリロードする。
  static constexpr unsigned long kLiveDebounceMs = 1500;
  // FAST_REFRESHの繰り返しによる残像を防ぐため、この回数リロードするごとに
  // 1回FULL_REFRESHを挟む。
  static constexpr int kFullRefreshEvery = 5;
  // reloadFromDisk()内のreader_.open()が失敗した場合の再試行間隔。PC側が
  // まだ書き込み中(ファイルハンドルを保持したまま)のタイミングと重なると、
  // SDカードの「同一ファイルを読み書き両方のハンドルで同時に開けない」制約に
  // より一時的に開けないことがある(TxtReaderService::closeFileHandle()の
  // コメント参照)。すぐに諦めず短い間隔で自動的に再試行することで、ユーザーが
  // 手動で画面を出入りし直さなくても内容が反映されるようにする。
  static constexpr unsigned long kReloadRetryMs = 300;

  void layout();
  void updateStatusAndFooter();
  // reader_.open()に成功した場合のみtrueを返す。失敗時はpendingReloadDueMs_を
  // 短時間後に再セットし、呼び出し側(pollForUpdate())が改めて自動再試行する。
  bool reloadFromDisk();
  void drawContent(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight) const;
  const Font& contentFont() const { return contentFont_ ? *contentFont_ : *font_; }

  BleTransferService& ble_;
  const Font* font_;
  const Font* contentFont_ = nullptr;
  uint16_t fbWidth_;
  uint16_t fbHeight_;
  TxtReaderService reader_;
  StatusBar statusBar_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[1];
  char pageLabel_[16] = "1/1";
  String currentPath_;
  String titleText_;

  int contentTop_ = 0;
  int viewportWidthPx_ = 0;
  int viewportHeightPx_ = 1;

  unsigned long pendingReloadDueMs_ = 0;  // 0なら保留中のリロードなし
  int reloadCount_ = 0;
  bool needsFullRefreshPending_ = false;
};
