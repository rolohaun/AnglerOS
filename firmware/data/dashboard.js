// AnglerOS Dashboard: WebSocket bridge, G-code console, jog controls,
// draggable widgets, and stored macros.

(function () {
  // ---- Draggable dashboard panels ----
  const gridEl = document.getElementById('dashboard-grid');
  const resetLayoutBtn = document.getElementById('layout-reset');
  const DEFAULT_LAYOUT = ['print', 'temps', 'toolhead', 'machine', 'terminal', 'macros', 'extruder'];
  let draggedWidget = null;

  function widgetKey(el) { return el && el.dataset ? el.dataset.widget : null; }

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
    });

    resetLayoutBtn.addEventListener('click', () => {
      applyLayout(DEFAULT_LAYOUT);
      saveLayout();
    });
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
    return /^ok\s+T:/.test(line) || /^T:.*B:/.test(line);
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

  setInterval(() => { if (ws && ws.readyState === WebSocket.OPEN) send('M105', true); }, 8000);

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
        macros.splice(i, 1); saveMacros(); renderMacros();
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
  });

  function escapeHtml(s) {
    return s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  }

  // ---- Init ----
  wireDashboardDrag();
  setLink('disconnected');
  connect();
  loadMacros();
})();
