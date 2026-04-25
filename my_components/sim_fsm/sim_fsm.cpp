// HuiFlow 洗車機模擬板 — sim_fsm 實作（Phase 2）
// LED 驅動 + UART2 frame parser（7-byte WASHER_TABLE）+ scenario 引擎
#include "sim_fsm.h"
#include "scenarios.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include <cstdio>

namespace esphome {
namespace sim_fsm {

static const char *const TAG = "sim_fsm";

// 主板那側極性：INPUT_PULLUP + inverted:true → 物理 LOW 讀作 ON
static inline int led_on_level() { return 0; }
static inline int led_off_level() { return 1; }

// 主板 WASHER_TABLE (ws_client.cpp:77-88) 第 5 個 byte 的對照
// 7-byte frame: FD 03 C7 50 <CMD> <CHK> DF
static WasherCmd cmd_byte_to_enum(uint8_t cmd_byte) {
  switch (cmd_byte) {
    case 0x82: return WC_STARTWASHER1;
    case 0x88: return WC_STARTWASHER2;
    case 0x81: return WC_STARTWASHER3;
    case 0x84: return WC_STARTWASHER4;
    case 0x8C: return WC_STARTWASHER5;
    case 0x83: return WC_STARTWASHER6;
    case 0x85: return WC_GOWASHER;
    case 0x8A: return WC_RESETWASHER;
    case 0x89: return WC_HARDRESETWASHER;
    case 0x90: return WC_SPAREWASHER;
    default:   return WC_NONE;
  }
}

const char *washer_cmd_name(WasherCmd cmd) {
  switch (cmd) {
    case WC_STARTWASHER1: return "startwasher1";
    case WC_STARTWASHER2: return "startwasher2";
    case WC_STARTWASHER3: return "startwasher3";
    case WC_STARTWASHER4: return "startwasher4";
    case WC_STARTWASHER5: return "startwasher5";
    case WC_STARTWASHER6: return "startwasher6";
    case WC_GOWASHER:     return "gowasher";
    case WC_RESETWASHER:  return "resetwasher";
    case WC_HARDRESETWASHER: return "hardresetwasher";
    case WC_SPAREWASHER:  return "sparewasher";
    default: return "none";
  }
}

// ─── Scenario 腳本定義（實體化 extern） ────────────────────────────
// 每步 wait_gowasher=true → 停在 BLUE 等主板送 UART2 gowasher 才推進
// dwell_ms 是 GREEN 相位停留時間（BLUE 相位靠 wait_gowasher 條件轉換）
static const ScenarioStep SW5_STEPS[] = {
    {SC_GREEN, 500, false, "Standby"},
    {SC_BLUE,  0,   true,  "Standby->Blue"},
    {SC_GREEN, 500, false, "Pre-Wash"},
    {SC_BLUE,  0,   true,  "Pre-Wash->Blue"},
    {SC_GREEN, 500, false, "Foam"},
    {SC_BLUE,  0,   true,  "Foam->Blue"},
    {SC_GREEN, 500, false, "AIScanning"},
    {SC_BLUE,  0,   true,  "AIScanning->Blue"},
    {SC_GREEN, 500, false, "Final_Rinse"},
    {SC_BLUE,  0,   true,  "Final_Rinse->Blue"},
    {SC_GREEN, 500, false, "Complete"},
    {SC_OFF,   1000, false, "Off"},
};
const Scenario SCENARIO_SW5 = {"SW5", sizeof(SW5_STEPS) / sizeof(SW5_STEPS[0]), SW5_STEPS};

static const ScenarioStep SW6_STEPS[] = {
    {SC_GREEN, 500, false, "Standby"},
    {SC_BLUE,  0,   true,  "Standby->Blue"},
    {SC_GREEN, 500, false, "Pre-Soak"},
    {SC_BLUE,  0,   true,  "Pre-Soak->Blue"},
    {SC_GREEN, 500, false, "Pre-Wash"},
    {SC_BLUE,  0,   true,  "Pre-Wash->Blue"},
    {SC_GREEN, 500, false, "Foam"},
    {SC_BLUE,  0,   true,  "Foam->Blue"},
    {SC_GREEN, 500, false, "Rinse"},
    {SC_BLUE,  0,   true,  "Rinse->Blue"},
    {SC_GREEN, 500, false, "AIScanning"},
    {SC_BLUE,  0,   true,  "AIScanning->Blue"},
    {SC_GREEN, 500, false, "Final_Rinse"},
    {SC_BLUE,  0,   true,  "Final_Rinse->Blue"},
    {SC_GREEN, 500, false, "Coating"},
    {SC_BLUE,  0,   true,  "Coating->Blue"},
    {SC_GREEN, 500, false, "Complete"},
    {SC_OFF,   1000, false, "Off"},
};
const Scenario SCENARIO_SW6 = {"SW6", sizeof(SW6_STEPS) / sizeof(SW6_STEPS[0]), SW6_STEPS};

// ─── setup ─────────────────────────────────────────────────────────
void SimFsmComponent::setup() {
  ESP_LOGI(TAG, "sim_fsm setup: blue=GPIO%d green=GPIO%d red=GPIO%d uart_rx=GPIO%d",
           pin_blue_, pin_green_, pin_red_, pin_uart_rx_);

  gpio_config_t led_cfg = {};
  led_cfg.mode = GPIO_MODE_OUTPUT;
  led_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  led_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  led_cfg.intr_type = GPIO_INTR_DISABLE;
  led_cfg.pin_bit_mask =
      (1ULL << pin_blue_) | (1ULL << pin_green_) | (1ULL << pin_red_);
  gpio_config(&led_cfg);

  gpio_set_level((gpio_num_t) pin_blue_, led_off_level());
  gpio_set_level((gpio_num_t) pin_green_, led_off_level());
  gpio_set_level((gpio_num_t) pin_red_, led_off_level());
  current_color_ = SC_OFF;

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

  ESP_LOGI(TAG, "sim_fsm setup done, UART2 RX on GPIO%d @9600", pin_uart_rx_);
}

void SimFsmComponent::loop() {
  const uint32_t now = millis();
  // UART2 RX 每 10ms 讀一次（足夠 9600 bps @ 7-byte frame）
  if (now - last_rx_poll_ms_ >= 10) {
    last_rx_poll_ms_ = now;
    poll_uart_rx_();
  }
  // Scenario 引擎推進
  if (scenario_ != nullptr) {
    tick_scenario_();
  }
}

// ─── LED 控制 ──────────────────────────────────────────────────────
void SimFsmComponent::set_led(SimColor c) {
  if (c == current_color_) return;
  current_color_ = c;
  apply_led_outputs_();
  ESP_LOGI(TAG, "LED -> %s", led_name());
  emit_state_("led_change", std::string(led_name()));
}

void SimFsmComponent::apply_led_outputs_() {
  gpio_set_level((gpio_num_t) pin_blue_,
                 current_color_ == SC_BLUE ? led_on_level() : led_off_level());
  gpio_set_level((gpio_num_t) pin_green_,
                 current_color_ == SC_GREEN ? led_on_level() : led_off_level());
  gpio_set_level((gpio_num_t) pin_red_,
                 current_color_ == SC_RED ? led_on_level() : led_off_level());
}

const char *SimFsmComponent::led_name() const {
  switch (current_color_) {
    case SC_OFF:   return "off";
    case SC_RED:   return "red";
    case SC_GREEN: return "green";
    case SC_BLUE:  return "blue";
  }
  return "unknown";
}

// ─── UART2 RX 讀取 + parser ────────────────────────────────────────
void SimFsmComponent::poll_uart_rx_() {
  uint8_t buf[64];
  int n = uart_read_bytes(UART_NUM_2, buf, sizeof(buf), 0);
  if (n <= 0) return;
  for (int i = 0; i < n; i++) {
    if (rx_cb_) rx_cb_(buf[i]);
    feed_parser_byte_(buf[i]);
  }
}

void SimFsmComponent::feed_parser_byte_(uint8_t b) {
  switch (parser_state_) {
    case PS_WAIT_FD:
      if (b == 0xFD) parser_state_ = PS_WAIT_03;
      break;
    case PS_WAIT_03:
      parser_state_ = (b == 0x03) ? PS_WAIT_C7 : PS_WAIT_FD;
      if (b == 0xFD) parser_state_ = PS_WAIT_03;  // 重新同步
      break;
    case PS_WAIT_C7:
      parser_state_ = (b == 0xC7) ? PS_WAIT_50 : PS_WAIT_FD;
      if (b == 0xFD) parser_state_ = PS_WAIT_03;
      break;
    case PS_WAIT_50:
      parser_state_ = (b == 0x50) ? PS_WAIT_CMD : PS_WAIT_FD;
      if (b == 0xFD) parser_state_ = PS_WAIT_03;
      break;
    case PS_WAIT_CMD:
      pending_cmd_ = b;
      parser_state_ = PS_WAIT_CHK;
      break;
    case PS_WAIT_CHK:
      pending_chk_ = b;
      parser_state_ = PS_WAIT_DF;
      break;
    case PS_WAIT_DF:
      if (b == 0xDF) {
        // 完整 frame 解完，派發 CMD
        WasherCmd cmd = cmd_byte_to_enum(pending_cmd_);
        if (cmd != WC_NONE) {
          dispatch_washer_cmd_(cmd);
        } else {
          ESP_LOGW(TAG, "Unknown UART2 CMD byte 0x%02X chk 0x%02X", pending_cmd_, pending_chk_);
        }
      } else {
        ESP_LOGW(TAG, "UART2 frame end byte wrong: expected 0xDF got 0x%02X", b);
      }
      parser_state_ = PS_WAIT_FD;
      break;
  }
}

void SimFsmComponent::dispatch_washer_cmd_(WasherCmd cmd) {
  const uint32_t now = millis();
  // 主板連發 5 次、每次間隔 100ms（共 ~400ms 窗口）
  // 同一 cmd 在 500ms 內重複到達 → 只派發第一次
  if (cmd == last_cmd_ && (now - last_cmd_dispatch_ms_) < 500) {
    return;  // swallow repeat
  }
  last_cmd_ = cmd;
  last_cmd_dispatch_ms_ = now;
  stats_total_cmds_++;

  ESP_LOGI(TAG, "UART2 CMD: %s (0x%02X)", washer_cmd_name(cmd), pending_cmd_);
  if (cmd_cb_) cmd_cb_(cmd);

  // 若 scenario 在 BLUE 等 gowasher → 推進到下一步
  if (scenario_ != nullptr && scenario_waiting_gowasher_ && cmd == WC_GOWASHER) {
    ESP_LOGI(TAG, "scenario: gowasher received → advance");
    advance_scenario_step_();
  }
  // 若收到 resetwasher / hardresetwasher → 中止 scenario
  if (scenario_ != nullptr && (cmd == WC_RESETWASHER || cmd == WC_HARDRESETWASHER)) {
    ESP_LOGI(TAG, "scenario: %s received → abort", washer_cmd_name(cmd));
    stop_scenario();
  }
}

// ─── Scenario 引擎 ─────────────────────────────────────────────────
bool SimFsmComponent::start_scenario(const char *scenario_name, float speed_factor) {
  const Scenario *sc = nullptr;
  if (scenario_name != nullptr && strcmp(scenario_name, "SW5") == 0) {
    sc = &SCENARIO_SW5;
  } else if (scenario_name != nullptr && strcmp(scenario_name, "SW6") == 0) {
    sc = &SCENARIO_SW6;
  } else {
    ESP_LOGW(TAG, "start_scenario: unknown name '%s'", scenario_name ? scenario_name : "(null)");
    return false;
  }

  if (speed_factor < 0.05f) speed_factor = 0.05f;
  if (speed_factor > 2.0f) speed_factor = 2.0f;

  scenario_ = sc;
  scenario_step_idx_ = 0;
  scenario_speed_factor_ = speed_factor;
  scenario_run_id_++;
  scenario_start_ms_ = millis();
  scenario_waiting_gowasher_ = false;
  stats_total_runs_++;

  // 進第 0 步
  const ScenarioStep &s0 = sc->steps[0];
  set_led(s0.target_led);
  scenario_step_enter_ms_ = millis();
  scenario_step_dwell_ms_ = (uint32_t)((float) s0.dwell_ms * speed_factor);
  scenario_waiting_gowasher_ = s0.wait_gowasher;

  char buf[96];
  snprintf(buf, sizeof(buf), "%s run#%u step 1/%d '%s' dwell=%ums speed=%.2fx",
           sc->name, (unsigned) scenario_run_id_, sc->step_count, s0.label,
           (unsigned) scenario_step_dwell_ms_, speed_factor);
  ESP_LOGI(TAG, "start_scenario: %s", buf);
  emit_state_("scenario_start", buf);
  return true;
}

void SimFsmComponent::stop_scenario() {
  if (scenario_ == nullptr) return;
  const Scenario *sc = reinterpret_cast<const Scenario *>(scenario_);
  ESP_LOGI(TAG, "stop_scenario: %s run#%u aborted at step %d",
           sc->name, (unsigned) scenario_run_id_, scenario_step_idx_ + 1);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s run#%u aborted at step %d",
           sc->name, (unsigned) scenario_run_id_, scenario_step_idx_ + 1);
  emit_state_("scenario_abort", buf);
  scenario_ = nullptr;
  scenario_waiting_gowasher_ = false;
  set_led(SC_OFF);
}

void SimFsmComponent::tick_scenario_() {
  if (scenario_waiting_gowasher_) return;  // 等 UART2 gowasher，不 tick
  const uint32_t now = millis();
  if ((now - scenario_step_enter_ms_) < scenario_step_dwell_ms_) return;
  advance_scenario_step_();
}

void SimFsmComponent::advance_scenario_step_() {
  const Scenario *sc = reinterpret_cast<const Scenario *>(scenario_);
  if (sc == nullptr) return;

  scenario_step_idx_++;
  if (scenario_step_idx_ >= sc->step_count) {
    finish_scenario_();
    return;
  }

  const ScenarioStep &s = sc->steps[scenario_step_idx_];
  set_led(s.target_led);
  scenario_step_enter_ms_ = millis();
  scenario_step_dwell_ms_ = (uint32_t)((float) s.dwell_ms * scenario_speed_factor_);
  scenario_waiting_gowasher_ = s.wait_gowasher;

  char buf[96];
  snprintf(buf, sizeof(buf), "%s run#%u step %d/%d '%s' dwell=%ums%s",
           sc->name, (unsigned) scenario_run_id_, scenario_step_idx_ + 1, sc->step_count,
           s.label, (unsigned) scenario_step_dwell_ms_,
           s.wait_gowasher ? " (wait gowasher)" : "");
  ESP_LOGI(TAG, "scenario: %s", buf);
  emit_state_("scenario_step", buf);
}

void SimFsmComponent::finish_scenario_() {
  const Scenario *sc = reinterpret_cast<const Scenario *>(scenario_);
  if (sc == nullptr) return;
  uint32_t duration_ms = millis() - scenario_start_ms_;
  char buf[80];
  snprintf(buf, sizeof(buf), "%s run#%u finished in %ums",
           sc->name, (unsigned) scenario_run_id_, (unsigned) duration_ms);
  ESP_LOGI(TAG, "scenario: %s", buf);
  emit_state_("scenario_finish", buf);
  scenario_ = nullptr;
  scenario_waiting_gowasher_ = false;
}

const char *SimFsmComponent::scenario_name() const {
  if (scenario_ == nullptr) return "idle";
  return reinterpret_cast<const Scenario *>(scenario_)->name;
}

int SimFsmComponent::scenario_total() const {
  if (scenario_ == nullptr) return 0;
  return reinterpret_cast<const Scenario *>(scenario_)->step_count;
}

void SimFsmComponent::emit_state_(const char *ev, const std::string &detail) {
  if (state_cb_) state_cb_(ev, detail);
}

}  // namespace sim_fsm
}  // namespace esphome
