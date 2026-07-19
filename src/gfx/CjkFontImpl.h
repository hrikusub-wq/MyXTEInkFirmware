#pragma once
#include <Arduino.h>
#include <SDCardManager.h>

#include <vector>

#include "Font.h"

// crosspoint-jp(https://github.com/zrn-ns/crosspoint-jp)のSDカードフォント形式
// (.cpfont)を読み、FontインターフェースとしてCJK(日本語)テキストを描画する実装。
//
// crosspoint-jpのEpdFont/SdCardFont/GfxRendererを土台にした移植だが、以下は
// 意図的に持ち込んでいない(README参照):
//   - カーニング・合字・縦書きグリフ置換 (.cpfontのファイル形式には含まれるが、
//     このリーダーは横書き専用でパース時にセクションごとスキップする)
//   - 複数スタイル(bold/italic) — 最初に見つかったスタイル1つだけを使う
//   - グレースケール/ダークモード — FrameBufferOpsは1bppのみなので、
//     2bit/pixelのグリフ値は「0=白、1以上=黒」でしきい値化する
//   - DEFLATE圧縮(FontDecompressor/uzlib) — SDカードフォントは非圧縮の生
//     ビットマップとして格納されているため、そもそも不要
//
// メモリ上には「文字コード区間テーブル」(全件、数十KB程度)のみ保持し、実際の
// グリフメトリクス・ビットマップはコードポイントごとにSDから都度シーク読みする。
// 直近使用した最大200グリフ分だけを小さなキャッシュに保持し、同一ページ内での
// 再読み込みを減らす(crosspoint-jp本家のprewarm+ページキャッシュ機構ほど
// 高度なことはしていない)。
class CjkFontImpl : public Font {
 public:
  // .cpfontファイルを開き、区間テーブルをRAMに読み込む。失敗時はfalseを返し、
  // その場合drawText/measureTextは何も描画せず幅0を返す(呼び出し側でのフォールバック用)。
  bool begin(const char* path);
  bool ready() const { return ready_; }

  int measureText(const char* utf8Text) const override;
  int lineHeight() const override;
  void drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int x, int y, const char* utf8Text) const override;
  // cache_を空にし、std::vectorの内部バッファ容量もshrink_to_fit()でヒープへ
  // 返す(clear()だけでは容量が保持されたままになる、Font.hのコメント参照)。
  // 読書中のファイル切り替え時にTxtReaderService::close()経由で呼ばれる。
  void clearCache() const override;

 private:
  // .cpfontのグリフレコード(ファイル上16バイト、区間テーブルの1件と対応)。
  // ファイルからのオンデマンド読み込み結果をキャッシュする単位。
  struct CachedGlyph {
    uint32_t codepoint = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    uint16_t advanceX = 0;  // 12.4固定小数点(1/16px単位)
    int16_t left = 0;       // カーソル位置からグリフ左上までのXオフセット
    int16_t top = 0;        // ベースラインからグリフ左上までのYオフセット
    std::vector<uint8_t> bitmap;  // 2bit/pixel、フラットパック(行ごとのパディングなし)
  };

  // .cpfontの文字コード区間テーブル(ファイル上12バイト)。
  struct Interval {
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t offset = 0;  // グリフ配列内の先頭インデックス
  };

  int32_t findGlyphIndex(uint32_t codepoint) const;
  // グリフをキャッシュから探す。なければSDから読み込みキャッシュに追加する。
  // 見つからない場合は代替文字'?'で再試行し、それも失敗したらnullptrを返す。
  // constだがfile_/cache_(mutable)を通じて内部状態を更新する
  // (Fontインターフェースのmeasure/drawがconstであるための実用上の妥協。
  // GfxRendererの同種のコメントを参考にした)。
  const CachedGlyph* fetchGlyph(uint32_t codepoint) const;

  // ASCII(半角英数字・記号、0x20-0x7E)の字送り幅(12.4固定小数点)を計算する。
  // .cpfontのadvanceXはCJK(全角)前提で作られているものが多く、そのまま使うと
  // 半角のはずの英数字が全角相当の幅に間延びして表示される(実機で確認済み)。
  // ビットマップ自体(g.width/left/top)は変更せず見た目のグリフ形状はそのまま、
  // カーソルの送り幅だけをグリフの実際のインク幅基準に計算し直す。
  // measureText/drawTextの両方から呼び、折り返し計算と実描画の幅がずれないようにする。
  int32_t asciiAdvanceFp(uint32_t codepoint, const CachedGlyph& g) const;

  bool ready_ = false;
  mutable FsFile file_;

  std::vector<Interval> intervals_;
  uint32_t glyphsFileOffset_ = 0;
  uint32_t bitmapFileOffset_ = 0;
  int ascender_ = 0;
  int advanceY_ = 1;

  static constexpr size_t kCacheCapacity = 200;
  mutable std::vector<CachedGlyph> cache_;
  // ヒープ逼迫でグリフ確保に失敗した旨のログは、文字ごとに何度も出すと(特に
  // 大量の文字を含むファイルで)Serial出力自体が重く体感の「フリーズ」を
  // 悪化させるため、1回だけ出す(fetchGlyph()参照)。
  mutable bool loggedHeapExhausted_ = false;
};
