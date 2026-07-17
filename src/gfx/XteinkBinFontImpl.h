#pragma once
#include <Arduino.h>
#include <SDCardManager.h>

#include <vector>

#include "Font.h"

// XTEink Web Font Maker(https://github.com/lakafior/XTEink-Web-Font-Maker)が生成する
// 「1-bit, fixed-grid」の.binフォント形式を読み込むFont実装。純正Xteinkファームウェア
// 向けのカスタムフォント変換ツール(XTEinkToolkit等)が使う形式で、CjkFontImpl(.cpfont)
// より単純: ヘッダが一切なく、Unicodeコードポイント(0x0000〜0xFFFF、BMPのみ)で
// 直接インデックスされた固定サイズグリフの配列がそのままファイルとして並んでいるだけ。
//
// 1グリフ = width×heightピクセルの固定ボックス(等幅フォント)、1bit/pixel、
// MSBファースト、1行 = ceil(width/8)バイトでパッキング(FrameBufferOps/MiniFontと同じ
// 規約)。コードポイントcpのグリフは、ファイル先頭からcp×(ceil(width/8)×height)バイト目
// に直接存在する(区間テーブル探索や圧縮展開が不要)。
//
// ファイル自体に幅・高さの記録が無いため、begin()の引数として明示的に渡す必要がある
// (このツールがダウンロード時に生成する既定のファイル名"font_{width}x{height}.bin"に
// 幅・高さの情報が入っている)。誤った値を渡すと検出できずに読み違えるだけになるため、
// begin()ではファイルサイズが「0x10000文字分ちょうどか」だけを健全性チェックしている。
//
// 等幅(固定グリフ)フォントのため、CJK以外の文字(特に細い文字と太い文字が混在する
// ラテン文字)は字間が不自然になることがある(変換ツール側の"Optical Alignment"
// オプションで軽減はできるが、根本的な字送りは常に固定)。
class XteinkBinFontImpl : public Font {
 public:
  // .binファイルを開く。widthPx/heightPxはファイル名等から人間が読み取って渡す。
  // ファイルサイズが期待値(0x10000 × ceil(widthPx/8) × heightPx バイト)と一致しない
  // 場合は不正なサイズ指定とみなし失敗する。
  bool begin(const char* path, int widthPx, int heightPx);
  bool ready() const { return ready_; }

  // ファイル名(パスでもよい、末尾が大小問わず".bin"であればよい)から、拡張子直前の
  // "{width}x{height}"または"{width}×{height}"パターンを解析してwidthPx/heightPxを
  // 求める。XTEink Web Font Makerの既定命名規則("font_{width}x{height}.bin")や、
  // 手動で末尾に"32×46"のように付けたファイル名を想定している。パターンが見つから
  // ない、あるいは数値化できない場合はfalseを返す(呼び出し側はそのファイルを
  // 一覧から除外するなどの対応を想定)。
  static bool parseDimensions(const String& filename, int& outWidth, int& outHeight);

  int measureText(const char* utf8Text) const override;
  int lineHeight() const override;
  void drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, const char* utf8Text) const override;

 private:
  struct CachedGlyph {
    uint32_t codepoint = 0;
    std::vector<uint8_t> bitmap;
  };

  const std::vector<uint8_t>* fetchGlyph(uint32_t codepoint) const;

  bool ready_ = false;
  mutable FsFile file_;
  int width_ = 0;
  int height_ = 0;
  uint32_t widthBytes_ = 0;
  uint32_t charBytes_ = 0;

  static constexpr size_t kCacheCapacity = 64;
  mutable std::vector<CachedGlyph> cache_;
};
