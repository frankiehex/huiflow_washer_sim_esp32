// HuiFlow 洗車機模擬板 — sim_fsm 實作
// Phase 1：手動 LED 切換（對應主板 GPIO21/47/35, INPUT_PULLUP + inverted），
//          UART2 RX 原始 byte 讀取（主板 GPIO1 TX @ 9600 8N1）
#include "sim_fsm.h"

#include "driver/gpio.h"
#include "driver/uart.h"

namespace esphome {
namespace sim_fsm {

static const char *const TAG = "sim_fsm";

// 主板那側極性：INPUT_PULLUP + inverted:true → GPIO 物理 LOW 讀作 ON
// 所以模擬板要 LED ON → 驅動 GPIO LOW，要 OFF → 驅動 GPIO HIGH
static inline int led_on_level() { return 0; }   // LOW
static inline int led_off_level() { return 1; }  // HIGH

void SimFsmComponent::setup() {
  ESP_LOGI(TAG, "sim_fsm setup: blue=GPIO%d green=GPIO%d red=GPIO%d uart_rx=GPIO%d",
           pin_blue_, pin_green_, pin_red_, pin_uart_rx_);

  // 3 顆 LED 輸出：push-pull output
  gpio_config_t led_cfg = {};
  led_cfg.mode = GPIO_MODE_OUTPUT;
  led_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  led_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  led_cfg.intr_type = GPIO_INTR_DISABLE;
  led_cfg.pin_bit_mask =
      (1ULL << pin_blue_) | (1ULL << pin_green_) | (1ULL << pin_red_);
  gpio_config(&led_cfg);

  // Boot 安全預設：全滅（HIGH）
  gpio_set_level((gpio_num_t) pin_blue_, led_off_level());
  gpio_set_level((gpio_num_t) pin_green_, led_off_level());
  gpio_set_level((gpio_num_t) pin_red_, led_off_level());
  current_color_ = SC_OFF;

  // UART2 RX-only：監聽主板 GPIO1 (TX) → 模擬板 pin_uart_rx_ (RX)
  // 9600 8N1，不啟用 TX（TX pin 設 UART_PIN_NO_CHANGE 表示不佔用）
  uart_config_t uart_cfg = {};
  uart_cfg.baud_rate = 9600;
  uart_cfg.data_bits = UART_DATA_8_BITS;
  uart_cfg.parity = UART_PARITY_DISABLE;
  uart_cfg.stop_bits = UART_STOP_BITS_1;
  uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_cfg.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(UART_NUM_2, 256, 0, 0, nullptr, 0);
  uart_param_config(UART_NUM_2, &uart_cfg);
  uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, (gpio_num_t) pin_uart_rx_,
               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  ESP_LOGI(TAG, "sim_fsm setup done, UART2 RX listening on GPIO%d @9600", pin_uart_rx_);
}

void SimFsmComponent::loop() {
  const uint32_t now = millis();
  // 10ms poll UART2 RX buffer（足夠收 9600 bps @ 7-byte frame）
  if (now - last_rx_poll_ms_ >= 10) {
    last_rx_poll_ms_ = now;
    poll_uart_rx_();
  }
}

void SimFsmComponent::poll_uart_rx_() {
  uint8_t buf[64];
  int n = uart_read_bytes(UART_NUM_2, buf, sizeof(buf), 0);
  if (n <= 0) return;
  for (int i = 0; i < n; i++) {
    ESP_LOGD(TAG, "UART2 RX byte: 0x%02X", buf[i]);
    if (rx_cb_) rx_cb_(buf[i]);
  }
}

void SimFsmComponent::set_led(SimColor c) {
  if (c == current_color_) return;
  current_color_ = c;
  apply_led_outputs_();
  ESP_LOGI(TAG, "LED set to %s", led_name());
}

void SimFsmComponent::apply_led_outputs_() {
  // 一次只亮一顆（模擬洗車機 3 色燈互斥）
  gpio_set_level((gpio_num_t) pin_blue_,
                 current_color_ == SC_BLUE ? led_on_level() : led_off_level());
  gpio_set_level((gpio_num_t) pin_green_,
                 current_color_ == SC_GREEN ? led_on_level() : led_off_level());
  gpio_set_level((gpio_num_t) pin_red_,
                 current_color_ == SC_RED ? led_on_level() : led_off_level());
}

const char *SimFsmComponent::led_name() const {
  switch (current_color_) {
    case SC_OFF:
      return "off";
    case SC_RED:
      return "red";
    case SC_GREEN:
      return "green";
    case SC_BLUE:
      return "blue";
  }
  return "unknown";
}

}  // namespace sim_fsm
}  // namespace esphome
