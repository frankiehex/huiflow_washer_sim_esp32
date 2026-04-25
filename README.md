# HuiFlow 洗車機模擬板（Washer Simulator Board）

ESP32-S3 模擬板，取代真實洗車機 + RPi WS server，快速壓力測試 HuiFlow 主板狀態機（v4.0.68+）。

## 用途

真實洗車機跑一輪流程 3~10 分鐘，一小時只能測 ~20 次。模擬板讓同一輪壓縮到 **3~5 秒**，30 分鐘跑 1000 輪，自動檢查時序 / 配對 / fail：

- WS 重連幻象（LAN WS 斷線→重連期間訊息遺失）
- NVS 洗車狀態恢復邊界
- destructive reset 時序（`wash_init` 進 RED 時）
- `wash_nopower` 快速切換邊緣
- `wash_paused` 旗標吸收色變邊緣
- UART2 frame 連發 5 次 / 100ms 間隔的時序壓力

## 硬體（4 條 + GND，腳位 1:1 對齊主板）

| 模擬板 GPIO | 方向 | 主板 GPIO | 主板 entity |
|------------|------|-----------|-------------|
| 21 | OUT | 21 | washer_led_blue |
| 47 | OUT | 47 | washer_led_green |
| 35 | OUT | 35 | washer_led_red |
| 1 | IN (UART2 RX) | 1 (TX) | UART2 9600 8N1 |
| GND | — | GND | 共地 |

主板 INPUT_PULLUP + inverted → 模擬板 LOW = 主板讀「LED ON」。  
固定 IP `192.168.1.10/24`，WiFi `huiflow`，WS server `0.0.0.0:8090`，無認證。

## Assertion 規則 (v0.7.1)

| ID | 觸發 | 期待 | 視窗 |
|----|------|------|------|
| A1 | 模擬板 LED→GREEN | 主板 EVENT wash_stage phase="green" | 500ms |
| A2 | 模擬板 LED→BLUE | 主板 EVENT wash_stage phase="blue" | 500ms |
| A3 | 模擬板 LED→RED | wash_stage wash_total=0 (destructive reset) | 1000ms |
| A4 | 模擬板 LED→OFF | EVENT wash_nopower | 1500ms |
| A5 | wash_init unlock_pwd | wash_stage phase="lock" | 1000ms |
| A6 | lock then wash_unlock | EVENT wash_unlock | 1500ms |
| A7 | UART2 收到 CMD | 5 frames within 600ms, intervals 60-150ms | 100ms |
| A8 | sim 主動 disconnect | 主板重連 | 60000ms |
| A9 | sim 送 reboot CMD | 主板 reboot + reconnect | 30000ms |

## Dashboard

`http://192.168.1.10/`，三層即時面板：

- **頂部 sticky**：LED / WS Client / Scenario / Batch / Main Phase / Last EVENT / Last UART CMD / **Assertion PASS:N FAIL:N pend:N fault:N** / RX/TX
- **第二排（紅底，動態顯示）**：最近一筆 fail summary
- **第三排**：Assertion Results 滾動表（最近 100 筆）+ 「📥 CSV 下載」+ 「🗑 Clear」

控制按鈕涵蓋：手動 LED 切換 / CMD 測試 / Scenario 控制 / **Batch x10/x100** / Test A5/A6/A8/A9 / 故障注入 sliders 0-100% (flicker/power_loss/red_burst)。

## 開發

```bash
# 驗證
esphome config iot-washer-sim-base.yaml

# 編譯
esphome compile iot-washer-sim-base.yaml

# 燒錄（首次 USB）
esphome run iot-washer-sim-base.yaml --device /dev/cu.usbmodemXXX

# 後續 OTA
esphome upload iot-washer-sim-base.yaml --device 192.168.1.10
```

## OTA

獨立 repo `frankiehex/H_OTA_SIM` 存 manifest + firmware：
- Manifest: https://raw.githubusercontent.com/frankiehex/H_OTA_SIM/main/manifest.json
- Firmware: https://raw.githubusercontent.com/frankiehex/H_OTA_SIM/main/firmware.ota.bin

每次開機 8s 後自動檢查；Dashboard「Check OTA Now」手動觸發。

## Phase 進度

| Phase | 內容 | 版本 |
|-------|------|------|
| 1 | 骨架 + WiFi + LED 手控 + WS handshake + OTA | v0.1.0 |
| 2 | UART2 parser + scenario 引擎 + EVENT parser | v0.2.0 |
| 3 | Assertion (A1-A4) + 故障注入 + batch runner | v0.3.0 |
| 4 | Result CSV streaming + Dashboard rolling table + A5 | v0.4.0 |
| 5 | Smart batch (auto wash_init) + A8 | v0.5.0 |
| 6 | A6 + A9 + per-rule stats + 文件刷新 | v0.6.0 |
| 7 | A7 UART2 burst + A8/A9 timing fix | v0.7.1 (current) |

完整規格見 [`docs/spec.md`](docs/spec.md)。Plan 見 `~/.claude/plans/washer-sim-ip-10-gw-1-humming-reef.md`。
