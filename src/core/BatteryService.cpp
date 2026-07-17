#include "BatteryService.h"

namespace {
// README.mdのGPIO割り当て: 0=I2C SCL, 20=I2C SDA
constexpr int kSdaPin = 20;
constexpr int kSclPin = 0;
}  // namespace

bool BatteryService::begin() {
  initialized_ = gauge_.begin(Wire, BQ27220_I2C_ADDRESS, kSdaPin, kSclPin, 400000U);
  return initialized_;
}

int BatteryService::readPercent() {
  if (!initialized_) return -1;
  return gauge_.readStateOfChargePercent();
}

int BatteryService::readMillivolts() {
  if (!initialized_) return -1;
  return gauge_.readVoltageMillivolts();
}

bool BatteryService::readRawBatteryStatus(uint16_t& status) {
  if (!initialized_) return false;
  return gauge_.readBatteryStatus(status);
}

int BatteryService::readCurrentMilliamps() {
  if (!initialized_) return INT16_MIN;
  return gauge_.readCurrentMilliamps();
}

bool BatteryService::isCharging() {
  constexpr int kChargingThresholdMa = 5;  // ノイズ・オフセット誤差を避けるマージン
  const int current = readCurrentMilliamps();
  if (current == INT16_MIN) return false;
  return current > kChargingThresholdMa;
}
