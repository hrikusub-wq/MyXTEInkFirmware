#pragma once
#include <Arduino.h>

// テキスト描画の抽象インターフェース。
//
// 現時点ではMiniFont(ASCII専用・固定5x7ドット)しか実装がないが、フェーズ5で
// SDカードから読み込む可変幅CJKフォントに差し替える予定のため、UIコンポーネント
// 側はこのインターフェースだけに依存し、フォントの実装詳細(固定幅か可変幅か、
// グリフのサイズ)を一切知らない設計にする。
//
// 文字列は常にUTF-8として扱う。「1バイト=1文字」という決め打ちはせず、
// 実装側(MiniFont等)がバイト列からコードポイントを1つずつ取り出して処理する。
class Font {
 public:
  virtual ~Font() = default;

  // utf8Textを描画した場合のピクセル幅を返す(中央寄せ・右寄せのレイアウト計算用)。
  virtual int measureText(const char* utf8Text) const = 0;

  // 1行の高さ(次の行へ送るときのアドバンス量、px)を返す。
  // SettingRowなど行ベースのレイアウトはこれを使って行の高さ・間隔を決める。
  virtual int lineHeight() const = 0;

  // utf8Textを(x, y)を左上として描画する。
  virtual void drawText(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                        int x, int y, const char* utf8Text) const = 0;
};
