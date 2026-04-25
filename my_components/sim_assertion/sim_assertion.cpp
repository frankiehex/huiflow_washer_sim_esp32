// HuiFlow 洗車機模擬板 — sim_assertion 實作
#include "sim_assertion.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <climits>
#include <cstdio>

namespace esphome {
namespace sim_assertion {

static const char *const TAG = "sim_assertion";

void SimAssertionComponent::setup() {
  results_.reserve(MAX_RESULTS);
  ESP_LOGI(TAG, "sim_assertion setup, MAX_RESULTS=%u", (unsigned) MAX_RESULTS);
}

void SimAssertionComponent::loop() {
  if (pending_.empty()) return;
  const uint32_t now = millis();
  // 檢查超時
  for (auto it = pending_.begin(); it != pending_.end(); ) {
    if (now > it->deadline_ms) {
      AssertionResult r;
      r.assertion_id = it->assertion_id;
      r.pass = false;
      r.expected_within_ms = it->deadline_ms - it->created_ms;
      r.actual_ms = UINT32_MAX;  // sentinel: timeout
      r.timestamp_ms = now;
      char buf[160];
      snprintf(buf, sizeof(buf), "TIMEOUT: expected event=%s phase=%s within %ums",
               it->expected_event.c_str(),
               it->expected_phase.empty() ? "(any)" : it->expected_phase.c_str(),
               (unsigned) r.expected_within_ms);
      r.detail = buf;
      record_result_(r);
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

void SimAssertionComponent::expect_event(const std::string &assertion_id,
                                         const std::string &expected_event,
                                         const std::string &expected_phase,
                                         int expected_total,
                                         uint32_t window_ms) {
  PendingExpectation e;
  e.assertion_id = assertion_id;
  e.expected_event = expected_event;
  e.expected_phase = expected_phase;
  e.expected_total = expected_total;
  e.created_ms = millis();
  e.deadline_ms = e.created_ms + window_ms;
  pending_.push_back(e);
  ESP_LOGD(TAG, "expect[%s]: event=%s phase=%s total=%d within %ums",
           assertion_id.c_str(), expected_event.c_str(),
           expected_phase.empty() ? "(any)" : expected_phase.c_str(),
           expected_total, (unsigned) window_ms);
}

void SimAssertionComponent::observe_event(const std::string &event_name,
                                          const std::string &wash_phase,
                                          int wash_stage,
                                          int wash_total) {
  if (pending_.empty()) return;
  const uint32_t now = millis();
  // FIFO：找第一個匹配的 pending
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    bool event_match = (it->expected_event.empty() || it->expected_event == event_name);
    bool phase_match = (it->expected_phase.empty() || it->expected_phase == wash_phase);
    bool total_match = true;
    if (it->expected_total >= 0) {
      total_match = (it->expected_total == wash_total);
    }
    if (event_match && phase_match && total_match) {
      // PASS!
      AssertionResult r;
      r.assertion_id = it->assertion_id;
      r.pass = true;
      r.expected_within_ms = it->deadline_ms - it->created_ms;
      r.actual_ms = now - it->created_ms;
      r.timestamp_ms = now;
      char buf[160];
      snprintf(buf, sizeof(buf), "PASS: event=%s phase=%s stage=%d total=%d in %ums (window %ums)",
               event_name.c_str(),
               wash_phase.empty() ? "-" : wash_phase.c_str(),
               wash_stage, wash_total,
               (unsigned) r.actual_ms, (unsigned) r.expected_within_ms);
      r.detail = buf;
      record_result_(r);
      pending_.erase(it);
      return;
    }
  }
  // 沒匹配也是正常情況（很多 EVENT 並非 sim_fsm 觸發的）
}

void SimAssertionComponent::record_result_(const AssertionResult &r) {
  if (r.pass) {
    total_pass_++;
    last_pass_summary_ = r.assertion_id + ": " + r.detail;
  } else {
    total_fail_++;
    last_fail_summary_ = r.assertion_id + ": " + r.detail;
    ESP_LOGW(TAG, "FAIL %s: %s", r.assertion_id.c_str(), r.detail.c_str());
  }

  // 環形 buffer 寫入
  if (results_.size() < MAX_RESULTS) {
    results_.push_back(r);
  } else {
    results_[results_write_idx_] = r;
  }
  results_write_idx_ = (results_write_idx_ + 1) % MAX_RESULTS;
}

void SimAssertionComponent::reset_stats() {
  total_pass_ = 0;
  total_fail_ = 0;
  pending_.clear();
  results_.clear();
  results_write_idx_ = 0;
  last_fail_summary_.clear();
  last_pass_summary_.clear();
  ESP_LOGI(TAG, "stats reset");
}

}  // namespace sim_assertion
}  // namespace esphome
