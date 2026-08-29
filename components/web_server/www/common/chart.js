'use strict';
/* Minimal dependency-free time-series renderer for the management page
 * history plot (feature 009). No external library — CSP is default-src 'self'
 * and the device serves this file from flash.
 *
 * window.drawTimeSeries(canvas, spec)
 *   spec.times  : number[]  epoch seconds, ascending
 *   spec.series : Array<{ label:string, color:string, unit:string,
 *                         values:(number|null)[] }>   (1..3 entries)
 *
 * Each series is scaled independently so temperature, pressure and humidity
 * can share one plot. The first series drives the left axis + gridlines, the
 * second (if any) the right axis; further series draw as lines only.
 */
(function () {
  const PAD = { top: 26, right: 46, bottom: 22, left: 46 };
  const AXIS = '#5a7a9f';
  const GRID = 'rgba(90,122,159,0.22)';
  const TEXT = '#90caf9';

  function niceExtent(vals) {
    const nums = vals.filter(v => v != null && !Number.isNaN(v));
    if (!nums.length) return null;
    let lo = Math.min(...nums), hi = Math.max(...nums);
    if (lo === hi) { lo -= 1; hi += 1; }
    const pad = (hi - lo) * 0.08;
    return { lo: lo - pad, hi: hi + pad };
  }

  function fmtTime(epoch, spanSec) {
    const d = new Date(epoch * 1000);
    const p = n => String(n).padStart(2, '0');
    if (spanSec > 3 * 86400) return `${p(d.getMonth() + 1)}-${p(d.getDate())}`;
    if (spanSec > 86400) return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}h`;
    return `${p(d.getHours())}:${p(d.getMinutes())}`;
  }

  window.drawTimeSeries = function drawTimeSeries(canvas, spec) {
    const times = spec && spec.times || [];
    const series = spec && spec.series || [];
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

    const plotW = cssW - PAD.left - PAD.right;
    const plotH = cssH - PAD.top - PAD.bottom;
    if (times.length < 2 || plotW <= 0 || plotH <= 0) return;

    const t0 = times[0], t1 = times[times.length - 1];
    const span = Math.max(1, t1 - t0);
    const xOf = t => PAD.left + ((t - t0) / span) * plotW;

    const extents = series.map(s => niceExtent(s.values));

    /* gridlines + left/right axis labels from series 0 / series 1 */
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
      if (extents[0]) {
        const v = extents[0].hi - (r / rows) * (extents[0].hi - extents[0].lo);
        ctx.textAlign = 'right';
        ctx.fillText(v.toFixed(0), PAD.left - 6, y);
      }
      if (extents[1]) {
        const v = extents[1].hi - (r / rows) * (extents[1].hi - extents[1].lo);
        ctx.textAlign = 'left';
        ctx.fillText(v.toFixed(0), PAD.left + plotW + 6, y);
      }
    }

    /* x axis ticks */
    ctx.strokeStyle = AXIS;
    ctx.fillStyle = TEXT;
    ctx.textAlign = 'center';
    const xticks = 4;
    for (let i = 0; i <= xticks; i++) {
      const tt = t0 + (i / xticks) * span;
      const x = xOf(tt);
      ctx.fillText(fmtTime(tt, span), x, cssH - PAD.bottom + 10);
    }

    /* series polylines */
    series.forEach((s, si) => {
      const ext = extents[si];
      if (!ext) return;
      const yOf = v => PAD.top + (ext.hi - v) / (ext.hi - ext.lo) * plotH;
      ctx.strokeStyle = s.color;
      ctx.lineWidth = 1.8;
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
    });

    /* legend */
    let lx = PAD.left;
    series.forEach(s => {
      ctx.fillStyle = s.color;
      ctx.fillRect(lx, PAD.top - 16, 10, 10);
      ctx.fillStyle = TEXT;
      ctx.textAlign = 'left';
      const label = s.unit ? `${s.label} (${s.unit})` : s.label;
      ctx.fillText(label, lx + 14, PAD.top - 11);
      lx += 16 + ctx.measureText(label).width + 18;
    });
  };
})();
