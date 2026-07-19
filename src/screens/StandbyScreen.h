#pragma once
#include <EInkDisplay.h>

#include <vector>

#include "../core/BatteryService.h"
#include "../core/FileBrowserService.h"
#include "../core/RtcService.h"
#include "../core/SettingsService.h"
#include "../ui/FooterGuide.h"
#include "../ui/Screen.h"
#include "../ui/SettingRow.h"

// 待機画面。SDカードの/System/standby配下にある.jpg/.jpegファイルを一覧表示し、CONFIRMで
// 選んだ画像をE-inkに表示したあとディープスリープへ移行する(PowerManager参照)。
//
// - リスト表示中: LEFT/RIGHT・UP/DOWNでフォーカス移動、CONFIRMで選択画像を表示、
//   BACKでホームへ戻る(他のリスト画面と同じ配置)
// - 画像表示中: CONFIRMでディープスリープに入る(consumeSleepRequested()参照、
//   実際のスリープ突入はmain.cpp側がPowerManager経由で行う)、BACKでリストに戻る
//
// RTC同期の停止については追加実装が不要: ディープスリープ突入=ハードリブートの
// ため、スリープ中はloop()自体が止まり、時計の定期ポーリングも自動的に停止する
// (復帰後はsetup()が毎回RTCを読み直すため、「次回起動時にのみ同期」という要件は
// 既存の起動シーケンスで自然に満たされる)。
class StandbyScreen : public Screen {
 public:
  StandbyScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, FileBrowserService& fileBrowser,
                EInkDisplay& display, AppSettings& appSettings, BatteryService& battery, RtcService& rtc);

  // 画面を開くたびにmain.cppが呼ぶ。/System/standby配下の.jpg/.jpegファイル一覧を
  // 再スキャンし、画像表示中の状態があればリセットしてリスト表示に戻す。
  void onEnter();

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // 画像表示中にCONFIRMでディープスリープ突入が要求された場合にtrueを1回だけ
  // 返す(main.cpp側がこれを見てPowerManager::enterDeepSleepStandby()を呼ぶ。
  // その関数は戻らないため、この画面側では完結させられない)。
  bool consumeSleepRequested();

  // 画像表示中(リスト表示ではなくJPEGを全画面表示している)かどうか。main.cpp側が
  // 「画像表示中はバッテリー変化等の定期ポーリングによる再描画をスキップする」
  // 判定に使う(render()は画像表示中、初回描画後は何もせずreturnするだけの実装
  // のため、他の要因でrenderAndRefresh()が呼ばれるとdisplay.clearScreen()で
  // framebufferが白紙化されたままパネルに送られ、写真が消えてしまう不具合が
  // あった)。また、リスト画面から画像表示へ遷移する最初の1回はFULL_REFRESHを
  // 使うべきかの判定にも使う(FAST_REFRESHの差分更新だと直前のリスト表示の
  // 残像が写真に重なって見える不具合があった)。
  bool isShowingImage() const { return mode_ == Mode::kShowingImage; }

  // リスト表示から画像表示へ遷移した直後、main.cpp側がrender()/displayBuffer()の
  // 通常の1回きりのパイプラインの代わりに呼ぶ。デコード前に「LOADING...」を
  // 一度表示してから本番の画像を表示する2段階の書き込みが必要なため、
  // Screenインターフェースの通常のrender()1回だけでは実現できず、この専用
  // メソッドで完結させる(以前は4階調グレースケール駆動も行っていたが、退色の
  // 真因だったため廃止した、StandbyScreen.cppのquantizeLevel()宣言部コメント
  // 参照)。呼び出し後はimageDrawn_がtrueになり、以降の(バッテリー変化等による)
  // 通常のrender()呼び出しは何もしない。
  void showImage();

  // setBatteryPercent/setBatteryCharging removed due to StatusBar removal

 private:
  enum class Mode { kList, kShowingImage };

  static constexpr int kMaxVisibleRows = 24;
  static constexpr int kFooterHeight = 32;
  // 「リストの視認性を上げてほしい」というフィードバックを受けて拡大(以前は10、
  // FolderScreen.hのkRowPaddingコメント参照)。
  static constexpr int kRowPadding = 30;
  static constexpr const char* kStandbyDir = "/System/standby";

  void reloadFileList();
  void updateRowSelection();
  void layoutRows(const Font& font);
  void drawOverlays(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;

  FileBrowserService& fileBrowser_;
  EInkDisplay& display_;
  AppSettings& appSettings_;
  BatteryService& battery_;
  RtcService& rtc_;
  // showImage()冒頭の「読み込み中」表示用(JPEGデコードに時間がかかる間、
  // 一覧画面がそのまま残って見えるのを避けるため)。Screen::render()は毎回font
  // 引数を受け取れるが、showImage()は専用メソッドでその経路を経由しない
  // ため、コンストラクタで受け取ったフォントを保持しておく。
  const Font* font_;
  uint16_t fbWidth_;
  uint16_t fbHeight_;

  Mode mode_ = Mode::kList;
  std::vector<String> jpegFiles_;
  int focusIndex_ = 0;
  // render()は呼ばれるたびに画像を描き直す必要はない(重いデコード処理のため、
  // 画像表示モードに入った最初の1回だけ実行する)。
  bool imageDrawn_ = false;
  bool sleepRequested_ = false;

  FooterGuide footer_;
  FooterGuideItem footerItems_[2];

  SettingRow rows_[kMaxVisibleRows];
  String rowLabels_[kMaxVisibleRows];
  int visibleRowCount_ = 0;
};
