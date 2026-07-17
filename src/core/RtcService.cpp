#include "RtcService.h"

namespace {
// README.mdのGPIO割り当て: 0=I2C SCL, 20=I2C SDA (BatteryServiceと共有のI2Cバス)
constexpr int kSdaPin = 20;
constexpr int kSclPin = 0;
constexpr uint32_t kI2cFrequency = 400000U;

constexpr uint8_t kDs3231Address = 0x68;
constexpr uint8_t kRegSeconds = 0x00;  // 0x00-0x06: sec,min,hour,dow,date,month,year
constexpr uint8_t kRegStatus = 0x0F;   // bit7 = Oscillator Stop Flag

uint8_t bcdToDec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
uint8_t decToBcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

// Tomohiko Sakamotoのアルゴリズムで曜日(1=日曜〜7=土曜)を求める。
// DS3231のDOWレジスタ(0x03)はアラーム機能でのみ参照され、時刻の読み書き自体には
// 影響しないが、他ツールでチップを直接読んだときのために整合した値を書いておく。
uint8_t dayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  uint16_t y = year;
  if (month < 3) y -= 1;
  return static_cast<uint8_t>((y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7) + 1;
}
}  // namespace

bool RtcService::begin() {
  Wire.begin(kSdaPin, kSclPin, kI2cFrequency);

  // 疎通確認としてステータスレジスタを1バイト読む。
  Wire.beginTransmission(kDs3231Address);
  Wire.write(kRegStatus);
  if (Wire.endTransmission(false) != 0) {
    available_ = false;
    return false;
  }
  if (Wire.requestFrom(kDs3231Address, (uint8_t)1) < 1 || Wire.available() < 1) {
    available_ = false;
    return false;
  }
  Wire.read();

  available_ = true;
  return true;
}

bool RtcService::readDateTime(RtcDateTime& out) const {
  if (!available_) return false;

  Wire.beginTransmission(kDs3231Address);
  Wire.write(kRegSeconds);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kDs3231Address, (uint8_t)7) < 7) return false;

  const uint8_t rawSec = Wire.read();
  const uint8_t rawMin = Wire.read();
  const uint8_t rawHour = Wire.read();
  Wire.read();  // 曜日レジスタ(0x03) — 未使用
  const uint8_t rawDate = Wire.read();
  const uint8_t rawMonth = Wire.read();
  const uint8_t rawYear = Wire.read();

  if (Wire.available() > 0) return false;

  out.second = bcdToDec(rawSec & 0x7F);
  out.minute = bcdToDec(rawMin & 0x7F);
  if (rawHour & 0x40) {
    // 12時間モード: bit5=PM, bits4-0=時(1-12)
    uint8_t h12 = bcdToDec(rawHour & 0x1F);
    const bool pm = rawHour & 0x20;
    if (h12 == 12) h12 = 0;
    out.hour = pm ? (h12 + 12) : h12;
  } else {
    out.hour = bcdToDec(rawHour & 0x3F);
  }
  out.day = bcdToDec(rawDate & 0x3F);
  out.month = bcdToDec(rawMonth & 0x1F);
  out.year = 2000 + bcdToDec(rawYear);

  return true;
}

bool RtcService::writeDateTime(const RtcDateTime& dt) {
  if (!available_) return false;

  const uint16_t yearIn2000s = (dt.year >= 2000) ? (dt.year - 2000) : dt.year;
  const uint8_t dow = dayOfWeek(dt.year, dt.month, dt.day);

  Wire.beginTransmission(kDs3231Address);
  Wire.write(kRegSeconds);
  Wire.write(decToBcd(dt.second));
  Wire.write(decToBcd(dt.minute));
  Wire.write(decToBcd(dt.hour));  // bit6=0固定 → 常に24時間モードで書き込む
  Wire.write(decToBcd(dow));
  Wire.write(decToBcd(dt.day));
  Wire.write(decToBcd(dt.month));  // bit7(century)=0固定
  Wire.write(decToBcd(static_cast<uint8_t>(yearIn2000s)));
  if (Wire.endTransmission() != 0) return false;

  // 時刻を明示的に設定したのでOSFをクリアする(以後の電源断だけを検知対象にする)。
  Wire.beginTransmission(kDs3231Address);
  Wire.write(kRegStatus);
  Wire.write((uint8_t)0x00);
  return Wire.endTransmission() == 0;
}

bool RtcService::lostPower() const {
  if (!available_) return true;

  Wire.beginTransmission(kDs3231Address);
  Wire.write(kRegStatus);
  if (Wire.endTransmission(false) != 0) return true;
  if (Wire.requestFrom(kDs3231Address, (uint8_t)1) < 1 || Wire.available() < 1) return true;

  return (Wire.read() & 0x80) != 0;
}

namespace {
int daysInMonth(int year, int month) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (month == 2 && isLeap) return 29;
  return kDays[month - 1];
}
}  // namespace

RtcDateTime addHoursToDateTime(const RtcDateTime& dt, int hours) {
  RtcDateTime result = dt;

  int totalHour = static_cast<int>(dt.hour) + hours;
  int dayDelta = 0;
  while (totalHour < 0) {
    totalHour += 24;
    dayDelta--;
  }
  while (totalHour >= 24) {
    totalHour -= 24;
    dayDelta++;
  }
  result.hour = static_cast<uint8_t>(totalHour);

  if (dayDelta != 0) {
    int day = static_cast<int>(result.day) + dayDelta;
    int month = static_cast<int>(result.month);
    int year = static_cast<int>(result.year);

    while (day < 1) {
      month--;
      if (month < 1) {
        month = 12;
        year--;
      }
      day += daysInMonth(year, month);
    }
    while (day > daysInMonth(year, month)) {
      day -= daysInMonth(year, month);
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    }

    result.day = static_cast<uint8_t>(day);
    result.month = static_cast<uint8_t>(month);
    result.year = static_cast<uint16_t>(year);
  }

  return result;
}
