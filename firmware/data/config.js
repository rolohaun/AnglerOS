// AnglerOS — Configuration tab (Phase 1).
// Loads a board profile + settings schema, renders a kinematics-aware form,
// fetches the latest Marlin release, generates a Marlin config.ini live, and
// persists everything to the ESP32 (/api/config).

(function () {
  const boardSel = document.getElementById('board');
  const kinSel = document.getElementById('kinematics');
  const versionEl = document.getElementById('marlin-version');
  const formEl = document.getElementById('config-form');
  const previewEl = document.getElementById('config-preview');
  const statusEl = document.getElementById('cfg-status');

  let schema = null;      // printer.schema.json
  let board = null;       // boards/<id>.json
  let marlinTag = null;   // e.g. "2.1.2.5"
  const values = {};      // field id -> current value

  // ---- Loading ----
  async function loadJSON(url) {
    const r = await fetch(url, { cache: 'no-store' });
    if (!r.ok) throw new Error(url + ' -> ' + r.status);
    return r.json();
  }

  async function fetchMarlinVersion() {
    try {
      const r = await fetch(
        'https://api.github.com/repos/MarlinFirmware/Marlin/releases/latest',
        { headers: { Accept: 'application/vnd.github+json' } });
      if (!r.ok) throw new Error(r.status);
      const j = await r.json();
      return j.tag_name || null;
    } catch (e) {
      return null;
    }
  }

  function kinematics() { return kinSel.value; }

  function fieldApplies(f) {
    if (!f.when || !f.when.kinematics) return true;
    return f.when.kinematics.includes(kinematics());
  }
  function groupApplies(g) {
    if (!g.when || !g.when.kinematics) return true;
    return g.when.kinematics.includes(kinematics());
  }

  // ---- Rendering ----
  function optionList(opts) {
    return opts.map((o) => `<option value="${o.value}">${o.label}</option>`).join('');
  }

  function fieldControl(f) {
    const v = values[f.id];
    switch (f.type) {
      case 'select':
        return `<select data-id="${f.id}">${optionList(f.options)}</select>`;
      case 'thermistor':
        return `<select data-id="${f.id}">${optionList(schema.thermistors)}</select>`;
      case 'switch':
        return `<label class="switch"><input type="checkbox" data-id="${f.id}"${v ? ' checked' : ''}/><span></span></label>`;
      case 'vector3':
        return `<div class="vec3">
          <input type="number" step="any" data-id="${f.id}" data-idx="0" value="${v[0]}"/>
          <input type="number" step="any" data-id="${f.id}" data-idx="1" value="${v[1]}"/>
          <input type="number" step="any" data-id="${f.id}" data-idx="2" value="${v[2]}"/>
        </div>`;
      case 'text':
        return `<input type="text" data-id="${f.id}" value="${v}"/>`;
      default: // number
        return `<input type="number" step="any" data-id="${f.id}" value="${v}"/>`;
    }
  }

  function render() {
    formEl.innerHTML = '';
    for (const g of schema.groups) {
      if (!groupApplies(g)) continue;
      const fields = g.fields.filter(fieldApplies);
      if (!fields.length) continue;

      const card = document.createElement('div');
      card.className = 'group-card';
      card.innerHTML = `<div class="group-title">${g.title}</div>`;
      const grid = document.createElement('div');
      grid.className = 'group-grid';
      for (const f of fields) {
        const row = document.createElement('div');
        row.className = 'group-field' + (f.type === 'switch' ? ' is-switch' : '');
        const unit = f.unit ? ` <span class="unit">${f.unit}</span>` : '';
        const help = f.help ? `<div class="field-help">${f.help}</div>` : '';
        row.innerHTML = `<label>${f.label}${unit}</label>${fieldControl(f)}${help}`;
        grid.appendChild(row);
      }
      card.appendChild(grid);
      formEl.appendChild(card);
    }
    syncControlsToValues();
    wireInputs();
    regenerate();
  }

  // Set select/checkbox states that can't be expressed in the HTML string cleanly.
  function syncControlsToValues() {
    formEl.querySelectorAll('select[data-id]').forEach((el) => {
      el.value = String(values[el.dataset.id]);
    });
    formEl.querySelectorAll('input[type=checkbox][data-id]').forEach((el) => {
      el.checked = !!values[el.dataset.id];
    });
  }

  function wireInputs() {
    formEl.querySelectorAll('[data-id]').forEach((el) => {
      const ev = el.type === 'checkbox' || el.tagName === 'SELECT' ? 'change' : 'input';
      el.addEventListener(ev, () => {
        const id = el.dataset.id;
        if (el.dataset.idx !== undefined) {
          values[id][+el.dataset.idx] = num(el.value);
        } else if (el.type === 'checkbox') {
          values[id] = el.checked;
        } else if (el.type === 'number') {
          values[id] = num(el.value);
        } else {
          values[id] = el.value;
        }
        regenerate();
      });
    });
  }

  function num(x) { const n = parseFloat(x); return isNaN(n) ? 0 : n; }

  // ---- Defaults / values ----
  function seedDefaults() {
    for (const g of schema.groups) {
      for (const f of g.fields) {
        if (values[f.id] === undefined) {
          values[f.id] = Array.isArray(f.default) ? f.default.slice() : f.default;
        }
      }
    }
  }

  // ---- config.ini generation ----
  function v3(id) { const a = values[id] || [0, 0, 0]; return `{ ${a[0]}, ${a[1]}, ${a[2]} }`; }

  function generateIni() {
    const kin = kinematics();
    const lines = [];
    const date = new Date().toISOString().slice(0, 10);
    // The Configurations repo publishes each release as a branch named
    // "release-<tag>" (there is no bare "<tag>" tag), so that's the ref we pull
    // the base example from. Fall back to the 2.1.x dev branch when offline.
    const ref = marlinTag ? 'release-' + marlinTag : 'bugfix-2.1.x';

    lines.push('# AnglerOS generated config.ini — ' + date);
    lines.push(`# Board: ${board.name} | Kinematics: ${kin} | Marlin source tag: ${marlinTag || 'bugfix-2.1.x'} | config ref: ${ref}`);
    lines.push('');
    lines.push('[config:base]');

    const base = board.base_examples[kin];
    lines.push(`ini_use_config = ${base ? base + ' @ ' + ref + ', ' : ''}base`);
    lines.push(`MOTHERBOARD = ${board.motherboard}`);
    if (kin === 'corexy') lines.push('COREXY = on');
    lines.push('');

    // Motion arrays assembled from the per-axis fields.
    let sx, sy, sz, fx, fy, fz, ax, ay, az;
    if (kin === 'delta') {
      sx = sy = sz = values.tower_steps;
      fx = fy = fz = values.tower_feedrate;
      ax = ay = az = values.tower_accel;
    } else {
      sx = sy = values.xy_steps; sz = values.z_steps;
      fx = fy = values.xy_feedrate; fz = values.z_feedrate;
      ax = ay = values.xy_accel; az = values.z_accel;
    }
    lines.push('# Steppers & motion');
    lines.push(`DEFAULT_AXIS_STEPS_PER_UNIT = { ${sx}, ${sy}, ${sz}, ${values.e_steps} }`);
    lines.push(`DEFAULT_MAX_FEEDRATE = { ${fx}, ${fy}, ${fz}, ${values.e_feedrate} }`);
    lines.push(`DEFAULT_MAX_ACCELERATION = { ${ax}, ${ay}, ${az}, ${values.e_accel} }`);
    lines.push('');

    // TMC2209 drivers.
    lines.push('# TMC2209 drivers (UART)');
    for (const ax2 of board.axes) lines.push(`${ax2}_DRIVER_TYPE = ${board.driver}`);
    let cx, cz;
    if (kin === 'delta') { cx = values.tower_current; cz = values.tower_current; }
    else { cx = values.xy_current; cz = values.z_current; }
    lines.push(`X_CURRENT = ${cx}`);
    lines.push(`Y_CURRENT = ${cx}`);
    lines.push(`Z_CURRENT = ${cz}`);
    lines.push(`E0_CURRENT = ${values.e_current}`);
    for (const ax2 of board.axes) lines.push(`${ax2}_MICROSTEPS = ${values.microsteps}`);
    if (values.stealthchop) {
      lines.push('STEALTHCHOP_XY = on');
      lines.push('STEALTHCHOP_Z = on');
      lines.push('STEALTHCHOP_E = on');
    }
    if (values.sensorless_homing) lines.push('SENSORLESS_HOMING = on');
    lines.push('');

    // Scalar fields declared with an `ini` key, grouped by their schema group.
    for (const g of schema.groups) {
      if (!groupApplies(g)) continue;
      const scalars = g.fields.filter((f) => fieldApplies(f) && f.ini && f.type !== 'vector3');
      const vecs = g.fields.filter((f) => fieldApplies(f) && f.ini && f.type === 'vector3');
      if (!scalars.length && !vecs.length) continue;
      lines.push('# ' + g.title);
      for (const f of scalars) lines.push(`${f.ini} = ${fmt(f)}`);
      for (const f of vecs) lines.push(`${f.ini} = ${v3(f.id)}`);
      lines.push('');
    }

    // Feature switches that aren't a simple ini= mapping.
    lines.push('# Temperature control');
    lines.push(`PIDTEMP = ${values.hotend_pid ? 'on' : 'off'}`);
    lines.push(`PIDTEMPBED = ${values.bed_pid ? 'on' : 'off'}`);

    return lines.join('\n') + '\n';
  }

  function fmt(f) {
    const v = values[f.id];
    if (f.type === 'switch') return v ? 'on' : 'off';
    return v;
  }

  function regenerate() {
    previewEl.textContent = generateIni();
  }

  // ---- Persistence ----
  async function loadSaved() {
    try {
      const saved = await loadJSON('/api/config');
      if (saved && saved.values) Object.assign(values, saved.values);
    } catch (e) { /* none saved yet */ }
  }

  async function save() {
    statusEl.textContent = 'Saving…';
    try {
      const body = JSON.stringify({
        board: board.id, kinematics: kinematics(), marlin: marlinTag,
        values, ini: generateIni(),
      });
      const r = await fetch('/api/config', {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body,
      });
      if (!r.ok) throw new Error(r.status);
      statusEl.textContent = 'Saved to device.';
    } catch (e) {
      statusEl.textContent = 'Save failed: ' + e.message;
    }
  }

  function download() {
    const blob = new Blob([generateIni()], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'config.ini';
    a.click();
    URL.revokeObjectURL(a.href);
  }

  // ---- Init ----
  async function init() {
    try {
      schema = await loadJSON('/schema/printer.schema.json');
      board = await loadJSON('/boards/' + boardSel.value + '.json');
    } catch (e) {
      formEl.innerHTML = '<p class="note">Could not load configuration schema: ' + e.message + '</p>';
      return;
    }
    seedDefaults();
    await loadSaved();

    marlinTag = await fetchMarlinVersion();
    versionEl.textContent = marlinTag
      ? 'latest stable: ' + marlinTag
      : 'version unavailable (offline?) — using bugfix-2.1.x';

    render();

    kinSel.addEventListener('change', render);
    boardSel.addEventListener('change', async () => {
      board = await loadJSON('/boards/' + boardSel.value + '.json');
      render();
    });
    document.getElementById('cfg-save').addEventListener('click', save);
    document.getElementById('cfg-download').addEventListener('click', download);
  }

  init();
})();
