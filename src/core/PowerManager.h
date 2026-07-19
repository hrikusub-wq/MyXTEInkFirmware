#pragma once
#include <Arduino.h>

// ディープスリープへの移行・GPIO13(バッテリー電源ラッチ)のホールド処理を担当する。
// main.cppの肥大化を避けるため、GPIO操作・wake設定をここに分離する。
//
// 背景(README「注意点」のGPIO13事故を参照): GPIO13はバッテリーからの給電を
// 自己保持する電源ラッチMOSFETのゲート制御ピンで、過去にopen-drainでの駆動が
// 原因でバッテリー単独駆動時に電源が落ちて実機がフリーズする不具合が起きている。
// ディープスリープはチップが自らGPIOのpush-pull出力状態を解除してしまうため、
// 何もしなければ同種の問題が再発する。ESP32-C3の`gpio_hold_en()`/
// `gpio_deep_sleep_hold_en()`でこのピンの出力状態(HIGH)をスリープ中も
// 明示的に固定し、復帰後は必ず`gpio_hold_dis()`で解除してから通常の
// GPIO初期化を行う必要がある。
//
// 電気的な補足: このピンをHIGHに保つ以上、バッテリーからボード全体
// (SDカード・E-inkパネル・DS3231・BQ27220含む)への給電ラインは切れない。
// ESP32コア自体は数µA〜数十µAまで下がるが、これは「真の全断」ではなく
// 「ESP32コア+E-inkパネルを低電力状態にしつつ、電源ラッチとRTCドメインは
// 給電を維持する」設計であることに留意すること。
namespace PowerManager {

// setup()の最初、GPIO13を(OUTPUT+HIGH)に再設定した直後に呼ぶこと。
// ESP-IDFのgpio_hold_dis()のドキュメント上、「ホールドされていたピンを
// 解除する前に、まずそのピンをHIGHへ再設定しておく」順序が必須なため
// (先に解除するとホールドが外れた瞬間に電圧が不定になりうる)。
// ホールドされていなければ無害なno-opなので、通常起動時も含め常に無条件で
// 呼んでよい。
void releaseGpioHoldOnBoot(int8_t batteryLatchPin);

// 待機画面がCONFIRMされた時点で呼ぶ。GPIO13をHIGHで確実にホールドしたまま、
// 電源ボタン(アクティブLOW)での復帰を有効にしてディープスリープに入る。
// この関数は戻らない(esp_deep_sleep_start()が内部で呼ばれ、復帰時はsetup()
// から再実行される=通常のリブートと同じ経路を辿る。直前の画面状態の復元は
// 行わず、常にホーム画面から再開する)。
void enterDeepSleepStandby(int8_t batteryLatchPin, int8_t powerButtonPin);

}  // namespace PowerManager
