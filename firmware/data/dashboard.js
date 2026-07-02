// AnglerOS Dashboard: WebSocket bridge, G-code console, jog controls,
// draggable widgets, and stored macros.

(function () {
  // ---- Draggable dashboard panels ----
  const gridEl = document.getElementById('dashboard-grid');
  const resetLayoutBtn = document.getElementById('layout-reset');
  const DEFAULT_LAYOUT = ['print', 'camera', 'files', 'temps', 'toolhead', 'machine', 'light', 'terminal', 'macros', 'extruder'];
  let draggedWidget = null;
  let masonryFrame = 0;

  function widgetKey(el) { return el && el.dataset ? el.dataset.widget : null; }

  function resizeMasonryItem(widget) {
    if (!widget) return;
    const style = getComputedStyle(gridEl);
    const rowHeight = parseFloat(style.gridAutoRows) || 8;
    const rowGap = parseFloat(style.rowGap) || 0;
    widget.style.gridRowEnd = 'auto';
    const rows = Math.ceil((widget.getBoundingClientRect().height + rowGap) / (rowHeight + rowGap));
    widget.style.gridRowEnd = 'span ' + Math.max(1, rows);
  }

  function resizeMasonry() {
    cancelAnimationFrame(masonryFrame);
    masonryFrame = requestAnimationFrame(() => {
      gridEl.querySelectorAll('[data-widget]').forEach(resizeMasonryItem);
    });
  }

  function wireMasonry() {
    resizeMasonry();
    if ('ResizeObserver' in window) {
      const observer = new ResizeObserver(resizeMasonry);
      gridEl.querySelectorAll('[data-widget]').forEach((widget) => observer.observe(widget));
    }
    window.addEventListener('resize', resizeMasonry);
    window.addEventListener('angleros:view', (e) => {
      if (e.detail && e.detail.view === 'dashboard') resizeMasonry();
    });
  }

  function saveLayout() {
    try {
      const order = Array.from(gridEl.querySelectorAll('[data-widget]')).map(widgetKey);
      localStorage.setItem('angleros.dashboard.layout', JSON.stringify(order));
    } catch (e) {}
  }

  function applyLayout(order) {
    if (!Array.isArray(order)) return;
    const byKey = new Map(Array.from(gridEl.querySelectorAll('[data-widget]')).map((el) => [widgetKey(el), el]));
    const placed = new Set();
    order.forEach((key) => {
      const el = byKey.get(key);
      if (el) {
        gridEl.appendChild(el);
        placed.add(key);
      }
    });
    DEFAULT_LAYOUT.forEach((key) => {
      const el = byKey.get(key);
      if (el && !placed.has(key)) gridEl.appendChild(el);
    });
    resizeMasonry();
  }

  function loadLayout() {
    try { applyLayout(JSON.parse(localStorage.getItem('angleros.dashboard.layout'))); } catch (e) {}
  }

  function clearDropMarks() {
    gridEl.querySelectorAll('.drop-before, .drop-after').forEach((el) => {
      el.classList.remove('drop-before', 'drop-after');
    });
  }

  function dragTarget(e) {
    const target = e.target.closest('[data-widget]');
    return target && target !== draggedWidget ? target : null;
  }

  function wireDashboardDrag() {
    loadLayout();
    gridEl.querySelectorAll('[data-widget]').forEach((widget) => {
      widget.addEventListener('dragstart', (e) => {
        if (e.target.closest('button, input, select, textarea')) {
          e.preventDefault();
          return;
        }
        draggedWidget = widget;
        widget.classList.add('dragging');
        e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/plain', widgetKey(widget));
      });
      widget.addEventListener('dragend', () => {
        widget.classList.remove('dragging');
        clearDropMarks();
        draggedWidget = null;
        saveLayout();
        resizeMasonry();
      });
    });

    gridEl.addEventListener('dragover', (e) => {
      if (!draggedWidget) return;
      e.preventDefault();
      const target = dragTarget(e);
      clearDropMarks();
      if (!target) return;
      const rect = target.getBoundingClientRect();
      const after = e.clientX > rect.left + rect.width / 2;
      target.classList.add(after ? 'drop-after' : 'drop-before');
    });

    gridEl.addEventListener('drop', (e) => {
      if (!draggedWidget) return;
      e.preventDefault();
      const target = dragTarget(e);
      if (target) {
        const rect = target.getBoundingClientRect();
        const after = e.clientX > rect.left + rect.width / 2;
        gridEl.insertBefore(draggedWidget, after ? target.nextSibling : target);
      }
      clearDropMarks();
      saveLayout();
      resizeMasonry();
    });

    resetLayoutBtn.addEventListener('click', () => {
      applyLayout(DEFAULT_LAYOUT);
      saveLayout();
    });

    wireMasonry();
  }

  // ---- WebSocket link to the printer ----
  const logEl = document.getElementById('term-log');
  const linkEl = document.getElementById('link-state');
  let ws = null;

  function setLink(state) {
    linkEl.textContent = state;
    linkEl.className = 'link-pill ' + (state === 'connected' ? 'link-on' : 'link-off');
  }

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    ws = new WebSocket(proto + location.host + '/ws');
    ws.onopen = () => setLink('connected');
    ws.onclose = () => { setLink('disconnected'); setTimeout(connect, 2000); };
    ws.onerror = () => ws.close();
    ws.onmessage = (e) => handleLine(e.data);
  }

  function send(line, silent) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      if (!silent) appendLog(line, 'sent');
      ws.send(line);
    } else if (!silent) {
      appendLog('! not connected: ' + line, 'err');
    }
  }

  // ---- Console log ----
  function appendLog(text, cls) {
    const atBottom = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 40;
    const div = document.createElement('div');
    div.className = 'log-line' + (cls ? ' ' + cls : '');
    div.textContent = text;
    logEl.appendChild(div);
    while (logEl.childElementCount > 500) logEl.removeChild(logEl.firstChild);
    if (atBottom) logEl.scrollTop = logEl.scrollHeight;
  }

  function handleLine(line) {
    parseTemps(line);
    if (isTempNoise(line)) return;
    appendLog(line);
  }

  function isTempNoise(line) {
    // Temp reports, and the bare per-command "ok" acks that flood the console
    // while a print job streams.
    return /^ok\s+T:/.test(line) || /^T:.*B:/.test(line) || /^ok\s*$/.test(line);
  }

  // ---- Temperatures ----
  const tHot = document.getElementById('t-hotend');
  const tHotSet = document.getElementById('t-hotend-set');
  const tBed = document.getElementById('t-bed');
  const tBedSet = document.getElementById('t-bed-set');

  function parseTemps(line) {
    const h = line.match(/T:\s*(-?[\d.]+)\s*\/\s*(-?[\d.]+)/);
    const b = line.match(/B:\s*(-?[\d.]+)\s*\/\s*(-?[\d.]+)/);
    if (h) { tHot.textContent = Math.round(h[1]); tHotSet.textContent = Math.round(h[2]); }
    if (b) { tBed.textContent = Math.round(b[1]); tBedSet.textContent = Math.round(b[2]); }
  }

  // While a job streams, Marlin auto-reports temps (M155) and polling M105
  // would inject stray "ok" acks that corrupt the job's flow control.
  setInterval(() => {
    if (jobActive) return;
    if (ws && ws.readyState === WebSocket.OPEN) send('M105', true);
  }, 8000);

  // ---- Console input ----
  document.getElementById('term-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const input = document.getElementById('term-cmd');
    const cmd = input.value.trim();
    if (cmd) send(cmd);
    input.value = '';
  });

  // ---- Jog ----
  function jogStep() { return document.getElementById('jog-step').value; }
  function jog(axis, dir) {
    const d = (dir < 0 ? '-' : '') + jogStep();
    const feed = axis === 'Z' ? 600 : 3000;
    send('G91'); send('G1 ' + axis + d + ' F' + feed); send('G90');
  }

  document.querySelectorAll('[data-jog]').forEach((b) => b.addEventListener('click', () => {
    const a = b.dataset.jog;
    if (a === 'home') return send('G28');
    jog(a[0], a[1] === '+' ? 1 : -1);
  }));
  document.getElementById('motors-off').addEventListener('click', () => send('M84'));

  // ---- Flash LED ----
  const flashSlider = document.getElementById('flash-brightness');
  const flashLabel = document.getElementById('flash-brightness-label');
  const flashToggle = document.getElementById('flash-toggle');
  const flashOff = document.getElementById('flash-off');
  const flashStatus = document.getElementById('flash-status');
  let lastFlashBrightness = 60;
  let flashTimer = null;

  function clampPercent(value) {
    return Math.max(0, Math.min(100, parseInt(value, 10) || 0));
  }

  function applyFlashUi(value, status) {
    const brightness = clampPercent(value);
    if (flashSlider) flashSlider.value = brightness;
    if (flashLabel) flashLabel.textContent = brightness + ' %';
    if (flashToggle) flashToggle.textContent = brightness > 0 ? 'On' : 'Turn on';
    if (flashStatus) flashStatus.textContent = status || (brightness > 0 ? 'Flash LED on' : 'Flash LED off');
    if (brightness > 0) lastFlashBrightness = brightness;
    resizeMasonry();
  }

  async function saveFlashBrightness(value, quiet) {
    const brightness = clampPercent(value);
    applyFlashUi(brightness, quiet ? undefined : 'Setting brightness...');
    try {
      const body = new URLSearchParams({ brightness: String(brightness) });
      const r = await fetch('/api/flash', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body,
      });
      if (!r.ok) throw new Error(r.status);
      applyFlashUi(brightness);
    } catch (e) {
      applyFlashUi(brightness, 'Local preview only');
    }
  }

  function queueFlashSave(value) {
    clearTimeout(flashTimer);
    flashTimer = setTimeout(() => saveFlashBrightness(value, true), 120);
  }

  async function loadFlashBrightness() {
    try {
      const r = await fetch('/api/flash', { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      const data = await r.json();
      applyFlashUi(data.brightness || 0);
    } catch (e) {
      applyFlashUi(0, 'Flash LED unavailable');
    }
  }

  if (flashSlider) {
    flashSlider.addEventListener('input', () => {
      applyFlashUi(flashSlider.value);
      queueFlashSave(flashSlider.value);
    });
    flashSlider.addEventListener('change', () => saveFlashBrightness(flashSlider.value, false));
  }
  if (flashToggle) {
    flashToggle.addEventListener('click', () => {
      const next = clampPercent(flashSlider.value) > 0 ? 0 : lastFlashBrightness;
      saveFlashBrightness(next, false);
    });
  }
  if (flashOff) flashOff.addEventListener('click', () => saveFlashBrightness(0, false));

  // ---- Macros ----
  const listEl = document.getElementById('macro-list');
  const editor = document.getElementById('macro-editor');
  const form = document.getElementById('macro-form');
  const nameEl = document.getElementById('macro-name');
  const gcodeEl = document.getElementById('macro-gcode');
  let macros = [];
  let editIndex = -1;

  const DEFAULT_MACROS = [
    { name: 'Home All', gcode: 'G28' },
    { name: 'Firmware Info', gcode: 'M115' },
    { name: 'Report Position', gcode: 'M114' },
    { name: 'Disable Steppers', gcode: 'M84' },
    { name: 'Delta Auto-Calibration', gcode: 'G28\nG33' },
    { name: 'Stepper Movement Test', gcode: 'G91\nG1 X10 F1500\nG1 X-10\nG1 Y10\nG1 Y-10\nG1 Z10\nG1 Z-10\nG90' },
  ];

  async function loadMacros() {
    try {
      const r = await fetch('/api/macros', { cache: 'no-store' });
      const data = await r.json();
      macros = Array.isArray(data) && data.length ? data : DEFAULT_MACROS.slice();
      if (!Array.isArray(data) || !data.length) saveMacros();
    } catch (e) {
      macros = DEFAULT_MACROS.slice();
    }
    renderMacros();
    resizeMasonry();
  }

  async function saveMacros() {
    try {
      await fetch('/api/macros', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(macros),
      });
    } catch (e) { /* stays in memory */ }
  }

  function runMacro(m) {
    m.gcode.split('\n').map((l) => l.trim()).filter(Boolean).forEach(send);
  }

  function renderMacros() {
    listEl.innerHTML = '';
    macros.forEach((m, i) => {
      const row = document.createElement('div');
      row.className = 'macro-row';
      row.innerHTML =
        `<button class="macro-run" title="Run">${escapeHtml(m.name)}</button>` +
        '<button class="macro-edit btn-icon" title="Edit">E</button>' +
        '<button class="macro-del btn-icon" title="Delete">X</button>';
      row.querySelector('.macro-run').addEventListener('click', () => runMacro(m));
      row.querySelector('.macro-edit').addEventListener('click', () => openEditor(i));
      row.querySelector('.macro-del').addEventListener('click', () => {
        macros.splice(i, 1); saveMacros(); renderMacros(); resizeMasonry();
      });
      listEl.appendChild(row);
    });
  }

  function openEditor(index) {
    editIndex = index;
    const m = index >= 0 ? macros[index] : { name: '', gcode: '' };
    nameEl.value = m.name;
    gcodeEl.value = m.gcode;
    editor.hidden = false;
    nameEl.focus();
  }

  document.getElementById('macro-add').addEventListener('click', () => openEditor(-1));
  document.getElementById('macro-cancel').addEventListener('click', () => { editor.hidden = true; });
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    const m = { name: nameEl.value.trim(), gcode: gcodeEl.value.trim() };
    if (!m.name) return;
    if (editIndex >= 0) macros[editIndex] = m; else macros.push(m);
    saveMacros(); renderMacros();
    editor.hidden = true;
    resizeMasonry();
  });

  function escapeHtml(s) {
    return s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  }

  // ---- Print job ----
  const jobFileEl = document.getElementById('job-file');
  const jobBarEl = document.getElementById('job-bar');
  const jobStateEl = document.getElementById('job-state');
  const jobPctEl = document.getElementById('job-pct');
  const jobElapsedEl = document.getElementById('job-elapsed');
  const jobSentEl = document.getElementById('job-sent');
  const jobPauseBtn = document.getElementById('job-pause');
  const jobResumeBtn = document.getElementById('job-resume');
  const jobCancelBtn = document.getElementById('job-cancel');
  let jobActive = false;

  function fmtBytes(n) {
    if (!n && n !== 0) return '--';
    if (n >= 1048576) return (n / 1048576).toFixed(1) + ' MB';
    if (n >= 1024) return (n / 1024).toFixed(0) + ' KB';
    return n + ' B';
  }

  function fmtElapsed(sec) {
    if (!sec && sec !== 0) return '--';
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
    return (h ? h + 'h ' : '') + m + 'm ' + s + 's';
  }

  async function printControl(action) {
    try {
      const r = await fetch('/api/print/' + action, { method: 'POST' });
      const data = await r.json();
      if (!data.ok && data.err) appendLog('! ' + data.err, 'err');
    } catch (e) {
      appendLog('! print ' + action + ' failed', 'err');
    }
  }

  function updateJob(p) {
    if (!p) return;
    jobActive = p.state === 'printing' || p.state === 'paused';
    jobFileEl.textContent = p.file && p.file.length ? p.file : 'No active job';
    jobStateEl.textContent = p.state;
    jobPctEl.textContent = jobActive || p.state === 'done' ? p.progress + '%' : '--';
    jobBarEl.style.width = (jobActive || p.state === 'done' ? p.progress : 0) + '%';
    jobElapsedEl.textContent = jobActive || p.state === 'done' ? fmtElapsed(p.elapsed) : '--';
    jobSentEl.textContent = p.bytes_total
      ? fmtBytes(p.bytes_sent) + ' / ' + fmtBytes(p.bytes_total) : '--';
    jobPauseBtn.disabled = p.state !== 'printing';
    jobResumeBtn.disabled = p.state !== 'paused';
    jobCancelBtn.disabled = !jobActive;
  }

  jobPauseBtn.addEventListener('click', () => printControl('pause'));
  jobResumeBtn.addEventListener('click', () => printControl('resume'));
  jobCancelBtn.addEventListener('click', () => {
    if (confirm('Cancel the current print?')) printControl('cancel');
  });

  // Emergency stop: halt Marlin immediately and stop streaming.
  const estopBtn = document.querySelector('.estop');
  if (estopBtn) {
    estopBtn.addEventListener('click', () => {
      send('M112');
      printControl('cancel');
    });
  }

  // ---- G-code files ----
  const gcodeListEl = document.getElementById('gcode-list');
  const filesStatusEl = document.getElementById('files-status');
  const uploadInput = document.getElementById('gcode-upload');

  async function loadFiles() {
    try {
      const r = await fetch('/api/gcode/list', { cache: 'no-store' });
      const data = await r.json();
      renderFiles(data);
    } catch (e) {
      filesStatusEl.textContent = 'Could not load file list';
    }
  }

  function renderFiles(data) {
    gcodeListEl.innerHTML = '';
    const files = (data && data.files) || [];
    if (!files.length) {
      gcodeListEl.innerHTML = '<div class="muted gcode-empty">No files yet - upload a .gcode file.</div>';
    }
    files.forEach((f) => {
      const row = document.createElement('div');
      row.className = 'macro-row';
      row.innerHTML =
        `<button class="macro-run gcode-name" title="Print">${escapeHtml(f.name)}` +
        `<span class="gcode-size">${fmtBytes(f.size)}</span></button>` +
        '<button class="btn-icon gcode-print" title="Print">&#9654;</button>' +
        '<button class="btn-icon gcode-del" title="Delete">X</button>';
      const startPrint = () => {
        if (jobActive) { appendLog('! a job is already running', 'err'); return; }
        if (!confirm('Start printing ' + f.name + '?')) return;
        const body = new URLSearchParams({ name: f.name });
        fetch('/api/print/start', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body,
        }).then((r) => r.json()).then((d) => {
          if (!d.ok) appendLog('! ' + (d.err || 'could not start print'), 'err');
          else appendLog('Printing ' + f.name, 'sent');
        }).catch(() => appendLog('! could not start print', 'err'));
      };
      row.querySelector('.gcode-name').addEventListener('click', startPrint);
      row.querySelector('.gcode-print').addEventListener('click', startPrint);
      row.querySelector('.gcode-del').addEventListener('click', () => {
        if (!confirm('Delete ' + f.name + '?')) return;
        const body = new URLSearchParams({ name: f.name });
        fetch('/api/gcode/delete', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body,
        }).then(() => loadFiles());
      });
      gcodeListEl.appendChild(row);
    });
    if (data) {
      filesStatusEl.textContent =
        (data.storage === 'sd' ? 'SD card' : 'Internal storage') +
        ' - ' + fmtBytes(data.free) + ' free';
    }
    resizeMasonry();
  }

  if (uploadInput) {
    uploadInput.addEventListener('change', () => {
      const file = uploadInput.files[0];
      if (!file) return;
      const xhr = new XMLHttpRequest();
      const form = new FormData();
      form.append('file', file, file.name);
      xhr.open('POST', '/api/gcode/upload');
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          filesStatusEl.textContent = 'Uploading ' + Math.round((e.loaded / e.total) * 100) + '%';
        }
      };
      xhr.onload = () => {
        uploadInput.value = '';
        if (xhr.status === 200) { filesStatusEl.textContent = 'Uploaded ' + file.name; loadFiles(); }
        else if (xhr.status === 507) filesStatusEl.textContent = 'Not enough storage for ' + file.name;
        else filesStatusEl.textContent = 'Upload failed (' + xhr.status + ')';
      };
      xhr.onerror = () => { filesStatusEl.textContent = 'Upload failed'; uploadInput.value = ''; };
      xhr.send(form);
    });
  }

  // ---- Camera ----
  const camImg = document.getElementById('cam-stream');
  const camToggle = document.getElementById('cam-toggle');
  const camPlaceholder = document.getElementById('cam-placeholder');
  const camStatus = document.getElementById('cam-status');
  let camAvailable = false;
  let camRunning = false;

  function camUrl(path) {
    return location.protocol + '//' + location.hostname + ':81' + path;
  }

  function setCamRunning(on) {
    camRunning = on && camAvailable;
    if (camRunning) {
      camImg.src = camUrl('/stream');
      camImg.style.display = 'block';
      camPlaceholder.style.display = 'none';
      camToggle.textContent = 'Stop';
    } else {
      camImg.removeAttribute('src');
      camImg.style.display = 'none';
      camPlaceholder.style.display = 'flex';
      camPlaceholder.textContent = camAvailable ? 'Camera off' : 'No camera detected';
      camToggle.textContent = 'Start';
    }
    resizeMasonry();
  }

  if (camToggle) camToggle.addEventListener('click', () => setCamRunning(!camRunning));
  if (camImg) camImg.addEventListener('error', () => { if (camRunning) setCamRunning(false); });

  let camInitDone = false;
  window.addEventListener('angleros:status', (e) => {
    const s = e.detail || {};
    updateJob(s.print);
    if (typeof s.camera !== 'boolean') return;

    if (!camInitDone) {
      camInitDone = true;
      camAvailable = s.camera;
      camToggle.disabled = !camAvailable;
      camStatus.textContent = camAvailable ? 'Live view + print light (Flash LED panel)' : '';
      // Defer auto-start so the stream doesn't compete with the page's own
      // assets for the ESP32's limited Wi-Fi throughput while loading.
      if (camAvailable) setTimeout(() => { if (!camRunning) setCamRunning(true); }, 1500);
      return;
    }

    // Track camera enable/disable done from the System tab without a reload.
    if (s.camera !== camAvailable) {
      camAvailable = s.camera;
      camToggle.disabled = !camAvailable;
      camStatus.textContent = camAvailable ? 'Live view + print light (Flash LED panel)' : 'Camera disabled in System tab';
      if (!camAvailable && camRunning) setCamRunning(false);
      else if (camAvailable && !camRunning) setCamRunning(true);
      else setCamRunning(camRunning);
    }
  });

  // Pause the stream while the tab is hidden — it otherwise keeps saturating
  // the board's Wi-Fi in the background.
  let camPausedByTab = false;
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
      if (camRunning) { camPausedByTab = true; setCamRunning(false); }
    } else if (camPausedByTab) {
      camPausedByTab = false;
      setTimeout(() => setCamRunning(true), 400);
    }
  });

  // ---- Init ----
  wireDashboardDrag();
  setLink('disconnected');
  connect();
  loadFlashBrightness();
  loadMacros();
  loadFiles();
  setCamRunning(false);
})();
