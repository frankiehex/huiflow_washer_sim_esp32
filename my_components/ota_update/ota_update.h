#pragma once

#include "esphome/core/component.h"
#include <string>

extern "C" {
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
}

namespace esphome {
namespace ota_update {

// OTA phase — exposed publicly for LED status in YAML lambda
enum OTAPhase {
  OTA_PHASE_IDLE = 0,       // Not doing anything
  OTA_PHASE_WAITING,        // Waiting for network / retry delay
  OTA_PHASE_CHECKING,       // Fetching manifest
  OTA_PHASE_DOWNLOADING,    // Downloading firmware
  OTA_PHASE_FLASHING,       // Writing to flash
  OTA_PHASE_REBOOTING,      // Success, about to reboot
  OTA_PHASE_FAILED,         // Last attempt failed (will retry)
  OTA_PHASE_UP_TO_DATE,     // No update available
};

class OTAUpdateComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override;

  void set_manifest_url(const std::string &url) { this->manifest_url_ = url; }
  void set_current_version(const std::string &ver) { this->current_version_ = ver; }
  void set_check_on_boot(bool check) { this->check_on_boot_ = check; }
  void set_device_id(const std::string &id) { this->device_id_ = id; }
  void set_manifest_token(const std::string &token) { this->manifest_token_ = token; }

  // Callable from lambda
  void check_for_update();
  bool is_updating() const { return this->updating_; }
  OTAPhase get_phase() const { return this->phase_; }
  int get_progress_pct() const { return this->progress_pct_; }
  const std::string &get_status() const { return this->status_; }
  const std::string &get_remote_version() const { return this->remote_version_; }

  // Publish to text_sensor (set from YAML lambda)
  void publish_sensor(const std::string &name, const std::string &value);

 protected:
  // Background task for OTA (blocking HTTP + flash writes)
  static void ota_task_fn(void *arg);
  void do_ota_check_and_update();
  void task_set_status(const char *msg);  // thread-safe status update from bg task

  // Raw socket + mbedTLS helpers
  bool parse_url(const std::string &url, std::string &host, uint16_t &port, std::string &path, bool &use_ssl);
  bool tls_connect(const std::string &host, uint16_t port);
  void tls_disconnect();
  ssize_t tls_write(const void *buf, size_t len);
  ssize_t tls_read(void *buf, size_t len);
  int http_get(const std::string &url, char *body_buf, int body_cap, int *content_length_out);

  std::string manifest_url_;
  std::string current_version_;
  std::string device_id_;
  std::string manifest_token_;
  bool check_on_boot_{true};

  // State
  bool updating_{false};
  volatile OTAPhase phase_{OTA_PHASE_IDLE};
  volatile int progress_pct_{0};
  std::string status_{"Idle"};
  std::string remote_version_;
  std::string firmware_url_;

  // Thread-safe status buffer: background task writes here, loop() publishes
  char task_status_buf_[128]{};
  volatile bool task_status_pending_{false};

  // Boot retry
  uint32_t boot_time_{0};
  uint32_t last_check_time_{0};
  uint8_t boot_attempts_{0};
  bool boot_check_done_{false};

  // OTA task result (set by background task, read by loop)
  enum OTAResult { RES_NONE, RES_SUCCESS, RES_FAILED, RES_NO_UPDATE };
  volatile OTAResult result_{RES_NONE};
  std::string ota_error_;

  TaskHandle_t ota_task_handle_{nullptr};

  // Raw socket + mbedTLS
  int sock_fd_{-1};
  bool ssl_active_{false};
  mbedtls_ssl_context ssl_;
  mbedtls_ssl_config ssl_conf_;
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context ctr_drbg_;
  bool mbedtls_initialized_{false};
  std::string connected_host_;  // for SNI

  static const uint8_t MAX_BOOT_RETRIES = 5;
  static const uint32_t BOOT_FIRST_DELAY_MS = 8000;   // First check 8s after boot
  static const uint32_t BOOT_RETRY_INTERVAL_MS = 30000; // Retry every 30s
};

}  // namespace ota_update
}  // namespace esphome
