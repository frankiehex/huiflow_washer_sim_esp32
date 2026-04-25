# 快速使用指南

## 5 分鐘上手

### 1. 接線（5 條）
sim board ↔ main board，杜邦線 GPIO 號碼 1:1 對接：

| sim | main |
|-----|------|
| GPIO 21 | GPIO 21 (washer_led_blue) |
| GPIO 47 | GPIO 47 (washer_led_green) |
| GPIO 35 | GPIO 35 (washer_led_red) |
| GPIO 1 | GPIO 1 (UART2 TX) |
| GND | GND |

兩塊板各自獨立供電（USB / DC）。

### 2. 上電
- sim 開機後 8 秒會自動檢查 OTA 是否有新版
- WiFi 自動連 `huiflow`，固定 IP `192.168.1.10`
- 主板自動連 sim WS server，~3-5 秒看到 dashboard 上 `WS Client: CONNECTED`

### 3. 打開 Dashboard
瀏覽器開 `http://192.168.1.10/`

頂部狀態條 8 欄即時更新：
- LED / WS Client / Scenario / Batch / Main Phase / Last EVENT / Last UART CMD / Assertion 統計

### 4. 一鍵測試（不需手動操作）
按 「**Run Self-Test (A5+A6+A8+A9)**」按鈕
- 約 2 分鐘跑完 4 條 assertion
- 不需洗車流程互動，純 WS CMD 路徑驗證
- 跑完底部 `Self Test Status` 顯示 `DONE: P pass / F fail | per-rule: ...`

### 5. 手動測試（接好線後）

#### 測試 LED → main board GPIO 通路
1. 按 `LED → GREEN` → main board SSE 應立即顯示 `washer_led_green: ON`
2. 按 `LED → BLUE` 同理
3. 按 `LED → OFF` 三色全暗

#### 測試完整流程（需主板 wash_init 設定步驟）
1. 按 `Send CMD startwasher5` → 模擬板送 CMD 給主板
2. 主板 UART2 送 startwasher5 frame 給模擬板
3. 模擬板 `Last UART CMD` 顯示 `startwasher5`，`Faults Injected` 增加 1 if A7 fail
4. 按 `Run SW5 @0.1x` → 模擬板自己跑 SW5 12 步，每步 50ms

#### 跑 100 輪 batch
1. 按 `Reset Assertion Stats`
2. 按 `Batch Run SW5 x100 @0.2x`
3. 約 5-10 分鐘跑完
4. 看 `Assertion PASS:N FAIL:N` 統計
5. 按 `📥 CSV` 下載完整 results 表

#### 故障注入壓力測試
1. 拉 `Fault Red Burst Prob` 滑桿到 30%
2. 按 `Batch Run SW5 x100 @0.2x`
3. 觀察 destructive reset (A3) 命中率

## 常見問題

### Dashboard 顯示 `WS Client: disconnected`
- 主板可能沒在線 → ping 192.168.1.18 確認
- 主板 secrets.yaml 的 `ws_lan_server` 必須是 `192.168.1.10`

### `assertion_pass` / `assertion_fail` 都 0
- 沒按過任何 Test 按鈕，assertion 引擎沒事可做
- 按 Test A5/A6/A8/A9 或 Run Self-Test 開始

### LED 切換 sim 改了但主板沒反應
- 杜邦線可能脫落 → 萬用表測通斷
- GND 沒接（最常忘）→ 兩板必須共地
- main board 主板 fw 是否 v4.0.68+ → `curl http://192.168.1.18/text_sensor/firmware_version`

### v0.7.0 之前 A8/A9 顯示 39ms 即 PASS
- v0.7.0 已修：升級到 v0.7.1+

### A5/A6 fail 但 A8/A9 pass
- 主板 v4.0.74 行為差異：wash_unlock 在 not_locked 狀態下不發 EVENT（這是設計）
- A6 必須在已 lock 狀態下才能驗證

## OTA 升級

按 Dashboard `Check OTA Now` 會拉取 `frankiehex/H_OTA_SIM/main/manifest.json`，  
有新版自動下載 + 重啟。

手動推新版到 OTA repo：
```bash
cd /Users/f/huiflow_washer_sim_esp32
# 改 SIM_FIRMWARE_VER 後
esphome compile iot-washer-sim-base.yaml
cp .esphome/build/iot-washer-sim/.pioenvs/iot-washer-sim/firmware.ota.bin ota/firmware.ota.bin
# 改 ota/manifest.json 的 version
cp ota/firmware.ota.bin /Users/f/H_OTA_SIM/
cp ota/manifest.json /Users/f/H_OTA_SIM/
cd /Users/f/H_OTA_SIM && git add -A && git commit -m "release: vX.Y.Z" && git push
```

## 進階

完整 9 條 assertion + 故障模式 + scenario 細節見 [`docs/spec.md`](spec.md)。  
所有版本演進見 [`CHANGELOG.md`](../CHANGELOG.md)。
