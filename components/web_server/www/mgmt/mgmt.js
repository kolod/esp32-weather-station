'use strict';
(async () => {
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

  /* ── Poll /api/status every 5 s ── */
  async function refreshStatus() {
    try {
      status = await (await fetch('/api/status')).json();
      renderStatus(status);
    } catch(_) {}
  }

  function renderStatus(s) {
    $('fw-version').textContent = s.fw_version ? `v${s.fw_version}` : '';

    const temp = s.temperature_valid
      ? `${s.temperature_c.toFixed(1)}`
      : '---';
    const unit = s.temp_unit === 'F'
      ? `${(s.temperature_c * 9/5 + 32).toFixed(1)} °F`
      : `${s.temperature_c.toFixed(1)} °C`;
    $('temp-val').textContent = s.temperature_valid
      ? (s.temp_unit === 'F' ? (s.temperature_c*9/5+32).toFixed(1) : s.temperature_c.toFixed(1))
      : '---';
    $('temp-unit').textContent = s.temperature_valid ? (s.temp_unit === 'F' ? '°F' : '°C') : '';

    if (s.time_synced && s.now) {
      const d = new Date(s.now * 1000);
      const h = String(d.getUTCHours()).padStart(2,'0');
      const m = String(d.getUTCMinutes()).padStart(2,'0');
      $('time-val').textContent = `${h}:${m}`;
      $('time-sync').textContent = s.time_mode === 'utc' ? 'UTC' : 'LOCAL';
    } else {
      $('time-val').textContent = '--:--';
      $('time-sync').textContent = 'no sync';
    }

    $('wifi-status').textContent =
      `WiFi: ${s.wifi?.state || '—'} · ${s.wifi?.ssid || ''} · ${s.wifi?.ip || ''}`;

    if (s.tz_name) {
      for (const opt of sel.options) {
        if (opt.value === s.tz_name) { opt.selected = true; break; }
      }
    }

    $('hist-count').textContent = `${s.history_records ?? 0} records`;
  }

  /* ── Save timezone ── */
  $('btn-save-tz').addEventListener('click', async () => {
    await fetch('/api/config', {
      method: 'PUT',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({tz_name: sel.value}),
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
        $('ota-status').textContent = 'Update applied — rebooting…';
        setTimeout(() => location.reload(), 8000);
      } else {
        let msg = 'Update failed.';
        try { msg = JSON.parse(xhr.responseText).error || msg; } catch(_) {}
        $('ota-status').textContent = msg;
        $('btn-upload').disabled = false;
      }
    };
    xhr.onerror = () => {
      $('ota-status').textContent = 'Connection error.';
      $('btn-upload').disabled = false;
    };

    xhr.send(file);
  });

  /* ── Load history ── */
  $('btn-load-hist').addEventListener('click', async () => {
    $('btn-load-hist').disabled = true;
    try {
      const now = Math.floor(Date.now()/1000);
      const from = now - 7*24*3600; /* last 7 days for quick view */
      const data = await (await fetch(`/api/history?from=${from}&to=${now}`)).json();
      const tbody = $('hist-body');
      tbody.innerHTML = '';
      const recs = data.records.slice(-100).reverse();
      recs.forEach(r => {
        const tr = document.createElement('tr');
        const dt = new Date(r.timestamp * 1000).toISOString().replace('T',' ').slice(0,19);
        tr.innerHTML = `<td>${dt}</td><td>${r.temperature.toFixed(2)} °C</td>`;
        tbody.appendChild(tr);
      });
    } catch(_) {}
    $('btn-load-hist').disabled = false;
  });

  /* Start polling */
  refreshStatus();
  setInterval(refreshStatus, 5000);
})();
