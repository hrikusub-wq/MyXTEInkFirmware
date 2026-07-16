#pragma once
#include <Arduino.h>
#include <BQ27220.h>

// X3実機のバッテリー(BQ27220フューエルゲージ、I2C 0x55)を読み取るラッパー。
// kode_BQ27220(https://github.com/kodediy/kode_BQ27220, Apache-2.0)を使用。
//
// 注意: このライブラリのレジスタマップは、作者コメントによれば類似のTI fuel
// gauge(bq27441等)からの推定値であり、BQ27220向けに完全な検証はされていない。
// 標準コマンド(電圧・残量・ステータス等)のオフセットはTI fuel gauge間で
// 共通していることが多いため動作する可能性は高いが、実機での値の妥当性確認
// (電圧が3.0〜4.3V程度に収まるか等)を経てから信頼すること。
class BatteryService {
 public:
  // I2Cを初期化し、BQ27220と通信できるか確認する。
  bool begin();

  bool ready() const { return initialized_; }

  // 残量(0-100)。読み取り失敗時は-1を返す。
  int readPercent();

  // 電圧(mV)。読み取り失敗時は-1を返す。
  int readMillivolts();

  // 生のBatteryStatus()レジスタ値(0x0A)。充電中判定のビット位置は未検証のため、
  // 現時点ではデバッグ・検証用途のみ。読み取り失敗時はfalseを返す。
  bool readRawBatteryStatus(uint16_t& status);

 private:
  BQ27220 gauge_;
  bool initialized_ = false;
};
