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

  // 生のBatteryStatus()レジスタ値(0x0A)。BatteryStatus()のビット定義は
  // ライブラリ未検証(README参照)のため充電判定には使わず、デバッグ用途のみ。
  // 読み取り失敗時はfalseを返す。
  bool readRawBatteryStatus(uint16_t& status);

  // 瞬時電流(mA、符号付き)。読み取り失敗時はINT16_MINを返す。
  int readCurrentMilliamps();

  // 充電中かどうか。瞬時電流がプラス方向にある程度の大きさを持つ場合に
  // 充電中と判定する(kode_BQ27220の作者コメント"positive=charging"に基づく)。
  // 実機のUSB接続時にelectric current=+61mA・BatteryStatus DSGビット=0という
  // 整合する結果が得られたことを根拠にしているが、ポゴピン等USB以外の充電手段が
  // なく非充電時との比較検証はできていない点に注意。
  bool isCharging();

 private:
  BQ27220 gauge_;
  bool initialized_ = false;
};
