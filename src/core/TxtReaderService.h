#pragma once
#include <Arduino.h>
#include <SDCardManager.h>

#include <vector>

#include "../gfx/Font.h"

// SDカード上のTXT/Markdownファイルをページ単位でストリーム表示するためのコアロジック。
// ファイル全体をRAMに展開せず、チャンク単位で読みながら折り返し(word wrap)し、
// 各ページの開始バイトオフセットの一覧(ページインデックス)を構築する。
// 一度構築したインデックスはSD上の隠しディレクトリ(/.reader_cache/)にキャッシュし、
// 同じファイル・同じレイアウトで再度開いたときの再スキャンを省く。
//
// crosspoint-reader(https://github.com/crosspoint-reader/crosspoint-reader)の
// TxtReaderActivity(src/activities/reader/TxtReaderActivity.cpp)と同じ
// 「ページインデックス構築+キャッシュ」方式を参考にした。グレースケール・
// 傾きセンサー・多言語対応・フォント切り替えなど、本プロジェクトでまだ存在しない
// 機能には依存していない。
//
// 制限事項: 行の折り返し判定はバイト単位で行うが、結果はマルチバイトUTF-8の
// 文字境界まで後退させてから確定するため文字の途中で切れることはない
// (appendWrapped()内のisUtf8Continuation()参照)。また、1回のページ読み込みで
// 読むチャンクは4KB固定のため、極端に短い行が大量に続く場合そのページの行数が
// 本来の上限より少なくなることがある(本文が欠落することはない)。
//
// Markdownモード(markdownMode=true)での制限事項はopen()のコメントを参照。

// 履歴画面(HistoryScreen)用の1件分(最近開いた本のパスと進捗%)。
struct BookHistoryEntry {
  String path;
  int percent = 0;
};

// ブックマーク1件分(TxtReaderScreen「READING SETTINGS」→「BOOKMARKS」用)。
struct Bookmark {
  uint32_t offset = 0;  // ファイル内バイトオフセット(kPaged/kScrollどちらのモードでもこの値だけで位置を復元できる)
  int percent = 0;      // 追加した時点の読了位置(%、一覧表示用)
  String preview;        // 追加した時点の表示内容の先頭部分(一覧表示用、約20文字)
};

class TxtReaderService {
 public:
  // 表示モード。kPagedは従来通りページ単位でめくる方式、kScrollは数行ずつ連続して
  // 読み進める方式(README「読書モードについて」参照)。TxtReaderScreenがCONFIRM
  // ボタンでsetReadMode()を呼んで切り替える。
  enum class ReadMode { kPaged, kScroll };

  // ページ1行分の描画情報。Markdownモードでない場合はheadingLevel=0、
  // skipPrefixChars=0、rawContent=falseで、textはそのまま表示すればよい。
  struct RenderLine {
    String text;
    uint8_t headingLevel = 0;      // 0=見出しでない、1〜6=見出しレベル(#の数、open()のheadingFont1/2/3参照)
    bool isListItem = false;       // trueならlistFont(open()参照。未指定ならbodyFont)で描画する
    uint8_t skipPrefixChars = 0;   // 描画時に先頭何文字を隠すか("## "の見出し記号など)
    bool rawContent = false;       // trueならコードブロック内の行、記号除去(*)を行わない
  };

