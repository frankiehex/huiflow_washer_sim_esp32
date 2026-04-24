// HuiFlow 洗車機模擬板 — sim_ws_server 實作
// 對照主板 ws_client.cpp lan_* 反向：主板是 client、我們是 server
// Client→Server frame 必 masked；Server→Client frame 必 unmasked（RFC 6455）
#include "sim_ws_server.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cerrno>
#include <cstring>
#include <cstdlib>

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

namespace esphome {
namespace sim_ws_server {

static const char *const TAG = "sim_ws_server";

// RFC 6455 GUID for Sec-WebSocket-Accept 計算
static const char *const WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 應用層 PING 間隔（主板 expects {"type":"PING"} 並回 {"type":"PONG"}）
static const uint32_t APP_PING_INTERVAL_MS = 5000;

// 連線閒置 timeout（15s 無任何 RX → 主動斷線，測主板重連）
static const uint32_t IDLE_TIMEOUT_MS = 15000;

// Handshake HTTP headers 上限，超過視為惡意 client
static const size_t MAX_HTTP_HEADER_BYTES = 4096;

// Frame payload 上限（主板的 STATUS 也就 ~2KB，8KB 綽綽有餘）
static const size_t MAX_WS_PAYLOAD_BYTES = 8192;

void SimWsServerComponent::setup() {
  ESP_LOGI(TAG, "setup: WS server on port %u", port_);
  if (!start_listen_()) {
    ESP_LOGE(TAG, "Failed to start listen socket, will retry in loop()");
  }
}

bool SimWsServerComponent::start_listen_() {
  if (listen_fd_ >= 0) return true;

  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_fd_ < 0) {
    ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
    return false;
  }

