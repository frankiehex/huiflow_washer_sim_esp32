// HuiFlow 洗車機模擬板 — sim_fsm
// Phase 2：LED GPIO + UART2 frame parser（7-byte WASHER_TABLE）+ scenario 引擎
// UART2 frame 格式（主板 ws_client.cpp:77-88 WASHER_TABLE）：
//   FD 03 C7 50 <CMD> <CHK> DF
// 10 個 CMD byte → startwasher1~6 / gowasher / resetwasher / hardresetwasher / sparewasher
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

// UART2 洗車機指令（對應 main board WASHER_TABLE 索引）
enum WasherCmd : uint8_t {
  WC_NONE = 0xFF,
  WC_STARTWASHER1 = 0,
  WC_STARTWASHER2 = 1,
  WC_STARTWASHER3 = 2,
  WC_STARTWASHER4 = 3,
  WC_STARTWASHER5 = 4,
  WC_STARTWASHER6 = 5,
  WC_GOWASHER = 6,
  WC_RESETWASHER = 7,
  WC_HARDRESETWASHER = 8,
  WC_SPAREWASHER = 9,
};

const char *washer_cmd_name(WasherCmd cmd);

// === Callbacks ===
// UART2 原始 byte（方便 Dashboard 顯示 hex dump）
using RxByteLogCb = std::function<void(uint8_t byte_val)>;
// UART2 7-byte frame 完整解完（已 dedupe 5 次連發）
using OnWasherCmdCb = std::function<void(WasherCmd cmd)>;
// 每次 FSM 狀態變化（LED 色 / scenario step 前進）
using OnFsmStateCb = std::function<void(const char *event_name, const std::string &detail)>;