  // ファイルを開き、ページインデックスを構築(またはキャッシュから読み込む)し、
  // 保存済みの読書進捗があれば復元する。失敗時(ファイルが開けない等)はfalseを返す。
  //
  // markdownMode=trueの場合、行頭の見出し記号(#〜######)・コードフェンス(```)・
  // 箇条書き記号(-/*/+)を認識する(詳細はTxtReaderService.cppのclassifyMarkdownLine()
  // 参照)。太字/斜体の*記号はページインデックス構築時には除去せず(バイトオフセット
  // 計算を単純に保つため)、描画時にTxtReaderScreen側で取り除く想定。
  //
  // headingFont1/2/3は見出しレベル(#の数)ごとの描画・折り返し計算に使うフォント
  // (nullptrならbodyFontを使う)。H1はheadingFont1、H2はheadingFont2、H3〜H6は
  // すべてheadingFont3を使う(3段階までしかサイズを分けない設計。詳細はREADME
  // 「Markdown対応について」参照)。
  //
  // viewportHeightPxは1ページに収められる合計ピクセル高さ(行数ではない)。見出しは
  // 本文よりも背が高いフォントで描画されることが多く、「1ページに入る行数」を
  // 本文フォント基準の固定値で決め打ちすると、見出しを含むページの実際の描画高さが
  // この値を超えてフッターや画面外にはみ出す不具合になる(各行はそれぞれ自分の
  // フォントのlineHeight()だけピクセルを消費する、という前提でページを区切る必要が
  // ある)。そのため1行ずつ「そのフォントでの行高さ」を積算しながら、
  // viewportHeightPxを超える手前でページを区切る(loadLinesAtOffset()参照)。
  //
  // 既知の制限: コードブロックがページ境界をまたぐ場合、そのページへ直接ジャンプ
  // (保存済み進捗からの再開など)して開くとコードブロック内かどうかの状態が
  // 復元されず、そのページだけ見出し等の記法を誤認識する可能性がある
  // (逐次的にページをめくっている限りは正しく動作する)。
  //
  // listFontは箇条書き行(行頭が"- "/"* "/"+ ")の折り返し計算に使う(headingFontと
  // 同じ理由: 実際の描画フォントが本文と異なるサイズの場合、折り返し計算も
  // そのフォントで行わないと画面幅をはみ出しうるため)。nullptrならbodyFontを使う。
  // 太字(**text**)は行内の一部だけに適用される装飾のため、この仕組みでは
  // 扱わない(折り返し計算は常にbodyFont/headingFont*/listFontのいずれかで行い、
  // 太字フォントへの切り替えはTxtReaderScreen側の描画時のみに閉じている。
  // *記号自体は折り返し計算に含まれるため、太字の有無で幅が変わっても
  // 多少余白が残るだけで崩れることはない)。
  bool open(const String& path, const Font& bodyFont, int viewportWidthPx, int viewportHeightPx,
            bool markdownMode = false, const Font* headingFont1 = nullptr, const Font* headingFont2 = nullptr,
            const Font* headingFont3 = nullptr, const Font* listFont = nullptr);
  void close();
  bool isOpen() const { return fileOpen_; }

  const String& path() const { return path_; }
  int currentPage() const { return currentPage_; }
  int totalPages() const { return static_cast<int>(pageOffsets_.size()); }
  // kScrollモード中はscrollOffset_のファイル内位置(バイト単位)から計算する。
  int progressPercent() const;

  // 現在の表示内容(折り返し済み、画面に収まる分)。kPaged/kScrollどちらのモードでも
  // このvectorを描画すればよい(TxtReaderScreen側はモードを意識しなくてよい)。
  const std::vector<RenderLine>& currentLines() const { return currentLines_; }

  // ページ移動。kPagedモード時のみ意味を持つ(kScroll中に呼んでも何もしない)。
  // 範囲外への移動は何もせずfalseを返す。
  bool nextPage();
  bool prevPage();

  ReadMode readMode() const { return readMode_; }
  // 表示モードを切り替える。kPaged→kScrollでは現在ページの先頭から、
  // kScroll→kPagedでは現在のスクロール位置を含むページから再開する
  // (どちら向きでも「今読んでいた場所」が失われないようにするため)。
  // 同じモードへの切り替えは何もしない。
  void setReadMode(ReadMode mode);

  // kScrollモード用。表示位置をおよそlines行分(見出し等の高さにより多少前後する)
  // 進める/前回位置に戻す。戻る方向は履歴スタック(直近kMaxScrollHistory件)を辿る
  // ため、進んだのと完全に同じ経路を逆順に戻る。ファイル末尾で進めない、または
  // 履歴が空で戻れない場合はfalseを返す。kPagedモード中に呼んだ場合は何もせずfalseを返す。
  bool scrollForward(int lines);
  bool scrollBackward();

  // 現在の表示位置(kPaged中はページ先頭、kScroll中はスクロール位置)にブックマークを
  // 追加する。isOpen()でない場合はfalseを返す。1冊あたりkMaxBookmarks件を超えたら
  // 最も古いものから削除する(FIFO)。
  static constexpr int kMaxBookmarks = 20;
  bool addBookmark();

  // 現在開いている本のブックマーク一覧を、追加した順で返す(何も無ければfalseを返す)。
  bool readBookmarks(std::vector<Bookmark>& out) const;

