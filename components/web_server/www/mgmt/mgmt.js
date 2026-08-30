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
      pushLive(status);
    } catch (_) {}
  }

  let lastTz = null;

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

    /* Keep the plot's timezone in sync with the device setting. A change
       re-aligns the visible period (day/week/month boundaries are local). */
    const tz = s.tz_name || 'UTC';
    if (chart && tz !== lastTz) {
      lastTz = tz;
      chart.setTz(tz);
      if (view.mode === 'history' && view.period !== 'all') loadHistory();
      else updatePeriodControls();
    }
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
      try {
        status = JSON.parse(e.data);
        renderStatus(status);
        pushLive(status);
      } catch (_) {}
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

  /* ══════════════════════ History / live plot (feature 010) ══════════════════════ */

  const MAX_POINTS = 400;
  const LIVE_CAP = 10000;

  /* Durable view state (data-model.md → PlotViewState) */
  const view = {
    mode: 'history',           // 'history' | 'live'
    period: 'day',             // 'day' | 'week' | 'month' | 'all'
    anchor: Math.floor(Date.now() / 1000),
  };
  let selectedSeries = null;
  let chart = null;
  let histReqSeq = 0;

  /* In-memory buffer of readings seen this session, independent of the WS
     object lifecycle so it survives reconnects (FR-036/FR-037). */
  const liveBuffer = [];

  const mean = a => a.reduce((s, x) => s + x, 0) / a.length;
  const meanN = a => {
    const f = a.filter(x => x != null && !Number.isNaN(x));
    return f.length ? f.reduce((s, x) => s + x, 0) / f.length : null;
  };

  /* ── Timezone-aware calendar boundary math (research.md R2) ── */
  const tzName = () => status.tz_name || 'UTC';

  function tzParts(epochSec, tz) {
    const p = {};
    for (const { type, value } of new Intl.DateTimeFormat('en-US', {
      timeZone: tz, year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
    }).formatToParts(new Date(epochSec * 1000))) p[type] = value;
    return { y: +p.year, mo: +p.month, d: +p.day, h: +(p.hour === '24' ? 0 : p.hour) };
  }

  /* epoch (sec) of the wall-clock Y-M-D 00:00:00 in tz — offset inversion with
     one DST correction. Month/day overflow is normalised by Date.UTC. */
  function wallToEpoch(y, mo, d, tz) {
    const guess = Date.UTC(y, mo - 1, d, 0, 0, 0) / 1000;
    const p = tzParts(guess, tz);
    let off = Date.UTC(p.y, p.mo - 1, p.d, p.h, 0, 0) / 1000 - guess;
    let real = guess - off;
    const p2 = tzParts(real, tz);
    const off2 = Date.UTC(p2.y, p2.mo - 1, p2.d, p2.h, 0, 0) / 1000 - real;
    if (off2 !== off) real = guess - off2;
    return real;
  }

  const localMidnight = (e, tz) => { const p = tzParts(e, tz); return wallToEpoch(p.y, p.mo, p.d, tz); };
  const addDaysLocal = (e, n, tz) => { const p = tzParts(e, tz); return wallToEpoch(p.y, p.mo, p.d + n, tz); };
  function localDow(e, tz) { const p = tzParts(e, tz); return new Date(Date.UTC(p.y, p.mo - 1, p.d)).getUTCDay(); }
  function mondayOnOrBefore(e, tz) { return addDaysLocal(localMidnight(e, tz), -((localDow(e, tz) + 6) % 7), tz); }
  const firstOfMonth = (e, tz) => { const p = tzParts(e, tz); return wallToEpoch(p.y, p.mo, 1, tz); };
  const firstOfNextMonth = (e, tz) => { const p = tzParts(e, tz); return wallToEpoch(p.y, p.mo + 1, 1, tz); };

  /* { from, to (clamped to now, for the fetch), nominalEnd (for the axis) } */
  function periodWindow() {
    const now = Math.floor(Date.now() / 1000);
    if (view.mode === 'live') {
      const from = liveBuffer.length ? liveBuffer[0].t : now - 60;
      return { from, to: now, nominalEnd: now };
    }
    const tz = tzName();
    if (view.period === 'all') return { from: 0, to: now, nominalEnd: now };
    let from, nominalEnd;
    if (view.period === 'day') { from = localMidnight(view.anchor, tz); nominalEnd = addDaysLocal(from, 1, tz); }
    else if (view.period === 'week') { from = mondayOnOrBefore(view.anchor, tz); nominalEnd = addDaysLocal(from, 7, tz); }
    else { from = firstOfMonth(view.anchor, tz); nominalEnd = firstOfNextMonth(view.anchor, tz); }
    return { from, to: Math.min(nominalEnd, now), nominalEnd };
  }

  function isCurrentPeriod() {
    const now = Math.floor(Date.now() / 1000);
    const w = periodWindow();
    return w.from <= now && w.nominalEnd >= now;
  }

  function stepPeriod(dir) {
    const tz = tzName();
    if (view.period === 'day') view.anchor = addDaysLocal(localMidnight(view.anchor, tz), dir, tz) + 43200;
    else if (view.period === 'week') view.anchor = addDaysLocal(mondayOnOrBefore(view.anchor, tz), dir * 7, tz) + 3 * 86400;
    else { const p = tzParts(view.anchor, tz); view.anchor = wallToEpoch(p.y, p.mo + dir, 15, tz) + 43200; }
  }

  function rangeLabel() {
    if (view.mode === 'live') return t('mgmt_plot_range_live', 'Live session');
    if (view.period === 'all') return t('mgmt_plot_range_all', 'All data');
    const tz = tzName();
    const w = periodWindow();
    const dOpts = { timeZone: tz, year: 'numeric', month: 'short', day: 'numeric' };
    const fmtD = e => { try { return new Intl.DateTimeFormat(undefined, dOpts).format(new Date(e * 1000)); } catch (_) { return new Date(e * 1000).toISOString().slice(0, 10); } };
    if (view.period === 'day') return fmtD(w.from);
    return `${fmtD(w.from)} – ${fmtD(w.nominalEnd - 86400)}`;
  }

  /* ── Data shaping ── */
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

  function buildSeries(pts) {
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
    return { times: pts.map(p => p.timestamp), series };
  }

  function showEmpty(on) {
    $('hist-empty').classList.toggle('hidden', !on);
    $('hist-chart').classList.toggle('hidden', on);
  }

  function reapplySelection(spec) {
    if (selectedSeries != null && selectedSeries >= spec.series.length) selectedSeries = null;
    chart.setSelected(selectedSeries);
  }

  /* ── Live buffer ── */
  function pushLive(s) {
    if (!s || typeof s.now !== 'number' || !s.temperature_valid) return;
    const hasP = (s.sensor === 'bmp280' || s.sensor === 'bme280') && s.pressure_valid;
    const hasH = s.sensor === 'bme280' && s.humidity_valid;
    const e = {
      t: s.now,
      temperature: s.temperature_c,
      pressure: hasP ? s.pressure_hpa : null,
      humidity: hasH ? s.humidity_pct : null,
    };
    const last = liveBuffer[liveBuffer.length - 1];
    if (last && last.t === e.t) liveBuffer[liveBuffer.length - 1] = e;
    else liveBuffer.push(e);
    if (liveBuffer.length > LIVE_CAP) liveBuffer.splice(0, liveBuffer.length - LIVE_CAP);
    if (view.mode === 'live') scheduleLiveRender();
  }

  let liveRenderPending = false;
  function scheduleLiveRender() {
    if (liveRenderPending) return;
    liveRenderPending = true;
    requestAnimationFrame(() => { liveRenderPending = false; renderLive(); });
  }

  function renderLive() {
    updatePeriodControls();
    if (!liveBuffer.length) {
      showEmpty(true);
      chart.setData({ times: [], series: [] });
      return;
    }
    showEmpty(false);
    const pts = liveBuffer.map(e => ({
      timestamp: e.t, temperature: e.temperature, pressure: e.pressure, humidity: e.humidity,
    }));
    const spec = buildSeries(pts.length > MAX_POINTS ? downsample(pts, MAX_POINTS) : pts);
    chart.setData(spec);
    chart.setPeriodWindow({ from: liveBuffer[0].t, to: Math.floor(Date.now() / 1000) });
    reapplySelection(spec);
  }

  /* ── History load ── */
  async function loadHistory() {
    const seq = ++histReqSeq;
    updatePeriodControls();
    if (view.mode === 'live') { renderLive(); return; }

    const w = periodWindow();
    let data;
    try {
      data = await (await fetch(`/api/history?from=${Math.floor(w.from)}&to=${Math.floor(w.to)}`)).json();
    } catch (_) {
      data = { records: [] };
    }
    if (seq !== histReqSeq) return;   /* superseded by a newer view (Edge Cases) */

    const recs = (data.records || []).filter(r => typeof r.temperature === 'number');
    if (!recs.length) {
      showEmpty(true);
      chart.setData({ times: [], series: [] });
      chart.setPeriodWindow({ from: w.from, to: w.nominalEnd });
      return;
    }
    showEmpty(false);
    const spec = buildSeries(downsample(recs, MAX_POINTS));
    chart.setData(spec);
    if (view.period === 'all') {
      chart.setPeriodWindow({ from: spec.times[0], to: spec.times[spec.times.length - 1] });
    } else {
      chart.setPeriodWindow({ from: w.from, to: w.nominalEnd });
    }
    reapplySelection(spec);
  }

  /* ── Controls ── */
  function updatePeriodControls() {
    for (const b of $('period-sel').querySelectorAll('button[data-period]')) {
      b.classList.toggle('active', view.mode === 'history' && b.dataset.period === view.period);
    }
    $('btn-live').classList.toggle('active', view.mode === 'live');
    const navOff = view.mode !== 'history' || view.period === 'all';
    $('btn-prev').disabled = navOff;
    $('btn-next').disabled = navOff || isCurrentPeriod();
    $('hist-range').textContent = rangeLabel();
  }

  $('period-sel').addEventListener('click', e => {
    const btn = e.target.closest('button[data-period]');
    if (!btn) return;
    view.mode = 'history';
    view.period = btn.dataset.period;
    view.anchor = Math.floor(Date.now() / 1000);
    loadHistory();
  });

  $('btn-live').addEventListener('click', () => {
    view.mode = 'live';
    loadHistory();
  });

  $('btn-prev').addEventListener('click', () => {
    if ($('btn-prev').disabled) return;
    stepPeriod(-1);
    loadHistory();
  });

  $('btn-next').addEventListener('click', () => {
    if ($('btn-next').disabled) return;
    stepPeriod(1);
    loadHistory();
  });

  $('btn-reset-zoom').addEventListener('click', () => chart && chart.resetZoom());

  function toggleFullscreen() {
    const on = document.body.classList.toggle('plot-fullscreen');
    $('btn-fullscreen').textContent = on
      ? t('mgmt_plot_exit_fullscreen', 'Exit fullscreen')
      : t('mgmt_plot_fullscreen', 'Fullscreen');
    requestAnimationFrame(() => chart && chart.redraw());
  }
  $('btn-fullscreen').addEventListener('click', toggleFullscreen);
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape' && document.body.classList.contains('plot-fullscreen')) toggleFullscreen();
  });

  $('btn-prev').title = t('mgmt_plot_prev', 'Previous period');
  $('btn-next').title = t('mgmt_plot_next', 'Next period');

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
  chart = window.createTimeSeriesChart($('hist-chart'), {
    tzName: tzName(),
    noDataText: t('mgmt_plot_no_data', 'no data'),
    onSelect(i) { selectedSeries = i; chart.setSelected(i); },
    onZoomChange(z) { $('btn-reset-zoom').hidden = !z; },
  });

  refreshStatus();
  startPolling();
  connectWs();
  loadHistory();
})();