  int reuse = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (::bind(listen_fd_, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind() port %u failed: errno=%d", port_, errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (::listen(listen_fd_, 1) < 0) {
    ESP_LOGE(TAG, "listen() failed: errno=%d", errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // non-blocking accept
  int flags = fcntl(listen_fd_, F_GETFL, 0);
  fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

  ESP_LOGI(TAG, "Listening on 0.0.0.0:%u", port_);
  return true;
}

void SimWsServerComponent::loop() {
  // 若 listen socket 尚未建立（WiFi 還沒起來時 setup 失敗），重試
  if (listen_fd_ < 0) {
    if (!start_listen_()) return;
  }

  // 1) 嘗試接收新 client（只接 1 個）
  if (client_fd_ < 0) {
    accept_client_();
  }

  // 2) 讀 client 資料
  if (client_fd_ >= 0) {
    handle_client_rx_();
  }

  // 3) 應用層 PING
  if (client_fd_ >= 0 && handshake_done_) {
    uint32_t now = millis();
    if (now - last_ping_tx_ms_ >= APP_PING_INTERVAL_MS) {
      send_app_ping_();
      last_ping_tx_ms_ = now;
    }
    // Idle timeout check
    if (last_rx_ms_ && (now - last_rx_ms_ > IDLE_TIMEOUT_MS)) {
      close_client_("idle timeout");
    }
  }
}

void SimWsServerComponent::accept_client_() {
  struct sockaddr_in peer = {};
  socklen_t peer_len = sizeof(peer);
  int fd = ::accept(listen_fd_, (struct sockaddr *) &peer, &peer_len);
  if (fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    ESP_LOGE(TAG, "accept() failed: errno=%d", errno);
    return;
  }

  // non-blocking 避免 recv() 阻塞主 loop
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  char ip[16];
  inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
  ESP_LOGI(TAG, "Client connected from %s:%u (fd=%d)", ip, ntohs(peer.sin_port), fd);

  client_fd_ = fd;
  handshake_done_ = false;
  client_device_id_.clear();
  client_connect_ms_ = millis();
  last_rx_ms_ = client_connect_ms_;
  last_ping_tx_ms_ = client_connect_ms_;
  last_pong_rx_ms_ = client_connect_ms_;
  rx_accum_.clear();
}

void SimWsServerComponent::handle_client_rx_() {
  uint8_t buf[512];
  ssize_t n = ::recv(client_fd_, buf, sizeof(buf), 0);
  if (n == 0) {
    close_client_("peer closed");
    return;
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    ESP_LOGW(TAG, "recv() errno=%d, closing", errno);
    close_client_("recv error");
    return;
  }

  last_rx_ms_ = millis();
  stats_rx_bytes_ += n;
  rx_accum_.append((const char *) buf, n);

  if (!handshake_done_) {
    if (rx_accum_.size() > MAX_HTTP_HEADER_BYTES) {
      close_client_("http header too large");
      return;
    }
    if (!try_http_handshake_()) return;  // 等更多資料
    handshake_done_ = true;
    ESP_LOGI(TAG, "Handshake OK, device_id='%s'", client_device_id_.c_str());
    if (on_conn_change_) on_conn_change_(true, client_device_id_);
    // handshake 後剩餘資料可能已是 WS frame 開頭，落入下面 while
  }

  // 解所有可用的完整 frame
  while (!rx_accum_.empty() && client_fd_ >= 0) {
    if (!try_parse_one_frame_()) break;  // payload 不完整 → 等下輪
  }
}

bool SimWsServerComponent::try_http_handshake_() {
  size_t hdr_end = rx_accum_.find("\r\n\r\n");
  if (hdr_end == std::string::npos) return false;  // 尚未完整

  std::string headers = rx_accum_.substr(0, hdr_end);
  rx_accum_.erase(0, hdr_end + 4);  // 保留 header 之後的資料（理論上應為空，但以防萬一）

  // 取 request line（第一行）
  size_t first_crlf = headers.find("\r\n");
  std::string req_line = headers.substr(0, first_crlf);
  ESP_LOGD(TAG, "HTTP: %s", req_line.c_str());

  // 解析 path 裡的 device_id
  // 格式: "GET /ws?device_id=xxx HTTP/1.1"
  size_t path_start = req_line.find(' ');
  if (path_start == std::string::npos) {
    close_client_("bad request line");
    return false;
  }
  size_t path_end = req_line.find(' ', path_start + 1);
  std::string path = req_line.substr(path_start + 1, path_end - path_start - 1);
  size_t q_dev = path.find("device_id=");
  if (q_dev != std::string::npos) {
    size_t start = q_dev + 10;
    size_t end = path.find('&', start);
    client_device_id_ = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
  }

  // 找 Sec-WebSocket-Key（不分大小寫）
  std::string ws_key;
  size_t pos = 0;
  while (pos < headers.size()) {
    size_t eol = headers.find("\r\n", pos);
    if (eol == std::string::npos) eol = headers.size();
    std::string line = headers.substr(pos, eol - pos);
    pos = eol + 2;
    // 不分大小寫比對 "sec-websocket-key:"
    static const char kSecKey[] = "sec-websocket-key:";
    if (line.size() > sizeof(kSecKey) - 1) {
      bool match = true;
      for (size_t i = 0; i < sizeof(kSecKey) - 1; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != kSecKey[i]) {
          match = false;
          break;
        }
      }
      if (match) {
        size_t v_start = sizeof(kSecKey) - 1;
        while (v_start < line.size() && line[v_start] == ' ') v_start++;
        ws_key = line.substr(v_start);
        // 去尾空白
        while (!ws_key.empty() && (ws_key.back() == ' ' || ws_key.back() == '\r'))
          ws_key.pop_back();
        break;
      }
    }
  }

  if (ws_key.empty()) {
    ESP_LOGW(TAG, "Missing Sec-WebSocket-Key header, rejecting");
    close_client_("no ws key");
    return false;
  }

  std::string accept_key = compute_accept_key_(ws_key);
  if (!send_http_101_(accept_key)) {
    close_client_("send 101 failed");
    return false;
  }
  return true;
}

std::string SimWsServerComponent::compute_accept_key_(const std::string &ws_key) {
  std::string combined = ws_key + WS_MAGIC;

  uint8_t digest[20];
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, (const unsigned char *) combined.data(), combined.size());
  mbedtls_sha1_finish(&ctx, digest);
  mbedtls_sha1_free(&ctx);

  unsigned char b64[64];
  size_t b64_len = 0;
  mbedtls_base64_encode(b64, sizeof(b64), &b64_len, digest, sizeof(digest));
  return std::string((char *) b64, b64_len);
}

bool SimWsServerComponent::send_http_101_(const std::string &sec_ws_accept) {
  std::string resp =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: " + sec_ws_accept + "\r\n"
      "\r\n";
  size_t sent = 0;
  while (sent < resp.size()) {
    ssize_t r = ::send(client_fd_, resp.data() + sent, resp.size() - sent, 0);
    if (r <= 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        delay(5);
        continue;
      }
      return false;
    }
    sent += r;
  }
  stats_tx_bytes_ += sent;
  return true;
}

bool SimWsServerComponent::try_parse_one_frame_() {
  if (rx_accum_.size() < 2) return false;
  const uint8_t *p = (const uint8_t *) rx_accum_.data();
  uint8_t b0 = p[0];
  uint8_t b1 = p[1];
  uint8_t opcode = b0 & 0x0F;
  bool masked = (b1 & 0x80) != 0;
  size_t payload_len = b1 & 0x7F;
  size_t header_len = 2;

  if (payload_len == 126) {
    if (rx_accum_.size() < 4) return false;
    payload_len = ((size_t) p[2] << 8) | p[3];
    header_len = 4;
  } else if (payload_len == 127) {
    // 64-bit length not expected from main board
    close_client_("64-bit frame not supported");
    return false;
  }

  size_t mask_len = masked ? 4 : 0;
  if (rx_accum_.size() < header_len + mask_len + payload_len) return false;  // 不完整

  if (payload_len > MAX_WS_PAYLOAD_BYTES) {
    close_client_("frame payload too large");
    return false;
  }

  uint8_t mask[4] = {};
  if (masked) memcpy(mask, p + header_len, 4);

  const uint8_t *payload_start = p + header_len + mask_len;
  std::string payload(payload_len, '\0');
  for (size_t i = 0; i < payload_len; i++) {
    payload[i] = masked ? (payload_start[i] ^ mask[i % 4]) : payload_start[i];
  }

  // 移除已處理 bytes
  rx_accum_.erase(0, header_len + mask_len + payload_len);
  stats_rx_frames_++;

  switch (opcode) {
    case 0x01:
    case 0x02: {  // text / binary
      ESP_LOGD(TAG, "RX text(%u): %.*s", (unsigned) payload_len, (int) payload_len, payload.data());
      if (on_message_) on_message_(payload);
      // 應用層 PING → 回 PONG
      if (payload.find("\"type\":\"PING\"") != std::string::npos ||
          payload.find("\"type\": \"PING\"") != std::string::npos) {
        const char *pong = "{\"type\":\"PONG\"}";
        send_ws_frame_(0x01, (const uint8_t *) pong, strlen(pong));
      }
      break;
    }
    case 0x08:  // close
      ESP_LOGI(TAG, "RX close from client");
      close_client_("client sent close");
      break;
    case 0x09:  // ping
      ESP_LOGD(TAG, "RX ping, replying pong");
      send_ws_frame_(0x0A, (const uint8_t *) payload.data(), payload.size());
      break;
    case 0x0A:  // pong
      last_pong_rx_ms_ = millis();
      break;
    default:
      ESP_LOGW(TAG, "RX unknown opcode 0x%02X", opcode);
      break;
  }
  return true;
}

bool SimWsServerComponent::send_ws_frame_(uint8_t opcode, const uint8_t *data, size_t len) {
  if (client_fd_ < 0) return false;

  uint8_t hdr[4];
  size_t hdr_len = 0;
  hdr[0] = 0x80 | (opcode & 0x0F);  // FIN=1
  if (len < 126) {
    hdr[1] = (uint8_t) len;  // Server→Client 不 mask
    hdr_len = 2;
  } else {
    hdr[1] = 126;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = len & 0xFF;
    hdr_len = 4;
  }

  // 送 header
  size_t sent = 0;
  while (sent < hdr_len) {
    ssize_t r = ::send(client_fd_, hdr + sent, hdr_len - sent, 0);
    if (r <= 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        delay(2);
        continue;
      }
      close_client_("send hdr fail");
      return false;
    }
    sent += r;
  }
  stats_tx_bytes_ += hdr_len;

