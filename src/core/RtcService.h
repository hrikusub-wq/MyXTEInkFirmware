#pragma once
#include <Arduino.h>
#include <Wire.h>

// DS3231が保持する日時。年はBCDの下2桁ではなく西暦フル値で表す
// (centuryビットは無視し、常に2000年代として扱う。2099年問題は許容)。
struct RtcDateTime {
  uint16_t year = 2000;
  uint8_t month = 1;   // 1-12
  uint8_t day = 1;      // 1-31
  uint8_t hour = 0;      // 0-23
  uint8_t minute = 0;
  uint8_t second = 0;
};

// X3実機のRTC(DS3231、I2C 0x68)を読み書きするラッパー。
// crosspoint-reader(https://github.com/crosspoint-reader/crosspoint-reader)の
// lib/hal/HalClock.cppを参考に、外部ライブラリを使わずWireで直接レジスタを
// 読み書きする(libdriver/ds3231のような汎用プラットフォーム抽象化層は、
// 時刻の読み書きだけが目的のこの用途には過剰なため採用しなかった)。
class RtcService {
 public:
  // I2Cを初期化し、DS3231と通信できるか確認する。
  bool begin();

  bool ready() const { return available_; }

  // 現在の日時を取得する。取得失敗時(未検出・通信エラー)はfalseを返す。
  bool readDateTime(RtcDateTime& out) const;

  // 日時を書き込む。値の範囲は呼び出し側で保証すること(このメソッドは検証しない)。
  bool writeDateTime(const RtcDateTime& dt);

  // Oscillator Stop Flag(ステータスレジスタ0x0Fのbit7)を確認する。
  // trueの場合、バックアップ電源(コイン電池等)が一度でも切れて発振が停止した
  // ことを示し、現在保持されている時刻は信頼できない(要再設定)。
  bool lostPower() const;

 private:
  bool available_ = false;
};

// dtにhours(負も可)を加算した日時を返す(日/月/年の繰り上がり・繰り下がりを
// 考慮する、うるう年対応)。秒は変更しない。タイムゾーンオフセットの適用など、
// RTCの生値とローカル表示時刻を相互変換する用途に使う。
RtcDateTime addHoursToDateTime(const RtcDateTime& dt, int hours);
