# HuiFlow 洗車機模擬板 — 技術規格 (v0.8.2)

> 完整版本演進見 [`CHANGELOG.md`](../CHANGELOG.md)

## Purpose

以 ESP32-S3 模擬板取代「真實洗車機 + RPi WS server」，讓主板 v4.0.68 狀態機可在 3~5 秒內跑完一輪流程、每小時累積上千次，暴露 WS 重連幻象 / NVS 恢復 / destructive reset 邊界等 race condition。

## Hardware

### 模擬板
- **MCU**：ESP32-S3-DevKitC-1（同主板型號，可沿用 toolchain）
- **網路**：WiFi STA（AP `huiflow`, psk `#EDCxu.6@@`）
- **固定 IP**：`192.168.1.10/24`，gw `192.168.1.1`

### 對主板接線（4 條 + GND，腳位 1:1 對齊）

| 模擬板 GPIO | 方向 | 主板 GPIO | 主板 entity | 電氣 |
|------------|------|-----------|-------------|------|
| 21 | OUTPUT | 21 | `washer_led_blue` | LOW=ON / HIGH=OFF |
| 47 | OUTPUT | 47 | `washer_led_green` | 同上 |
| 35 | OUTPUT | 35 | `washer_led_red` | 同上（GPIO 35 必須不設 flash_size: 16MB）|
| 1 | INPUT (UART2 RX) | 1 (UART2 TX) | 監聽洗車機指令 | 9600 8N1 |
| GND | — | GND | 共地 | — |

**極性**：主板 `INPUT_PULLUP + inverted:true` → 模擬板 LOW = 主板讀 ON。  
**Boot 預設**：3 顆 LED 全 HIGH = 主板讀「沒電」`WP_NOPOWER`，安全預設。  
**不接**：EMG/GO 按鈕線（用 WS CMD `go_enable` / `emergency_enable` 軟體觸發）。

## Network Protocol

### WebSocket Server (port 8090)
- Path: `GET /ws?device_id=<id> HTTP/1.1`
- 標準 RFC 6455 handshake (SHA1+base64)
- TCP 純明文，無 TLS、無認證
- Frame: client→server 必 masked，server→client 不 mask
- WS ping (0x09)/pong (0x0A) 每 10s（主板送）
- 應用層 `{"type":"PING"}`/`{"type":"PONG"}` 每 5s（模擬板送）
- 15s idle timeout 自動斷線

### CMD（模擬板 → 主板）
- 系統：`status` / `reboot` / `getsn` / `check_ota`
- 洗車流程：`wash_init` (含 `total`/`steps`/`unlock_pwd`) / `wash_unlock` / `wash_goto` / `wash_reset` / `wash_stop` / `wash_start`
- UART2 派發：`startwasher1~6` / `gowasher` / `resetwasher` / `hardresetwasher` / `sparewasher`

### EVENT（主板 → 模擬板）
- `wash_stage`: 步驟轉換 (8~10 欄位)
- `wash_unlock` / `wash_nopower` (v4.0.67+)
- `washer_status`: LED 顏色變化
- `go_enable` / `emergency_enable` / `emergency_stop`: 按鈕
- `ac_power_off` / `car_over_height` / `leak_detected` / `water_level_ok`: I/O

## UART2 Frame Format

主板送 7-byte frame：`FD 03 C7 50 <CMD> <CHK> DF`

| CMD byte | 指令 |
|----------|------|
| 0x82 | startwasher1 |
| 0x88 | startwasher2 |
| 0x81 | startwasher3 |
| 0x84 | startwasher4 |
| 0x8C | startwasher5 |
| 0x83 | startwasher6 |
| 0x85 | gowasher |
| 0x8A | resetwasher |
| 0x89 | hardresetwasher |
| 0x90 | sparewasher |

主板每個 CMD 連發 5 次，間隔 100ms。模擬板 parser 在 500ms 內收到同 CMD 5 次 → 視為 1 次有效指令（dedupe）。

## Components

```
my_components/
├── sim_fsm/          # LED 驅動 + UART2 RX + scenario 引擎 + 故障注入 + batch runner
├── sim_ws_server/    # port 8090 WS server + 輕量 EVENT parser
├── sim_assertion/    # 規則引擎 + per-rule stats + result CSV streaming
└── ota_update/       # Cloud OTA (從主板 repo 移植，移除 reboot tag breadcrumb)
```

### sim_fsm 功能
- LED OUTPUT 控制（R/G/B 互斥）
- UART2 9600 8N1 RX，7-byte frame state machine parser
- 5 次連發去重（500ms 視窗）
- Scenario 引擎：SW5 (12 步) / SW6 (18 步)
- speed_factor (0.05~2.0x) 壓縮時間
- BLUE 階段 wait_gowasher → 等 UART2 0x85 推進
- 故障注入：FLICKER / POWER_LOSS / RED_BURST 機率 0~100%
- Batch runner：連跑 N 輪 + inter_run_ms 間隔
- batch_round_callback hook → YAML 注入 wash_init / wash_reset

### sim_ws_server 功能
- raw TCP + WebSocket handshake
- frame codec (text / ping / pong / close)
- 應用層 PING/PONG 心跳
- 輕量 EVENT parser（strstr 不依賴 cJSON）
- 抽 type / event / wash_phase / wash_stage / wash_total
- on_message / on_connection_change / on_parsed_event 三個 callback