class SimFsmComponent : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_led_pins(int blue, int green, int red) {
    pin_blue_ = blue;
    pin_green_ = green;
    pin_red_ = red;
  }
  void set_uart_rx_pin(int rx) { pin_uart_rx_ = rx; }

  void set_rx_byte_log_callback(RxByteLogCb cb) { rx_cb_ = std::move(cb); }
  void set_on_washer_cmd_callback(OnWasherCmdCb cb) { cmd_cb_ = std::move(cb); }
  void set_on_fsm_state_callback(OnFsmStateCb cb) { state_cb_ = std::move(cb); }

  void setup() override;
  void loop() override;

  // === Phase 1 公開 API ===
  void set_led(SimColor c);
  SimColor get_led() const { return current_color_; }
  const char *led_name() const;

  // === Phase 2：scenario 引擎 ===
  // 啟動指定 scenario（SW5 / SW6），speed_factor 範圍 0.1~1.0（壓縮時間）
  bool start_scenario(const char *scenario_name, float speed_factor);
  // 手動停止 scenario（中途取消）
  void stop_scenario();
  // 查詢狀態
  bool scenario_running() const { return scenario_ != nullptr; }
  const char *scenario_name() const;
  int scenario_step() const { return scenario_step_idx_; }   // 1-based 當前步驟
  int scenario_total() const;                                // 總步驟數
  uint32_t scenario_run_id() const { return scenario_run_id_; }

  // 統計
  uint32_t total_scenarios_run() const { return stats_total_runs_; }
  uint32_t total_cmds_received() const { return stats_total_cmds_; }
  WasherCmd last_cmd_received() const { return last_cmd_; }

  // === Phase 3：批次跑 ===
  // 連續跑 count 輪 scenario，每輪結束等 inter_run_ms 後啟動下一輪
  // 若 inter_run_ms < 100，最低 100ms（避免主板 wash_reset 沒消化）
  void start_batch(const char *scenario_name, float speed_factor,
                   uint32_t count, uint32_t inter_run_ms);
  void stop_batch();
  uint32_t batch_remaining() const { return batch_remaining_; }
  uint32_t batch_completed() const { return batch_completed_; }
  bool batch_active() const { return batch_remaining_ > 0; }

  // === Phase 3：故障注入 ===
  // prob 0~100 (%)；inject_blue 每次進 BLUE 相位時做機率 roll
  void set_fault_flicker(uint8_t prob_pct) { fault_flicker_prob_ = prob_pct; }
  void set_fault_power_loss(uint8_t prob_pct) { fault_power_loss_prob_ = prob_pct; }
  void set_fault_red_burst(uint8_t prob_pct) { fault_red_burst_prob_ = prob_pct; }
  uint8_t fault_flicker() const { return fault_flicker_prob_; }
  uint8_t fault_power_loss() const { return fault_power_loss_prob_; }
  uint8_t fault_red_burst() const { return fault_red_burst_prob_; }
  uint32_t fault_count() const { return stats_fault_count_; }

 protected:
  int pin_blue_ = -1;
  int pin_green_ = -1;
  int pin_red_ = -1;
  int pin_uart_rx_ = -1;

  SimColor current_color_ = SC_OFF;
  RxByteLogCb rx_cb_ = nullptr;
  OnWasherCmdCb cmd_cb_ = nullptr;
  OnFsmStateCb state_cb_ = nullptr;

  // === UART2 RX frame parser state machine ===
  // 7-byte frame: FD 03 C7 50 <CMD> <CHK> DF
  enum ParserState : uint8_t {
    PS_WAIT_FD = 0,
    PS_WAIT_03,
    PS_WAIT_C7,
    PS_WAIT_50,
    PS_WAIT_CMD,
    PS_WAIT_CHK,
    PS_WAIT_DF,
  };
  ParserState parser_state_ = PS_WAIT_FD;
  uint8_t pending_cmd_ = 0;
  uint8_t pending_chk_ = 0;

  // Dedupe 5 次連發：同一 CMD 在 500ms 內重複到達 → 只派發第一次
  WasherCmd last_cmd_ = WC_NONE;
  uint32_t last_cmd_dispatch_ms_ = 0;

  // 輪詢 UART2 RX 節流
  uint32_t last_rx_poll_ms_ = 0;

  // === Scenario 引擎 ===
  const void *scenario_ = nullptr;     // 指向 Scenario struct（避免 forward decl）
  int scenario_step_idx_ = 0;          // 0-based 內部索引
  uint32_t scenario_step_enter_ms_ = 0;
  uint32_t scenario_step_dwell_ms_ = 0; // 計算後的實際 dwell（含 speed_factor）
  float scenario_speed_factor_ = 1.0f;
  bool scenario_waiting_gowasher_ = false;
  uint32_t scenario_run_id_ = 0;       // 每次 start_scenario 遞增
  uint32_t scenario_start_ms_ = 0;

  // 統計
  uint32_t stats_total_runs_ = 0;
  uint32_t stats_total_cmds_ = 0;
  uint32_t stats_fault_count_ = 0;

  // === Phase 3：batch runner state ===
  uint32_t batch_remaining_ = 0;
  uint32_t batch_completed_ = 0;
  uint32_t batch_inter_run_ms_ = 1000;
  uint32_t batch_inter_run_until_ms_ = 0;  // 在此 ms 之後才啟動下一輪
  std::string batch_scenario_name_;
  float batch_speed_factor_ = 1.0f;

  // === Phase 3：fault injection ===
  uint8_t fault_flicker_prob_ = 0;
  uint8_t fault_power_loss_prob_ = 0;
  uint8_t fault_red_burst_prob_ = 0;
  // 當 fault active 時，覆蓋 scenario LED 控制
  bool fault_active_ = false;
  enum FaultType { FT_NONE, FT_FLICKER, FT_POWER_LOSS, FT_RED_BURST };
  FaultType fault_type_ = FT_NONE;
  uint32_t fault_until_ms_ = 0;
  uint32_t fault_next_toggle_ms_ = 0;
  bool fault_flicker_state_ = false;
  SimColor fault_pre_color_ = SC_OFF;

  // 內部工具
  void apply_led_outputs_();
  void poll_uart_rx_();
  void feed_parser_byte_(uint8_t b);
  void dispatch_washer_cmd_(WasherCmd cmd);
  void tick_scenario_();
  void advance_scenario_step_();
  void finish_scenario_();
  void emit_state_(const char *ev, const std::string &detail);

  // === Phase 3 helpers ===
  void tick_batch_();
  void tick_fault_();
  void maybe_inject_fault_();
  void end_fault_();
};

}  // namespace sim_fsm
}  // namespace esphome
