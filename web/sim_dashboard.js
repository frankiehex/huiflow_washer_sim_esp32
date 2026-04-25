// HuiFlow 洗車機模擬板 Dashboard — Phase 1 骨架
// 目標：在 ESPHome web_server v2 預設 entity 列表上加一個頂部狀態條
//       顯示當前 LED 顏色 + WS client 連線狀態 + RX/TX 統計
// Phase 2 會擴充：即時 EVENT log table + scenario 控制 + assertion 結果
(function () {
  'use strict';

  const BANNER_ID = 'sim-banner';

  function renderBanner() {
    if (document.getElementById(BANNER_ID)) return;
    const bar = document.createElement('div');
    bar.id = BANNER_ID;
    bar.style.cssText = [
      'position:sticky',
      'top:0',
      'z-index:100',
      'background:#1a1a1a',
      'color:#eee',
      'padding:10px 16px',
      'font-family:monospace',
      'font-size:14px',
      'border-bottom:2px solid #444',
      'display:flex',
      'gap:24px',
      'flex-wrap:wrap',
      'align-items:center',
    ].join(';');
    bar.innerHTML = `
      <strong>WASHER SIM</strong>
      <span>LED: <span id="sim-led" style="font-weight:bold">--</span></span>
      <span>WS Client: <span id="sim-ws" style="font-weight:bold">--</span></span>
      <span>Scenario: <span id="sim-scn" style="font-weight:bold">idle</span> <span id="sim-scn-step">-</span></span>
      <span>Batch: <span id="sim-batch" style="color:#9cf">-</span></span>
      <span>Main Phase: <span id="sim-mphase" style="font-weight:bold;color:#fa3">--</span> <span id="sim-mstep">-</span></span>
      <span>Last EVENT: <span id="sim-mev" style="color:#8f8">--</span></span>
      <span>Last UART CMD: <span id="sim-ucmd" style="color:#fc6">--</span></span>
      <span style="border-left:1px solid #555;padding-left:16px">
        Assertion <span style="color:#3c6">PASS:<span id="sim-pass">0</span></span>
        / <span style="color:#f66">FAIL:<span id="sim-fail">0</span></span>
        / pend:<span id="sim-pending">0</span>
        / fault:<span id="sim-faults">0</span>
      </span>
      <span>RX: <span id="sim-rx">0</span></span>
      <span>TX: <span id="sim-tx">0</span></span>
    `;
    // 第二排：最近 fail
    const fb = document.createElement('div');
    fb.id = 'sim-failbar';
    fb.style.cssText = 'position:sticky;top:48px;z-index:99;background:#220;color:#f88;padding:6px 16px;font-family:monospace;font-size:12px;border-bottom:1px solid #444;display:none';
    fb.innerHTML = `<span style="color:#fa3">last fail:</span> <span id="sim-lastfail">--</span>`;
    if (body.firstChild && body.firstChild.nextSibling) body.insertBefore(fb, body.firstChild.nextSibling);
    else body.appendChild(fb);

    // Phase 4：Assertion 結果 rolling table（top:80）
    const rt = document.createElement('div');
    rt.id = 'sim-results';
    rt.style.cssText = [
      'background:#1a1a1a','color:#ddd','padding:8px 16px',
      'font-family:monospace','font-size:11px',
      'border-bottom:2px solid #444','max-height:280px','overflow-y:auto',
    ].join(';');
    rt.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:4px">
        <strong>Assertion Results (rolling, last <span id="sim-rcount">0</span>/100)</strong>
        <span>
          <button id="sim-download-csv" style="background:#244;color:#9cf;border:1px solid #468;padding:2px 8px;cursor:pointer">📥 CSV</button>
          <button id="sim-clear-table" style="background:#422;color:#fc6;border:1px solid #864;padding:2px 8px;margin-left:4px;cursor:pointer">🗑 Clear</button>
        </span>
      </div>
      <div id="sim-perrule" style="display:flex;flex-wrap:wrap;gap:4px;margin-bottom:6px;font-size:11px"></div>
      <table style="width:100%;border-collapse:collapse" id="sim-rtable">
        <thead style="background:#222"><tr style="text-align:left">
          <th style="padding:4px">#</th>
          <th style="padding:4px">time</th>
          <th style="padding:4px">rule</th>
          <th style="padding:4px">P/F</th>
          <th style="padding:4px">actual</th>
          <th style="padding:4px">window</th>
          <th style="padding:4px">detail</th>
        </tr></thead>
        <tbody id="sim-rtbody"></tbody>
      </table>
    `;
    body.insertBefore(rt, body.children[2] || null);

    // Rolling buffer
    window._sim_results = [];
    window._sim_results_max = 100;

    document.getElementById('sim-download-csv').onclick = () => {
      const lines = ['rule_id,pass_fail,actual_ms,window_ms,timestamp_ms,detail'];
      for (const r of window._sim_results) lines.push(r._csv);
      const blob = new Blob([lines.join('\n')], {type: 'text/csv'});
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `sim-assertion-${Date.now()}.csv`;
      a.click();
      URL.revokeObjectURL(url);
    };
    document.getElementById('sim-clear-table').onclick = () => {
      window._sim_results = [];
      document.getElementById('sim-rtbody').innerHTML = '';
      document.getElementById('sim-rcount').textContent = '0';
    };
    const body = document.body;
    if (body.firstChild) body.insertBefore(bar, body.firstChild);
    else body.appendChild(bar);
  }

  // ESPHome v2 dashboard 透過 <esp-app> 自訂元素渲染 entity。
  // SSE 事件 `state` 會廣播 {id, value} ，攔下關心的 entity id 同步到 banner。
  const LED_COLORS = {
    off: '#555',
    red: '#ff4444',
    green: '#33cc66',
    blue: '#3388ff',
  };

  function updateFromEntity(id, value) {
    switch (id) {
      case 'text_sensor-current_led': {
        const el = document.getElementById('sim-led');
        if (el) {
          el.textContent = value.toUpperCase();
          el.style.color = LED_COLORS[value] || '#eee';
        }
        break;
      }
      case 'text_sensor-ws_client_connected': {
        const el = document.getElementById('sim-ws');
        if (el) {
          el.textContent = value === 'yes' ? 'CONNECTED' : 'disconnected';
          el.style.color = value === 'yes' ? '#33cc66' : '#ff6666';
        }
        break;
      }
      case 'text_sensor-ws_client_device_id': {
        const el = document.getElementById('sim-dev');
        if (el) el.textContent = value || '--';
        break;
      }
      case 'sensor-ws_rx_frames': {
        const el = document.getElementById('sim-rx');
        if (el) el.textContent = String(Math.round(Number(value) || 0));
        break;
      }
      case 'sensor-ws_tx_frames': {
        const el = document.getElementById('sim-tx');
        if (el) el.textContent = String(Math.round(Number(value) || 0));
        break;
      }
      case 'text_sensor-scenario_name': {
        const el = document.getElementById('sim-scn');
        if (el) el.textContent = value || 'idle';
        break;
      }
      case 'text_sensor-scenario_step': {
        const el = document.getElementById('sim-scn-step');
        if (el) el.textContent = value || '-';
        break;
      }
      case 'text_sensor-main_wash_phase': {
        const el = document.getElementById('sim-mphase');
        if (el) {
          el.textContent = (value || '--').toUpperCase();
          el.style.color = LED_COLORS[value] || '#fa3';
        }
        break;
      }
      case 'text_sensor-main_wash_step': {
        const el = document.getElementById('sim-mstep');
        if (el) el.textContent = value || '-';
        break;
      }
      case 'text_sensor-main_last_event': {
        const el = document.getElementById('sim-mev');
        if (el) el.textContent = value || '--';
        break;
      }
      case 'text_sensor-last_washer_cmd': {
        const el = document.getElementById('sim-ucmd');
        if (el) el.textContent = value || '--';
        break;
      }
      case 'sensor-assertion_pass': {
        const el = document.getElementById('sim-pass');
        if (el) el.textContent = String(Math.round(Number(value) || 0));
        break;
      }
      case 'sensor-assertion_fail': {
        const el = document.getElementById('sim-fail');
        if (el) el.textContent = String(Math.round(Number(value) || 0));
        break;
      }
      case 'sensor-assertion_pending': {
        const el = document.getElementById('sim-pending');
        if (el) el.textContent = String(Math.round(Number(value) || 0));
        break;
      }
      case 'sensor-faults_injected': {
        const el = document.getElementById('sim-faults');
        if (el) el.textContent = String(Math.round(Number(value) || 0));
        break;
      }
      case 'sensor-batch_remaining': {
        const r = Math.round(Number(value) || 0);
        const elBatch = document.getElementById('sim-batch');
        if (elBatch) {
          if (r > 0) elBatch.textContent = `running (${r} left)`;
          else if (window._sim_batch_completed > 0) elBatch.textContent = `done (${window._sim_batch_completed})`;
          else elBatch.textContent = '-';
        }
        window._sim_batch_remaining = r;
        break;
      }
      case 'sensor-batch_completed': {
        const c = Math.round(Number(value) || 0);
        window._sim_batch_completed = c;
        const elBatch = document.getElementById('sim-batch');
        const r = window._sim_batch_remaining || 0;
        if (elBatch && r === 0 && c > 0) elBatch.textContent = `done (${c})`;
        break;
      }
      case 'text_sensor-assertion_last_fail': {
        const el = document.getElementById('sim-lastfail');
        const fb = document.getElementById('sim-failbar');
        if (el) el.textContent = value || '--';
        if (fb) fb.style.display = (value && value.trim() !== '') ? '' : 'none';
        break;
      }
      case 'text_sensor-assertion_per_rule': {
        // 格式 "A1:5/0@320ms|A2:3/2@420ms|..."
        const box = document.getElementById('sim-perrule');
        if (!box) break;
        box.innerHTML = '';
        if (!value || value === '(none)') break;
        for (const part of value.split('|')) {
          const m = part.match(/^([A-Z]\d+):(\d+)\/(\d+)(?:@(\d+)ms)?$/);
          if (!m) continue;
          const rule = m[1], pass = +m[2], fail = +m[3], avg = m[4] ? `@${m[4]}ms` : '';
          const total = pass + fail;
          const passPct = total ? Math.round(pass * 100 / total) : 0;
          const chip = document.createElement('span');
          chip.style.cssText = `display:inline-flex;align-items:center;gap:4px;padding:2px 6px;border-radius:3px;background:#222;border:1px solid #444`;
          chip.innerHTML = `
            <strong style="color:#fff">${rule}</strong>
            <span style="color:#3c6">${pass}</span>/<span style="color:#f66">${fail}</span>
            <span style="display:inline-block;width:60px;height:8px;background:#400;border-radius:2px;overflow:hidden">
              <span style="display:inline-block;width:${passPct}%;height:100%;background:#3c6"></span>
            </span>
            <span style="color:#888">${avg}</span>
          `;
          box.appendChild(chip);
        }
        break;
      }
      case 'text_sensor-assertion_result_csv': {
        // 格式：rule_id,P/F,actual_ms,window_ms,timestamp_ms,detail...
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
        // 跳過重複（template sensor 同值不重發，但保險）
        const last = window._sim_results[window._sim_results.length - 1];
        if (last && last._csv === value) break;
        window._sim_results.push(rec);
        if (window._sim_results.length > window._sim_results_max) window._sim_results.shift();
        // 渲染新增 row
        const tbody = document.getElementById('sim-rtbody');
        if (!tbody) break;
        const tr = document.createElement('tr');
        const colorBg = rec.pass ? '#1a2a1a' : '#2a1a1a';
        const colorPF = rec.pass ? '#3c6' : '#f66';
        const tsAgo = ((Date.now() - performance.timeOrigin - parseInt(rec.ts_ms,10)) / 1000).toFixed(1);
        tr.style.cssText = `background:${colorBg}`;
        tr.innerHTML = `
          <td style="padding:3px 4px;color:#888">${window._sim_results.length}</td>
          <td style="padding:3px 4px">+${(parseInt(rec.ts_ms,10)/1000).toFixed(1)}s</td>
          <td style="padding:3px 4px;font-weight:bold">${rec.rule}</td>
          <td style="padding:3px 4px;color:${colorPF};font-weight:bold">${rec.pass?'PASS':'FAIL'}</td>
          <td style="padding:3px 4px">${rec.actual_ms}ms</td>
          <td style="padding:3px 4px">${rec.window_ms}ms</td>
          <td style="padding:3px 4px;color:#bbb">${rec.detail}</td>
        `;
        tbody.insertBefore(tr, tbody.firstChild);
        // 限制 DOM rows
        while (tbody.children.length > window._sim_results_max) tbody.removeChild(tbody.lastChild);
        const rcount = document.getElementById('sim-rcount');
        if (rcount) rcount.textContent = String(window._sim_results.length);
        break;
      }
    }
  }

  // 攔 EventSource 廣播
  function hookEvents() {
    const es = new EventSource('/events');
    es.addEventListener('state', (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg && typeof msg.id === 'string' && msg.value !== undefined) {
          updateFromEntity(msg.id, String(msg.value));
        }
      } catch (e) { /* 忽略 parse 錯 */ }
    });
    es.onerror = () => {
      // 重連由瀏覽器自動處理，這裡不做事
    };
  }

  function init() {
    renderBanner();
    hookEvents();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
