#include "MdImageReaderService.h"

#include <cstring>

#include "../gfx/FrameBufferOps.h"
#include "TxtReaderService.h"

namespace {
constexpr char kIndexMagic[8] = {'X', 'T', 'E', 'M', 'D', 'I', '0', '1'};
constexpr uint8_t kIndexVersion = 1;
// 実機のセーフエリア寸法(TxtReaderScreen.hのkContentMargin/kFooterHeightから
// 導かれる値、main.cppのLOGICAL_WIDTH/LOGICAL_HEIGHT=528/792が前提)。PC側の
// レンダリング結果とこれが一致しない場合、キャッシュを無効として扱う。
constexpr uint16_t kExpectedSafeWidth = 496;
constexpr uint16_t kExpectedSafeHeight = 728;

constexpr char kPgcMagic[8] = {'X', 'T', 'E', 'P', 'G', 'C', '0', '1'};
constexpr uint32_t kPgcHeaderSize = 32;

// TxtReaderService.cppの同名定数(無名namespace内でこのファイルから参照できない)
// と文字列を完全に一致させること。変更する場合は両ファイルで同期する。
constexpr const char* kReaderCacheDir = "/.reader_cache";
constexpr const char* kLastBookPath = "/.reader_cache/last_book.txt";
constexpr const char* kHistoryPath = "/.reader_cache/history.log";

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

String MdImageReaderService::sanitize(const String& devicePath) {
  String s = devicePath;
  s.replace('/', '_');
  while (s.startsWith("_")) s.remove(0, 1);
  return s;
}

String MdImageReaderService::cacheDirFor(const String& mdPath) {
  return String("/User/.md_cache/") + sanitize(mdPath);
}

String MdImageReaderService::pagePath(int index) const {
  char buf[24];
  snprintf(buf, sizeof(buf), "/page_%04d.pgc", index);
  return cacheDirFor(path_) + buf;
}

String MdImageReaderService::cachePathBase() const {
  return String(kReaderCacheDir) + "/" + sanitize(path_);
}

bool MdImageReaderService::hasValidImageCache(const String& mdPath) {
  const String idxPath = cacheDirFor(mdPath) + "/index.bin";
  FsFile f = SdMan.open(idxPath.c_str(), O_RDONLY);
  if (!f) return false;

  uint8_t header[kIndexHeaderSize];
  const bool readOk = f.read(header, kIndexHeaderSize) == static_cast<int>(kIndexHeaderSize);
  f.close();
  if (!readOk) return false;

  if (memcmp(header, kIndexMagic, 8) != 0) return false;
  if (header[8] != kIndexVersion) return false;
  if (readU16(header + 10) != kExpectedSafeWidth) return false;
  if (readU16(header + 12) != kExpectedSafeHeight) return false;
  if (readU16(header + 14) == 0) return false;  // page_count
  const uint32_t sourceSize = readU32(header + 16);

  FsFile mdFile = SdMan.open(mdPath.c_str(), O_RDONLY);
  if (!mdFile) return false;
  const uint32_t actualSize = static_cast<uint32_t>(mdFile.size());
  mdFile.close();
  return actualSize == sourceSize;
}

bool MdImageReaderService::loadIndex() {
  FsFile f = SdMan.open(indexPath().c_str(), O_RDONLY);
  if (!f) return false;

  uint8_t header[kIndexHeaderSize];
  if (f.read(header, kIndexHeaderSize) != static_cast<int>(kIndexHeaderSize)) {
    f.close();
    return false;
  }
  if (memcmp(header, kIndexMagic, 8) != 0 || header[8] != kIndexVersion) {
    f.close();
    return false;
  }
  const uint16_t pageCount = readU16(header + 14);
  if (pageCount == 0) {
    f.close();
    return false;
  }

  pageByteOffsets_.clear();
  pageByteOffsets_.reserve(pageCount);
  bool ok = true;
  for (uint16_t i = 0; i < pageCount && ok; i++) {
    uint8_t buf[4];
    if (f.read(buf, 4) != 4) {
      ok = false;
      break;
    }
    pageByteOffsets_.push_back(readU32(buf));
  }
  f.close();
  if (!ok) {
    pageByteOffsets_.clear();
    return false;
  }

  pageCount_ = static_cast<int>(pageCount);
  return true;
}

bool MdImageReaderService::open(const String& mdPath) {
  close();
  if (!hasValidImageCache(mdPath)) return false;

  path_ = mdPath;
  if (!loadIndex()) {
    path_ = "";
    return false;
  }

  loadProgress();
  saveProgress();  // 開いた時点の状態を「最後に開いた本」へ反映する(TxtReaderServiceと同じ)
  return true;
}

void MdImageReaderService::close() {
  path_ = "";
  pageCount_ = 0;
  currentPage_ = 0;
  pageByteOffsets_.clear();
}

int MdImageReaderService::progressPercent() const {
  if (pageCount_ <= 0) return 0;
  int pct = ((currentPage_ + 1) * 100) / pageCount_;
  if (pct > 100) pct = 100;
  return pct;
}

bool MdImageReaderService::renderCurrentPage(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y) {
  if (!isOpen()) return false;

  FsFile f = SdMan.open(pagePath(currentPage_).c_str(), O_RDONLY);
  if (!f) return false;

  uint8_t header[kPgcHeaderSize];
  if (f.read(header, kPgcHeaderSize) != static_cast<int>(kPgcHeaderSize) || memcmp(header, kPgcMagic, 8) != 0) {
    f.close();
    return false;
  }
  const uint16_t widthPx = readU16(header + 8);
  const uint16_t heightPx = readU16(header + 10);
  const uint16_t widthBytes = readU16(header + 12);
  if (widthPx == 0 || heightPx == 0 || widthBytes == 0) {
    f.close();
    return false;
  }

  // ページ全体(約45KB)を一度に保持する大きなバッファは持たず、数行分ずつ
  // ストリーム読みしながら1行ずつフレームバッファへblitする(RAM制約、
  // クラスコメント参照)。
  static constexpr size_t kRowBatchBufferSize = 512;
  uint8_t rowBuffer[kRowBatchBufferSize];
  const int rowsPerBatch = static_cast<int>(kRowBatchBufferSize / widthBytes);
  if (rowsPerBatch <= 0) {
    f.close();
    return false;  // 1行すら入らない極端なケース(通常起こらない)
  }

  bool ok = true;
  int row = 0;
  while (row < heightPx && ok) {
    const int remaining = static_cast<int>(heightPx) - row;
    const int batchRows = (rowsPerBatch < remaining) ? rowsPerBatch : remaining;
    const size_t batchBytes = static_cast<size_t>(batchRows) * widthBytes;
    if (f.read(rowBuffer, batchBytes) != static_cast<int>(batchBytes)) {
      ok = false;
      break;
    }
    for (int i = 0; i < batchRows; i++) {
      FrameBufferOps::blitBitmapRow(fb, fbWidth, fbHeight, x, y + row + i, rowBuffer + i * widthBytes, widthPx);
    }
    row += batchRows;
  }
  f.close();
  return ok;
}

bool MdImageReaderService::nextPage() {
  if (currentPage_ + 1 >= pageCount_) return false;
  currentPage_++;
  saveProgress();
  return true;
}

bool MdImageReaderService::prevPage() {
  if (currentPage_ <= 0) return false;
  currentPage_--;
  saveProgress();
  return true;
}

bool MdImageReaderService::addBookmark() {
  if (!isOpen()) return false;

  std::vector<Bookmark> bookmarks;
  readBookmarks(bookmarks);

  Bookmark b;
  b.offset = (currentPage_ >= 0 && currentPage_ < static_cast<int>(pageByteOffsets_.size()))
                 ? pageByteOffsets_[static_cast<size_t>(currentPage_)]
                 : 0;
  b.percent = progressPercent();
  char buf[24];
  snprintf(buf, sizeof(buf), "PAGE %d/%d", currentPage_ + 1, pageCount_);
  b.preview = buf;

  if (bookmarks.size() >= static_cast<size_t>(kMaxBookmarks)) {
    bookmarks.erase(bookmarks.begin());  // 最古を削除(FIFO)
  }
  bookmarks.push_back(b);
  writeBookmarks(bookmarks);
  return true;
}

bool MdImageReaderService::readBookmarks(std::vector<Bookmark>& out) const {
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

void MdImageReaderService::writeBookmarks(const std::vector<Bookmark>& bookmarks) const {
  String content;
  for (const auto& b : bookmarks) {
    content += String(b.offset) + "|" + String(b.percent) + "|" + b.preview + "\n";
  }
  SdMan.writeFile(bookmarksFilePath().c_str(), content);
}

bool MdImageReaderService::jumpToBookmark(int index) {
  std::vector<Bookmark> bookmarks;
  if (!readBookmarks(bookmarks)) return false;
  if (index < 0 || index >= static_cast<int>(bookmarks.size())) return false;

  const uint32_t targetOffset = bookmarks[static_cast<size_t>(index)].offset;
  int page = 0;
  for (size_t i = 0; i < pageByteOffsets_.size(); i++) {
    if (pageByteOffsets_[i] <= targetOffset) {
      page = static_cast<int>(i);
    } else {
      break;  // pageByteOffsets_は単調増加(PC側Paginatorの生成順そのまま)
    }
  }
  currentPage_ = page;
  saveProgress();
  return true;
}

void MdImageReaderService::loadProgress() {
  currentPage_ = 0;
  FsFile f = SdMan.open(progressFilePath().c_str(), O_RDONLY);
  if (f) {
    uint32_t page = 0;
    if (f.read(reinterpret_cast<uint8_t*>(&page), sizeof(page)) == sizeof(page)) {
      currentPage_ = static_cast<int>(page);
    }
    f.close();
  }
  if (currentPage_ < 0) currentPage_ = 0;
  if (currentPage_ >= pageCount_) currentPage_ = pageCount_ - 1;
}

void MdImageReaderService::saveProgress() const {
  FsFile f = SdMan.open(progressFilePath().c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (f) {
    const uint32_t page = static_cast<uint32_t>(currentPage_);
    f.write(reinterpret_cast<const uint8_t*>(&page), sizeof(page));
    f.close();
  }
  // ホーム画面が「続きから」を出せるよう、直近に開いていた本としても記録する
  // (TxtReaderService::saveProgress()と同じタイミング・同じファイルを共有する)。
  SdMan.writeFile(kLastBookPath, path_ + "\n" + String(progressPercent()));
  updateHistory(path_, progressPercent());
}

void MdImageReaderService::updateHistory(const String& path, int percent) const {
  // 読み込みはTxtReaderServiceの公開staticメソッドを再利用する(history.logの
  // パース・フォーマットを二重に持たないため)。書き込み側(dedupe+先頭挿入+
  // 上限切り詰め)はTxtReaderService::updateHistory()が非公開のため複製する。
  std::vector<BookHistoryEntry> entries;
  TxtReaderService::readHistory(entries);

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

  if (entries.size() > static_cast<size_t>(TxtReaderService::kMaxHistoryEntries)) {
    entries.resize(TxtReaderService::kMaxHistoryEntries);
  }

  String content;
  for (const auto& e : entries) {
    content += String(e.percent) + "|" + e.path + "\n";
  }
  SdMan.writeFile(kHistoryPath, content);
}
