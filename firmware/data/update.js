// Runtime Marlin settings editor. Reads M503/M420 reports from the shared
// printer WebSocket, applies supported edits, and ends every save with M500.
(function () {
  const marlin = window.AnglerMarlinSettings;
  if (!marlin) return;

  const readBtn = document.getElementById('update-read');
  const saveBtn = document.getElementById('update-save');
  const meshReadBtn = document.getElementById('update-mesh-read');
  const meshRebuildBtn = document.getElementById('update-mesh-rebuild');
  const meshPointBtn = document.getElementById('update-mesh-point-save');
  const statusEl = document.getElementById('update-status');
  const reportEl = document.getElementById('update-report');

  const bindings = [
    ['update-steps-x', 'm92', 'X'], ['update-steps-y', 'm92', 'Y'],
    ['update-steps-z', 'm92', 'Z'], ['update-steps-e', 'm92', 'E'],
    ['update-hotend-p', 'm301', 'P'], ['update-hotend-i', 'm301', 'I'],
    ['update-hotend-d', 'm301', 'D'], ['update-bed-p', 'm304', 'P'],
    ['update-bed-i', 'm304', 'I'], ['update-bed-d', 'm304', 'D'],
    ['update-probe-x', 'm851', 'X'], ['update-probe-y', 'm851', 'Y'],
    ['update-probe-z', 'm851', 'Z'], ['update-linear-k', 'm900', 'K'],
    ['update-current-x', 'm906', 'X'], ['update-current-y', 'm906', 'Y'],
    ['update-current-z', 'm906', 'Z'], ['update-current-e', 'm906', 'E'],
    ['update-leveling-fade', 'm420', 'Z'],
  ];

  let state = marlin.createState();
  let printerAvailable = false;
  let printActive = false;
  let readComplete = false;
  let reading = false;
  let readTimer = null;
  let reportLines = [];

  function setStatus(text, kind) {
    statusEl.textContent = text;
    statusEl.className = 'update-status ' + (kind || 'muted');
  }

  function send(line, silent) {
    window.dispatchEvent(new CustomEvent('angleros:send-gcode', {
      detail: { line, silent: !!silent },
    }));
  }

  function sendSequence(commands, delayMs) {
    commands.forEach((line, index) => {
      setTimeout(() => send(line, false), index * (delayMs || 120));
    });
  }

  function controlsAvailable() {
    return printerAvailable && !printActive;
  }

  function updateAvailability() {
    const available = controlsAvailable();
    readBtn.disabled = !available || reading;
    saveBtn.disabled = !available || !readComplete || reading;
    const leveling = available && readComplete && !!state.supported.m420 && !reading;
    meshReadBtn.disabled = !available || reading;
    meshRebuildBtn.disabled = !leveling;
    meshPointBtn.disabled = !leveling;

    if (printActive) setStatus('Runtime updates are disabled while a print job is active.', 'warn');
    else if (!printerAvailable) setStatus('Waiting for a printer connection.');
  }

  function renderSupport() {
    document.querySelectorAll('.update-card[data-setting]').forEach((card) => {
      const key = card.dataset.setting;
      const supported = !!state.supported[key];
      card.classList.toggle('unsupported', !supported);
      const badge = card.querySelector('[data-support-for]');
      if (badge) badge.textContent = supported ? 'Reported by Marlin' : (readComplete ? 'Not reported' : 'Read printer first');
      card.querySelectorAll('input').forEach((input) => { input.disabled = !supported; });
    });
    updateAvailability();
  }

  function renderValues() {
    bindings.forEach(([id, group, field]) => {
      const input = document.getElementById(id);
      const value = state.values[group][field];
      if (input && value !== null && value !== undefined) input.value = value;
    });
    const leveling = document.getElementById('update-leveling-enabled');
    if (state.values.m420.S !== null) leveling.checked = !!state.values.m420.S;
    renderSupport();
  }

  function collectValues() {
    bindings.forEach(([id, group, field]) => {
      if (!state.supported[group]) return;
      const input = document.getElementById(id);
      const value = input.value.trim() === '' ? null : Number(input.value);
      state.values[group][field] = Number.isFinite(value) ? value : null;
    });
    if (state.supported.m420) {
      state.values.m420.S = document.getElementById('update-leveling-enabled').checked ? 1 : 0;
    }
  }

  function isReportNoise(line) {
    return /^\s*(?:ok\s+)?T:/.test(line) || /^\s*ok\s*$/.test(line);
  }

  function showReport() {
    reportEl.textContent = reportLines.length ? reportLines.slice(-100).join('\n') : 'No settings were reported.';
  }

  function finishRead() {
    if (!reading) return;
    reading = false;
    readComplete = marlin.supportedCount(state) > 0;
    renderValues();
    showReport();
    const count = marlin.supportedCount(state);
    if (count) setStatus(`Read ${count} supported setting groups. Edit values, then save with M500.`, 'ok');
    else setStatus('Marlin did not report editable settings. Check that M503 is enabled.', 'warn');
  }

  function scheduleReadFinish(delay) {
    clearTimeout(readTimer);
    readTimer = setTimeout(finishRead, delay || 1400);
  }

  function startRead(includeSettings) {
    if (!controlsAvailable()) return;
    state = includeSettings ? marlin.createState() : state;
    if (includeSettings) {
      readComplete = false;
      reportLines = [];
    }
    reading = true;
    renderSupport();
    setStatus(includeSettings ? 'Reading active Marlin settings…' : 'Refreshing bed mesh report…');
    if (includeSettings) send('M503', true);
    setTimeout(() => send('M420 V', true), includeSettings ? 250 : 0);
    scheduleReadFinish(4000);
  }

  function saveChanges() {
    if (!controlsAvailable() || !readComplete) return;
    collectValues();
    const commands = marlin.buildCommands(state);
    if (!commands.length) {
      setStatus('No supported values are available to save.', 'warn');
      return;
    }
    sendSequence(commands, 140);
    setStatus(`Sending ${commands.length - 1} setting commands, followed by M500…`);
    setTimeout(() => setStatus('Changes sent and stored. Read again to verify.', 'ok'), commands.length * 140 + 900);
  }

  function rebuildMesh() {
    if (!controlsAvailable() || !state.supported.m420) return;
    if (!confirm('Home the printer, probe a new bed mesh, and store it with M500?')) return;
    sendSequence(['G28', 'G29', 'M500'], 180);
    setStatus('Mesh rebuild queued: G28, G29, then M500. Watch the printer and console.', 'warn');
  }

  function saveMeshPoint() {
    if (!controlsAvailable() || !state.supported.m420) return;
    const i = Number(document.getElementById('update-mesh-i').value);
    const j = Number(document.getElementById('update-mesh-j').value);
    const z = Number(document.getElementById('update-mesh-z').value);
    if (!Number.isInteger(i) || i < 0 || !Number.isInteger(j) || j < 0 || !Number.isFinite(z)) {
      setStatus('Mesh I and J must be non-negative integers and Z must be a number.', 'warn');
      return;
    }
    const value = marlin.format(z);
    sendSequence([`M421 I${i} J${j} Z${value}`, 'M500'], 160);
    setStatus(`Mesh point I${i} J${j} set to Z${value} and stored.`, 'ok');
  }

  readBtn.addEventListener('click', () => startRead(true));
  saveBtn.addEventListener('click', saveChanges);
  meshReadBtn.addEventListener('click', () => startRead(false));
  meshRebuildBtn.addEventListener('click', rebuildMesh);
  meshPointBtn.addEventListener('click', saveMeshPoint);

  window.addEventListener('angleros:printer-line', (event) => {
    if (!reading) return;
    const line = String((event.detail || {}).line || '');
    if (!isReportNoise(line)) reportLines.push(line);
    if (marlin.parseLine(state, line)) renderValues();
    scheduleReadFinish(1400);
  });

  window.addEventListener('angleros:status', (event) => {
    const status = event.detail || {};
    printerAvailable = !!status.printer_available && status.printer_link !== 'none';
    const job = status.print || {};
    printActive = job.state === 'printing' || job.state === 'paused';
    updateAvailability();
  });

  renderSupport();
  updateAvailability();
})();