  // 指定インデックス(readBookmarks()の並び順)のブックマーク位置へ移動する。現在の
  // ReadMode(kPaged/kScroll)は変えず、そのモードのままそのオフセットを表示する。
  // 範囲外ならfalseを返す。
  bool jumpToBookmark(int index);

  // 最後に開いていた本のパスと進捗(%)を取得する(ホーム画面のプレースホルダー表示用)。
  // 一度も本を開いたことがない場合はfalseを返す。
  static bool readLastBook(String& outPath, int& outPercent);

  // 最近開いた本の一覧を新しい順に返す(履歴画面用)。何も無ければfalseを返す。
  static constexpr int kMaxHistoryEntries = 10;
  static bool readHistory(std::vector<BookHistoryEntry>& out);

 private:
  bool loadPageAt(int pageIndex);
  bool loadLinesAtOffset(uint32_t offset, std::vector<RenderLine>& outLines, uint32_t& nextOffset);
  // offsetからlines行分(折り返し後の表示行数、見出し等の高さにより多少前後する)
  // 前進した先のバイトオフセットを返す。loadLinesAtOffset()と同じ折り返し・
  // Markdown解析ロジックを、viewportHeightPx_を一時的にlines行分の予算に差し替えて
  // 呼ぶことで実現している(currentLines_・currentPage_など画面表示用の状態は
  // 変更しない)。
  uint32_t advanceByLines(uint32_t offset, int lines);
  // headingLevel(1〜6)に対応するheadingFont1_/2_/3_を返す(H3以上はheadingFont3_)。
  // levelが0(見出しでない)の場合の呼び出しは想定しない。
  const Font* headingFontForLevel(uint8_t level) const;
  void buildPageIndex();
  bool loadIndexCache();
  void saveIndexCache() const;
  void loadProgress();
  // kScroll中はcurrentPage_をscrollOffset_に同期させてから保存する(非const)。
  void saveProgress();
  static void updateHistory(const String& path, int percent);
  String cachePathBase() const;
  String bookmarksFilePath() const { return cachePathBase() + ".bookmarks"; }
  void writeBookmarks(const std::vector<Bookmark>& bookmarks) const;
  // 現在のスクロール位置(scrollOffset_)からcurrentLines_を再構築する(kScroll専用)。
  void loadScrollWindow();
  // offset以下で最大のページ境界にcurrentPage_を合わせる(kScroll→kPagedへの
  // 切り替え時、kScroll中のsaveProgress()での「最後に開いたページ」保存、
  // jumpToBookmark()のkPaged側で使う)。
  void syncCurrentPageToOffset(uint32_t offset);

  FsFile file_;
  String path_;
  bool fileOpen_ = false;
  uint32_t fileSize_ = 0;
  const Font* font_ = nullptr;
  const Font* headingFont1_ = nullptr;
  const Font* headingFont2_ = nullptr;
  const Font* headingFont3_ = nullptr;
  const Font* listFont_ = nullptr;
  int viewportWidthPx_ = 0;
  int viewportHeightPx_ = 1;
  bool markdownMode_ = false;
  bool inCodeBlock_ = false;  // buildPageIndex()の逐次スキャン中のみ正しく維持される(上記の既知の制限を参照)

  std::vector<uint32_t> pageOffsets_;
  std::vector<RenderLine> currentLines_;
  int currentPage_ = 0;

  // scrollForward()で通過した位置を戻れるようにするための履歴1件分。offsetだけでなく
  // その時点のinCodeBlock_も保持することで、コードブロック内で戻ってもコードブロック
  // 記法の誤認識が起きないようにする(pageOffsets_ジャンプ時の既知の制限より正確)。
  struct ScrollHistoryEntry {
    uint32_t offset = 0;
    bool inCodeBlock = false;
  };

  ReadMode readMode_ = ReadMode::kPaged;
  uint32_t scrollOffset_ = 0;                       // kScroll中の表示ウィンドウ先頭バイトオフセット
  std::vector<ScrollHistoryEntry> scrollHistory_;   // scrollForward()で通過した位置(scrollBackward()用のLIFO)
  static constexpr size_t kMaxScrollHistory = 500;  // 際限のないメモリ増加を防ぐ上限(約4KB分)
};

// TxtReaderScreen等から`TxtReaderService::RenderLine`と書かずに済むようにするエイリアス。
using RenderLine = TxtReaderService::RenderLine;