### sim_assertion 功能
- expect_event(rule_id, event, phase, total, window_ms) 入隊期待
- observe_event(event, phase, stage, total) 配對 / 標記 pass
- loop() 檢查超時 → 標記 fail
- per-rule stats（pass/fail/avg_ms）
- result CSV streaming（每筆 result 推到 SSE）
- 環形 buffer 100 筆 results

## Assertion Rules (v0.6.0)

| ID | 規則 | 觸發來源 | 期待 EVENT | 視窗 |
|----|------|---------|-----------|------|
| A1 | LED→GREEN | sim_fsm.set_led | `wash_stage` phase="green" | 500ms |
| A2 | LED→BLUE | sim_fsm.set_led | `wash_stage` phase="blue" | 500ms |
| A3 | LED→RED | sim_fsm.set_led | `wash_stage` wash_total=0 (destructive reset) | 1000ms |
| A4 | LED→OFF | sim_fsm.set_led | `wash_nopower` | 1500ms |
| A5 | wash_init unlock_pwd | Test A5 button | `wash_stage` phase="lock" | 1000ms |
| A6 | wash_unlock CMD | Test A6 button | EVENT `wash_unlock` | 1000ms |
| A7 | UART2 5-frame burst | (Phase 7 待加) | — | — |
| A8 | WS disconnect | Test A8 / batch | reconnect | 60000ms |
| A9 | reboot CMD | Test A9 button | reconnect | 30000ms |

## Web Dashboard

URL: `http://192.168.1.10/`

### 第一排 (sticky top)
- LED 色 / WS Client 連線 / Scenario+step / Batch 進度
- Main Phase / Main Step / Last EVENT / Last UART CMD
- **Assertion PASS:N / FAIL:N / pend:N / fault:N**
- WS RX/TX frame 數

### 第二排 (sticky, 紅底)
- 最近一筆 fail summary（無 fail 時隱藏）

### 第三排
- **Assertion Results 滾動表**（最近 100 筆，PASS 綠 / FAIL 紅）
- 欄位：# / 時間 / 規則 / P/F / actual / window / detail
- 「📥 CSV」按鈕：下載完整 buffer 為 CSV
- 「🗑 Clear」按鈕：清空表

### 控制按鈕
- LED 手動切換 (OFF/RED/GREEN/BLUE)
- 直接 CMD 測試 (status/reboot/wash_reset/startwasher5)
- Scenario 控制 (Run SW5 @0.1x/@1.0x、Run SW6 @0.1x、Stop Scenario)
- **Batch (Batch SW5 x10 @0.2x、Batch SW5 x100 @0.2x、Stop Batch)**
- **Assertion 測試 (Reset Stats、Test A5、Test A6、Test A8、Test A9)**
- WS 控制 (Disconnect Client)
- OTA (Check OTA Now)

### 故障注入 sliders (number entity)
- Fault Flicker Prob (%): 0~100
- Fault Power Loss Prob (%): 0~100
- Fault Red Burst Prob (%): 0~100

## OTA

- Manifest URL: https://raw.githubusercontent.com/frankiehex/H_OTA_SIM/main/manifest.json
- Firmware URL: https://raw.githubusercontent.com/frankiehex/H_OTA_SIM/main/firmware.ota.bin
- 每次開機 8s 後自動檢查 (check_on_boot)、失敗重試 5 次每 30s
- Dashboard 「Check OTA Now」按鈕手動觸發

## Phase 進度

| Phase | 內容 | 狀態 |
|-------|------|------|
| 1 | 骨架 + WiFi + LED 手控 + WS handshake | ✅ v0.1.0 |
| 1.5 | OTA 元件 + H_OTA_SIM repo | ✅ v0.1.0 |
| 2 | UART2 parser + scenario 引擎 + EVENT parser | ✅ v0.2.0 |
| 3 | Assertion 引擎 (A1~A4) + 故障注入 + batch runner | ✅ v0.3.0 |
| 4 | Result CSV streaming + Dashboard rolling table + A5 | ✅ v0.4.0 |
| 5 | Smart batch (wash_init auto-inject) + A8 | ✅ v0.5.0 |
| 6 | Per-rule stats + A6 + A9 | ✅ v0.6.0 |
| 7 | A7 UART2 burst | ✅ v0.7.0 |
| 7.1 | A8/A9 timing fix (globals 2-stage expect) | ✅ v0.7.1 |
| 8 | Self-Test script 一鍵串 A5/A6/A8/A9 | ✅ v0.8.0 |
| 8.1 | min_actual_ms guard 防 stale fast-PASS | ✅ v0.8.1 |
| 8.2 | rx_accum stuck 防護 + sim restart 按鈕 | ✅ v0.8.2 (current) |
| 9 | 1000-round stress test + per-rule histogram | TODO |

## Resources

| 版本 | RAM | Flash | Build time |
|------|-----|-------|-----------|
| v0.1.0 | 11.0% | 42.7% | 38s |
| v0.2.0 | 11.0% | 47.8% | — |
| v0.3.0 | ~12% | ~50% | 21s |
| v0.4.0 | ~12% | ~50% | 11s |
| v0.5.0 | ~12% | ~50% | 13s |
| v0.6.0 | ~12% | ~50% | 12s |
