'use strict';
(async () => {
  /* Static text is applied by /i18n.js (data-i18n attributes); t()/tn()
     cover the dynamic strings, with English fallbacks at each call site. */
  const { t, tn } = await window.i18nReady;

  const $ = id => document.getElementById(id);
  let status = {};

  /* ── Populate timezone dropdown ── */
  const zones = await (await fetch('/api/timezones')).json().catch(() => []);
  const sel = $('sel-tz');
  zones.forEach(z => {
    const opt = document.createElement('option');
    opt.value = z.name; opt.textContent = z.name;
    sel.appendChild(opt);
  });

  /* ── Status rendering (fed by the WebSocket, or the poll fallback) ── */
  async function refreshStatus() {
    try {
      status = await (await fetch('/api/status')).json();
      renderStatus(status);
    } catch (_) {}
  }

  function renderStatus(s) {
    $('fw-version').textContent = s.fw_version ? `v${s.fw_version}` : '';

    /* Temperature quarter */
    $('temp-val').textContent = s.temperature_valid
      ? (s.temp_unit === 'F' ? (s.temperature_c * 9 / 5 + 32).toFixed(1) : s.temperature_c.toFixed(1))
      : '---';
    $('temp-unit').textContent = s.temperature_valid ? (s.temp_unit === 'F' ? '°F' : '°C') : '';

    /* Pressure quarter: any Bosch sensor supplies it (BMP280 or BME280) */
    const hasPressure = s.sensor === 'bmp280' || s.sensor === 'bme280';
    $('press-val').textContent = (hasPressure && s.pressure_valid)
      ? s.pressure_hpa.toFixed(1) : '---';

    /* Humidity quarter: only a BME280 supplies it (feature 007) */
    $('hum-val').textContent = (s.sensor === 'bme280' && s.humidity_valid)
      ? s.humidity_pct.toFixed(0) : '---';

    /* Time quarter — displayable from NTP or a restored RTC. */
    const timeSource = s.time_source ?? (s.time_synced ? 'ntp' : 'none');
    if (timeSource !== 'none' && s.now) {
      const d = new Date(s.now * 1000);
      let h, m;
      if (s.time_mode === 'utc') {
        h = String(d.getUTCHours()).padStart(2, '0');
        m = String(d.getUTCMinutes()).padStart(2, '0');
      } else {
        try {
          const parts = new Intl.DateTimeFormat('en-GB', {
            hour: '2-digit', minute: '2-digit', hour12: false,
            timeZone: s.tz_name || 'UTC',
          }).formatToParts(d);
          h = (parts.find(p => p.type === 'hour')?.value ?? '00').padStart(2, '0');
          m = (parts.find(p => p.type === 'minute')?.value ?? '00').padStart(2, '0');
        } catch (_) {
          h = String(d.getUTCHours()).padStart(2, '0');
          m = String(d.getUTCMinutes()).padStart(2, '0');
        }
      }
      $('time-val').textContent = `${h}:${m}`;
      $('time-sync').textContent = s.time_mode === 'utc'
        ? t('mgmt_badge_utc', 'UTC') : t('mgmt_badge_local', 'LOCAL');
    } else {
      $('time-val').textContent = '--:--';
      $('time-sync').textContent = t('mgmt_no_sync', 'no sync');
    }

    const lastSync = s.time_last_sync
      ? new Date(s.time_last_sync * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC'
      : null;
    const sync = lastSync ? ` (${t('mgmt_last_sync', 'last sync')} ${lastSync})` : '';
    $('time-source').textContent =
      timeSource === 'ntp' ? t('mgmt_time_synced', 'Time: synchronized') + sync :
      timeSource === 'rtc' ? t('mgmt_time_rtc', 'Time: from backup clock') + sync :
                             t('mgmt_time_unavailable', 'Time: not available');

    const wifiState =
      s.wifi?.state === 'connected' ? t('mgmt_wifi_connected', 'connected') :
      s.wifi?.state === 'retrying'  ? t('mgmt_wifi_retrying', 'retrying') :
      (s.wifi?.state || '—');
    $('wifi-status').textContent =
      `WiFi: ${wifiState} · ${s.wifi?.ssid || ''} · ${s.wifi?.ip || ''}`;

    if (s.tz_name) {
      for (const opt of sel.options) {
        if (opt.value === s.tz_name) { opt.selected = true; break; }
      }
    }

    $('hist-count').textContent = tn('mgmt_records', s.history_records ?? 0,
                                     `${s.history_records ?? 0} records`);
  }

  /* ── Live readings over WebSocket, with a poll fallback ── */
  let ws = null;
  let retryDelay = 1000;
  let pollTimer = null;

  function startPolling() {
    if (pollTimer == null) pollTimer = setInterval(refreshStatus, 5000);
  }
  function stopPolling() {
    if (pollTimer != null) { clearInterval(pollTimer); pollTimer = null; }
  }

  function connectWs() {
    try {
      ws = new WebSocket(`wss://${location.host}/api/ws`);
    } catch (_) {
      scheduleReconnect();
      return;
    }
    ws.onopen = () => {
      retryDelay = 1000;
      stopPolling();
    };
    ws.onmessage = e => {
      try { status = JSON.parse(e.data); renderStatus(status); } catch (_) {}
    };
    ws.onclose = () => { ws = null; scheduleReconnect(); };
    ws.onerror = () => { if (ws) ws.close(); };
  }

  function scheduleReconnect() {
    startPolling();
    const jitter = 0.8 + Math.random() * 0.4;
    setTimeout(connectWs, Math.round(retryDelay * jitter));
    retryDelay = Math.min(retryDelay * 2, 30000);
  }

  /* ── Save timezone ── */
  $('btn-save-tz').addEventListener('click', async () => {
    await fetch('/api/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tz_name: sel.value }),
    });
    refreshStatus();
  });

  /* ── OTA upload ── */
  $('fw-file').addEventListener('change', () => {
    $('btn-upload').disabled = !$('fw-file').files.length;
  });

  $('btn-upload').addEventListener('click', async () => {
    const file = $('fw-file').files[0];
    if (!file) return;
    $('btn-upload').disabled = true;
    $('ota-progress').classList.remove('hidden');
    $('ota-status').textContent = '';

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');

    xhr.upload.onprogress = e => {
      if (e.lengthComputable) {
        const pct = Math.round(e.loaded / e.total * 100);
        $('ota-bar').value = pct;
        $('ota-pct').textContent = `${pct}%`;
      }
    };

    xhr.onload = () => {
      if (xhr.status === 200) {
        $('ota-status').textContent = t('mgmt_ota_applied', 'Update applied — rebooting…');
        setTimeout(() => location.reload(), 8000);
      } else {
        let msg = t('mgmt_ota_failed', 'Update failed.');
        try { msg = JSON.parse(xhr.responseText).error || msg; } catch (_) {}
        $('ota-status').textContent = msg;
        $('btn-upload').disabled = false;
      }
    };
    xhr.onerror = () => {
      $('ota-status').textContent = t('mgmt_ota_conn_error', 'Connection error.');
      $('btn-upload').disabled = false;
    };

    xhr.send(file);
  });

  /* ── History plot ── */
  const PERIOD_SEC = { day: 86400, week: 604800, month: 2592000, all: 0 };
  const MAX_POINTS = 400;
  let curPeriod = 'day';
  let lastSpec = null;

  const mean = a => a.reduce((s, x) => s + x, 0) / a.length;
  const meanN = a => {
    const f = a.filter(x => x != null && !Number.isNaN(x));
    return f.length ? f.reduce((s, x) => s + x, 0) / f.length : null;
  };

  function downsample(recs, maxN) {
    if (recs.length <= maxN) {
      return recs.map(r => ({
        timestamp: r.timestamp, temperature: r.temperature,
        pressure: r.pressure, humidity: r.humidity,
      }));
    }
    const t0 = recs[0].timestamp, t1 = recs[recs.length - 1].timestamp;
    const span = Math.max(1, t1 - t0);
    const buckets = new Array(maxN);
    for (const r of recs) {
      const bi = Math.min(maxN - 1, Math.floor((r.timestamp - t0) / span * maxN));
      (buckets[bi] || (buckets[bi] = [])).push(r);
    }
    const out = [];
    for (let i = 0; i < maxN; i++) {
      const b = buckets[i];
      if (!b) continue;
      out.push({
        timestamp: Math.round(mean(b.map(r => r.timestamp))),
        temperature: mean(b.map(r => r.temperature)),
        pressure: meanN(b.map(r => r.pressure)),
        humidity: meanN(b.map(r => r.humidity)),
      });
    }
    return out;
  }

  function showEmpty(on) {
    $('hist-empty').classList.toggle('hidden', !on);
    $('hist-chart').classList.toggle('hidden', on);
  }

  async function loadHistory(period) {
    curPeriod = period;
    const now = Math.floor(Date.now() / 1000);
    const from = PERIOD_SEC[period] ? now - PERIOD_SEC[period] : 0;
    let data;
    try {
      data = await (await fetch(`/api/history?from=${from}&to=${now}`)).json();
    } catch (_) {
      data = { records: [] };
    }
    const recs = (data.records || []).filter(r => typeof r.temperature === 'number');
    if (!recs.length) { lastSpec = null; showEmpty(true); return; }
    showEmpty(false);

    const pts = downsample(recs, MAX_POINTS);
    const series = [{
      label: t('mgmt_legend_temperature', 'Temperature'), color: '#4fc3f7', unit: '°C',
      values: pts.map(p => p.temperature),
    }];
    if (pts.some(p => p.pressure != null)) {
      series.push({
        label: t('mgmt_legend_pressure', 'Pressure'), color: '#ffb74d', unit: 'hPa',
        values: pts.map(p => p.pressure),
      });
    }
    if (pts.some(p => p.humidity != null)) {
      series.push({
        label: t('mgmt_legend_humidity', 'Humidity'), color: '#81c784', unit: '%',
        values: pts.map(p => p.humidity),
      });
    }
    lastSpec = { times: pts.map(p => p.timestamp), series };
    window.drawTimeSeries($('hist-chart'), lastSpec);
  }

  $('period-sel').addEventListener('click', e => {
    const btn = e.target.closest('button[data-period]');
    if (!btn) return;
    for (const b of $('period-sel').children) b.classList.toggle('active', b === btn);
    loadHistory(btn.dataset.period);
  });

  let resizeTimer = null;
  window.addEventListener('resize', () => {
    if (resizeTimer) clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => {
      if (lastSpec) window.drawTimeSeries($('hist-chart'), lastSpec);
    }, 200);
  });

  /* ── Boot log (006): fetched once on load; on any failure the section
     degrades to a localized "not available" message (never breaks the page) ── */
  (async () => {
    try {
      const res = await fetch('/api/boot.log');
      if (!res.ok) throw new Error(`http ${res.status}`);
      $('bootlog-content').textContent = await res.text();
    } catch (_) {
      $('bootlog-content').classList.add('hidden');
      $('bootlog-unavailable').classList.remove('hidden');
    }
  })();

  /* ── Start ── */
  refreshStatus();
  startPolling();
  connectWs();
  loadHistory('day');
})();
