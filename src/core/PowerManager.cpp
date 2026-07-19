#include "PowerManager.h"

#include <driver/gpio.h>
#include <esp_sleep.h>

namespace PowerManager {

void releaseGpioHoldOnBoot(int8_t batteryLatchPin) {
  gpio_hold_dis(static_cast<gpio_num_t>(batteryLatchPin));
  gpio_deep_sleep_hold_dis();
}

void enterDeepSleepStandby(int8_t batteryLatchPin, int8_t powerButtonPin) {
  // 念のため直前に再アサートしてからホールドする。
  digitalWrite(batteryLatchPin, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(batteryLatchPin));
  gpio_deep_sleep_hold_en();

  // 電源ボタン(アクティブLOW)のプルアップがディープスリープ中も保持されるかは
  // ESP32-C3のGPIOドメイン設計次第で不確実なため、スリープ直前に明示的に
  // 再設定しておく(プルアップが失われるとフロートしたピンがノイズで誤ウェイク
  // する可能性がある)。
  gpio_pullup_en(static_cast<gpio_num_t>(powerButtonPin));

  // ESP32-C3はESP32(無印)/S3のようなext0/ext1 wakeup APIを持たないため、
  // 汎用のGPIO wakeup APIを使う(チップ差異、実装ミスが起きやすい箇所)。
  esp_deep_sleep_enable_gpio_wakeup(1ULL << powerButtonPin, ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_deep_sleep_start();
}

}  // namespace PowerManager
