#include "CjkFontImpl.h"

#include <cstring>
#include <utility>

#include "FrameBufferOps.h"

namespace {

constexpr char kMagic[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
constexpr uint16_t kVersionMin = 4;
constexpr uint16_t kVersionMax = 5;
constexpr uint32_t kHeaderSize = 32;
constexpr uint32_t kStyleTocEntrySize = 32;
constexpr uint32_t kIntervalRecordSize = 12;  // uint32×3 (first, last, offset)
constexpr uint32_t kGlyphRecordSize = 16;     // .cpfontのグリフ1件分のバイト数(EpdGlyph相当)
constexpr uint32_t kKernClassEntrySize = 3;
constexpr uint32_t kLigaturePairSize = 8;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t readI16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// 12.4固定小数点(1/16px)⇔pxの変換。crosspoint-jpのfp4名前空間と同じ規約
// (4ビットの端数、四捨五入は+8してから右シフト)。
int32_t fp4FromPixel(int px) { return static_cast<int32_t>(px) << 4; }
int fp4ToPixel(int32_t fp) { return static_cast<int>((fp + 8) >> 4); }

// UTF-8の次の1コードポイントを取り出し、pを次の文字の先頭へ進める。
// 文字列末尾(\0)ではpを進めずに0を返す。不正なバイト列は1バイトだけ読み飛ばし
// U+FFFD(置換文字)を返す(バッファ外読み出しはしない: 各継続バイトのチェックは
// 直前のチェックが真の場合のみ短絡評価で進むため、\0に達したら必ずそこで止まる)。
uint32_t utf8Next(const char*& p) {
  const auto b0 = static_cast<uint8_t>(*p);
  if (b0 == 0) return 0;

  auto isCont = [](const char* q) { return (static_cast<uint8_t>(*q) & 0xC0) == 0x80; };

  if ((b0 & 0x80) == 0x00) {
    p += 1;
    return b0;
  }
  if ((b0 & 0xE0) == 0xC0) {
    if (!isCont(p + 1)) {
      p += 1;
      return 0xFFFD;
    }
    const uint32_t cp = (static_cast<uint32_t>(b0 & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
    p += 2;
    return cp;
  }
  if ((b0 & 0xF0) == 0xE0) {
    if (!isCont(p + 1) || !isCont(p + 2)) {
      p += 1;
      return 0xFFFD;
    }
    const uint32_t cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) | ((static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[2]) & 0x3F);
    p += 3;
    return cp;
  }
  if ((b0 & 0xF8) == 0xF0) {
    if (!isCont(p + 1) || !isCont(p + 2) || !isCont(p + 3)) {
      p += 1;
      return 0xFFFD;
    }
    const uint32_t cp = (static_cast<uint32_t>(b0 & 0x07) << 18) | ((static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
                        ((static_cast<uint8_t>(p[2]) & 0x3F) << 6) | (static_cast<uint8_t>(p[3]) & 0x3F);
    p += 4;
    return cp;
  }
  p += 1;
  return 0xFFFD;
}

}  // namespace

bool CjkFontImpl::begin(const char* path) {
  ready_ = false;
  intervals_.clear();
  cache_.clear();
  if (file_) file_.close();

  file_ = SdMan.open(path, O_RDONLY);
  if (!file_) return false;

  uint8_t header[kHeaderSize];
  if (file_.read(header, kHeaderSize) != static_cast<int>(kHeaderSize) || memcmp(header, kMagic, 8) != 0) {
    file_.close();
    return false;
  }

  const uint16_t version = readU16(header + 8);
  if (version < kVersionMin || version > kVersionMax) {
    file_.close();
    return false;
  }
  const bool is2Bit = (readU16(header + 10) & 1) != 0;
  if (!is2Bit) {
    // 1bit/pixel形式のSDカードフォントは未対応(手元で検証できていないため)。
    file_.close();
    return false;
  }
  const uint8_t styleCount = header[12];
  if (styleCount == 0) {
    file_.close();
    return false;
  }

  // 複数スタイル(bold/italic等)には対応せず、最初に見つかった有効なスタイル
  // (通常はregular)だけを使う。
  bool found = false;
  uint32_t intervalCount = 0;
  uint32_t intervalsFileOffset = 0;
  for (uint8_t i = 0; i < styleCount && !found; i++) {
    uint8_t toc[kStyleTocEntrySize];
    if (file_.read(toc, kStyleTocEntrySize) != static_cast<int>(kStyleTocEntrySize)) {
      file_.close();
      return false;
    }

    const uint32_t ivCount = readU32(toc + 4);
    const uint32_t glyphCount = readU32(toc + 8);
    if (ivCount == 0 || glyphCount == 0) continue;

    advanceY_ = toc[12];
    ascender_ = readI16(toc + 13);
    const uint16_t kernLeftEntryCount = readU16(toc + 17);
    const uint16_t kernRightEntryCount = readU16(toc + 19);
    const uint8_t kernLeftClassCount = toc[21];
    const uint8_t kernRightClassCount = toc[22];
    const uint8_t ligaturePairCount = toc[23];
    const uint32_t dataOffset = readU32(toc + 24);

    // ファイル上のセクションレイアウトはcrosspoint-jpのSdCardFont::computeStyleFileOffsets()
    // と同じ計算式。カーニング/合字セクション自体は読まないが、後続のビットマップ
    // セクションの開始位置を正しく求めるためにサイズだけ計算に使う。
    const uint32_t intervalsOffset = dataOffset;
    const uint32_t glyphsOffset = intervalsOffset + ivCount * kIntervalRecordSize;
    const uint32_t kernLeftOffset = glyphsOffset + glyphCount * kGlyphRecordSize;
    const uint32_t kernRightOffset = kernLeftOffset + kernLeftEntryCount * kKernClassEntrySize;
    const uint32_t kernMatrixOffset = kernRightOffset + kernRightEntryCount * kKernClassEntrySize;
    const uint32_t ligatureOffset =
        kernMatrixOffset + static_cast<uint32_t>(kernLeftClassCount) * kernRightClassCount;
    bitmapFileOffset_ = ligatureOffset + ligaturePairCount * kLigaturePairSize;

    glyphsFileOffset_ = glyphsOffset;
    intervalCount = ivCount;
    intervalsFileOffset = intervalsOffset;
    found = true;
  }
  if (!found) {
    file_.close();
    return false;
  }

  // 区間テーブル全件をRAMに読み込む(数百〜数千件、数十KB程度)。
  if (!file_.seek(intervalsFileOffset)) {
    file_.close();
    return false;
  }
  intervals_.reserve(intervalCount);
  for (uint32_t i = 0; i < intervalCount; i++) {
    uint8_t rec[kIntervalRecordSize];
    if (file_.read(rec, kIntervalRecordSize) != static_cast<int>(kIntervalRecordSize)) {
      file_.close();
      intervals_.clear();
      return false;
    }
    Interval iv;
    iv.first = readU32(rec);
    iv.last = readU32(rec + 4);
    iv.offset = readU32(rec + 8);
    intervals_.push_back(iv);
  }

  ready_ = true;
  return true;
}

int32_t CjkFontImpl::findGlyphIndex(uint32_t codepoint) const {
  if (intervals_.empty()) return -1;
  int lo = 0, hi = static_cast<int>(intervals_.size()) - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const Interval& iv = intervals_[mid];
    if (codepoint < iv.first) {
      hi = mid - 1;
    } else if (codepoint > iv.last) {
      lo = mid + 1;
    } else {
      return static_cast<int32_t>(iv.offset + (codepoint - iv.first));
    }
  }
  return -1;
}

const CjkFontImpl::CachedGlyph* CjkFontImpl::fetchGlyph(uint32_t codepoint) const {
  for (const auto& g : cache_) {
    if (g.codepoint == codepoint) return &g;
  }

  const int32_t globalIndex = findGlyphIndex(codepoint);
  if (globalIndex < 0) {
    if (codepoint == '?') return nullptr;
    return fetchGlyph('?');
  }

  uint8_t rec[kGlyphRecordSize];
  const uint32_t fileOff = glyphsFileOffset_ + static_cast<uint32_t>(globalIndex) * kGlyphRecordSize;
  if (!file_.seek(fileOff) || file_.read(rec, kGlyphRecordSize) != static_cast<int>(kGlyphRecordSize)) {
    return nullptr;
  }

  CachedGlyph g;
  g.codepoint = codepoint;
  g.width = rec[0];
  g.height = rec[1];
  g.advanceX = readU16(rec + 2);
  g.left = readI16(rec + 4);
  g.top = readI16(rec + 6);
  const uint16_t dataLength = readU16(rec + 8);
  const uint32_t dataOffset = readU32(rec + 12);

  if (dataLength > 0) {
    g.bitmap.resize(dataLength);
    if (!file_.seek(bitmapFileOffset_ + dataOffset) ||
        file_.read(g.bitmap.data(), dataLength) != static_cast<int>(dataLength)) {
      return nullptr;
    }
  }

  if (cache_.size() >= kCacheCapacity) cache_.erase(cache_.begin());
  cache_.push_back(std::move(g));
  return &cache_.back();
}

int CjkFontImpl::lineHeight() const { return advanceY_ > 0 ? advanceY_ : 1; }

int CjkFontImpl::measureText(const char* utf8Text) const {
  if (!ready_) return 0;

  int32_t cursorFP = 0;
  const char* p = utf8Text;
  uint32_t cp;
  while ((cp = utf8Next(p)) != 0) {
    const CachedGlyph* g = fetchGlyph(cp);
    if (!g) {
      cursorFP += fp4FromPixel(lineHeight() / 2);
      continue;
    }
    cursorFP += g->advanceX;
  }
  return fp4ToPixel(cursorFP);
}

void CjkFontImpl::drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y,
                           const char* utf8Text) const {
  if (!ready_) return;

  const int baselineY = y + ascender_;
  int32_t cursorFP = fp4FromPixel(x);
  const char* p = utf8Text;
  uint32_t cp;
  while ((cp = utf8Next(p)) != 0) {
    const CachedGlyph* g = fetchGlyph(cp);
    if (!g) {
      cursorFP += fp4FromPixel(lineHeight() / 2);
      continue;
    }

    if (g->width > 0 && g->height > 0 && !g->bitmap.empty()) {
      const int glyphX0 = fp4ToPixel(cursorFP) + g->left;
      const int glyphY0 = baselineY - g->top;
      for (int row = 0; row < g->height; row++) {
        for (int col = 0; col < g->width; col++) {
          // ビットマップは行ごとのパディングなしのフラットな2bit/pixelパック
          // (crosspoint-jpのGfxRenderer::renderChar()と同じ添字計算)。
          const uint32_t pixelPos = static_cast<uint32_t>(row) * g->width + static_cast<uint32_t>(col);
          const uint8_t byte = g->bitmap[pixelPos / 4];
          const uint8_t shift = static_cast<uint8_t>((3 - (pixelPos % 4)) * 2);
          const uint8_t raw = (byte >> shift) & 0x3;
          if (raw == 0) continue;  // 0=背景(白)。1以上は白黒2値ではすべて黒として描く。
          FrameBufferOps::setBlackPixel(fb, fbWidth, fbHeight, glyphX0 + col, glyphY0 + row);
        }
      }
    }

    cursorFP += g->advanceX;
  }
}
