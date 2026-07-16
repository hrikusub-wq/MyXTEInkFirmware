#pragma once
#include <Arduino.h>

#include "../gfx/Font.h"

// handleButton()の戻り値。「見た目が変わったか」に加え、画面遷移の意図を伝える。
// 実際の画面切り替え(どのScreenインスタンスに切り替えるか)は、画面数がまだ
// 少ないうちはmain.cpp側で素直にswitchする。画面が増えて管理が煩雑になったら
// 専用のScreenManager(スタック)導入を検討する。
enum class ScreenAction {
  kNone,             // 変化なし
  kRedraw,           // 同じ画面内で見た目が変わった(部分更新でよい)
  kNavigateForward,  // 別の画面を開きたい(例: ホーム→フォルダ)
  kNavigateBack,     // 前の画面に戻りたい(例: フォルダ→ホーム)
};

// 1画面(ホーム、フォルダ、設定など)の共通インターフェース。
//
// 実際のWidgetリストの保持・フォーカス移動ロジックは画面ごとに異なるため、
// 具体的な画面クラス側に委ねる。過度に汎用的な自前フレームワークは作り込まず、
// 「描画する」「ボタン入力を受け取る」という最小限の型だけをここで揃える。
class Screen {
 public:
  virtual ~Screen() = default;

  // 画面全体を描画する。背景クリア(display.clearScreen())は呼び出し側が行う。
  virtual void render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) = 0;

  // ボタン入力を受け取る(フォーカス移動や画面遷移に使う)。
  virtual ScreenAction handleButton(uint8_t buttonIndex) = 0;
};
