// HuiFlow 洗車機模擬板 — sim_ws_server
// Phase 1 骨架：port 8090 raw TCP + WebSocket handshake（SHA1+base64）+
//             frame codec（RFC 6455）+ WS ping/pong + 應用層 JSON PING/PONG
//
// 協議對照主板 my_components/ws_client/ws_client.cpp:4141-4436（lan_* 系列）反向實作。
// Phase 2 會擴充 event_parser（解 wash_stage/STATUS 等）+ CMD 廣播 API。
#pragma once

#include "esphome/core/component.h"
#include <cstdint>
#include <functional>
#include <string>

namespace esphome {
namespace sim_ws_server {

// 回傳收到主板送來的 JSON message（Dashboard 顯示 + Phase 2 的 event_parser 用）
using OnMessageCb = std::function<void(const std::string &msg)>;

// 連線狀態變化通知（Dashboard 顯示主板 online/offline）
using OnConnectionChangeCb = std::function<void(bool connected, const std::string &device_id)>;

// Phase 2: 主板 EVENT 解析後（已抽出 type/event/wash_phase 等核心欄位）
// type 是 "EVENT" / "STATUS" / "CMD_RESULT" 之一
// event_name 是 "wash_stage" / "wash_unlock" / "washer_status" 等（STATUS 時為空）
// wash_phase 是 "idle"/"green"/"blue"/"red"/"done"/"nopower"/"lock"（無則空）
// raw 完整 JSON（供深度檢查）
struct ParsedEvent {
  std::string type;
  std::string event_name;
  std::string wash_phase;
  int wash_stage = -1;
  int wash_total = -1;
  std::string raw;
};
using OnParsedEventCb = std::function<void(const ParsedEvent &ev)>;

class SimWsServerComponent : public Component {
 public:
  // setup_priority::AFTER_WIFI 確保 WiFi 起來後才開 listen socket
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_port(uint16_t port) { port_ = port; }

  void setup() override;
  void loop() override;

  // 外部訂閱：收到主板訊息 / 連線狀態變化 / 解析後 EVENT
  void set_on_message_callback(OnMessageCb cb) { on_message_ = std::move(cb); }
  void set_on_connection_change_callback(OnConnectionChangeCb cb) { on_conn_change_ = std::move(cb); }
  void set_on_parsed_event_callback(OnParsedEventCb cb) { on_parsed_event_ = std::move(cb); }

  // 對主板送 text frame（JSON）
  // 回傳 true=送出成功，false=無 client 或 send 失敗
  bool send_text(const std::string &text);

  // 查詢狀態
  bool is_client_connected() const { return client_fd_ >= 0 && handshake_done_; }
  const std::string &client_device_id() const { return client_device_id_; }
  uint32_t bytes_rx() const { return stats_rx_bytes_; }
  uint32_t bytes_tx() const { return stats_tx_bytes_; }
  uint32_t frames_rx() const { return stats_rx_frames_; }
  uint32_t frames_tx() const { return stats_tx_frames_; }

  // 主動關閉當前 client（Phase 3 assertion A8 用來測斷線重連）
  void disconnect_client(const char *reason);

 protected:
  uint16_t port_ = 8090;

  int listen_fd_ = -1;
  int client_fd_ = -1;
  bool handshake_done_ = false;
  std::string client_device_id_;
  uint32_t client_connect_ms_ = 0;
  uint32_t last_rx_ms_ = 0;
  uint32_t last_ping_tx_ms_ = 0;
  uint32_t last_pong_rx_ms_ = 0;

  // 接收緩衝（HTTP handshake + WS frames 共用，Phase 1 用簡單 growable string）
  std::string rx_accum_;
  // Phase 8.3：rx_accum_ 進度追蹤，無進度 + 有資料 → 視為 stale 半開連線
  size_t rx_accum_last_size_ = 0;
  uint32_t rx_accum_last_progress_ms_ = 0;

  OnMessageCb on_message_ = nullptr;
  OnConnectionChangeCb on_conn_change_ = nullptr;
  OnParsedEventCb on_parsed_event_ = nullptr;

  // 統計
  uint32_t stats_rx_bytes_ = 0;
  uint32_t stats_tx_bytes_ = 0;
  uint32_t stats_rx_frames_ = 0;
  uint32_t stats_tx_frames_ = 0;

  // 內部工具
  bool start_listen_();
  void accept_client_();
  void handle_client_rx_();
  bool try_http_handshake_();
  bool try_parse_one_frame_();
  bool send_ws_frame_(uint8_t opcode, const uint8_t *data, size_t len);
  bool send_http_101_(const std::string &sec_ws_accept);
  static std::string compute_accept_key_(const std::string &ws_key);
  void close_client_(const char *reason);
  void send_app_ping_();
  void parse_and_dispatch_event_(const std::string &payload);
};

}  // namespace sim_ws_server
}  // namespace esphome
