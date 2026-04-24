#include "ota_update.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/network/util.h"
// 模擬板移除 ws_client 依賴（只用於主板的 reboot 兇手 breadcrumb）

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <cJSON.h>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

extern "C" {
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
}

namespace esphome {
namespace ota_update {

static const char *TAG = "ota_update";

// ============================================================
// mbedTLS BIO callbacks (same pattern as ws_client)
// ============================================================
static int mbed_send_cb(void *ctx, const unsigned char *buf, size_t len) {
  int fd = *(int *)ctx;
  ssize_t ret = ::send(fd, buf, len, 0);
  if (ret < 0) {
    int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return -1;
  }
  return (int)ret;
}

static int mbed_recv_cb(void *ctx, unsigned char *buf, size_t len) {
  int fd = *(int *)ctx;
  ssize_t ret = ::recv(fd, buf, len, 0);
  if (ret < 0) {
    int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
    return -1;
  }
  return (int)ret;
}

// ============================================================
// URL parser
// ============================================================
bool OTAUpdateComponent::parse_url(const std::string &url, std::string &host,
                                    uint16_t &port, std::string &path, bool &use_ssl) {
  // https://host:port/path or http://host:port/path
  size_t pos = 0;
  if (url.compare(0, 8, "https://") == 0) {
    use_ssl = true;
    pos = 8;
    port = 443;
  } else if (url.compare(0, 7, "http://") == 0) {
    use_ssl = false;
    pos = 7;
    port = 80;
  } else {
    return false;
  }

  size_t slash = url.find('/', pos);
  std::string host_port;
  if (slash == std::string::npos) {
    host_port = url.substr(pos);
    path = "/";
  } else {
    host_port = url.substr(pos, slash - pos);
    path = url.substr(slash);
  }

  size_t colon = host_port.find(':');
  if (colon != std::string::npos) {
    host = host_port.substr(0, colon);
    port = (uint16_t)atoi(host_port.substr(colon + 1).c_str());
  } else {
    host = host_port;
  }
  return !host.empty();
}

// ============================================================
// TLS connect (raw socket + mbedTLS)
// ============================================================
bool OTAUpdateComponent::tls_connect(const std::string &host, uint16_t port) {
  // DNS resolve
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", port);

  int ret = getaddrinfo(host.c_str(), port_str, &hints, &res);
  if (ret != 0 || !res) {
    ESP_LOGE(TAG, "DNS failed for %s: %d", host.c_str(), ret);
    return false;
  }

  struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;

  // TCP connect
  this->sock_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (this->sock_fd_ < 0) {
    freeaddrinfo(res);
    ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
    return false;
  }

  // v3.16.51: RCVTIMEO 5s → 15s 回滾（v3.16.49 改 5s 導致 TLS 握手被截斷，OTA 完全失敗）
  //   TLS handshake + HTTP header 讀取單次阻塞可能超過 5s，stall detection 由上層 30s 負責
  struct timeval tv;
  tv.tv_sec = 15;
  tv.tv_usec = 0;
  setsockopt(this->sock_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(this->sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  int conn_ret = ::connect(this->sock_fd_, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
  freeaddrinfo(res);

  if (conn_ret != 0) {
    ESP_LOGE(TAG, "TCP connect failed to %s:%u: errno=%d", host.c_str(), port, errno);
    ::close(this->sock_fd_);
    this->sock_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "TCP connected to %s:%u", host.c_str(), port);

  // mbedTLS setup
  mbedtls_ssl_init(&this->ssl_);
  mbedtls_ssl_config_init(&this->ssl_conf_);
  mbedtls_entropy_init(&this->entropy_);
  mbedtls_ctr_drbg_init(&this->ctr_drbg_);
  this->mbedtls_initialized_ = true;

  ret = mbedtls_ctr_drbg_seed(&this->ctr_drbg_, mbedtls_entropy_func, &this->entropy_, nullptr, 0);
  if (ret != 0) {
    ESP_LOGE(TAG, "ctr_drbg_seed failed: -0x%04x", -ret);
    this->tls_disconnect();
    return false;
  }

  ret = mbedtls_ssl_config_defaults(&this->ssl_conf_,
                                     MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    ESP_LOGE(TAG, "ssl_config_defaults failed: -0x%04x", -ret);
    this->tls_disconnect();
    return false;
  }

  mbedtls_ssl_conf_authmode(&this->ssl_conf_, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&this->ssl_conf_, mbedtls_ctr_drbg_random, &this->ctr_drbg_);
  mbedtls_ssl_conf_min_tls_version(&this->ssl_conf_, MBEDTLS_SSL_VERSION_TLS1_2);

  // ALPN http/1.1 (GitHub requires it)
  static const char *alpn[] = {"http/1.1", nullptr};
  mbedtls_ssl_conf_alpn_protocols(&this->ssl_conf_, alpn);

  ret = mbedtls_ssl_setup(&this->ssl_, &this->ssl_conf_);
  if (ret != 0) {
    ESP_LOGE(TAG, "ssl_setup failed: -0x%04x", -ret);
    this->tls_disconnect();
    return false;
  }

  // SNI
  mbedtls_ssl_set_hostname(&this->ssl_, host.c_str());
  mbedtls_ssl_set_bio(&this->ssl_, &this->sock_fd_, mbed_send_cb, mbed_recv_cb, nullptr);

  // TLS handshake
  ESP_LOGI(TAG, "TLS handshake to %s...", host.c_str());
  uint32_t t0 = millis();
  while ((ret = mbedtls_ssl_handshake(&this->ssl_)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      char err_buf[100];
      mbedtls_strerror(ret, err_buf, sizeof(err_buf));
      ESP_LOGE(TAG, "TLS handshake FAILED: -0x%04x %s", -ret, err_buf);
      this->tls_disconnect();
      return false;
    }
    if (millis() - t0 > 30000) {
      ESP_LOGE(TAG, "TLS handshake timeout (30s)");
      this->tls_disconnect();
      return false;
    }
  }

  this->ssl_active_ = true;
  this->connected_host_ = host;
  ESP_LOGI(TAG, "TLS OK in %ums (cipher: %s)", millis() - t0, mbedtls_ssl_get_ciphersuite(&this->ssl_));
  return true;
}

// ============================================================
// TLS disconnect / cleanup
// ============================================================
void OTAUpdateComponent::tls_disconnect() {
  if (this->ssl_active_) {
    mbedtls_ssl_close_notify(&this->ssl_);
    this->ssl_active_ = false;
  }
  if (this->mbedtls_initialized_) {
    mbedtls_ssl_free(&this->ssl_);
    mbedtls_ssl_config_free(&this->ssl_conf_);
    mbedtls_ctr_drbg_free(&this->ctr_drbg_);
    mbedtls_entropy_free(&this->entropy_);
    this->mbedtls_initialized_ = false;
  }
  if (this->sock_fd_ >= 0) {
    ::close(this->sock_fd_);
    this->sock_fd_ = -1;
  }
  this->connected_host_.clear();
}

// ============================================================
// TLS read / write wrappers
// ============================================================
ssize_t OTAUpdateComponent::tls_write(const void *buf, size_t len) {
  if (this->ssl_active_) {
    int ret;
    do {
      ret = mbedtls_ssl_write(&this->ssl_, (const unsigned char *)buf, len);
    } while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return (ret < 0) ? -1 : ret;
  }
  return ::send(this->sock_fd_, buf, len, 0);
}

ssize_t OTAUpdateComponent::tls_read(void *buf, size_t len) {
  if (this->ssl_active_) {
    int ret = mbedtls_ssl_read(&this->ssl_, (unsigned char *)buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      errno = EAGAIN;
      return -1;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) return 0;
    return (ret < 0) ? -1 : ret;
  }
  return ::recv(this->sock_fd_, buf, len, 0);
}

// ============================================================
// HTTP GET helper — returns HTTP status code, fills body_buf
// For firmware download: body_buf=NULL, content_length_out set
// ============================================================
int OTAUpdateComponent::http_get(const std::string &url, char *body_buf, int body_cap, int *content_length_out) {
  std::string host, path;
  uint16_t port;
  bool use_ssl;

  if (!this->parse_url(url, host, port, path, use_ssl)) {
    ESP_LOGE(TAG, "Invalid URL: %s", url.c_str());
    return -1;
  }

  // Connect (reuse if same host)
  if (this->sock_fd_ < 0 || this->connected_host_ != host) {
    this->tls_disconnect();
    if (use_ssl) {
      if (!this->tls_connect(host, port)) return -1;
    } else {
      // Plain TCP (unlikely for GitHub but supported)
      struct addrinfo hints = {}, *res = nullptr;
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      char port_str[8];
      snprintf(port_str, sizeof(port_str), "%u", port);
      if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res) return -1;
      this->sock_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (this->sock_fd_ < 0) { freeaddrinfo(res); return -1; }
      struct timeval tv = {15, 0};  // v3.16.51: 5s 回滾到 15s（TLS 握手截斷修復）
      setsockopt(this->sock_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
      setsockopt(this->sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      if (::connect(this->sock_fd_, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        ::close(this->sock_fd_);
        this->sock_fd_ = -1;
        return -1;
      }
      freeaddrinfo(res);
      this->connected_host_ = host;
    }
  }

  // Send HTTP GET request
  char req_buf[768];
  int req_len;
  if (!this->manifest_token_.empty()) {
    req_len = snprintf(req_buf, sizeof(req_buf),
      "GET %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "User-Agent: ESP32-OTA/1.0\r\n"
      "Authorization: token %s\r\n"
      "Connection: close\r\n"
      "Accept: */*\r\n"
      "\r\n",
      path.c_str(), host.c_str(), this->manifest_token_.c_str());
  } else {
    req_len = snprintf(req_buf, sizeof(req_buf),
      "GET %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "User-Agent: ESP32-OTA/1.0\r\n"
      "Connection: close\r\n"
      "Accept: */*\r\n"
      "\r\n",
      path.c_str(), host.c_str());
  }

  ssize_t sent = this->tls_write(req_buf, req_len);
  if (sent <= 0) {
    ESP_LOGE(TAG, "HTTP send failed");
    this->tls_disconnect();
    return -1;
  }

  // Read response headers
  // We read byte by byte to find \r\n\r\n (simple but reliable)
  char hdr_buf[1024];
  int hdr_len = 0;
  bool headers_done = false;
  uint32_t read_start = millis();

  while (!headers_done && hdr_len < (int)sizeof(hdr_buf) - 1) {
    if (millis() - read_start > 30000) {
      ESP_LOGE(TAG, "HTTP header read timeout");
      this->tls_disconnect();
      return -1;
    }
    ssize_t r = this->tls_read(hdr_buf + hdr_len, 1);
    if (r <= 0) {
      if (r < 0 && errno == EAGAIN) { vTaskDelay(1); continue; }
      ESP_LOGE(TAG, "HTTP header read error");
      this->tls_disconnect();
      return -1;
    }
    hdr_len++;
    if (hdr_len >= 4 &&
        hdr_buf[hdr_len-4] == '\r' && hdr_buf[hdr_len-3] == '\n' &&
        hdr_buf[hdr_len-2] == '\r' && hdr_buf[hdr_len-1] == '\n') {
      headers_done = true;
    }
  }
  hdr_buf[hdr_len] = '\0';

  // Parse HTTP status
  int http_status = 0;
  if (sscanf(hdr_buf, "HTTP/%*d.%*d %d", &http_status) != 1) {
    ESP_LOGE(TAG, "Cannot parse HTTP status");
    this->tls_disconnect();
    return -1;
  }

  // Parse Content-Length
  int content_length = -1;
  const char *cl = strcasestr(hdr_buf, "Content-Length:");
  if (cl) {
    content_length = atoi(cl + 15);
  }
  if (content_length_out) *content_length_out = content_length;

  ESP_LOGI(TAG, "HTTP %d, Content-Length: %d", http_status, content_length);

  // Handle 3xx redirects
  if (http_status >= 300 && http_status < 400) {
    const char *loc = strcasestr(hdr_buf, "Location:");
    if (loc) {
      loc += 9;
      while (*loc == ' ') loc++;
      const char *end = strstr(loc, "\r\n");
      if (end) {
        std::string new_url(loc, end - loc);
        ESP_LOGI(TAG, "Redirect -> %s", new_url.c_str());
        this->tls_disconnect();  // Close old connection
        return this->http_get(new_url, body_buf, body_cap, content_length_out);
      }
    }
    this->tls_disconnect();
    return http_status;
  }

  // Read body into buffer (for manifest)
  if (body_buf && body_cap > 0) {
    int body_read = 0;
    while (body_read < body_cap - 1) {
      if (millis() - read_start > 30000) break;
      int to_read = body_cap - 1 - body_read;
      if (content_length >= 0 && to_read > content_length - body_read) {
        to_read = content_length - body_read;
      }
      if (to_read <= 0) break;
      ssize_t r = this->tls_read(body_buf + body_read, to_read);
      if (r <= 0) {
        if (r < 0 && errno == EAGAIN) { vTaskDelay(1); continue; }
        break;  // EOF or error
      }
      body_read += r;
    }
    body_buf[body_read] = '\0';
    ESP_LOGI(TAG, "Body: %d bytes", body_read);
  }

  return http_status;
}

// ============================================================
// Component lifecycle
// ============================================================
float OTAUpdateComponent::get_setup_priority() const {
  return setup_priority::AFTER_WIFI - 10.0f;
}

void OTAUpdateComponent::setup() {
  this->boot_time_ = millis();
  this->last_check_time_ = this->boot_time_;
  this->status_ = "Idle";
  this->phase_ = OTA_PHASE_WAITING;
  ESP_LOGI(TAG, "OTA ready (v%s), manifest: %s", this->current_version_.c_str(), this->manifest_url_.c_str());
  ESP_LOGI(TAG, "Boot check: %s, max retries: %u, interval: %us",
           this->check_on_boot_ ? "yes" : "no", MAX_BOOT_RETRIES, BOOT_RETRY_INTERVAL_MS / 1000);
}

void OTAUpdateComponent::task_set_status(const char *msg) {
  strncpy(this->task_status_buf_, msg, sizeof(this->task_status_buf_) - 1);
  this->task_status_buf_[sizeof(this->task_status_buf_) - 1] = '\0';
  this->task_status_pending_ = true;
}

void OTAUpdateComponent::loop() {
  uint32_t now = millis();

  // Publish status from background task (thread-safe: only loop() calls publish_sensor)
  if (this->task_status_pending_) {
    this->task_status_pending_ = false;
    this->status_ = this->task_status_buf_;
    this->publish_sensor("OTA Status", this->status_);
  }

  // Handle background task results
  if (this->result_ == RES_SUCCESS) {
    this->result_ = RES_NONE;
    this->updating_ = false;
    this->phase_ = OTA_PHASE_REBOOTING;
    this->boot_check_done_ = true;
    char buf[80];
    snprintf(buf, sizeof(buf), "Updated %s -> %s, rebooting...",
             this->current_version_.c_str(), this->remote_version_.c_str());
    this->status_ = buf;
    this->publish_sensor("OTA Status", this->status_);
    ESP_LOGI(TAG, "OTA success! Rebooting in 4 seconds...");
    // 模擬板無 reboot breadcrumb 系統（主板專用，追 PC=0 crash）
    delay(4000);
    esp_restart();
  }

  if (this->result_ == RES_FAILED) {
    this->result_ = RES_NONE;
    this->updating_ = false;
    this->phase_ = OTA_PHASE_FAILED;
    char buf[128];
    snprintf(buf, sizeof(buf), "Failed (#%u/%u): %s",
             this->boot_attempts_, MAX_BOOT_RETRIES, this->ota_error_.c_str());
    this->status_ = buf;
    this->publish_sensor("OTA Status", this->status_);
    ESP_LOGE(TAG, "OTA failed (#%u): %s", this->boot_attempts_, this->ota_error_.c_str());
    this->last_check_time_ = now;
  }

  if (this->result_ == RES_NO_UPDATE) {
    this->result_ = RES_NONE;
    this->updating_ = false;
    this->phase_ = OTA_PHASE_UP_TO_DATE;
    this->boot_check_done_ = true;
    if (!this->remote_version_.empty()) {
      this->status_ = "Up to date (v" + this->current_version_ + ", remote=v" + this->remote_version_ + ")";
    } else {
      this->status_ = "Up to date (v" + this->current_version_ + ")";
    }
    this->publish_sensor("OTA Status", this->status_);
    ESP_LOGI(TAG, "No update (current: %s, remote: %s)",
             this->current_version_.c_str(), this->remote_version_.c_str());
  }

  // Boot auto-check with retry
  if (!this->check_on_boot_ || this->boot_check_done_ || this->updating_) return;

  uint32_t wait_ms = (this->boot_attempts_ == 0) ? BOOT_FIRST_DELAY_MS : BOOT_RETRY_INTERVAL_MS;
  if (now - this->last_check_time_ < wait_ms) return;

  auto ips = network::get_ip_addresses();
  if (ips.empty() || !ips[0].is_set()) {
    this->phase_ = OTA_PHASE_WAITING;
    if (now - this->last_check_time_ >= 30000) {
      this->last_check_time_ = now;
      this->status_ = "Waiting for network...";
      this->publish_sensor("OTA Status", this->status_);
      ESP_LOGW(TAG, "OTA waiting for network (attempt %u/%u)", this->boot_attempts_, MAX_BOOT_RETRIES);
    }
    return;
  }

  if (this->boot_attempts_ >= MAX_BOOT_RETRIES) {
    this->boot_check_done_ = true;
    this->phase_ = OTA_PHASE_IDLE;
    char buf[64];
    snprintf(buf, sizeof(buf), "Gave up after %u attempts", this->boot_attempts_);
    this->status_ = buf;
    this->publish_sensor("OTA Status", this->status_);
    ESP_LOGW(TAG, "OTA boot check gave up after %u attempts", this->boot_attempts_);
    return;
  }

  this->boot_attempts_++;
  ESP_LOGI(TAG, "Boot OTA check #%u/%u starting...", this->boot_attempts_, MAX_BOOT_RETRIES);
  this->check_for_update();
}

void OTAUpdateComponent::check_for_update() {
  if (this->updating_) {
    ESP_LOGW(TAG, "OTA already in progress");
    return;
  }
  // Note: device_id_ may be empty — OTA still proceeds (no serial binding required)
  if (this->device_id_.empty()) {
    ESP_LOGW(TAG, "OTA: no device serial, proceeding anyway");
  }
  this->updating_ = true;
  this->phase_ = OTA_PHASE_CHECKING;
  this->progress_pct_ = 0;
  this->status_ = "Checking...";
  this->publish_sensor("OTA Status", this->status_);

  BaseType_t ret = xTaskCreate(ota_task_fn, "ota_upd", 12288, this, 5, &this->ota_task_handle_);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA task!");
    this->ota_error_ = "Task create failed";
    this->result_ = RES_FAILED;
    this->updating_ = false;
  }
}

// ============================================================
// Background task
// ============================================================

static int compare_versions(const char *a, const char *b) {
  int a1 = 0, a2 = 0, a3 = 0;
  int b1 = 0, b2 = 0, b3 = 0;
  sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
  sscanf(b, "%d.%d.%d", &b1, &b2, &b3);
  if (a1 != b1) return a1 - b1;
  if (a2 != b2) return a2 - b2;
  return a3 - b3;
}

void OTAUpdateComponent::ota_task_fn(void *arg) {
  auto *self = (OTAUpdateComponent *)arg;
  self->do_ota_check_and_update();
  self->ota_task_handle_ = nullptr;
  vTaskDelete(nullptr);
}

void OTAUpdateComponent::do_ota_check_and_update() {
  // ---- Step 1: Fetch manifest JSON ----
  this->phase_ = OTA_PHASE_CHECKING;
  this->task_set_status("[1/6] Fetching manifest...");
  ESP_LOGI(TAG, "[1/6] Fetching manifest...");

  char manifest_body[1024];
  memset(manifest_body, 0, sizeof(manifest_body));

  int status = this->http_get(this->manifest_url_, manifest_body, sizeof(manifest_body), nullptr);
  this->tls_disconnect();  // Close manifest connection

  if (status != 200) {
    char buf[80];
    snprintf(buf, sizeof(buf), "[1/6] Manifest fetch HTTP=%d", status);
    this->ota_error_ = buf;
    this->result_ = RES_FAILED;
    return;
  }

  ESP_LOGI(TAG, "Manifest: %s", manifest_body);

  // ---- Step 2: Parse manifest ----
  this->task_set_status("[2/6] Parsing manifest...");

  cJSON *root = cJSON_Parse(manifest_body);
  if (!root) {
    this->ota_error_ = "[2/6] JSON parse failed";
    this->result_ = RES_FAILED;
    return;
  }

  cJSON *jver = cJSON_GetObjectItem(root, "version");
  cJSON *jurl = cJSON_GetObjectItem(root, "url");
  if (!jver || !cJSON_IsString(jver) || !jurl || !cJSON_IsString(jurl)) {
    cJSON_Delete(root);
    this->ota_error_ = "[2/6] Invalid manifest (need version + url)";
    this->result_ = RES_FAILED;
    return;
  }

  this->remote_version_ = jver->valuestring;
  this->firmware_url_ = jurl->valuestring;

  // Check device_id targeting (optional field in manifest)
  cJSON *jdev = cJSON_GetObjectItem(root, "device_id");
  std::string target_device;
  if (jdev && cJSON_IsString(jdev) && strlen(jdev->valuestring) > 0) {
    target_device = jdev->valuestring;
  }
  cJSON_Delete(root);

  ESP_LOGI(TAG, "Remote: %s, current: %s, target: %s, device: %s",
           this->remote_version_.c_str(), this->current_version_.c_str(),
           target_device.empty() ? "(all)" : target_device.c_str(),
           this->device_id_.empty() ? "(none)" : this->device_id_.c_str());

  // ---- Step 3: Check serial + version ----
  // Always show serial check result first (for debugging)
  char sn_buf[200];
  if (!target_device.empty()) {
    snprintf(sn_buf, sizeof(sn_buf), "[3/6] SN: manifest=%s device=%s %s",
             target_device.c_str(),
             this->device_id_.empty() ? "(none)" : this->device_id_.c_str(),
             (target_device == this->device_id_) ? "MATCH" : "MISMATCH");
  } else {
    snprintf(sn_buf, sizeof(sn_buf), "[3/6] SN: (all devices)");
  }
  this->task_set_status(sn_buf);
  ESP_LOGI(TAG, "%s", sn_buf);
  vTaskDelay(pdMS_TO_TICKS(500));  // Let user see SN result briefly

  // If manifest specifies a device_id, only update that device
  if (!target_device.empty() && target_device != this->device_id_) {
    char skip_buf[200];
    snprintf(skip_buf, sizeof(skip_buf), "SN mismatch: manifest=%s, device=%s (v%s remote=v%s)",
             target_device.c_str(), this->device_id_.c_str(),
             this->current_version_.c_str(), this->remote_version_.c_str());
    this->task_set_status(skip_buf);
    ESP_LOGW(TAG, "%s", skip_buf);
    this->result_ = RES_NO_UPDATE;
    return;
  }

  // Show version comparison
  char ver_buf[120];
  snprintf(ver_buf, sizeof(ver_buf), "[3/6] Version: current=v%s remote=v%s",
           this->current_version_.c_str(), this->remote_version_.c_str());
  this->task_set_status(ver_buf);
  ESP_LOGI(TAG, "%s", ver_buf);

  if (compare_versions(this->remote_version_.c_str(), this->current_version_.c_str()) <= 0) {
    this->result_ = RES_NO_UPDATE;
    return;
  }

  ESP_LOGI(TAG, "Update available: %s -> %s", this->current_version_.c_str(), this->remote_version_.c_str());

  // ---- Step 4: Download firmware ----
  this->phase_ = OTA_PHASE_DOWNLOADING;
  this->progress_pct_ = 0;
  this->task_set_status("[4/6] Preparing OTA partition...");
  ESP_LOGI(TAG, "[4/6] Preparing OTA partition...");

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
  if (!update_partition) {
    this->ota_error_ = "[4/6] No OTA partition";
    this->result_ = RES_FAILED;
    return;
  }

  ESP_LOGI(TAG, "Target partition: %s (0x%08x, %uB)",
           update_partition->label, (unsigned)update_partition->address, (unsigned)update_partition->size);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (err != ESP_OK) {
    char buf[64];
    snprintf(buf, sizeof(buf), "[4/6] OTA begin failed: 0x%x", (unsigned)err);
    this->ota_error_ = buf;
    this->result_ = RES_FAILED;
    return;
  }

  // HTTP GET for firmware (body_buf=NULL, we read manually)
  this->task_set_status("[4/6] Connecting to firmware server...");
  ESP_LOGI(TAG, "[4/6] Connecting to firmware server...");

  int content_length = -1;
  status = this->http_get(this->firmware_url_, nullptr, 0, &content_length);

  if (status != 200) {
    this->tls_disconnect();
    esp_ota_abort(ota_handle);
    char buf[80];
    snprintf(buf, sizeof(buf), "[4/6] FW fetch HTTP=%d", status);
    this->ota_error_ = buf;
    this->result_ = RES_FAILED;
    return;
  }

  ESP_LOGI(TAG, "Firmware size: %d bytes", content_length);

  // ---- Step 5: Read firmware and write to flash ----
  // v3.16.49 changes:
  //   chunk 1024 → 4096（減少 4 倍 tls_read syscall 開銷）
  //   hard timeout 120s → 300s（容忍慢速 CDN / 大韌體）
  //   新增 stall detection：連續 30s 沒進度 → 中止（比硬 300s 更精準）
  this->phase_ = OTA_PHASE_FLASHING;
  static const int OTA_CHUNK_SIZE = 4096;
  char *chunk = (char *)malloc(OTA_CHUNK_SIZE);
  if (!chunk) {
    this->tls_disconnect();
    esp_ota_abort(ota_handle);
    this->ota_error_ = "malloc failed";
    this->result_ = RES_FAILED;
    return;
  }

  int total_read = 0;
  bool write_ok = true;
  uint32_t dl_start = millis();
  uint32_t last_data_ms = dl_start;  // v3.16.49: 最後一次成功讀到資料的時間

  while (true) {
    uint32_t now = millis();
    // Hard timeout: 300 seconds for entire download (v3.16.49: 120→300)
    if (now - dl_start > 300000) {
      this->ota_error_ = "Download timeout (300s)";
      write_ok = false;
      break;
    }
    // Stall detection: 30 seconds without any data (v3.16.49 新增)
    if (now - last_data_ms > 30000) {
      this->ota_error_ = "Download stalled (no data 30s)";
      write_ok = false;
      break;
    }

    ssize_t read_len = this->tls_read(chunk, OTA_CHUNK_SIZE);
    if (read_len < 0) {
      if (errno == EAGAIN) { vTaskDelay(1); continue; }
      this->ota_error_ = "FW read error";
      write_ok = false;
      break;
    }
    if (read_len == 0) break;  // EOF
    last_data_ms = now;  // v3.16.49: 重設 stall timer

    err = esp_ota_write(ota_handle, chunk, read_len);
    if (err != ESP_OK) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Write failed @%d: 0x%x", total_read, (unsigned)err);
      this->ota_error_ = buf;
      write_ok = false;
      break;
    }

    total_read += read_len;

    if (content_length > 0) {
      int new_pct = (int)((int64_t)total_read * 100 / content_length);
      // Update status every 10% or on first chunk
      if (new_pct / 10 != this->progress_pct_ / 10 || this->progress_pct_ == 0) {
        char pct_buf[64];
        snprintf(pct_buf, sizeof(pct_buf), "[5/6] Downloading: %d%% (%dKB/%dKB)",
                 new_pct, total_read / 1024, content_length / 1024);
        this->task_set_status(pct_buf);
      }
      this->progress_pct_ = new_pct;
    }

    if (total_read % (50 * 1024) < 1024) {
      ESP_LOGI(TAG, "OTA: %d/%d bytes (%d%%)", total_read, content_length, this->progress_pct_);
    }
  }

  free(chunk);
  this->tls_disconnect();

  if (!write_ok) {
    esp_ota_abort(ota_handle);
    this->result_ = RES_FAILED;
    return;
  }

  ESP_LOGI(TAG, "Download complete: %d bytes in %us", total_read, (millis() - dl_start) / 1000);

  this->task_set_status("[6/6] Verifying firmware...");
  ESP_LOGI(TAG, "[6/6] Verifying firmware...");

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    char buf[64];
    snprintf(buf, sizeof(buf), "[6/6] Verify failed: 0x%x", (unsigned)err);
    this->ota_error_ = buf;
    this->result_ = RES_FAILED;
    return;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    char buf[64];
    snprintf(buf, sizeof(buf), "[6/6] Set boot failed: 0x%x", (unsigned)err);
    this->ota_error_ = buf;
    this->result_ = RES_FAILED;
    return;
  }

  ESP_LOGI(TAG, "OTA successful! New boot: %s", update_partition->label);
  this->result_ = RES_SUCCESS;
}

void OTAUpdateComponent::publish_sensor(const std::string &name, const std::string &value) {
  for (auto *obj : App.get_text_sensors()) {
    if (obj->get_name() == name) {
      obj->publish_state(value);
      return;
    }
  }
}

}  // namespace ota_update
}  // namespace esphome