  // 送 payload
  sent = 0;
  while (sent < len) {
    ssize_t r = ::send(client_fd_, data + sent, len - sent, 0);
    if (r <= 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        delay(2);
        continue;
      }
      close_client_("send payload fail");
      return false;
    }
    sent += r;
  }
  stats_tx_bytes_ += len;
  stats_tx_frames_++;
  return true;
}

bool SimWsServerComponent::send_text(const std::string &text) {
  if (!is_client_connected()) return false;
  return send_ws_frame_(0x01, (const uint8_t *) text.data(), text.size());
}

void SimWsServerComponent::send_app_ping_() {
  const char *ping = "{\"type\":\"PING\"}";
  send_ws_frame_(0x01, (const uint8_t *) ping, strlen(ping));
}

void SimWsServerComponent::close_client_(const char *reason) {
  if (client_fd_ < 0) return;
  ESP_LOGI(TAG, "Closing client (fd=%d): %s", client_fd_, reason);
  ::close(client_fd_);
  client_fd_ = -1;
  handshake_done_ = false;
  rx_accum_.clear();
  if (on_conn_change_) on_conn_change_(false, client_device_id_);
  client_device_id_.clear();
}

void SimWsServerComponent::disconnect_client(const char *reason) {
  close_client_(reason ? reason : "manual disconnect");
}

}  // namespace sim_ws_server
}  // namespace esphome
