#pragma once
#include "../ui/FooterGuide.h"
#include "../ui/HomeGridButton.h"
#include "../ui/Screen.h"
#include "../core/BatteryService.h"
#include "../core/RtcService.h"
#include "../core/SettingsService.h"
#include "../ui/SettingRow.h"

// ホーム画面。上部に直近の本(TXT読書画面での進捗があれば実データ、なければ
// プレースホルダー表示)、下部に小さなアイコン+ラベルのボタンを「アプリグリッド」
// 的に表示する(スマートフォンのホーム画面のように、列数kColsPerRowで折り返し、
// ボタンが増えるほど行数が自動的に増える)。フッターのすぐ上を最終行として下から
// 上へ行を積み上げるため、ボタンが少ないうちは本プレースホルダー領域との間に
// 空白が残る(意図的な見た目。以前は2x2の大きなグリッドで領域全体を埋めていたが、
// シンプルな見た目かつ将来ボタンを追加しやすい構成にしたいというフィードバックを
// 受けてこの配置に変更した)。
//
// 今後ボタン(=今後実装する他機能への入り口)を追加する場合は:
//   1. 下のGridButton enumに値を追記する
//   2. HomeScreen.cppのkButtonDefs[]に対応する定義(アイコン・ラベル)を同じ順序で追記する
//   3. kButtonCountを実際のボタン数に合わせて更新する(数が食い違うとコンストラクタの
//      static_assertでビルドエラーになるため気づける)
// だけでよく、レイアウト計算(行数・座標)やLEFT/RIGHT/UP/DOWNのフォーカス移動は
// 自動的に追従する。
//
// EPUB対応はまだないため、上部の本情報が示せるのはTXTファイルの進捗のみ。
class HomeScreen : public Screen {
 public:
  // グリッドボタンのどれが選ばれたかをmain.cpp(画面遷移の管理者)に伝えるための識別子。
  // 値はHomeScreen.cppのkButtonDefs[]内での並び順(=配列インデックス)と対応させること
  // (lastActivatedButton()参照)。
  enum class GridButton {
    kRead,
    kSettings,
    kFolder,
    kBluetooth,
    kStandby,
    kLiveText,
    // 今後ボタンを追加する場合はここに追記(kButtonDefs[]の並び順と一致させる)
  };

  HomeScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, BatteryService& battery, RtcService& rtc, AppSettings& appSettings);

  void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) override;
  ScreenAction handleButton(uint8_t buttonIndex) override;

  // ScreenActionがkNavigateForwardを返した直後に、main.cpp側が
  // 「どのグリッドボタンが選ばれて遷移が要求されたか」を知るために呼ぶ。
  GridButton lastActivatedButton() const { return static_cast<GridButton>(focusIndex_); }

  // StatusBar methods removed

  // 直近に開いていた本を反映する(起動時・読書画面を閉じたときにmain.cppが呼ぶ)。
  // pathが空のままなら「本がまだない」プレースホルダー表示が続く。
  void setLastBook(const String& path, int percent);

  // GridButton::kReadがCONFIRMされScreenAction::kOpenFileが返ったとき、
  // main.cpp側がここから開くべきファイルパスを取得する。
  const String& lastBookPath() const { return lastBookPath_; }

  // setClockText removed

 private:
  static constexpr int kButtonCount = 6;  // 実装済みのボタン数(kButtonDefs[]の要素数と一致させること)
  static constexpr int kColsPerRow = 3;   // 1行に並べる列数。超えた分は自動で次の行になる
  static constexpr int kRowCount = (kButtonCount + kColsPerRow - 1) / kColsPerRow;  // 切り上げ除算
  static constexpr int kFooterHeight = 32;
  static constexpr int kBookAreaHeight = 220;  // 上部の本プレースホルダー領域の高さ
  static constexpr int kGridMargin = 16;
  static constexpr int kButtonRowHeight = 100;  // ボタン行1つ分の高さ(アイコン40px+広めのラベル余白)
  static constexpr int kButtonRowBottomMargin = 8;  // 最終行とフッターの間隔

  void updateFocus();
  void drawBookPlaceholder(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const;

  FooterGuide footer_;
  FooterGuideItem footerItems_[3];
  HomeGridButton buttons_[kButtonCount];
  int focusIndex_ = static_cast<int>(GridButton::kFolder);

  String lastBookPath_;
  String lastBookTitle_;
  int lastBookPercent_ = 0;
  BatteryService& battery_;
  RtcService& rtc_;
  AppSettings& appSettings_;
};
