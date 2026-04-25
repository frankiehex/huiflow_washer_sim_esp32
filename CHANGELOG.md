# Changelog

## v0.8.2 (current) — 2026-04-25

- **sim_ws_server**：rx_accum_ 超 16KB 無法解 frame 自動 close + reconnect（防 stale 半開連線）
- **新增** Sim Restart 按鈕（debug 用，主板互動卡住時重置）
- **記錄** v4.0.74 主板互動偶發 ws_rx_frames 不增（待後續觀察）

## v0.8.1 — 2026-04-25

- **assertion** 加 `min_actual_ms` 防 stale fast-PASS：
  - A8 min=500ms（主板 backoff 至少 500ms）
  - A9 min=3000ms（reboot 至少 3 秒）
- 實機驗證：A8=5095ms ✅、A9=15058ms ✅（真實重連時間）

## v0.8.0 — 2026-04-25

- **新增** ESPHome script `run_self_test`：一鍵串接 A5→A6→A8→A9（~2 分鐘）
- **text_sensor** "Self Test Status" 顯示每階段進度
- **button** "Run Self-Test" 觸發

## v0.7.1 — 2026-04-25

- **修復** A8/A9 expect 時序 race：用 globals 兩階段（先 disconnect 後 expect）

## v0.7.0 — 2026-04-25

- **新增** A7 UART2 burst rate assertion：5 frames within 600ms, intervals 60-150ms
- **sim_fsm** dispatch_washer_cmd_ 分離 burst tracking 與 dedupe
- 1s 無新 frame 自動 flush 不完整 burst

## v0.6.0 — 2026-04-25

- **新增** A6 wash_unlock + A9 reboot+reconnect assertion
- **per-rule stats** RuleStats { pass, fail, total_actual_ms } map
- **per_rule_summary()** 輸出 "A1:5/0@320ms|A2:3/2@420ms|..."
- **docs/spec.md** 完整 v0.6.0 規格刷新

## v0.5.0 — 2026-04-25

- **smart batch**：sim_fsm 加 OnBatchRoundCb，YAML lambda 起始注入 wash_init+startwasher5、結束送 wash_reset
- **A8 WS reconnect** assertion（自主 disconnect → 60s 內主板重連）

## v0.4.0 — 2026-04-25

- **assertion result CSV streaming**：每筆 result 推到 text_sensor `Assertion Result CSV`
- **Dashboard rolling table**：JS rolling buffer 100 筆 + PASS/FAIL 顏色 + 「📥 CSV」下載 + 「🗑 Clear」
- **A5** wash_init+unlock_pwd → wash_phase=lock assertion

## v0.3.0 — 2026-04-25

- **sim_assertion** 元件：規則引擎 + expect_event/observe_event/loop timeout
- **規則 A1~A4**（LED 顏色變化 → 主板 EVENT）
- **故障注入** FLICKER / POWER_LOSS / RED_BURST 機率 0~100%
- **batch runner**：start_batch(scenario, speed, count, inter_run_ms)

## v0.2.0 — 2026-04-25

- **UART2 frame parser**：FD/03/C7/50/CMD/CHK/DF state machine + 5 次連發 dedupe
- **scenario 引擎**：SW5 (12 步) / SW6 (18 步) + speed_factor + wait_gowasher
- **EVENT parser**：輕量 strstr 抽 type/event/wash_phase/wash_stage/wash_total
- **腳位對齊主板**：21/47/35/1（杜邦線 1:1 直通）

## v0.1.0 — 2026-04-25

- **骨架** + WiFi STA 固定 IP 192.168.1.10
- **WS server** port 8090 raw TCP + RFC 6455 handshake
- **3 LED OUTPUT + UART2 RX**（boot 全滅安全預設）
- **Dashboard** 頂部 sticky 狀態條
- **OTA** ota_update 元件 + frankiehex/H_OTA_SIM repo
