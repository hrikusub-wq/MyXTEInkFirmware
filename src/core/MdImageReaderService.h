#pragma once
#include <Arduino.h>
#include <SDCardManager.h>

#include <vector>

// PC側コンパニオンツール(XTEinkBLEFileSender)が事前レンダリングしたMarkdown
// ページ画像(.pgc、"/User/.md_cache/<name>/"配下)を表示するためのコアロジック。
//
// TxtReaderServiceと違い、行の折り返し・ページ分割は一切行わない(PC側で確定
// 済みのビットマップをそのまま表示するだけ)。かわりに"index.bin"(実機が読む
// 唯一のメタデータ。PC側は記録用にJSONの"meta.json"も書くが、このプロジェクトに
// JSON解析ライブラリが無いため実機は読まない)のページ数・ページごとのバイト
// オフセット配列を信じて、ページ送り・ブックマークのoffset解決を行う。
//
// ブックマーク(.bookmarks)・最終閲覧本(last_book.txt)・履歴(history.log)は
// TxtReaderService.cppと全く同じファイルパス・同じフォーマットで読み書きする
// (サニタイズ規則・区切り文字フォーマットは意図的に複製している。CjkFontImplと
// XteinkBinFontImplがutf8Next()をそれぞれ複製しているのと同じ、小さく自己完結
// させるための設計文化に倣う)。これにより画像ベースのMarkdownもHomeScreenの
// 「続きから」・HistoryScreenの履歴に無変更で反映される。
//
// RAM制約(AI_RULES.md参照、NimBLE起動後は空きヒープ約30〜45KB)のため、
// 1ページ分(496x728px 1bpp ≒ 45KB)を丸ごと保持するバッファは持たない。
// renderCurrentPage()はSDから数行ずつストリーム読みしながら1行ずつ
// フレームバッファへ描画する(StandbyScreenのJPEG表示がMCU行バンド単位で
// デコードする設計を踏襲した考え方)。
class MdImageReaderService {
 public:
  struct Bookmark {
    uint32_t offset = 0;  // 元.mdファイル内のバイトオフセット(TxtReaderService::Bookmarkと同じ意味)
    int percent = 0;
    String preview;  // 本文プレビューを持てないため"PAGE n/total"形式の固定文言
  };

  // mdPathに対応する画像キャッシュ(index.bin)を検証・読み込みし、保存済みの
  // 表示ページがあれば復元する。失敗時(index.bin不在・検証失敗)はfalseを返す
  // (呼び出し側=main.cppはTxtReaderScreenへフォールバックすること)。
  bool open(const String& mdPath);
  void close();
  bool isOpen() const { return path_.length() > 0; }
  const String& path() const { return path_; }

  int currentPage() const { return currentPage_; }
  int totalPages() const { return pageCount_; }
  int progressPercent() const;

  // 現在ページの.pgcをSDから数行ずつストリーム読みしながら、フレームバッファの
  // (x, y)を左上としてページ全体を描画する。
  bool renderCurrentPage(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y);

  bool nextPage();
  bool prevPage();

  static constexpr int kMaxBookmarks = 20;
  // 現在ページの先頭にブックマークを追加する。TxtReaderService::addBookmark()と
  // 同じ上限・FIFO削除ルール。isOpen()でない場合はfalseを返す。
  bool addBookmark();
  bool readBookmarks(std::vector<Bookmark>& out) const;
  bool jumpToBookmark(int index);

  // main.cppのkOpenFile分岐用: mdPathが有効な画像キャッシュを持つかどうかだけを
  // 軽量に判定する(index.binのヘッダ32Bのみ読む、ページ配列は読まない)。
  static bool hasValidImageCache(const String& mdPath);

 private:
  static constexpr uint32_t kIndexHeaderSize = 32;

  static String sanitize(const String& devicePath);
  static String cacheDirFor(const String& mdPath);
  String indexPath() const { return cacheDirFor(path_) + "/index.bin"; }
  String pagePath(int index) const;
  // "/.reader_cache/" + sanitize(path_)。TxtReaderService::cachePathBase()と同じ規則。
  String cachePathBase() const;
  String bookmarksFilePath() const { return cachePathBase() + ".bookmarks"; }
  String progressFilePath() const { return cachePathBase() + ".mdipos"; }

  bool loadIndex();
  void writeBookmarks(const std::vector<Bookmark>& bookmarks) const;
  void loadProgress();
  // 現在ページを.mdipos(独自拡張子、TxtReaderServiceの".pos"とは別ファイル)へ
  // 保存し、last_book.txt/history.logも更新する(TxtReaderService::saveProgress()
  // と同じタイミング=ページ送りのたびに呼ぶ)。
  void saveProgress() const;
  void updateHistory(const String& path, int percent) const;

  String path_;
  int pageCount_ = 0;
  int currentPage_ = 0;
  std::vector<uint32_t> pageByteOffsets_;
};
