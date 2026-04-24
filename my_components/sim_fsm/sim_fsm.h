// HuiFlow 洗車機模擬板 — sim_fsm
// Phase 1 骨架：LED GPIO 控制 + UART2 RX byte 原始 log（不 parse frame）
// Phase 2 擴充：scenario 腳本、UART2 frame parser、故障注入器
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cstdint>
#include <functional>
#include <string>

namespace esphome {
namespace sim_fsm {

enum SimColor : uint8_t {
  SC_OFF = 0,
  SC_RED = 1,
  SC_GREEN = 2,
  SC_BLUE = 3,
};

// UART2 byte 原始 log callback（交給 WS server / dashboard 顯示）
using RxByteLogCb = std::function<void(uint8_t byte_val)>;

class SimFsmComponent : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_led_pins(int blue, int green, int red) {
    pin_blue_ = blue;
    pin_green_ = green;
    pin_red_ = red;
  }

  void set_uart_rx_pin(int rx) { pin_uart_rx_ = rx; }

  // 外部訂閱 UART2 RX byte 事件（WS server 用來廣播、Dashboard 用來顯示）
  void set_rx_byte_log_callback(RxByteLogCb cb) { rx_cb_ = std::move(cb); }

  void setup() override;
  void loop() override;

  // Phase 1 公開 API：手動切 LED（Dashboard 3 個按鈕呼叫）
  void set_led(SimColor c);
  SimColor get_led() const { return current_color_; }

  // 回傳當前 LED 顏色字串（給 Dashboard 顯示 / sensor）
  const char *led_name() const;

 protected:
  int pin_blue_ = -1;
  int pin_green_ = -1;
  int pin_red_ = -1;
  int pin_uart_rx_ = -1;

  SimColor current_color_ = SC_OFF;
  RxByteLogCb rx_cb_ = nullptr;

  // UART2 RX 讀取節流
  uint32_t last_rx_poll_ms_ = 0;

  void apply_led_outputs_();
  void poll_uart_rx_();
};

}  // namespace sim_fsm
}  // namespace esphome
