#include "MiniFontImpl.h"

#include "MiniFont.h"

int MiniFontImpl::measureText(const char* utf8Text) const {
  return MiniFont::textWidth(utf8Text, scale_);
}

int MiniFontImpl::lineHeight() const {
  return MiniFont::lineHeight(scale_);
}

void MiniFontImpl::drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                            int x, int y, const char* utf8Text) const {
  MiniFont::drawText(fb, fbWidth, fbHeight, x, y, utf8Text, scale_);
}
