'use strict';
(async () => {
  /* ── Load i18n strings (server injects the chosen lang via data-lang attribute on <html>) ── */
  const lang = document.documentElement.dataset.lang || 'en';
  let t = {};
  try {
    const r = await fetch(`/i18n/${lang}.json`);
    t = await r.json();
  } catch (_) { /* fallback: English strings already in HTML */ }

  const $  = id => document.getElementById(id);
  const setText = (id, key) => { if (t[key]) $( id).textContent = t[key]; };

  setText('page-title', 'title');
  setText('heading',    'heading');
  setText('lbl-ssid',   'label_ssid');
  setText('lbl-pass',   'label_password');
  setText('lbl-tz',     'label_timezone');
  setText('btn-scan',   'btn_scan');
  setText('btn-submit', 'btn_submit');
  $('ssid').placeholder     = t.placeholder_ssid     || '';
  $('password').placeholder = t.placeholder_password || '';

  /* ── Populate timezone dropdown ── */
  try {
    const zones = await (await fetch('/api/timezones')).json();
    const sel = $('timezone');
    zones.forEach(z => {
      const opt = document.createElement('option');
      opt.value = z.name; opt.textContent = z.name;
      if (z.name === 'UTC') opt.selected = true;
      sel.appendChild(opt);
    });
  } catch (_) {}

  /* ── WiFi scan ── */
  $('btn-scan').addEventListener('click', async () => {
    $('btn-scan').disabled = true;
    showStatus('info', t.scanning || 'Scanning…');
    try {
      const nets = await (await fetch('/api/scan')).json();
      const dl = $('networks');
      dl.innerHTML = '';
      if (!nets.networks || nets.networks.length === 0) {
        showStatus('info', t.no_networks || 'No networks found.'); return;
      }
      nets.networks.forEach(n => {
        const opt = document.createElement('option');
        opt.value = n.ssid;
        opt.label = `${n.ssid} (${n.auth ? (t.secure || 'Secured') : (t.open || 'Open')})`;
        dl.appendChild(opt);
      });
      $('ssid').setAttribute('list', 'networks');
      hideStatus();
    } catch (_) { showStatus('error', t.status_failed_generic || 'Scan failed.'); }
    finally { $('btn-scan').disabled = false; }
  });

  /* ── Submit ── */
  $('btn-submit').addEventListener('click', async () => {
    const ssid = $('ssid').value.trim();
    const pass = $('password').value;
    const tz   = $('timezone').value;
    if (!ssid) return;

    $('btn-submit').disabled = true;
    showStatus('info', t.status_connecting || 'Connecting…');

    try {
      const resp = await fetch('/api/wifi', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ssid, password: pass, tz_name: tz}),
      });
      if (!resp.ok) { showStatus('error', t.status_failed_generic || 'Error.'); return; }

      /* Poll for result */
      for (let i = 0; i < 30; i++) {
        await delay(2000);
        const s = await (await fetch('/api/wifi/status')).json();
        if (s.state === 'connected') {
          showStatus('success', `${t.status_success || 'Connected!'} <a href="https://weather-${s.suffix}.local">https://weather-${s.suffix}.local</a>`);
          return;
        }
        if (s.state === 'failed') {
          const key = s.reason === 'auth'      ? 'status_failed_auth'
                    : s.reason === 'not_found' ? 'status_failed_not_found'
                    : 'status_failed_generic';
          showStatus('error', t[key] || 'Connection failed.');
          $('btn-submit').disabled = false;
          return;
        }
      }
      showStatus('error', t.status_failed_generic || 'Timeout.');
    } catch (_) { showStatus('error', t.status_failed_generic || 'Network error.'); }
    finally { $('btn-submit').disabled = false; }
  });

  function delay(ms) { return new Promise(r => setTimeout(r, ms)); }
  function showStatus(cls, html) {
    const el = $('status');
    el.className = `status ${cls}`;
    el.innerHTML = html;
  }
  function hideStatus() { $('status').className = 'status hidden'; }
})();
