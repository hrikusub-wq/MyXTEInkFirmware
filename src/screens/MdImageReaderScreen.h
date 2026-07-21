#pragma once
#include "../core/MdImageReaderService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"

// PC側コンパニオンツール(XTEinkBLEFileSender)が事前レンダリングしたMarkdown
// ページ画像(.pgc)を表示する読書画面。TxtReaderScreenと違い、本文の折り返し・
// フォント選択・Markdown記法解析は一切行わない(PC側でビットマップとして確定
// 済みのため)。そのためオーバーレイはブックマーク一覧のみで、TxtReaderScreenに
// あるREADING SETTINGS(表示モード切替・SCROLL LINES)は存在しない。
//
// ボタン割り当て(TxtReaderScreenと合わせる):
// - UP: 前のページ  - DOWN: 次のページ
// - LEFT: 短押しでブックマーク追加、長押しでブックマーク一覧を開く
//   (main.cpp側がisOverlayShown()を見て判定し、addBookmark()/openBookmarkList()
//   を直接呼ぶ。TxtReaderScreenの同種の仕組みと対称)
// - RIGHT/CONFIRM: 未使用(読書設定に相当する項目が無いため)
// - BACK: ホームへ戻る
class MdImageReaderScreen : public Screen {
 public:
  MdImageReaderScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font);

  // pathの.md/.markdownに対応する画像キャッシュを開く。失敗時はfalseを返す
  // (呼び出し側=main.cppはこの戻り値を見て、TxtReaderScreenへのフォールバックを
  // 決める。画面遷移自体はmain.cppが成功時のみ行う)。
  bool openFile(const String& path);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // 現在ページにブックマークを追加する(main.cppがLEFT短押しを検知して呼ぶ)。
  void addBookmark();
  // ブックマーク一覧オーバーレイを開く(main.cppがLEFT長押しを検知して呼ぶ)。
  void openBookmarkList();

  // オーバーレイ(ブックマーク一覧)を表示中かどうか。main.cpp側がLEFTの
  // 短押し/長押し特別処理を有効にするかどうかの判定に使う(TxtReaderScreenの
  // isOverlayShown()と同じ役割)。
  bool isOverlayShown() const { return showBookmarkList_; }

 private:
  static constexpr int kFooterHeight = 32;
  static constexpr int kContentMargin = 16;

  void updateFooter();
  void drawBookmarkList(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;

  const Font* font_;
  uint16_t fbWidth_;
  uint16_t fbHeight_;
  MdImageReaderService reader_;
  FooterGuide footer_;
  FooterGuideItem footerItems_[2];
  char pageLabel_[16] = "1/1";
  String titleText_;

  bool showBookmarkList_ = false;
  int bookmarkListFocus_ = 0;

  // ブックマーク追加直後、一瞬だけ確認表示を出す(TxtReaderScreenと同じ、
  // 「次の操作が来たら消す」方式)。
  bool showBookmarkToast_ = false;
};
