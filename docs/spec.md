# HuiFlow 洗車機模擬板 — 技術規格

## Purpose

以 ESP32-S3 模擬板取代「真實洗車機 + RPi WS server」，讓主板 v4.0.68 狀態機可在 3~5 秒內跑完一輪流程、每小時累積上千次，暴露 WS 重連幻象 / NVS 恢復 / destructive reset 邊界等 race condition。

## Hardware

### 模擬板
- **MCU**：ESP32-S3-DevKitC-1（同主板型號，可沿用 toolchain）
- **網路**：WiFi STA（AP `huiflow`, psk `#EDCxu.6@@`）
- **固定 IP**：`192.168.1.10/24`，gw `192.168.1.1`

### 對主板接線（4 條 + GND）

| 模擬板 GPIO | 方向 | 主板 GPIO | 主板 entity | 電氣意義 |
|------------|------|-----------|-------------|----------|
| 6 | OUTPUT | GPIO21 | `washer_led_blue` | 模擬板 LOW → 主板讀 ON |
| 7 | OUTPUT | GPIO47 | `washer_led_green` | 同上 |
| 8 | OUTPUT | GPIO35 | `washer_led_red` | 同上 |
| 15 | INPUT (UART2 RX) | GPIO1 (UART2 TX) | 監聽洗車機指令 | 9600 8N1 |
| GND | — | GND | 共地 | — |

**不接**：EMG / GO 按鈕線（主板實體按鈕可手動觸發，或透過 WS CMD `go_enable` / `emergency_enable` 軟體觸發）。

### 主板側極性對照（主板 `iot-washer-base.yaml:1665-1699`）

3 顆 LED 偵測引腳：`mode: INPUT_PULLUP` + `inverted: true` → 物理 LOW 讀作 ON，HIGH 讀作 OFF。  
所以模擬板**輸出 LOW = 主板讀 LED ON**。

### Boot 安全預設
模擬板上電時三個 LED GPIO 全部拉 HIGH（LED OFF）= 主板讀「洗車機沒電」，進 `WP_NOPOWER`，不誤觸。

## Network Protocol

### 主板 → 模擬板（WebSocket client）
- Path: `GET /ws?device_id=<id> HTTP/1.1`
- Upgrade headers: `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key: <b64>`, `Sec-WebSocket-Version: 13`
- TCP：純明文（無 TLS），port 8090
- Frame：RFC 6455，client→server 必 masked，server→client 不 mask
- Ping/Pong：雙層
  - WS 層 opcode 0x09（ping）↔ 0x0A（pong）：每 10s 一次
  - 應用層 `{"type":"PING"}` ↔ `{"type":"PONG"}`：每 5s 一次（Phase 1 模擬板主動發）

### 主板送 EVENT（`{"type":"EVENT","event":"<name>",...}`）
完整 schema 見主板 `my_components/ws_client/ws_client.cpp` 對應函數：
- `wash_stage`：步驟變化 / 相位切換（欄位最多，8~10 個）
- `wash_unlock`：解鎖事件（v4.0.67+）
- `wash_nopower`：LED 全滅事件（v4.0.67+）
- `washer_status`：LED 顏色上報（state + color）
- `go_enable` / `emergency_enable` / `emergency_stop`：按鈕事件
- `ac_power_off` / `car_over_height` / `leak_detected` / `water_level_ok`：I/O 事件

### 模擬板送 CMD（`{"type":"CMD","cmd":"<name>",...}`）
- 系統：`status`、`reboot`、`getsn`、`check_ota`
- 洗車流程：`wash_init` + `total`/`ids`/`unlock_pwd`、`wash_unlock`、`wash_goto`+`arg`、`wash_reset`、`wash_stop` / `wash_start`
- UART2 洗車機指令：`startwasher1~6`、`gowasher`、`resetwasher`、`hardresetwasher`、`sparewasher`
- 按鈕狀態：`go_enable`+`arg`+`ms`、`emergency_enable`+`on`+`ms`

CMD 回覆格式：`{"type":"CMD_RESULT","cmd":"xxx","result":"OK:detail"}` 或 `"ERR:reason"`。

## UART2 Frame Format

主板 `ws_client.cpp:77-88` WASHER_TABLE，7-byte frame：
```
FD 03 C7 50 <CMD> <CHK> DF
```

CMD byte 對應表：

| CMD | 指令 | CHK |
|-----|------|-----|
| 0x82 | startwasher1 | 0x4A |
| 0x88 | startwasher2 | 0x4B |
| 0x81 | startwasher3 | 0x4A |
| 0x84 | startwasher4 | 0x4B |
| 0x8C | startwasher5 | 0x4B |
| 0x83 | startwasher6 | 0x4B |
| 0x85 | gowasher | 0x4B |
| 0x8A | resetwasher | 0x4B |
| 0x89 | hardresetwasher | 0x43 |
| 0x90 | sparewasher | 0x3C |

**發送行為**：主板每個 CMD 連發 5 次，間隔 100ms。模擬板 parser 在 500ms 內收到同一 CMD 5 次 → 視為 1 次有效指令。

## Component Structure

```
my_components/
├── sim_fsm/          # LED 驅動 + UART2 RX（Phase 2 加 scenario + 故障注入）
├── sim_ws_server/    # port 8090 WS server + 協議互通
└── sim_assertion/    # Phase 3：9 條 assertion 規則引擎
```

## Phase 1 Scope（當前）

- [x] Repo 骨架 + WiFi + 固定 IP
- [x] 3 LED GPIO 控制（Dashboard 按鈕手動切 OFF/RED/GREEN/BLUE）
- [x] UART2 RX 收 byte 並顯示 hex dump
- [x] WS server handshake + 基本 frame codec + ping/pong
- [x] Dashboard 頂部狀態條（LED 色 / WS client 連線 / RX/TX 統計）
- [x] 按鈕送測試 CMD（status / reboot / wash_reset / startwasher5 / disconnect）

## Phase 2+（未實作）

- Phase 2：scenario 腳本（SW5 6 步 / SW6 9 步，可壓縮時間）+ EVENT parser
- Phase 3：故障注入（flicker / power_loss / red_burst）+ 9 條 assertion 規則
- Phase 4：OTA（`frankiehex/H_OTA_SIM` repo）+ 壓力測試 1000 輪
