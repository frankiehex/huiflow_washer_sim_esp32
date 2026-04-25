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
      <span>Main Phase: <span id="sim-mphase" style="font-weight:bold;color:#fa3">--</span> <span id="sim-mstep">-</span></span>
      <span>Last EVENT: <span id="sim-mev" style="color:#8f8">--</span></span>
      <span>Last UART CMD: <span id="sim-ucmd" style="color:#fc6">--</span></span>
      <span>RX: <span id="sim-rx">0</span></span>
      <span>TX: <span id="sim-tx">0</span></span>
    `;
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
