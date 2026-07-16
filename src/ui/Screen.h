#pragma once
#include <Arduino.h>

#include "../gfx/Font.h"

// 1画面(ホーム、フォルダ、設定など)の共通インターフェース。
//
// 実際のWidgetリストの保持・フォーカス移動ロジックは画面ごとに異なるため、
// 具体的な画面クラス(DemoSettingsScreen等)側に委ねる。過度に汎用的な自前
// フレームワークは作り込まず、「描画する」「ボタン入力を受け取る」という
// 最小限の型だけをここで揃える。
class Screen {
 public:
  virtual ~Screen() = default;

  // 画面全体を描画する。背景クリア(display.clearScreen())は呼び出し側が行う。
  virtual void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) = 0;

  // ボタン入力を受け取る(フォーカス移動や画面遷移に使う)。
  // 戻り値: 見た目が変わり再描画が必要ならtrue。
  virtual bool handleButton(uint8_t buttonIndex) = 0;
};
