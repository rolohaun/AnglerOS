// AnglerOS SPA shell: tabs, status polling, Wi-Fi setup, and theme controls.

(function () {
  const DEFAULT_THEME = '#3fd06a';
  const views = {
    dashboard: 'Dashboard',
    configuration: 'Configuration',
    system: 'System',
  };

  // --- Theme ---
  const themeInput = document.getElementById('theme-color');
  const themeReset = document.getElementById('theme-reset');
  const themeSwatches = document.querySelectorAll('.theme-swatch');
  const brandNameEl = document.getElementById('brand-name');
  const printerNameInput = document.getElementById('printer-name');
  const printerNameSave = document.getElementById('printer-name-save');
  const printerNameClear = document.getElementById('printer-name-clear');
  const printerNameStatus = document.getElementById('printer-name-status');
  const PRINTER_NAME_KEY = 'angleros.printer.name';
  const DEFAULT_BRAND_HTML = 'Angler<span class="accent">OS</span>';
  let printerNameTimer = null;

  function clamp(n) { return Math.max(0, Math.min(255, n)); }

  function hexToRgb(hex) {
    const m = String(hex).trim().match(/^#?([0-9a-f]{6})$/i);
    if (!m) return null;
    const n = parseInt(m[1], 16);
    return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
  }

  function rgbToHex(rgb) {
    return '#' + [rgb.r, rgb.g, rgb.b].map((n) => clamp(Math.round(n)).toString(16).padStart(2, '0')).join('');
  }

  function mix(hex, target, amount) {
    const a = hexToRgb(hex);
    const b = hexToRgb(target);
    if (!a || !b) return hex;
    return rgbToHex({
      r: a.r + (b.r - a.r) * amount,
      g: a.g + (b.g - a.g) * amount,
      b: a.b + (b.b - a.b) * amount,
    });
  }

  function applyTheme(color, persist) {
    const accent = hexToRgb(color) ? color.toLowerCase() : DEFAULT_THEME;
    document.documentElement.style.setProperty('--accent', accent);
    document.documentElement.style.setProperty('--accent-dim', mix(accent, '#101215', .35));
    document.documentElement.style.setProperty('--accent-hover', mix(accent, '#ffffff', .16));
    if (themeInput) themeInput.value = accent;
    themeSwatches.forEach((b) => b.classList.toggle('active', b.dataset.theme.toLowerCase() === accent));
    if (persist) {
      try { localStorage.setItem('angleros.theme.accent', accent); } catch (e) {}
    }
  }

  try { applyTheme(localStorage.getItem('angleros.theme.accent') || DEFAULT_THEME, false); }
  catch (e) { applyTheme(DEFAULT_THEME, false); }

  if (themeInput) {
    themeInput.addEventListener('input', () => applyTheme(themeInput.value, true));
  }
  themeSwatches.forEach((b) => b.addEventListener('click', () => applyTheme(b.dataset.theme, true)));
  if (themeReset) themeReset.addEventListener('click', () => applyTheme(DEFAULT_THEME, true));

  // --- Printer name ---
  function normalizePrinterName(name) {
    return String(name || '').replace(/\s+/g, ' ').trim().slice(0, 32);
  }

  function setPrinterNameStatus(text) {
    if (printerNameStatus) printerNameStatus.textContent = text || '';
  }

  function applyPrinterName(name, syncInput) {
    const clean = normalizePrinterName(name);
    if (brandNameEl) {
      if (clean) brandNameEl.textContent = clean;
      else brandNameEl.innerHTML = DEFAULT_BRAND_HTML;
    }
    document.title = clean || 'AnglerOS';
    if (syncInput !== false && printerNameInput && printerNameInput.value !== clean) printerNameInput.value = clean;
  }

  function storePrinterName(name) {
    try {
      const clean = normalizePrinterName(name);
      if (clean) localStorage.setItem(PRINTER_NAME_KEY, clean);
      else localStorage.removeItem(PRINTER_NAME_KEY);
    } catch (e) {}
  }

  async function savePrinterName(name, quiet) {
    const clean = normalizePrinterName(name);
    applyPrinterName(clean);
    storePrinterName(clean);
    if (!quiet) setPrinterNameStatus('Saving...');
    try {
      const r = await fetch('/api/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ printer_name: clean }),
      });
      if (!r.ok) throw new Error(r.status);
      if (!quiet) setPrinterNameStatus('Saved');
    } catch (e) {
      if (!quiet) setPrinterNameStatus('Saved locally');
    }
  }

  function queuePrinterNameSave() {
    clearTimeout(printerNameTimer);
    printerNameTimer = setTimeout(() => savePrinterName(printerNameInput.value, true), 700);
  }

  async function loadPrinterName() {
    let localName = '';
    try { localName = localStorage.getItem(PRINTER_NAME_KEY) || ''; } catch (e) {}
    applyPrinterName(localName);
    try {
      const r = await fetch('/api/settings', { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      const settings = await r.json();
      if (settings && Object.prototype.hasOwnProperty.call(settings, 'printer_name')) {
        const name = normalizePrinterName(settings.printer_name);
        applyPrinterName(name);
        storePrinterName(name);
      }
    } catch (e) {}
  }

  if (printerNameInput) {
    printerNameInput.addEventListener('input', () => {
      applyPrinterName(printerNameInput.value, false);
      queuePrinterNameSave();
      setPrinterNameStatus('');
    });
  }
  if (printerNameSave) printerNameSave.addEventListener('click', () => savePrinterName(printerNameInput.value, false));
  if (printerNameClear) printerNameClear.addEventListener('click', () => savePrinterName('', false));
  loadPrinterName();

  // --- Tab navigation ---
  const navItems = document.querySelectorAll('.nav-item');
  const title = document.getElementById('view-title');

  function show(view) {
    if (!views[view] || !document.getElementById('view-' + view)) view = 'dashboard';
    document.querySelectorAll('.view').forEach((v) => v.classList.remove('active'));
    document.getElementById('view-' + view).classList.add('active');
    navItems.forEach((n) => n.classList.toggle('active', n.dataset.view === view));
    title.textContent = views[view] || view;
    try { localStorage.setItem('angleros.view', view); } catch (e) {}
    window.dispatchEvent(new CustomEvent('angleros:view', { detail: { view } }));
  }

  navItems.forEach((n) => n.addEventListener('click', () => show(n.dataset.view)));

  let start = 'dashboard';
  try { start = localStorage.getItem('angleros.view') || start; } catch (e) {}
  show(start);

  // --- Status polling ---
  const dot = document.getElementById('status-dot');
  const text = document.getElementById('status-text');
  const ipEl = document.getElementById('status-ip');
  const fwEl = document.getElementById('fw-version');
  const setup = document.getElementById('setup');
  let submitting = false;

  function setText(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
  }

  function setMeter(id, value) {
    const el = document.getElementById(id);
    if (el) el.style.width = Math.max(0, Math.min(100, value || 0)) + '%';
  }

  function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) return '--';
    if (bytes >= 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + ' MB';
    if (bytes >= 1024) return Math.round(bytes / 1024) + ' KB';
    return bytes + ' B';
  }

  function formatUptime(seconds) {
    if (!Number.isFinite(seconds)) return '--';
    const d = Math.floor(seconds / 86400);
    const h = Math.floor((seconds % 86400) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    if (d) return d + 'd ' + h + 'h';
    if (h) return h + 'h ' + m + 'm';
    return m + 'm';
  }

  function usedPercent(free, total) {
    if (!Number.isFinite(free) || !Number.isFinite(total) || total <= 0) return 0;
    return Math.round(((total - free) / total) * 100);
  }

  function usedBytesPercent(used, total) {
    if (!Number.isFinite(used) || !Number.isFinite(total) || total <= 0) return 0;
    return Math.round((used / total) * 100);
  }

  function storageNote(used, total) {
    if (!Number.isFinite(used) || !Number.isFinite(total) || total <= 0) return 'Capacity unknown';
    return formatBytes(used) + ' used / ' + formatBytes(total) + ' total';
  }

  function updateSystem(s) {
    const cpu = Number.isFinite(s.cpu_load) ? s.cpu_load : 0;
    setText('sys-cpu', cpu + '%');
    setMeter('sys-cpu-bar', cpu);
    setText('sys-cpu-note', s.cpu_freq_mhz ? 'Loop-load estimate at ' + s.cpu_freq_mhz + ' MHz' : 'Loop-load estimate');

    const ramUsed = usedPercent(s.heap, s.heap_total);
    setText('sys-ram', ramUsed ? ramUsed + '% used' : formatBytes(s.heap) + ' free');
    setText('sys-ram-free', formatBytes(s.heap) + ' free / ' + formatBytes(s.heap_total) + ' total');
    setMeter('sys-ram-bar', ramUsed);

    const psramUsed = usedPercent(s.psram, s.psram_total);
    setText('sys-psram', s.psram_total ? psramUsed + '% used' : 'Not detected');
    setText('sys-psram-free', s.psram_total ? formatBytes(s.psram) + ' free / ' + formatBytes(s.psram_total) + ' total' : 'No PSRAM reported');
    setMeter('sys-psram-bar', psramUsed);

    const lfsUsed = usedBytesPercent(s.fs_used, s.fs_total);
    setText('sys-lfs', s.fs_total ? lfsUsed + '% used' : '--');
    setText('sys-lfs-free', s.fs_total ? storageNote(s.fs_used, s.fs_total) : 'LittleFS unavailable');
    setMeter('sys-lfs-bar', lfsUsed);

    const sdUsed = usedBytesPercent(s.sd_used, s.sd_total);
    setText('sys-sd', s.sd_mounted ? sdUsed + '% used' : 'Not mounted');
    setText('sys-sd-free', s.sd_mounted
      ? (s.sd_type || 'SD') + ' / ' + storageNote(s.sd_used, s.sd_total)
      : (s.sd_status || 'No SD card mounted'));
    setMeter('sys-sd-bar', sdUsed);

    setText('sys-uptime', formatUptime(s.uptime));
    const network = s.mode === 'sta'
      ? (s.ssid || 'Wi-Fi') + ' / ' + (s.rssi !== undefined ? s.rssi + ' dBm' : 'RSSI unknown')
      : 'Setup AP / ' + (s.ip || 'no IP');
    setText('sys-network', network + (s.printer_uart === false ? ' / UART unavailable' : ''));
  }

  async function poll() {
    try {
      const r = await fetch('/api/status', { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      const s = await r.json();
      dot.className = 'dot dot-on';
      text.textContent = s.mode === 'sta' ? 'online' : 'setup mode';
      ipEl.textContent = s.ip ? s.ip : '';
      if (s.fw) fwEl.textContent = 'v' + s.fw;
      if (!submitting) setup.hidden = s.mode !== 'ap';
      updateSystem(s);
      window.dispatchEvent(new CustomEvent('angleros:status', { detail: s }));
    } catch (e) {
      dot.className = 'dot dot-off';
      text.textContent = 'offline';
      ipEl.textContent = '';
      setText('sys-network', 'Status API unavailable');
    }
  }

  // --- Wi-Fi provisioning ---
  const form = document.getElementById('setup-form');
  const saveBtn = document.getElementById('setup-save');
  const msg = document.getElementById('setup-msg');

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    submitting = true;
    saveBtn.disabled = true;
    msg.textContent = 'Saving...';
    const body = new URLSearchParams({
      ssid: document.getElementById('ssid').value,
      pass: document.getElementById('pass').value,
    });
    try {
      const r = await fetch('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body,
      });
      if (!r.ok) throw new Error(r.status);
      msg.textContent =
        'Saved. Rebooting and joining your network. Reconnect your device to ' +
        'that Wi-Fi, then find AnglerOS at its new address.';
    } catch (err) {
      submitting = false;
      saveBtn.disabled = false;
      msg.textContent = 'Could not save credentials. Try again.';
    }
  });

  poll();
  setInterval(poll, 3000);
})();
