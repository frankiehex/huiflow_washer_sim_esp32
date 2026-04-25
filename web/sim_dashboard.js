// HuiFlow 洗車機模擬板 Dashboard — Card-based UI v2
// 隱藏 ESPHome 預設 entity 列表，自己渲染卡片式介面
(function () {
  'use strict';

  // === 樣式 ===
  const STYLE = `
    body { background: #0e1116 !important; color: #d8dde6 !important;
           font-family: -apple-system, "PingFang TC", "Microsoft JhengHei", system-ui, sans-serif !important;
           margin: 0 !important; padding: 0 !important; }
    /* 隱藏 ESPHome 預設 UI */
    body > esp-app, body > script + esp-app, body > script[src*="oi.esphome"] { display: none !important; }
    body > * { box-sizing: border-box; }

    .sim-app { max-width: 1400px; margin: 0 auto; padding: 16px 20px 60px; }

    .sim-app h1 { font-size: 20px; margin: 0 0 4px; color: #fff; font-weight: 600; }
    .sim-app .subtitle { font-size: 12px; color: #7a8597; margin-bottom: 16px; }

    /* 頂部狀態網格 */
    .stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
                 gap: 10px; margin-bottom: 16px; }
    .stat-card { background: #1a1f2a; border-radius: 8px; padding: 10px 14px;
                 border: 1px solid #232a37; }
    .stat-card .label { font-size: 11px; color: #7a8597; text-transform: uppercase;
                        letter-spacing: 0.04em; margin-bottom: 4px; }
    .stat-card .value { font-size: 20px; font-weight: 600; color: #fff;
                        font-variant-numeric: tabular-nums; }
    .stat-card .sub { font-size: 11px; color: #92a0b8; margin-top: 2px; }
    .stat-card.ok { border-color: #1f5f3f; }
    .stat-card.ok .value { color: #4ade80; }
    .stat-card.warn { border-color: #5d4a1f; }
    .stat-card.warn .value { color: #fbbf24; }
    .stat-card.err { border-color: #5d1f1f; }
    .stat-card.err .value { color: #f87171; }

    /* LED 顏色顯示 */
    .led-dot { display: inline-block; width: 14px; height: 14px; border-radius: 50%;
               margin-right: 8px; vertical-align: -2px;
               box-shadow: 0 0 8px currentColor; }

    /* 動作面板 */
    .panels { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
              gap: 12px; margin-bottom: 16px; }
    .panel { background: #1a1f2a; border-radius: 8px; padding: 12px 14px;
             border: 1px solid #232a37; }
    .panel h3 { font-size: 12px; color: #92a0b8; margin: 0 0 10px;
                text-transform: uppercase; letter-spacing: 0.06em; font-weight: 600; }
    .panel .btn-row { display: flex; flex-wrap: wrap; gap: 6px; }
    .btn { display: inline-flex; align-items: center; justify-content: center;
           background: #2a3142; color: #d8dde6; border: 1px solid #353e52;
           padding: 7px 11px; border-radius: 5px; cursor: pointer;
           font-size: 13px; font-family: inherit; min-height: 32px;
           transition: background 0.12s, border-color 0.12s; }
    .btn:hover { background: #353e52; border-color: #4a546a; }
    .btn:active { transform: translateY(1px); }
    .btn.primary { background: #1e4a72; border-color: #2a5f8f; color: #cfe5fb; }
    .btn.primary:hover { background: #295d8c; }
    .btn.danger { background: #4a1f1f; border-color: #6b2a2a; color: #f8c8c8; }
    .btn.danger:hover { background: #5d2828; }
    .btn.led-red { background: #4a1f1f; border-color: #cc4444; }
    .btn.led-green { background: #1f4a2c; border-color: #44cc66; }
    .btn.led-blue { background: #1f2c4a; border-color: #4488dd; }
    .btn.led-off { background: #2a2a2a; border-color: #555; }

    /* 狀態 chip */
    .chips { display: flex; flex-wrap: wrap; gap: 6px; padding: 6px 0; }
    .chip { display: inline-flex; align-items: center; gap: 6px;
            padding: 4px 10px; border-radius: 14px; background: #1a1f2a;
            border: 1px solid #232a37; font-size: 12px; }
    .chip strong { color: #fff; font-weight: 600; }
    .chip .pass { color: #4ade80; }
    .chip .fail { color: #f87171; }
    .chip .bar { display: inline-block; width: 50px; height: 6px;
                 background: #4a1f1f; border-radius: 3px; overflow: hidden; }
    .chip .bar > span { display: block; height: 100%; background: #4ade80; }
    .chip .avg { color: #7a8597; }

    /* Fail bar */
    .failbar { background: #2a1a1a; color: #fca5a5; padding: 8px 14px;
               border: 1px solid #5d1f1f; border-radius: 6px; margin-bottom: 12px;
               font-size: 13px; display: none; }

    /* 結果表 */
    .results-section { background: #1a1f2a; border-radius: 8px;
                       border: 1px solid #232a37; margin-bottom: 16px; overflow: hidden; }
    .results-head { display: flex; justify-content: space-between; align-items: center;
                    padding: 10px 14px; background: #161b24; border-bottom: 1px solid #232a37; }
    .results-head h3 { margin: 0; font-size: 12px; color: #92a0b8;
                       text-transform: uppercase; letter-spacing: 0.06em; font-weight: 600; }
    .results-actions { display: flex; gap: 6px; }
    .results-table-wrap { max-height: 320px; overflow-y: auto; }
    table.results { width: 100%; border-collapse: collapse; font-size: 12px;
                    font-family: ui-monospace, "SF Mono", monospace; }
    table.results thead { background: #161b24; position: sticky; top: 0; }
    table.results th { text-align: left; padding: 6px 10px; color: #7a8597;
                       font-weight: 500; font-size: 11px; text-transform: uppercase;
                       border-bottom: 1px solid #232a37; }
    table.results td { padding: 6px 10px; border-bottom: 1px solid #1a1f2a; }
    table.results tr.pass { background: #142318; }
    table.results tr.fail { background: #2a1418; }
    table.results .rule { font-weight: 600; color: #fff; }
    table.results .pf-pass { color: #4ade80; font-weight: 600; }
    table.results .pf-fail { color: #f87171; font-weight: 600; }
    table.results .detail { color: #92a0b8; font-size: 11px; }

    /* Footer log */
    .log-section { background: #1a1f2a; border-radius: 8px;
                   border: 1px solid #232a37; padding: 10px 14px; }
    .log-section h3 { margin: 0 0 8px; font-size: 12px; color: #92a0b8;
                      text-transform: uppercase; letter-spacing: 0.06em; }
    .log-line { font-family: ui-monospace, monospace; font-size: 11px;
                color: #92a0b8; word-break: break-all; padding: 2px 0; }
    .log-line .label { color: #7a8597; min-width: 110px; display: inline-block; }
    .log-line .value { color: #d8dde6; }

    /* Toggle advanced */
    .advanced-toggle { color: #4488cc; cursor: pointer; font-size: 12px; padding: 8px 0; }
    .advanced-toggle:hover { color: #5da0e8; }
  `;

  function injectStyle() {
    if (document.getElementById('sim-style')) return;
    const s = document.createElement('style');
    s.id = 'sim-style';
    s.textContent = STYLE;
    document.head.appendChild(s);
  }

  // === 渲染 ===
  // 按鈕定義：[label, button object_id, optional class]
  const BUTTONS = {
    led: [
      ['🔵 LED → BLUE',  'led_____blue',  'led-blue'],
      ['🟢 LED → GREEN', 'led_____green', 'led-green'],
      ['🔴 LED → RED',   'led_____red',   'led-red'],
      ['⚫ LED → OFF',   'led_____off',   'led-off'],
    ],
    cmd: [
      ['Status',       'send_cmd_status'],
      ['wash_reset',   'send_cmd_wash_reset'],
      ['startwasher5', 'send_cmd_startwasher5'],
      ['Reboot Main',  'send_cmd_reboot', 'danger'],
    ],
    scenario: [
      ['SW5 @0.1x', 'run_sw5__0_1x', 'primary'],
      ['SW5 @1.0x', 'run_sw5__1_0x'],
      ['SW6 @0.1x', 'run_sw6__0_1x'],
      ['Stop',      'stop_scenario', 'danger'],
    ],
    tests: [
      ['A5 lock',     'test_a5__wash_init_lock_'],
      ['A6 unlock',   'test_a6__lock_then_unlock_'],
      ['A8 reconnect', 'test_a8__disconnect_reconnect_'],
      ['A9 reboot',   'test_a9__reboot_reconnect_'],
      ['Self-Test 4-in-1', 'run_self-test__a5_a6_a8_a9_', 'primary'],
    ],
    stress: [
      ['Batch SW5 ×10',  'batch_run_sw5_x10__0_2x'],
      ['Batch SW5 ×100', 'batch_run_sw5_x100__0_2x', 'primary'],
      ['Burn-in 100x',   'run_burn-in_100x_sw5', 'primary'],
      ['Stop Batch',     'stop_batch', 'danger'],
      ['Reset Stats',    'reset_assertion_stats'],
    ],
    system: [
      ['WS Health Check', 'run_ws_health_check', 'primary'],
      ['Disconnect',      'disconnect_client',   'danger'],
      ['Sim Restart',     'sim_restart',         'danger'],
      ['Check OTA',       'check_ota_now'],
    ],
  };

  function press(objId) {
    fetch(`/button/${objId}/press`, {method: 'POST', headers: {'Content-Length': '0'}});
  }

  function makeBtn(label, objId, klass) {
    const b = document.createElement('button');
    b.className = 'btn' + (klass ? ' ' + klass : '');
    b.textContent = label;
    b.onclick = () => press(objId);
    return b;
  }

  function renderApp() {
    if (document.getElementById('sim-app')) return;
    const app = document.createElement('div');
    app.id = 'sim-app';
    app.className = 'sim-app';
    app.innerHTML = `
      <h1>🚿 HuiFlow Washer Simulator</h1>
      <div class="subtitle">v<span id="sv-fw">--</span> · uptime <span id="sv-up">--</span> · main board <span id="sv-mid">--</span></div>

      <div class="stat-grid">
        <div class="stat-card" id="sc-led"><div class="label">SIM LED</div><div class="value">--</div></div>
        <div class="stat-card" id="sc-ws"><div class="label">主板連線</div><div class="value">--</div><div class="sub" id="sc-ws-sub">--</div></div>
        <div class="stat-card" id="sc-mphase"><div class="label">主板 Wash Phase</div><div class="value">--</div><div class="sub" id="sc-mstep">step --</div></div>
        <div class="stat-card" id="sc-scenario"><div class="label">Scenario</div><div class="value">idle</div><div class="sub" id="sc-scenstep">--</div></div>
        <div class="stat-card" id="sc-batch"><div class="label">Batch</div><div class="value">idle</div><div class="sub" id="sc-batch-sub">--</div></div>
        <div class="stat-card" id="sc-pass"><div class="label">Assertion</div>
          <div class="value"><span style="color:#4ade80" id="sv-pass">0</span> / <span style="color:#f87171" id="sv-fail">0</span></div>
          <div class="sub">pending <span id="sv-pend">0</span> · faults <span id="sv-faults">0</span></div></div>
      </div>

      <div class="failbar" id="sim-failbar">⚠️ <span id="sv-lastfail">--</span></div>

      <div class="chips" id="sim-chips"></div>

      <div class="panels">
        <div class="panel"><h3>Manual LED</h3><div class="btn-row" id="bp-led"></div></div>
        <div class="panel"><h3>送 CMD 給主板</h3><div class="btn-row" id="bp-cmd"></div></div>
        <div class="panel"><h3>Scenario 引擎</h3><div class="btn-row" id="bp-scenario"></div></div>
        <div class="panel"><h3>Assertion 測試</h3><div class="btn-row" id="bp-tests"></div></div>
        <div class="panel"><h3>批次壓力</h3><div class="btn-row" id="bp-stress"></div></div>
        <div class="panel"><h3>系統 / 診斷</h3><div class="btn-row" id="bp-system"></div></div>
      </div>

      <div class="results-section">
        <div class="results-head">
          <h3>Assertion Results · 最近 <span id="sv-rcount">0</span>/100</h3>
          <div class="results-actions">
            <button class="btn" id="b-csv">📥 CSV</button>
            <button class="btn" id="b-clear">🗑 Clear</button>
          </div>
        </div>
        <div class="results-table-wrap">
          <table class="results">
            <thead><tr>
              <th>#</th><th>time</th><th>rule</th><th>P/F</th>
              <th>actual</th><th>window</th><th>detail</th>
            </tr></thead>
            <tbody id="sim-rtbody"></tbody>
          </table>
        </div>
      </div>

      <div class="log-section">
        <h3>即時 log</h3>
        <div class="log-line"><span class="label">Last EVENT:</span><span class="value" id="sv-lastev">--</span></div>
        <div class="log-line"><span class="label">Last UART CMD:</span><span class="value" id="sv-uart">--</span></div>
        <div class="log-line"><span class="label">Last RX:</span><span class="value" id="sv-lastrx">--</span></div>
        <div class="log-line"><span class="label">Self-Test:</span><span class="value" id="sv-self">--</span></div>
        <div class="log-line"><span class="label">Burn-in:</span><span class="value" id="sv-burn">--</span></div>
        <div class="log-line"><span class="label">WS Health:</span><span class="value" id="sv-health">--</span></div>
        <div class="log-line"><span class="label">FSM Event:</span><span class="value" id="sv-fsm">--</span></div>
        <div class="log-line"><span class="label">RX/TX frames:</span><span class="value">RX <span id="sv-rx">0</span> · TX <span id="sv-tx">0</span></span></div>
      </div>

      <div class="advanced-toggle" id="adv-toggle">▸ 顯示完整 ESPHome 預設 UI（advanced）</div>
    `;
    document.body.appendChild(app);

    // 渲染 button rows
    for (const [key, list] of Object.entries(BUTTONS)) {
      const container = document.getElementById('bp-' + key);
      if (!container) continue;
      for (const [label, objId, klass] of list) {
        container.appendChild(makeBtn(label, objId, klass));
      }
    }

    // CSV / Clear
    document.getElementById('b-csv').onclick = () => {
      const lines = ['rule_id,pass_fail,actual_ms,window_ms,timestamp_ms,detail'];
      for (const r of window._sim_results) lines.push(r._csv);
      const blob = new Blob([lines.join('\n')], {type: 'text/csv'});
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url; a.download = `sim-assertion-${Date.now()}.csv`; a.click();
      URL.revokeObjectURL(url);
    };
    document.getElementById('b-clear').onclick = () => {
      window._sim_results = [];
      document.getElementById('sim-rtbody').innerHTML = '';
      document.getElementById('sv-rcount').textContent = '0';
    };

    // Advanced toggle
    document.getElementById('adv-toggle').onclick = () => {
      const esp = document.querySelector('esp-app');
      if (!esp) return;
      const cur = esp.style.display;
      esp.style.setProperty('display', cur === 'block' ? 'none' : 'block', 'important');
      document.getElementById('adv-toggle').textContent =
        cur === 'block' ? '▸ 顯示完整 ESPHome 預設 UI（advanced）' : '▾ 隱藏 ESPHome 預設 UI';
    };

    window._sim_results = [];
    window._sim_results_max = 100;
  }

  // === SSE 事件處理 ===
  const LED_COLORS = { off: '#555', red: '#f87171', green: '#4ade80', blue: '#4488dd' };

  function setStatCard(id, klass) {
    const el = document.getElementById(id);
    if (!el) return;
    el.classList.remove('ok', 'warn', 'err');
    if (klass) el.classList.add(klass);
  }

  function fmtUptime(s) {
    s = Math.round(Number(s) || 0);
    if (s < 60) return s + ' s';
    if (s < 3600) return Math.floor(s/60) + 'm ' + (s%60) + 's';
    return Math.floor(s/3600) + 'h ' + Math.floor((s%3600)/60) + 'm';
  }

  function updateEntity(id, value) {
    const $ = (i) => document.getElementById(i);
    switch (id) {
      case 'text_sensor-firmware_version':
        if ($('sv-fw')) $('sv-fw').textContent = value || '--';
        break;
      case 'sensor-uptime':
        if ($('sv-up')) $('sv-up').textContent = fmtUptime(value);
        break;
      case 'text_sensor-current_led': {
        const card = $('sc-led');
        if (card) {
          const v = (value || 'off').toLowerCase();
          card.querySelector('.value').innerHTML =
            `<span class="led-dot" style="background:${LED_COLORS[v]||'#555'};color:${LED_COLORS[v]||'#555'}"></span>${v.toUpperCase()}`;
        }
        break;
      }
      case 'text_sensor-ws_client_connected': {
        const card = $('sc-ws');
        if (card) {
          const v = value === 'yes';
          card.querySelector('.value').textContent = v ? '✓ Connected' : '○ Disconnected';
          setStatCard('sc-ws', v ? 'ok' : 'err');
        }
        break;
      }
      case 'text_sensor-ws_client_device_id':
        if ($('sc-ws-sub')) $('sc-ws-sub').textContent = value ? `id: ${value}` : '--';
        if ($('sv-mid')) $('sv-mid').textContent = value || '(none)';
        break;
      case 'text_sensor-main_wash_phase': {
        const card = $('sc-mphase');
        if (card) {
          const v = value || '--';
          card.querySelector('.value').textContent = v.toUpperCase();
          card.querySelector('.value').style.color = LED_COLORS[v] || '#fbbf24';
        }
        break;
      }
      case 'text_sensor-main_wash_step':
        if ($('sc-mstep')) $('sc-mstep').textContent = value ? `step ${value}` : 'step --';
        break;
      case 'text_sensor-scenario_name': {
        const card = $('sc-scenario');
        if (card) card.querySelector('.value').textContent = value || 'idle';
        break;
      }
      case 'text_sensor-scenario_step':
        if ($('sc-scenstep')) $('sc-scenstep').textContent = value || '--';
        break;
      case 'sensor-batch_remaining': {
        const r = Math.round(Number(value) || 0);
        window._sim_batch_remaining = r;
        const card = $('sc-batch');
        if (card) {
          if (r > 0) card.querySelector('.value').textContent = `RUNNING ${r} left`;
          else card.querySelector('.value').textContent = 'idle';
        }
        break;
      }
      case 'sensor-batch_completed': {
        const c = Math.round(Number(value) || 0);
        if ($('sc-batch-sub')) $('sc-batch-sub').textContent = c > 0 ? `completed ${c}` : '--';
        break;
      }
      case 'sensor-assertion_pass':
        if ($('sv-pass')) $('sv-pass').textContent = Math.round(Number(value)||0);
        break;
      case 'sensor-assertion_fail': {
        const f = Math.round(Number(value)||0);
        if ($('sv-fail')) $('sv-fail').textContent = f;
        setStatCard('sc-pass', f > 0 ? 'warn' : 'ok');
        break;
      }
      case 'sensor-assertion_pending':
        if ($('sv-pend')) $('sv-pend').textContent = Math.round(Number(value)||0);
        break;
      case 'sensor-faults_injected':
        if ($('sv-faults')) $('sv-faults').textContent = Math.round(Number(value)||0);
        break;
      case 'sensor-ws_rx_frames':
        if ($('sv-rx')) $('sv-rx').textContent = Math.round(Number(value)||0);
        break;
      case 'sensor-ws_tx_frames':
        if ($('sv-tx')) $('sv-tx').textContent = Math.round(Number(value)||0);
        break;
      case 'text_sensor-last_event_name':
        if ($('sv-lastev')) $('sv-lastev').textContent = value || '--';
        break;
      case 'text_sensor-last_washer_cmd':
        if ($('sv-uart')) $('sv-uart').textContent = value || '--';
        break;
      case 'text_sensor-last_rx_message':
        if ($('sv-lastrx')) $('sv-lastrx').textContent = (value || '--').slice(0, 200);
        break;
      case 'text_sensor-self_test_status':
        if ($('sv-self')) $('sv-self').textContent = value || '--';
        break;
      case 'text_sensor-burn_in_status':
      case 'text_sensor-burnin_status':
        if ($('sv-burn')) $('sv-burn').textContent = value || '--';
        break;
      case 'text_sensor-ws_health_status':
        if ($('sv-health')) $('sv-health').textContent = value || '--';
        break;
      case 'text_sensor-fsm_last_event':
        if ($('sv-fsm')) $('sv-fsm').textContent = value || '--';
        break;
      case 'text_sensor-assertion_last_fail': {
        const fb = $('sim-failbar');
        if ($('sv-lastfail')) $('sv-lastfail').textContent = value || '--';
        if (fb) fb.style.display = (value && value.trim()) ? 'block' : 'none';
        break;
      }
      case 'text_sensor-assertion_per_rule': {
        const box = $('sim-chips');
        if (!box) break;
        box.innerHTML = '';
        if (!value || value === '(none)') break;
        for (const part of value.split('|')) {
          const m = part.match(/^([A-Z]\d+):(\d+)\/(\d+)(?:@(\d+)ms)?$/);
          if (!m) continue;
          const rule = m[1], pass = +m[2], fail = +m[3], avg = m[4] ? `${m[4]}ms` : '';
          const total = pass + fail;
          const passPct = total ? Math.round(pass * 100 / total) : 0;
          const chip = document.createElement('span');
          chip.className = 'chip';
          chip.innerHTML = `<strong>${rule}</strong>
            <span class="pass">${pass}</span>/<span class="fail">${fail}</span>
            <span class="bar"><span style="width:${passPct}%"></span></span>
            ${avg ? `<span class="avg">${avg}</span>` : ''}`;
          box.appendChild(chip);
        }
        break;
      }
      case 'text_sensor-assertion_result_csv': {
        if (!value) break;
        const parts = value.split(',');
        if (parts.length < 5) break;
        const rec = {
          rule: parts[0],
          pass: parts[1] === 'P',
          actual_ms: parts[2],
          window_ms: parts[3],
          ts_ms: parts[4],
          detail: parts.slice(5).join(','),
          _csv: value,
        };
        const last = window._sim_results[window._sim_results.length - 1];
        if (last && last._csv === value) break;
        window._sim_results.push(rec);
        if (window._sim_results.length > window._sim_results_max) window._sim_results.shift();
        const tbody = document.getElementById('sim-rtbody');
        if (!tbody) break;
        const tr = document.createElement('tr');
        tr.className = rec.pass ? 'pass' : 'fail';
        tr.innerHTML = `
          <td>${window._sim_results.length}</td>
          <td>+${(parseInt(rec.ts_ms,10)/1000).toFixed(1)}s</td>
          <td class="rule">${rec.rule}</td>
          <td class="${rec.pass ? 'pf-pass' : 'pf-fail'}">${rec.pass?'PASS':'FAIL'}</td>
          <td>${rec.actual_ms}ms</td>
          <td>${rec.window_ms}ms</td>
          <td class="detail">${rec.detail}</td>`;
        tbody.insertBefore(tr, tbody.firstChild);
        while (tbody.children.length > window._sim_results_max) tbody.removeChild(tbody.lastChild);
        if (document.getElementById('sv-rcount')) document.getElementById('sv-rcount').textContent = String(window._sim_results.length);
        break;
      }
    }
  }

  function hookEvents() {
    const es = new EventSource('/events');
    es.addEventListener('state', (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg && typeof msg.id === 'string' && msg.value !== undefined) {
          updateEntity(msg.id, String(msg.value));
        }
      } catch (e) {}
    });
  }

  function init() {
    injectStyle();
    renderApp();
    hookEvents();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
