#include "TxtReaderService.h"

namespace {
constexpr uint32_t kChunkSize = 4096;
constexpr uint32_t kIndexMagic = 0x31545854;  // "TXT1" (little-endian表記)
constexpr uint8_t kIndexVersion = 3;          // v3: ページ区切りを行数基準からピクセル高さ基準に変更
constexpr const char* kCacheDir = "/.reader_cache";
constexpr const char* kLastBookPath = "/.reader_cache/last_book.txt";
constexpr const char* kHistoryPath = "/.reader_cache/history.log";

template <typename T>
bool writePod(FsFile& f, const T& v) {
  return f.write(reinterpret_cast<const uint8_t*>(&v), sizeof(v)) == sizeof(v);
}
template <typename T>
bool readPod(FsFile& f, T& v) {
  return f.read(reinterpret_cast<uint8_t*>(&v), sizeof(v)) == static_cast<int>(sizeof(v));
}

// UTF-8の継続バイト(10xxxxxx)かどうか。
bool isUtf8Continuation(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

// リードバイトからUTF-8シーケンスのバイト数を判定する(gfx/MiniFont.cppの
// utf8SequenceLength()と同じ規約)。不正なバイト列は1として扱う。
int utf8LeadByteLength(char c) {
  const auto b = static_cast<uint8_t>(c);
  if ((b & 0x80) == 0x00) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1;
}

// sourceLine(改行を含まない1行)をmaxWidthで折り返しながらoutLinesに追加する。
// outLinesがmaxLinesに達したら打ち切る。実際に消費したsourceLineの文字数
// (バイト数)を返す。戻り値がsourceLine.length()未満の場合、ページが行数上限に
// 達して行の途中で打ち切られたことを意味し、残りは次ページの先頭になる。
//
// Markdownモードでも見出し記号(#)や*記号を含んだ「生のsourceLine」をそのまま
// 折り返し対象にする(記号を先に除去しない)。これはページのバイトオフセット計算を
// 単純に保つための意図的な設計判断: 記号除去で文字数が変わると、consumed
// (sourceLine内の消費バイト数)と実際にファイルへ書き込まれているバイト数が
// ずれてしまい、ページ境界の計算が壊れる。記号の除去は呼び出し側
// (TxtReaderScreen)が描画直前に行う(折り返し判定がわずかに保守的になる=
// 記号の分だけ余白が残ることがあるだけで、崩れることはない)。
size_t appendWrapped(const Font& font, const String& sourceLine, int maxWidth, int maxLines,
                      std::vector<String>& outLines) {
  size_t consumed = 0;
  String remaining = sourceLine;

  while (remaining.length() > 0) {
    if (static_cast<int>(outLines.size()) >= maxLines) break;

    if (font.measureText(remaining.c_str()) <= maxWidth) {
      outLines.push_back(remaining);
      consumed += remaining.length();
      break;
    }

    // 収まる最長のプレフィックスを二分探索
    int lo = 1, hi = static_cast<int>(remaining.length());
    while (lo < hi) {
      const int mid = (lo + hi + 1) / 2;
      if (font.measureText(remaining.substring(0, mid).c_str()) <= maxWidth) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    int breakAt = lo;

    // 二分探索の結果はバイト単位なので、マルチバイトUTF-8文字の途中を指している
    // 可能性がある(日本語のようにほぼ全バイトがマルチバイトの文章では常態的に
    // 起こる)。文字境界まで後退させ、不完全な文字を出力しないようにする。
    while (breakAt > 0 && isUtf8Continuation(remaining[breakAt])) breakAt--;
    // 極端に幅が狭く1文字も収まらない場合、0まで後退してしまうと前進できず無限
    // ループになる。その場合は幅を無視してでも先頭の1文字(複数バイトの可能性が
    // ある)を丸ごと消費し、必ず前進を保証する。
    if (breakAt == 0) breakAt = utf8LeadByteLength(remaining[0]);

    // 単語境界(スペース)で折り返せるなら優先する(スペース自体は1バイトのASCIIで
    // 継続バイトの値域と重ならないため、探索結果は常に文字境界になる)。
    const int spacePos = remaining.lastIndexOf(' ', breakAt > 0 ? breakAt - 1 : 0);
    if (spacePos > 0) breakAt = spacePos;

    outLines.push_back(remaining.substring(0, breakAt));

    int skip = breakAt;
    if (skip < static_cast<int>(remaining.length()) && remaining[skip] == ' ') skip++;
    consumed += static_cast<size_t>(skip);
    remaining = remaining.substring(skip);
  }

  return consumed;
}

// Markdownの1行を分類する。inCodeBlockは呼び出し側がページ読み込みをまたいで
// 保持する状態(```の内外を判定するため)。戻り値はこの行を表示するかどうか
// (コードフェンス行自体はfalseを返し非表示にする)。headingLevelは0(見出しでない)
// または1〜6(#の数)。
bool classifyMarkdownLine(const String& sourceLine, bool& inCodeBlock, uint8_t& headingLevel, bool& isListItem,
                          uint8_t& skipPrefixChars, bool& rawContent) {
  headingLevel = 0;
  isListItem = false;
  skipPrefixChars = 0;

  String trimmed = sourceLine;
  trimmed.trim();
  if (trimmed.startsWith("```")) {
    inCodeBlock = !inCodeBlock;
    rawContent = false;
    return false;
  }

  rawContent = inCodeBlock;
  if (inCodeBlock) return true;  // コードブロック内は見出し・箇条書きの記法解釈をしない

  int hashCount = 0;
  const int len = sourceLine.length();
  while (hashCount < len && hashCount < 6 && sourceLine[hashCount] == '#') hashCount++;
  if (hashCount >= 1 && hashCount < len && sourceLine[hashCount] == ' ') {
    headingLevel = static_cast<uint8_t>(hashCount);
    skipPrefixChars = static_cast<uint8_t>(hashCount + 1);
  }

  // 箇条書き記号("- "/"* "/"+ ")の判定はtrimmed(行頭の空白を無視)で行う。
  // 記号自体は表示上そのまま残す(skipPrefixCharsは使わない)ため、ネストした
  // 箇条書きのインデントも保持される。
  if (headingLevel == 0 && trimmed.length() >= 2 && trimmed[1] == ' ' &&
      (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+')) {
    isListItem = true;
  }

  return true;
}

}  // namespace

bool TxtReaderService::open(const String& path, const Font& bodyFont, int viewportWidthPx, int viewportHeightPx,
                            bool markdownMode, const Font* headingFont1, const Font* headingFont2,
                            const Font* headingFont3, const Font* listFont) {
  close();

  file_ = SdMan.open(path.c_str(), O_RDONLY);
  if (!file_) return false;

  path_ = path;
  fileSize_ = static_cast<uint32_t>(file_.size());
  font_ = &bodyFont;
  headingFont1_ = headingFont1;
  headingFont2_ = headingFont2;
  headingFont3_ = headingFont3;
  listFont_ = listFont;
  viewportWidthPx_ = viewportWidthPx;
  viewportHeightPx_ = (viewportHeightPx < 1) ? 1 : viewportHeightPx;
  markdownMode_ = markdownMode;
  fileOpen_ = true;

  SdMan.ensureDirectoryExists(kCacheDir);

  if (!loadIndexCache()) {
    buildPageIndex();
    saveIndexCache();
  }
  if (pageOffsets_.empty()) pageOffsets_.push_back(0);  // 空ファイルでも1ページとして扱う

  // 表示するページの読み込みはここから開始する(buildPageIndex()の全文スキャンで
  // inCodeBlock_がEOF時点の状態のまま残っている可能性があるためリセットする)。
  // 保存済みページへ直接ジャンプする場合、そのページが本当にコードブロック内かは
  // 復元できない(ヘッダのコメント「既知の制限」を参照)。
  inCodeBlock_ = false;

  loadProgress();
  loadPageAt(currentPage_);
  // ページをめくらずに閉じた場合でも「最後に開いた本」がホーム画面に反映されるよう、
  // 開いた時点の状態を保存しておく。
  saveProgress();

  return true;
}

void TxtReaderService::close() {
  if (fileOpen_) file_.close();
  fileOpen_ = false;
  path_ = "";
  fileSize_ = 0;
  font_ = nullptr;
  headingFont1_ = nullptr;
  headingFont2_ = nullptr;
  headingFont3_ = nullptr;
  listFont_ = nullptr;
  markdownMode_ = false;
  inCodeBlock_ = false;
  pageOffsets_.clear();
  currentLines_.clear();
  currentPage_ = 0;
  readMode_ = ReadMode::kPaged;
  scrollOffset_ = 0;
  scrollHistory_.clear();
}

const Font* TxtReaderService::headingFontForLevel(uint8_t level) const {
  if (level <= 1) return headingFont1_;
  if (level == 2) return headingFont2_;
  return headingFont3_;  // H3〜H6はまとめてheadingFont3_
}

int TxtReaderService::progressPercent() const {
  if (readMode_ == ReadMode::kScroll) {
    if (fileSize_ == 0) return 0;
    int pct = static_cast<int>((static_cast<uint64_t>(scrollOffset_) * 100) / fileSize_);
    if (pct > 100) pct = 100;
    return pct;
  }
  const int total = totalPages();
  if (total <= 0) return 0;
  int pct = ((currentPage_ + 1) * 100) / total;
  if (pct > 100) pct = 100;
  return pct;
}

bool TxtReaderService::loadPageAt(int pageIndex) {
  if (pageIndex < 0 || pageIndex >= totalPages()) return false;
  uint32_t nextOffset = 0;
  currentLines_.clear();
  loadLinesAtOffset(pageOffsets_[pageIndex], currentLines_, nextOffset);
  currentPage_ = pageIndex;
  return true;
}

bool TxtReaderService::nextPage() {
  if (readMode_ != ReadMode::kPaged) return false;
  if (currentPage_ + 1 >= totalPages()) return false;
  loadPageAt(currentPage_ + 1);
  saveProgress();
  return true;
}

bool TxtReaderService::prevPage() {
  if (readMode_ != ReadMode::kPaged) return false;
  if (currentPage_ <= 0) return false;
  loadPageAt(currentPage_ - 1);
  saveProgress();
  return true;
}

void TxtReaderService::syncCurrentPageToOffset(uint32_t offset) {
  // pageOffsets_は逐次スキャンで構築される単調増加列(buildPageIndex()参照)なので、
  // 先頭から見てoffsetを超えた時点で1つ前の値が「offsetを含むページ」。
  int page = 0;
  for (size_t i = 0; i < pageOffsets_.size(); i++) {
    if (pageOffsets_[i] <= offset) {
      page = static_cast<int>(i);
    } else {
      break;
    }
  }
  currentPage_ = page;
}

void TxtReaderService::loadScrollWindow() {
  uint32_t nextOffset = 0;
  currentLines_.clear();
  loadLinesAtOffset(scrollOffset_, currentLines_, nextOffset);
}

void TxtReaderService::setReadMode(ReadMode mode) {
  if (mode == readMode_) return;

  if (mode == ReadMode::kScroll) {
    // 現在表示中のページの先頭からスクロール表示を開始する(読んでいた場所を保つ)。
    scrollOffset_ = (currentPage_ >= 0 && currentPage_ < totalPages()) ? pageOffsets_[currentPage_] : 0;
    scrollHistory_.clear();
    readMode_ = ReadMode::kScroll;
    loadScrollWindow();
  } else {
    // 現在のスクロール位置を含むページに合わせてページモードへ戻る。
    syncCurrentPageToOffset(scrollOffset_);
    readMode_ = ReadMode::kPaged;
    loadPageAt(currentPage_);
  }
  saveProgress();
}

bool TxtReaderService::addBookmark() {
  if (!fileOpen_) return false;

  std::vector<Bookmark> bookmarks;
  readBookmarks(bookmarks);

  Bookmark b;
  b.offset = (readMode_ == ReadMode::kScroll)
                 ? scrollOffset_
                 : ((currentPage_ >= 0 && currentPage_ < totalPages()) ? pageOffsets_[currentPage_] : 0);
  b.percent = progressPercent();

  // 追加した時点の表示内容の先頭(空行でない最初の行)を一覧表示用のプレビューにする。
  for (const auto& line : currentLines_) {
    String t = line.text;
    if (line.skipPrefixChars > 0 && line.skipPrefixChars <= t.length()) {
      t = t.substring(line.skipPrefixChars);
    }
    t.trim();
    if (t.length() == 0) continue;

    constexpr size_t kPreviewMaxBytes = 20;
    if (t.length() > kPreviewMaxBytes) {
      size_t cut = kPreviewMaxBytes;
      while (cut > 0 && isUtf8Continuation(t[cut])) cut--;
      t = t.substring(0, cut);
    }
    t.replace('|', ' ');
    b.preview = t;
    break;
  }

  if (bookmarks.size() >= static_cast<size_t>(kMaxBookmarks)) {
    bookmarks.erase(bookmarks.begin());  // 上限に達したら最も古いものを捨てる(FIFO)
  }
  bookmarks.push_back(b);
  writeBookmarks(bookmarks);
  return true;
}

bool TxtReaderService::readBookmarks(std::vector<Bookmark>& out) const {
  out.clear();
  const String content = SdMan.readFile(bookmarksFilePath().c_str());
  if (content.length() == 0) return false;

  int start = 0;
  const int len = static_cast<int>(content.length());
  while (start < len) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = len;
    const String line = content.substring(start, nl);
    start = nl + 1;
    if (line.length() == 0) continue;

    const int sep1 = line.indexOf('|');
    if (sep1 < 0) continue;
    const int sep2 = line.indexOf('|', sep1 + 1);
    if (sep2 < 0) continue;

    Bookmark b;
    b.offset = static_cast<uint32_t>(line.substring(0, sep1).toInt());
    b.percent = line.substring(sep1 + 1, sep2).toInt();
    b.preview = line.substring(sep2 + 1);
    out.push_back(b);
  }
  return !out.empty();
}

void TxtReaderService::writeBookmarks(const std::vector<Bookmark>& bookmarks) const {
  String content;
  for (const auto& b : bookmarks) {
    content += String(b.offset) + "|" + String(b.percent) + "|" + b.preview + "\n";
  }
  SdMan.writeFile(bookmarksFilePath().c_str(), content);
}

bool TxtReaderService::jumpToBookmark(int index) {
  std::vector<Bookmark> bookmarks;
  if (!readBookmarks(bookmarks)) return false;
  if (index < 0 || index >= static_cast<int>(bookmarks.size())) return false;

  const uint32_t offset = bookmarks[static_cast<size_t>(index)].offset;
  if (readMode_ == ReadMode::kScroll) {
    scrollOffset_ = offset;
    scrollHistory_.clear();  // ジャンプ先は連続してスクロールしてきた経路ではないため履歴を破棄する
    loadScrollWindow();
  } else {
    syncCurrentPageToOffset(offset);
    loadPageAt(currentPage_);
  }
  saveProgress();
  return true;
}

bool TxtReaderService::scrollForward(int lines) {
  if (readMode_ != ReadMode::kScroll) return false;
  const uint32_t offsetBefore = scrollOffset_;
  const bool codeBlockBefore = inCodeBlock_;  // 戻る際に復元する「現在位置」の状態

  // advanceByLines()はloadLinesAtOffset()を呼ぶ副作用として、inCodeBlock_を
  // 新しいオフセット時点の状態に更新する(下記loadScrollWindow()がそれを使う)。
  const uint32_t next = advanceByLines(scrollOffset_, lines);
  if (next <= offsetBefore) {
    inCodeBlock_ = codeBlockBefore;  // 進めなかった場合は状態を元に戻しておく
    return false;  // ファイル末尾等でこれ以上進めない
  }

  if (scrollHistory_.size() >= kMaxScrollHistory) {
    // 上限に達したら最古の履歴を捨てる(あまり遠くまで戻れなくなるだけで、
    // それ以上前進はでき続ける)。
    scrollHistory_.erase(scrollHistory_.begin());
  }
  scrollHistory_.push_back(ScrollHistoryEntry{offsetBefore, codeBlockBefore});
  scrollOffset_ = next;
  loadScrollWindow();
  saveProgress();
  return true;
}

bool TxtReaderService::scrollBackward() {
  if (readMode_ != ReadMode::kScroll) return false;
  if (scrollHistory_.empty()) return false;  // ページ先頭(スクロール開始位置)より前には戻れない

  const ScrollHistoryEntry entry = scrollHistory_.back();
  scrollHistory_.pop_back();
  scrollOffset_ = entry.offset;
  inCodeBlock_ = entry.inCodeBlock;  // その時点で正しかったコードブロック内外状態を復元する
  loadScrollWindow();
  saveProgress();
  return true;
}

uint32_t TxtReaderService::advanceByLines(uint32_t offset, int lines) {
  if (offset >= fileSize_ || lines <= 0) return offset;

  // loadLinesAtOffset()はviewportHeightPx_(ピクセル高さ予算)を使い切るまで行を
  // 積む仕組みなので、それを「本文フォントの行高さ×lines」に一時的に差し替えて
  // 呼び出すことで、同じ折り返し・Markdown解析ロジックを流用する(見出し等が
  // 混じっていると実際に進む表示行数は多少前後するが、スクロール量の目安としては
  // 十分)。outLines自体は捨て、進んだ先のバイトオフセットだけを使う。
  const int bodyLineH = (font_ && font_->lineHeight() > 0) ? font_->lineHeight() : 1;
  const int savedHeight = viewportHeightPx_;
  viewportHeightPx_ = lines * bodyLineH;

  std::vector<RenderLine> tmp;
  uint32_t nextOffset = offset;
  loadLinesAtOffset(offset, tmp, nextOffset);

  viewportHeightPx_ = savedHeight;
  return nextOffset;
}

bool TxtReaderService::loadLinesAtOffset(uint32_t offset, std::vector<RenderLine>& outLines, uint32_t& nextOffset) {
  outLines.clear();
  if (offset >= fileSize_) return false;

  const uint32_t chunkSize = (fileSize_ - offset < kChunkSize) ? (fileSize_ - offset) : kChunkSize;
  std::vector<uint8_t> buffer(chunkSize);
  if (!file_.seek(offset)) return false;
  const int bytesRead = file_.read(buffer.data(), chunkSize);
  if (bytesRead <= 0) return false;
  const uint32_t n = static_cast<uint32_t>(bytesRead);

  bool inCodeBlock = inCodeBlock_;
  // このページで残っている描画可能ピクセル高さ。行を1つ追加するたびに、その行を
  // 描画した「そのフォント」のlineHeight()分だけ減らす(本文/見出し/リストで
  // フォントの高さが異なるため、行数ではなくピクセル高さで予算管理する。
  // ヘッダのコメント「はみ出し不具合」参照)。
  int heightRemaining = viewportHeightPx_;
  const int bodyLineH = (font_->lineHeight() > 0) ? font_->lineHeight() : 1;

  uint32_t pos = 0;
  while (pos < n && heightRemaining > 0) {
    uint32_t lineEnd = pos;
    while (lineEnd < n && buffer[lineEnd] != '\n') lineEnd++;

    const bool lineComplete = (lineEnd < n) || (offset + lineEnd >= fileSize_);
    if (!lineComplete && !outLines.empty()) break;  // チャンクの途中で切れた行は次回のチャンクに回す

    uint32_t contentEnd = lineEnd;
    if (contentEnd > pos && buffer[contentEnd - 1] == '\r') contentEnd--;  // CRLF対応

    String sourceLine;
    sourceLine.reserve(contentEnd - pos);
    for (uint32_t i = pos; i < contentEnd; i++) sourceLine += static_cast<char>(buffer[i]);

    if (sourceLine.length() == 0) {
      outLines.push_back(RenderLine{"", 0, false, 0, inCodeBlock});
      heightRemaining -= bodyLineH;  // 空行は本文フォントの高さで描画される(TxtReaderScreen::render()参照)
      pos = (lineEnd < n) ? (lineEnd + 1) : n;
      if (lineEnd >= n) break;
      continue;
    }

    uint8_t headingLevel = 0;
    bool isListItem = false;
    uint8_t skipPrefixChars = 0;
    bool rawContent = false;
    bool visible = true;
    if (markdownMode_) {
      visible = classifyMarkdownLine(sourceLine, inCodeBlock, headingLevel, isListItem, skipPrefixChars, rawContent);
      // 箇条書き記号(*/+)を"-"に正規化する(1文字→1文字の置換なのでバイト長は
      // 変わらず、後続のオフセット計算に影響しない)。見出し・コードブロック内は対象外。
      if (visible && !rawContent && headingLevel == 0 && sourceLine.length() >= 2 && sourceLine[1] == ' ' &&
          (sourceLine[0] == '*' || sourceLine[0] == '+')) {
        sourceLine.setCharAt(0, '-');
      }
    }

    if (!visible) {
      // コードフェンス行自体は空行として1行分だけ消費して表示する。
      outLines.push_back(RenderLine{"", 0, false, 0, false});
      heightRemaining -= bodyLineH;
      pos = (lineEnd < n) ? (lineEnd + 1) : n;
      if (lineEnd >= n) break;
      continue;
    }

    const Font* headingWrapFont = (headingLevel > 0) ? headingFontForLevel(headingLevel) : nullptr;
    const Font& wrapFont =
        headingWrapFont ? *headingWrapFont : ((isListItem && listFont_) ? *listFont_ : *font_);
    const int wrapLineH = (wrapFont.lineHeight() > 0) ? wrapFont.lineHeight() : 1;
    int roomRemaining = heightRemaining / wrapLineH;
    if (roomRemaining <= 0) {
      // このフォントの行の高さ的に1行も残り領域に入らない。ページ先頭の行(まだ何も
      // 描画していない)なら、無限ループを避けるためはみ出しを許容してでも1行だけ
      // 強制的に入れる(1行がviewportHeightPx_より背の高い極端なフォントの場合の保険)。
      // それ以外(既に他の行を描画済み)なら、このページはもう入らないので次ページへ回す。
      if (!outLines.empty()) break;
      roomRemaining = 1;
    }
    std::vector<String> wrapped;
    const size_t consumed = appendWrapped(wrapFont, sourceLine, viewportWidthPx_, roomRemaining, wrapped);

    for (size_t i = 0; i < wrapped.size(); i++) {
      RenderLine rl;
      rl.text = wrapped[i];
      rl.headingLevel = headingLevel;
      rl.isListItem = isListItem;
      rl.skipPrefixChars = (i == 0) ? skipPrefixChars : 0;
      rl.rawContent = rawContent;
      outLines.push_back(rl);
      heightRemaining -= wrapLineH;
    }

    if (consumed < static_cast<size_t>(sourceLine.length())) {
      // ページの残り高さが尽き、この行を最後まで描画できなかった。続きは次ページの先頭にする。
      pos += static_cast<uint32_t>(consumed);
      break;
    }

    pos = (lineEnd < n) ? (lineEnd + 1) : n;
    if (lineEnd >= n) break;
  }

  if (pos == 0) pos = 1;  // 無限ループ防止の保険(必ず前進させる)
  nextOffset = offset + pos;
  if (nextOffset > fileSize_) nextOffset = fileSize_;

  // このページを最後まで走査できた場合のみinCodeBlock_を更新する。buildPageIndex()の
  // 逐次スキャン中は正しく状態が引き継がれるが、loadPageAt()による単発読み込みでは
  // 呼び出し側(open()/nextPage()/prevPage())が前提とする状態次第になる
  // (ヘッダのコメント「既知の制限」を参照)。
  inCodeBlock_ = inCodeBlock;

  return !outLines.empty();
}

void TxtReaderService::buildPageIndex() {
  pageOffsets_.clear();
  pageOffsets_.push_back(0);
  inCodeBlock_ = false;  // ファイル先頭から逐次スキャンするのでここでリセットしてよい

  uint32_t offset = 0;
  while (offset < fileSize_) {
    std::vector<RenderLine> tmp;
    uint32_t nextOffset = offset;
    if (!loadLinesAtOffset(offset, tmp, nextOffset) || nextOffset <= offset) break;
    offset = nextOffset;
    if (offset < fileSize_) pageOffsets_.push_back(offset);
  }
}

String TxtReaderService::cachePathBase() const {
  String sanitized = path_;
  sanitized.replace('/', '_');
  while (sanitized.startsWith("_")) sanitized.remove(0, 1);
  return String(kCacheDir) + "/" + sanitized;
}

bool TxtReaderService::loadIndexCache() {
  FsFile f = SdMan.open((cachePathBase() + ".idx").c_str(), O_RDONLY);
  if (!f) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint32_t cachedFileSize = 0;
  int32_t cachedWidth = 0, cachedHeightPx = 0;
  uint8_t cachedMarkdownMode = 0;
  uint32_t numPages = 0;
  const bool headerOk = readPod(f, magic) && magic == kIndexMagic && readPod(f, version) &&
                        version == kIndexVersion && readPod(f, cachedFileSize) && cachedFileSize == fileSize_ &&
                        readPod(f, cachedWidth) && cachedWidth == viewportWidthPx_ && readPod(f, cachedHeightPx) &&
                        cachedHeightPx == viewportHeightPx_ && readPod(f, cachedMarkdownMode) &&
                        (cachedMarkdownMode != 0) == markdownMode_ && readPod(f, numPages);
  if (!headerOk) {
    f.close();
    return false;
  }

  pageOffsets_.clear();
  pageOffsets_.reserve(numPages);
  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t off = 0;
    if (!readPod(f, off)) {
      f.close();
      pageOffsets_.clear();
      return false;
    }
    pageOffsets_.push_back(off);
  }
  f.close();
  return !pageOffsets_.empty();
}

void TxtReaderService::saveIndexCache() const {
  FsFile f = SdMan.open((cachePathBase() + ".idx").c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return;

  writePod(f, kIndexMagic);
  writePod(f, kIndexVersion);
  writePod(f, fileSize_);
  writePod(f, static_cast<int32_t>(viewportWidthPx_));
  writePod(f, static_cast<int32_t>(viewportHeightPx_));
  writePod(f, static_cast<uint8_t>(markdownMode_ ? 1 : 0));
  writePod(f, static_cast<uint32_t>(pageOffsets_.size()));
  for (uint32_t off : pageOffsets_) writePod(f, off);
  f.close();
}

void TxtReaderService::loadProgress() {
  currentPage_ = 0;
  FsFile f = SdMan.open((cachePathBase() + ".pos").c_str(), O_RDONLY);
  if (f) {
    uint32_t page = 0;
    if (readPod(f, page)) currentPage_ = static_cast<int>(page);
    f.close();
  }
  if (currentPage_ < 0) currentPage_ = 0;
  if (currentPage_ >= totalPages()) currentPage_ = totalPages() - 1;
}

void TxtReaderService::saveProgress() {
  // kScroll中はcurrentPage_を更新していないため、保存前に現在のスクロール位置を
  // 含むページへ同期しておく(次回起動時にloadProgress()で正しい位置から再開できる
  // ようにするため。readMode_自体はkScrollのまま保たれ、開き直すとkPagedから始まる
  // 既存の挙動に合わせて、ページ単位での再開になる)。
  if (readMode_ == ReadMode::kScroll) syncCurrentPageToOffset(scrollOffset_);

  FsFile f = SdMan.open((cachePathBase() + ".pos").c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (f) {
    writePod(f, static_cast<uint32_t>(currentPage_));
    f.close();
  }
  // ホーム画面が「続きから」を出せるよう、直近に開いていた本としても記録する。
  SdMan.writeFile(kLastBookPath, path_ + "\n" + String(progressPercent()));
  updateHistory(path_, progressPercent());
}

void TxtReaderService::updateHistory(const String& path, int percent) {
  std::vector<BookHistoryEntry> entries;
  readHistory(entries);

  // 同じ本の既存エントリは除去してから先頭に付け直す(重複防止+最近順の維持)。
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].path == path) {
      entries.erase(entries.begin() + i);
      break;
    }
  }

