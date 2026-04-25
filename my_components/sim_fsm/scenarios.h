// HuiFlow 洗車機模擬板 — scenarios.h
// 預設情境腳本（SW5 6 步 / SW6 9 步），每步配置 LED 色 + dwell 時間
// dwell_ms 是 base 值，實際 dwell = base * speed_factor（0.1x~1.0x）
#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace sim_fsm {

// 前向宣告（避免循環引用）
enum SimColor : uint8_t;

struct ScenarioStep {
  SimColor target_led;    // 此步驟要切到的 LED 色
  uint32_t dwell_ms;      // 停留毫秒數（base）
  bool wait_gowasher;     // 若 true：切到 BLUE 後等 UART2 gowasher 才推進（不 dwell）
  const char *label;      // 人看的標籤（僅 log 用）
};

struct Scenario {
  const char *name;
  int step_count;
  const ScenarioStep *steps;
};

// SimColor 完整定義見 sim_fsm.h，這裡只 forward declare。
// 下面的 extern 腳本在 sim_fsm.cpp 實體化（避免 inline 到每個 TU 造成多重定義）。
extern const Scenario SCENARIO_SW5;
extern const Scenario SCENARIO_SW6;

}  // namespace sim_fsm
}  // namespace esphome
