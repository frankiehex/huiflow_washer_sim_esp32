# HuiFlow 洗車機模擬板（Washer Simulator Board）

ESP32-S3 模擬板，取代真實洗車機 + RPi WS server，快速壓力測試 HuiFlow 主板 v4.0.68 狀態機。

## 用途

在真實洗車機上跑一輪流程 3~10 分鐘，一小時只能測 ~20 次。這塊模擬板讓同一輪流程壓縮到 **3~5 秒**，30 分鐘內可跑完 1000 輪，暴露主板的偶發 race condition：

- WS 重連幻象（LAN WS 斷線→重連期間訊息遺失）
- NVS 洗車狀態恢復邊界
- destructive reset 時序（`wash_init` 進 RED 時）
- `wash_nopower` 快速切換邊緣
- `wash_paused` 旗標吸收色變邊緣

## 硬體

| 項目 | 規格 |
|------|------|
| MCU | ESP32-S3-N16R8 |
| 網路 | WiFi STA，固定 IP `192.168.1.10/24`，gw `192.168.1.1` |
| 對主板接線 | 4 條 + GND（3 LED 輸出 + UART2 RX） |

詳見 [`docs/spec.md`](docs/spec.md)。

## 開發

```bash
# 驗證配置
esphome config iot-washer-sim-base.yaml

# 編譯
esphome compile iot-washer-sim-base.yaml

# 燒錄（OTA，必須先連得上 192.168.1.10）
esphome upload iot-washer-sim-base.yaml --device 192.168.1.10
```

## Phase 進度

- **Phase 1（當前）**：骨架 + WiFi + LED 手動控制 + WS server handshake
- Phase 2：scenario 腳本 + EVENT parser
- Phase 3：故障注入 + assertion 引擎
- Phase 4：OTA + 壓力測試

規劃見 `/Users/f/.claude/plans/washer-sim-ip-10-gw-1-humming-reef.md`。