  BookHistoryEntry newEntry;
  newEntry.path = path;
  newEntry.percent = percent;
  entries.insert(entries.begin(), newEntry);

  if (entries.size() > static_cast<size_t>(kMaxHistoryEntries)) {
    entries.resize(kMaxHistoryEntries);
  }

  String content;
  for (const auto& e : entries) {
    content += String(e.percent) + "|" + e.path + "\n";
  }
  SdMan.writeFile(kHistoryPath, content);
}

bool TxtReaderService::readHistory(std::vector<BookHistoryEntry>& out) {
  out.clear();
  const String content = SdMan.readFile(kHistoryPath);
  if (content.length() == 0) return false;

  int start = 0;
  const int len = static_cast<int>(content.length());
  while (start < len) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = len;
    const String line = content.substring(start, nl);
    start = nl + 1;
    if (line.length() == 0) continue;

    const int sep = line.indexOf('|');
    if (sep < 0) continue;

    BookHistoryEntry e;
    e.percent = line.substring(0, sep).toInt();
    e.path = line.substring(sep + 1);
    if (e.path.length() > 0) out.push_back(e);
  }
  return !out.empty();
}

bool TxtReaderService::readLastBook(String& outPath, int& outPercent) {
  const String content = SdMan.readFile(kLastBookPath);
  if (content.length() == 0) return false;

  const int nl = content.indexOf('\n');
  if (nl < 0) return false;

  outPath = content.substring(0, nl);
  outPercent = content.substring(nl + 1).toInt();
  return outPath.length() > 0;
}
