#pragma once
#include <Arduino.h>

// システムフォントの種別。読書本文フォント(AppSettings::readerBodyFontKind)にも
// 同じ列挙値を流用する(意味は同じ「MiniFont/cpfont/bin」の3択のため)。
enum class SystemFontKind : uint8_t {
  kMiniFont = 0,  // 内蔵ASCII専用フォント(SD不要、常に使える最終フォールバック)
  kCjkFont = 1,   // SDカード上の.cpfont(CjkFontImpl)
  kBinFont = 2,   // SDカード上の.bin(XteinkBinFontImpl)。処理が軽い代わりにファイルサイズが大きい(README参照)
};

// 読書本文フォント(AppSettings::readerBodyFontKind==kCjkFont)のパスが未設定
// (空文字)の場合に使う既定の.cpfontパス。main.cpp(起動時の自動読み込み)と
// SettingsScreen(BOOK FONT設定行の現在選択の表示)の両方から参照する。
constexpr const char* kDefaultReaderBodyFontPath = "/fonts/NotoSansJp_12.cpfont";

// SDカードに永続化する設定値。
struct AppSettings {
  SystemFontKind systemFontKind = SystemFontKind::kMiniFont;
  uint8_t miniFontScale = 2;      // MiniFontImplの拡大率(1-4)。systemFontKind==kMiniFont時のみ意味を持つ
  char cjkFontPath[64] = {};      // systemFontKind==kCjkFont時に読み込む.cpfontのパス
  char binFontPath[64] = {};      // systemFontKind==kBinFont時に読み込む.binのパス(幅・高さはファイル名から解析、XteinkBinFontImpl::parseDimensions()参照)
  bool showClockInStatusBar = false;
  int8_t timezoneOffsetHours = 0;  // RTC生値からの時差(-9〜+9)。表示・編集時にRtcService::addHoursToDateTime()で適用する

  // 読書画面のMarkdown表示で使う、ロール別の.binフォントパス(いずれも空文字なら
  // 未設定)。SettingsScreenの「MARKDOWN」サブメニューで個別に設定する。
  // 見出しは最大3段階まで(H1/H2/H3、H4〜H6はH3を流用)。未設定のレベルは1つ上の
  // レベル(H3未設定→H2、H2未設定→H1)にカスケードし、H1が未設定なら内蔵の既定
  // 見出しフォントを使う(main.cpp::applyMarkdownFontSettings()参照)。
  // LIST/BOLDが未設定の場合は本文と同じフォントで代用する。
  char mdHeading1FontPath[64] = {};
  char mdHeading2FontPath[64] = {};
  char mdHeading3FontPath[64] = {};
  char mdListFontPath[64] = {};
  char mdBoldFontPath[64] = {};

  // 読書画面の本文フォント(SYSTEM FONTとは独立、SettingsScreenの「BOOK FONT」で
  // 設定する)。既定はkCjkFont+空パスで、この場合main.cppがkDefaultReaderBodyFontPath
  // を試す(設定を一度も変更していないユーザーの動作を変えないため)。
  SystemFontKind readerBodyFontKind = SystemFontKind::kCjkFont;
  char readerBodyCjkFontPath[64] = {};
  char readerBodyBinFontPath[64] = {};

  // 読書画面のSCROLLモード(TxtReaderScreen「READING SETTINGS」)でLEFT/RIGHT・
  // UP/DOWN 1回につき進める/戻る目安の行数(1〜9)。TxtReaderScreenの
  // 「READING SETTINGS」→「SCROLL LINES」で編集する。
  uint8_t scrollStepLines = 3;
};

// 設定(AppSettings)をSDカード上の1ファイルに読み書きする薄いラッパー。
// フォーマットはTxtReaderServiceのページインデックスキャッシュと同様、構造体を
// そのままバイナリで読み書きする単純な方式(設定項目数が少なく、キー/バリュー形式の
// 複雑さに見合わないため)。
class SettingsService {
 public:
  // SD上の設定ファイルを読み込む。ファイルが無い/壊れている場合はfalseを返し、
  // outはAppSettingsのデフォルト値のままにする(初回起動時の扱い)。
  static bool load(AppSettings& out);

  // 設定をSDへ保存する。失敗時はfalseを返す。
  static bool save(const AppSettings& settings);
};
