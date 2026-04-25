// HuiFlow 洗車機模擬板 — sim_assertion
// 規則引擎：sim_fsm 觸發 LED 變化時 expect_event() 入隊，
//          sim_ws_server EVENT parser 解到主板回應時 observe_event() 配對 / 標記 pass，
//          loop() 每輪檢查超時 → 標記 fail。
//
// Phase 3 MVP 規則（A1~A4，純 LED→EVENT 時序）：
//   A1: LED→GREEN → 主板送 wash_stage with phase="green" within 500ms
//   A2: LED→BLUE  → 主板送 wash_stage with phase="blue"  within 500ms
//   A3: LED→RED during wash → 主板送 wash_stage with wash_total=0 within 1000ms
//   A4: LED→OFF >500ms → 主板送 EVENT wash_nopower within 1500ms
//
// Phase 3+ 擴充（A5~A9）：
//   A5: wash_init unlock_pwd → wash_phase="lock"
//   A6: wash_unlock CMD when GREEN → wash_phase="green"
//   A7: UART2 frame 連發 5 次 / 100ms 間隔
//   A8: WS 主動斷線 → 主板 60s 內重連
//   A9: reboot CMD → reconnect 後送 STATUS
#pragma once

#include "esphome/core/component.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace sim_assertion {

struct AssertionResult {
  std::string assertion_id;     // "A1" / "A2" / "A3" / "A4" / ...
  bool pass;
  uint32_t expected_within_ms;
  uint32_t actual_ms;           // 實測耗時，UINT32_MAX 表示 timeout
  std::string detail;
  uint32_t timestamp_ms;
};

struct PendingExpectation {
  std::string assertion_id;
  std::string expected_event;   // 例 "wash_stage" 或 "wash_nopower"
  std::string expected_phase;   // 例 "green" / "blue" / "red" / "" (任意)
  int expected_total;           // -1=不限；0=destructive reset；>0=指定 total
  uint32_t deadline_ms;
  uint32_t created_ms;
};

class SimAssertionComponent : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void loop() override;

  // sim_fsm 觸發：LED 變化時宣告期待
  // expected_phase 為 "" 代表不限
  // expected_total: -1 不限，0 = destructive reset (wash_total=0)，>0 = 指定值
  void expect_event(const std::string &assertion_id,
                    const std::string &expected_event,
                    const std::string &expected_phase,
                    int expected_total,
                    uint32_t window_ms);

  // sim_ws_server EVENT parser 觸發：主板回應到達
  void observe_event(const std::string &event_name,
                     const std::string &wash_phase,
                     int wash_stage,
                     int wash_total);

  // 統計查詢
  uint32_t total_pass() const { return total_pass_; }
  uint32_t total_fail() const { return total_fail_; }
  uint32_t pending_count() const { return pending_.size(); }
  std::string last_fail_summary() const { return last_fail_summary_; }
  std::string last_pass_summary() const { return last_pass_summary_; }

  // 重置統計（測試批次起始用）
  void reset_stats();

  // 取最近 N 筆結果（Dashboard 拉表用）
  const std::vector<AssertionResult> &results() const { return results_; }

  // Phase 6：per-rule 統計
  struct RuleStats {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t total_actual_ms = 0;  // 用於計算 pass 平均耗時
  };
  // 回傳 "A1:5/0|A2:3/2|A3:0/1|..." 格式（Dashboard 顯示用）
  std::string per_rule_summary() const;

  // Phase 4：每筆 result 推送（給 YAML lambda 餵 SSE text_sensor）
  using OnResultCb = std::function<void(const AssertionResult &)>;
  void set_on_result_callback(OnResultCb cb) { on_result_ = std::move(cb); }

 protected:
  // 待匹配期待清單（FIFO）
  std::vector<PendingExpectation> pending_;
  // 結果環形 buffer（最多 100 筆，滿了從頭覆寫）
  std::vector<AssertionResult> results_;
  static const size_t MAX_RESULTS = 100;
  size_t results_write_idx_ = 0;

  uint32_t total_pass_ = 0;
  uint32_t total_fail_ = 0;
  std::string last_fail_summary_;
  std::string last_pass_summary_;

  // Phase 6：per-rule stats
  std::map<std::string, RuleStats> rule_stats_;

  void record_result_(const AssertionResult &r);
  OnResultCb on_result_ = nullptr;
};

}  // namespace sim_assertion
}  // namespace esphome
