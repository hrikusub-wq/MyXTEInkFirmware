#include "XteinkBinFontImpl.h"

#include <utility>

#include "FrameBufferOps.h"

namespace {

// UTF-8の次の1コードポイントを取り出し、pを次の文字の先頭へ進める(CjkFontImpl.cppの
// utf8Next()と同じ実装、それぞれのフォント実装が小さく自己完結するようこのまま重複させている)。
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

bool XteinkBinFontImpl::begin(const char* path, int widthPx, int heightPx) {
  ready_ = false;
  cache_.clear();
  if (file_) file_.close();
  if (widthPx <= 0 || heightPx <= 0) return false;

  file_ = SdMan.open(path, O_RDONLY);
  if (!file_) return false;

  width_ = widthPx;
  height_ = heightPx;
  widthBytes_ = (static_cast<uint32_t>(width_) + 7) / 8;
  charBytes_ = widthBytes_ * static_cast<uint32_t>(height_);

  // このフォーマットにはヘッダが無くサイズの記録も無いため、widthPx/heightPxの
  // 指定を誤っていても静かに読み違えるだけになってしまう。せめてファイル全体の
  // サイズが「0x10000文字分ちょうど」であることだけは検証する。
  const uint64_t expectedSize = static_cast<uint64_t>(charBytes_) * 0x10000ULL;
  if (static_cast<uint64_t>(file_.size()) != expectedSize) {
    file_.close();
    return false;
  }

  ready_ = true;
  return true;
}

bool XteinkBinFontImpl::parseDimensions(const String& filename, int& outWidth, int& outHeight) {
  String lower = filename;
  lower.toLowerCase();
  if (!lower.endsWith(".bin")) return false;

  int i = static_cast<int>(filename.length()) - 4;  // ".bin"の直前

  const int heightEnd = i;
  while (i > 0 && isDigit(filename[i - 1])) i--;
  const int heightStart = i;
  if (heightStart == heightEnd) return false;

  int sepStart;
  if (i > 0 && (filename[i - 1] == 'x' || filename[i - 1] == 'X')) {
    sepStart = i - 1;
  } else if (i > 1 && static_cast<uint8_t>(filename[i - 2]) == 0xC3 &&
             static_cast<uint8_t>(filename[i - 1]) == 0x97) {
    // "×"(U+00D7)のUTF-8表現は2バイト(0xC3 0x97)
    sepStart = i - 2;
  } else {
    return false;
  }

  i = sepStart;
  const int widthEnd = i;
  while (i > 0 && isDigit(filename[i - 1])) i--;
  const int widthStart = i;
  if (widthStart == widthEnd) return false;

  outHeight = filename.substring(heightStart, heightEnd).toInt();
  outWidth = filename.substring(widthStart, widthEnd).toInt();
  return outWidth > 0 && outHeight > 0;
}

const std::vector<uint8_t>* XteinkBinFontImpl::fetchGlyph(uint32_t codepoint) const {
  if (codepoint > 0xFFFF) codepoint = '?';  // BMP外の文字は代替文字にフォールバック

  for (const auto& g : cache_) {
    if (g.codepoint == codepoint) return &g.bitmap;
  }

  std::vector<uint8_t> bitmap(charBytes_);
  const uint32_t offset = codepoint * charBytes_;
  if (!file_.seek(offset) || file_.read(bitmap.data(), charBytes_) != static_cast<int>(charBytes_)) {
    return nullptr;
  }

  if (cache_.size() >= kCacheCapacity) cache_.erase(cache_.begin());
  cache_.push_back(CachedGlyph{codepoint, std::move(bitmap)});
  return &cache_.back().bitmap;
}

int XteinkBinFontImpl::lineHeight() const { return height_ > 0 ? height_ : 1; }

int XteinkBinFontImpl::measureText(const char* utf8Text) const {
  if (!ready_) return 0;
  int count = 0;
  const char* p = utf8Text;
  while (utf8Next(p) != 0) count++;
  return count * width_;
}

void XteinkBinFontImpl::drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, int x, int y,
                                 const char* utf8Text) const {
  if (!ready_) return;

  int cursorX = x;
  const char* p = utf8Text;
  uint32_t cp;
  while ((cp = utf8Next(p)) != 0) {
    const std::vector<uint8_t>* bitmap = fetchGlyph(cp);
    if (bitmap) {
      for (int row = 0; row < height_; row++) {
        for (int col = 0; col < width_; col++) {
          const uint32_t byteIdx = static_cast<uint32_t>(row) * widthBytes_ + static_cast<uint32_t>(col >> 3);
          const uint8_t byte = (*bitmap)[byteIdx];
          const uint8_t bit = (byte >> (7 - (col & 7))) & 1;
          if (bit) FrameBufferOps::setBlackPixel(fb, fbWidth, fbHeight, cursorX + col, y + row);
        }
      }
    }
    cursorX += width_;
  }
}
