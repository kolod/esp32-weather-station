'use strict';
/* Interactive, dependency-free time-series chart for the management page
 * history plot (feature 010 — supersedes the stateless drawTimeSeries of 009).
 * No external library: CSP is `default-src 'self'` and the file is served from
 * flash. All styling is done on <canvas> or via CSSOM (element.style.*) — never
 * a markup style attribute, which `style-src 'self'` would block.
 *
 *   const chart = window.createTimeSeriesChart(canvasEl, {
 *     tzName:     'UTC',      // IANA zone for x-axis + tooltip time formatting
 *     noDataText: 'no data',  // tooltip text for a gap in a curve
 *     onSelect(index|null) {},   // user clicked a line or a legend entry
 *     onZoomChange(isZoomed) {},  // viewport narrowed / restored to full period
 *   });
 *   chart.setData({ times:[epochSec…asc], series:[{label,unit,color,values:[num|null]}] });
 *   chart.setPeriodWindow({ from, to });   // full drawn time domain (epoch sec)
 *   chart.setSelected(index|null);
 *   chart.setTz(name);
 *   chart.resetZoom();
 *   chart.redraw();
 *   chart.destroy();
 *
 * Behaviour contract: specs/010-web-plot-enhancements/contracts/chart-controller.md
 */
(function () {
  const GRID = 'rgba(90,122,159,0.22)';
  const TEXT = '#90caf9';
  const CROSS = 'rgba(144,202,249,0.75)';
  const HIT_PX = 6;
  const WHEEL_SETTLE_MS = 150;
  const ZOOM_STEP = 0.85;

  function niceExtent(vals) {
    const nums = vals.filter(v => v != null && !Number.isNaN(v));
    if (!nums.length) return null;
    let lo = Math.min(...nums), hi = Math.max(...nums);
    if (lo === hi) { lo -= 1; hi += 1; }
    const pad = (hi - lo) * 0.08;
    return { lo: lo - pad, hi: hi + pad };
  }

  /* index of the element in ascending `arr` closest to x */
  function nearestIndex(arr, x) {
    if (!arr.length) return -1;
    let lo = 0, hi = arr.length - 1;
    if (x <= arr[0]) return 0;
    if (x >= arr[hi]) return hi;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (arr[mid] === x) return mid;
      if (arr[mid] < x) lo = mid + 1; else hi = mid - 1;
    }
    return (x - arr[hi] <= arr[lo] - x) ? hi : lo;
  }

  function distToSeg(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const len2 = dx * dx + dy * dy;
    let t = len2 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = Math.max(0, Math.min(1, t));
    return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
  }

  window.createTimeSeriesChart = function createTimeSeriesChart(canvas, options) {
    const opts = options || {};
    let tzName = opts.tzName || 'UTC';
    const noDataText = opts.noDataText || 'no data';
    const onSelect = typeof opts.onSelect === 'function' ? opts.onSelect : function () {};
    const onZoomChange = typeof opts.onZoomChange === 'function' ? opts.onZoomChange : function () {};

    let times = [];
    let series = [];
    let full = null;      // { t0, t1 } — nominal period window
    let domain = null;    // { t0, t1 } — current (possibly zoomed) view
    let selected = null;
    let hover = null;     // { index }
    let frozenExt = null; // per-series extents held during a wheel gesture
    let wheelTimer = null;
    let zoomed = false;
    let raf = 0;
    let down = null;

    const wrap = canvas.parentNode;
    const tip = document.createElement('div');
    tip.className = 'chart-tip';
    tip.hidden = true;
    if (wrap) wrap.appendChild(tip);

    let hit = null;       // geometry snapshot for pointer hit-testing

    try { canvas.style.touchAction = 'none'; } catch (_) {}

    /* ---- helpers ------------------------------------------------------- */

    const spanOf = d => Math.max(1, d.t1 - d.t0);

    function fmt(epoch, withMinutes) {
      const s = domain ? spanOf(domain) : 86400;
      const o = { timeZone: tzName, hour12: false };
      if (!withMinutes && s > 3 * 86400) { o.month = 'short'; o.day = 'numeric'; }
      else if (s > 86400) { o.month = 'short'; o.day = 'numeric'; o.hour = '2-digit'; o.minute = '2-digit'; }
      else { o.hour = '2-digit'; o.minute = '2-digit'; }
      try { return new Intl.DateTimeFormat(undefined, o).format(new Date(epoch * 1000)); }
      catch (_) { return new Date(epoch * 1000).toISOString().slice(0, 16).replace('T', ' '); }
    }

    function seriesExtent(si) {
      if (!domain) return niceExtent(series[si].values);
      const vals = [];
      for (let i = 0; i < times.length; i++) {
        if (times[i] >= domain.t0 && times[i] <= domain.t1) vals.push(series[si].values[i]);
      }
      return niceExtent(vals.length ? vals : series[si].values);
    }

    const axisIdx = () => (selected != null && selected < series.length) ? selected : 0;
    const extents = () => frozenExt || series.map((s, i) => seriesExtent(i));

    function schedule() {
      if (raf) return;
      raf = requestAnimationFrame(() => { raf = 0; draw(); });
    }

    function digitsFor(e) {
      const r = Math.abs(e.hi - e.lo);
      return r >= 50 ? 0 : (r >= 5 ? 1 : 2);
    }
    const tipDigits = s => s.unit === '%' ? 0 : 1;

    /* ---- drawing ------------------------------------------------------- */

    function strokeSeries(ctx, s, xOf, yOf) {
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < times.length; i++) {
        const v = s.values[i];
        if (v == null || Number.isNaN(v)) { pen = false; continue; }
        const x = xOf(times[i]), y = yOf(v);
        if (pen) ctx.lineTo(x, y); else ctx.moveTo(x, y);
        pen = true;
      }
      ctx.stroke();
    }

    function draw() {
      const dpr = window.devicePixelRatio || 1;
      const cssW = canvas.clientWidth || 600;
      const cssH = canvas.clientHeight || 220;
      canvas.width = Math.round(cssW * dpr);
      canvas.height = Math.round(cssH * dpr);
      const ctx = canvas.getContext('2d');
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, cssW, cssH);
      ctx.font = '10px system-ui,sans-serif';
      ctx.textBaseline = 'middle';

      /* PAD.right is small — there is no right-hand axis (feature 010) */
      const PAD = { top: 26, right: 12, bottom: 22, left: 46 };
      const plotW = cssW - PAD.left - PAD.right;
      const plotH = cssH - PAD.top - PAD.bottom;
      hit = null;
      if (!domain || times.length < 2 || plotW <= 0 || plotH <= 0) { tip.hidden = true; return; }

      const d0 = domain.t0, d1 = domain.t1, dSpan = Math.max(1, d1 - d0);
      const xOf = t => PAD.left + ((t - d0) / dSpan) * plotW;
      const ext = extents();
      const aExt = ext[axisIdx()];

      /* gridlines + left-axis labels (from the axis series only) */
      ctx.strokeStyle = GRID;
      ctx.fillStyle = TEXT;
      ctx.lineWidth = 1;
      const rows = 4;
      for (let r = 0; r <= rows; r++) {
        const y = PAD.top + (r / rows) * plotH;
        ctx.beginPath();
        ctx.moveTo(PAD.left, y);
        ctx.lineTo(PAD.left + plotW, y);
        ctx.stroke();
        if (aExt) {
          const v = aExt.hi - (r / rows) * (aExt.hi - aExt.lo);
          ctx.textAlign = 'right';
          ctx.fillText(v.toFixed(digitsFor(aExt)), PAD.left - 6, y);
        }
      }

      /* x-axis time ticks across the full domain */
      ctx.fillStyle = TEXT;
      ctx.textAlign = 'center';
      for (let i = 0; i <= 4; i++) {
        const tt = d0 + (i / 4) * dSpan;
        const x = Math.max(PAD.left + 14, Math.min(PAD.left + plotW - 14, xOf(tt)));
        ctx.fillText(fmt(tt, false), x, cssH - PAD.bottom + 10);
      }

      /* series polylines */
      const yOfs = [];
      series.forEach((s, si) => {
        const e = ext[si];
        if (!e) { yOfs.push(null); return; }
        const yOf = v => PAD.top + (e.hi - v) / (e.hi - e.lo) * plotH;
        yOfs.push(yOf);
        const isSel = selected === si;
        const dim = selected != null && !isSel;
        if (isSel) {                       /* glow halo */
          ctx.strokeStyle = s.color;
          ctx.globalAlpha = 0.22;
          ctx.lineWidth = 7;
          strokeSeries(ctx, s, xOf, yOf);
          ctx.globalAlpha = 1;
        }
        ctx.strokeStyle = s.color;
        ctx.globalAlpha = dim ? 0.3 : 1;
        ctx.lineWidth = isSel ? 2.4 : 1.8;
        strokeSeries(ctx, s, xOf, yOf);
        ctx.globalAlpha = 1;
      });

      /* legend (top-left), with hit-rects */
      const legend = [];
      let lx = PAD.left;
      series.forEach((s, si) => {
        const label = s.unit ? `${s.label} (${s.unit})` : s.label;
        const w = 16 + ctx.measureText(label).width + 18;
        const sel = selected === si;
        ctx.globalAlpha = (selected != null && !sel) ? 0.4 : 1;
        ctx.fillStyle = s.color;
        ctx.fillRect(lx, PAD.top - 16, 10, 10);
        ctx.fillStyle = TEXT;
        ctx.textAlign = 'left';
        ctx.fillText((sel ? '▸ ' : '') + label, lx + 14, PAD.top - 11);
        ctx.globalAlpha = 1;
        legend.push({ x: lx - 4, y: PAD.top - 22, w: w, h: 20, index: si });
        lx += w;
      });

      hit = { plot: { left: PAD.left, top: PAD.top, w: plotW, h: plotH }, d0, d1, dSpan, xOf, yOfs, legend };

      /* crosshair + tooltip */
      if (hover && hover.index >= 0 && hover.index < times.length) {
        const hx = xOf(times[hover.index]);
        if (hx >= PAD.left - 0.5 && hx <= PAD.left + plotW + 0.5) {
          ctx.strokeStyle = CROSS;
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(hx, PAD.top);
          ctx.lineTo(hx, PAD.top + plotH);
          ctx.stroke();
          renderTip(hx, cssW, cssH);
        } else { tip.hidden = true; }
      } else { tip.hidden = true; }
    }

    function renderTip(hx, cssW, cssH) {
      const i = hover.index;
      while (tip.firstChild) tip.removeChild(tip.firstChild);
      const head = document.createElement('div');
      head.className = 'chart-tip-t';
      head.textContent = fmt(times[i], true);
      tip.appendChild(head);
      series.forEach(s => {
        const v = s.values[i];
        const row = document.createElement('div');
        row.className = 'chart-tip-r';
        const sw = document.createElement('i');
        try { sw.style.background = s.color; } catch (_) {}
        row.appendChild(sw);
        const txt = document.createElement('span');
        txt.textContent = s.label + ': ' +
          ((v == null || Number.isNaN(v)) ? noDataText : v.toFixed(tipDigits(s)) + ' ' + s.unit);
        row.appendChild(txt);
        tip.appendChild(row);
      });
      tip.hidden = false;
      const tw = tip.offsetWidth, th = tip.offsetHeight;
      let left = hx + 12;
      if (left + tw > cssW) left = hx - 12 - tw;
      if (left < 0) left = 0;
      let top = 6;
      if (top + th > cssH - 4) top = Math.max(0, cssH - 4 - th);
      tip.style.left = left + 'px';
      tip.style.top = top + 'px';
    }

    /* ---- interaction ------------------------------------------------- */

    function toPlot(ev) {
      const r = canvas.getBoundingClientRect();
      return { x: ev.clientX - r.left, y: ev.clientY - r.top };
    }

    function nearestLine(p) {
      let best = { d: HIT_PX + 1, si: -1 };
      if (!hit || p.x < hit.plot.left || p.x > hit.plot.left + hit.plot.w) return best;
      series.forEach((s, si) => {
        const yOf = hit.yOfs[si];
        if (!yOf) return;
        let prev = null;
        for (let i = 0; i < times.length; i++) {
          const v = s.values[i];
          if (v == null || Number.isNaN(v)) { prev = null; continue; }
          const cur = { x: hit.xOf(times[i]), y: yOf(v) };
          if (prev) {
            const dd = distToSeg(p.x, p.y, prev.x, prev.y, cur.x, cur.y);
            if (dd < best.d) best = { d: dd, si };
          }
          prev = cur;
        }
      });
      return best;
    }

    function onPointerMove(ev) {
      if (!hit || times.length < 2) return;
      const p = toPlot(ev);
      const near = p.x >= hit.plot.left - 2 && p.x <= hit.plot.left + hit.plot.w + 2 &&
                   p.y >= hit.plot.top - 6 && p.y <= hit.plot.top + hit.plot.h + 6;
      if (near) {
        const epoch = hit.d0 + (p.x - hit.plot.left) / hit.plot.w * hit.dSpan;
        hover = { index: nearestIndex(times, epoch) };
        schedule();
        try { canvas.style.cursor = nearestLine(p).si >= 0 ? 'pointer' : 'crosshair'; } catch (_) {}
      } else if (hover) {
        hover = null; schedule();
      }
    }

    function onPointerLeave() { if (hover) { hover = null; schedule(); } }

    function onPointerDown(ev) {
      const p = toPlot(ev);
      down = { x: p.x, y: p.y };
    }

    function onPointerUp(ev) {
      const p = toPlot(ev);
      const click = down && Math.hypot(p.x - down.x, p.y - down.y) < 5;
      down = null;
      if (ev.pointerType === 'touch' && hover) { hover = null; schedule(); }
      if (!click || !hit) return;
      for (const L of hit.legend) {
        if (p.x >= L.x && p.x <= L.x + L.w && p.y >= L.y && p.y <= L.y + L.h) {
          onSelect(selected === L.index ? null : L.index);
          return;
        }
      }
      if (p.x >= hit.plot.left && p.x <= hit.plot.left + hit.plot.w &&
          p.y >= hit.plot.top && p.y <= hit.plot.top + hit.plot.h) {
        const b = nearestLine(p);
        onSelect(b.si >= 0 ? (selected === b.si ? null : b.si) : null);
      }
    }

    function onWheel(ev) {
      ev.preventDefault();
      if (!domain || !full || times.length < 2 || !hit) return;
      const p = toPlot(ev);
      const px = Math.max(hit.plot.left, Math.min(hit.plot.left + hit.plot.w, p.x));
      const epoch = hit.d0 + (px - hit.plot.left) / hit.plot.w * hit.dSpan;
      const factor = ev.deltaY < 0 ? ZOOM_STEP : 1 / ZOOM_STEP;
      const fullSpan = spanOf(full);
      const minSpan = Math.max(60, fullSpan / 5000);
      if (!frozenExt) frozenExt = series.map((s, i) => seriesExtent(i));

      let t0 = epoch - (epoch - domain.t0) * factor;
      let t1 = epoch + (domain.t1 - epoch) * factor;
      if (t1 - t0 >= fullSpan) { t0 = full.t0; t1 = full.t1; }
      else {
        if (t1 - t0 < minSpan) { const c = (t0 + t1) / 2; t0 = c - minSpan / 2; t1 = c + minSpan / 2; }
        if (t0 < full.t0) { t1 += full.t0 - t0; t0 = full.t0; }
        if (t1 > full.t1) { t0 -= t1 - full.t1; t1 = full.t1; }
      }
      domain = { t0, t1 };

      const nowZoomed = spanOf(domain) < fullSpan * 0.999;
      if (nowZoomed !== zoomed) { zoomed = nowZoomed; onZoomChange(zoomed); }

      if (wheelTimer) clearTimeout(wheelTimer);
      wheelTimer = setTimeout(() => { wheelTimer = null; frozenExt = null; schedule(); }, WHEEL_SETTLE_MS);
      schedule();
    }

    canvas.addEventListener('pointermove', onPointerMove);
    canvas.addEventListener('pointerdown', onPointerDown);
    canvas.addEventListener('pointerup', onPointerUp);
    canvas.addEventListener('pointerleave', onPointerLeave);
    canvas.addEventListener('pointercancel', onPointerLeave);
    canvas.addEventListener('wheel', onWheel, { passive: false });

    let ro = null;
    if (typeof ResizeObserver === 'function') {
      ro = new ResizeObserver(() => schedule());
      ro.observe(canvas);
    } else {
      window.addEventListener('resize', schedule);
    }

    /* ---- public API -------------------------------------------------- */

    return {
      setData(spec) {
        times = (spec && spec.times) || [];
        series = (spec && spec.series) || [];
        if (selected != null && selected >= series.length) selected = null;
        hover = null;
        frozenExt = null;
        if (!full && times.length >= 2) full = { t0: times[0], t1: times[times.length - 1] };
        if (!domain && full) domain = { t0: full.t0, t1: full.t1 };
        schedule();
      },
      setPeriodWindow(w) {
        if (!w) return;
        full = { t0: w.from, t1: Math.max(w.from + 1, w.to) };
        domain = { t0: full.t0, t1: full.t1 };
        frozenExt = null;
        if (zoomed) { zoomed = false; onZoomChange(false); }
        schedule();
      },
      setSelected(i) {
        const n = (i == null || i < 0 || i >= series.length) ? null : i;
        if (n === selected) return;
        selected = n;
        frozenExt = null;
        schedule();
      },
      setTz(name) { tzName = name || 'UTC'; schedule(); },
      resetZoom() {
        if (full) domain = { t0: full.t0, t1: full.t1 };
        frozenExt = null;
        if (zoomed) { zoomed = false; onZoomChange(false); }
        schedule();
      },
      redraw() { schedule(); },
      destroy() {
        canvas.removeEventListener('pointermove', onPointerMove);
        canvas.removeEventListener('pointerdown', onPointerDown);
        canvas.removeEventListener('pointerup', onPointerUp);
        canvas.removeEventListener('pointerleave', onPointerLeave);
        canvas.removeEventListener('pointercancel', onPointerLeave);
        canvas.removeEventListener('wheel', onWheel);
        if (ro) ro.disconnect(); else window.removeEventListener('resize', schedule);
        if (wheelTimer) clearTimeout(wheelTimer);
        if (tip.parentNode) tip.parentNode.removeChild(tip);
      },
    };
  };
})();
