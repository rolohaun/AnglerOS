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

  // The Marlin source ref this board builds against.
  function marlinRef() { return board.marlin_ref || marlinTag || 'bugfix-2.1.x'; }

  function conditionApplies(when) {
    if (!when) return true;
    if (when.kinematics && !when.kinematics.includes(kinematics())) return false;
    if (when.field) {
      const expected = when.equals === undefined ? true : when.equals;
      if (values[when.field] !== expected) return false;
    }
    return true;
  }
  function fieldApplies(f) {
    return conditionApplies(f.when);
  }
  function groupApplies(g) {
    return conditionApplies(g.when);
  }

  function hasConditionalDependents(id) {
    return schema.groups.some((g) =>
      (g.when && g.when.field === id)
      || g.fields.some((f) => f.when && f.when.field === id));
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
        if (hasConditionalDependents(id)) {
          render();
          return;
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
    // Which Marlin the board actually builds against. Some boards (RP2040 / SKR
    // Pico) require a specific branch because stable Marlin doesn't support them
    // yet. Otherwise use the latest stable release. The Configurations repo
    // names release branches "release-<tag>" (no bare "<tag>" tag exists).
    const sourceRef = marlinRef();
    const exampleRef = board.marlin_ref
      ? board.marlin_ref
      : (marlinTag ? 'release-' + marlinTag : 'bugfix-2.1.x');

    lines.push('# AnglerOS generated config.ini — ' + date);
    lines.push(`# Board: ${board.name} | Kinematics: ${kin} | Marlin source tag: ${sourceRef} | config ref: ${exampleRef}`);
    lines.push('');
    lines.push('[config:base]');

    const base = board.base_examples[kin];
    lines.push(`ini_use_config = ${base ? base + ' @ ' + exampleRef + ', ' : ''}base`);
    lines.push(`MOTHERBOARD = ${board.motherboard}`);
    // STM32 boards require a valid SERIAL_PORT (1-9 or -1); the generic default
    // config uses 0, which only works on AVR. Boards that need it declare one.
    if (board.serial_port !== undefined && board.serial_port !== null) {
      lines.push(`SERIAL_PORT = ${board.serial_port}`);
    }
    // Second port for the AnglerOS wired UART link (e.g. SKR Pico TFT header),
    // so the ESP32 gets its own connection alongside USB.
    if (board.serial_port_2 !== undefined && board.serial_port_2 !== null) {
      lines.push(`SERIAL_PORT_2 = ${board.serial_port_2}`);
    }
    if (kin === 'corexy') lines.push('COREXY = on');
    lines.push('');

    // Base examples enable extras our target boards don't wire up. Disable the
    // ones AnglerOS doesn't (yet) expose so they don't trip Marlin's sanity
    // checks (e.g. PSU_CONTROL needs a PS_ON_PIN the SKR Pico doesn't define).
    lines.push('# Base-example extras AnglerOS disables (not exposed in the UI)');
    lines.push('PSU_CONTROL = off');
    lines.push('');

    // The Updates tab uses M503 to read active runtime settings and M500 to
    // persist edits. Keep both commands and EEPROM feedback enabled.
    lines.push('# Runtime settings storage');
    lines.push('EEPROM_SETTINGS = on');
    lines.push('EEPROM_CHITCHAT = on');
    lines.push('DISABLE_M503 = off');
    if (board.eeprom_emulation) lines.push(`${board.eeprom_emulation} = on`);
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
      if (g.id === 'bed' && !values.bed_enabled) {
        lines.push('# Heated bed (disabled)');
        lines.push('TEMP_SENSOR_BED = 0');
        lines.push('');
        continue;
      }
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
    lines.push(`PIDTEMPBED = ${values.bed_enabled && values.bed_pid ? 'on' : 'off'}`);

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

  function saveBlob(text, name) {
    const blob = new Blob([text], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  function download() { saveBlob(generateIni(), 'config.ini'); }

  // Build a self-contained script (config embedded) that compiles the firmware
  // locally with PlatformIO — no GitHub, token, or fork needed.
  async function downloadBuilder(templateUrl, outName, howto) {
    statusEl.textContent = 'Preparing build script…';
    try {
      const tmpl = await (await fetch(templateUrl, { cache: 'no-store' })).text();
      const ini = generateIni();
      const hint = (board.flash_hint || '').replace(/\r?\n/g, ' ').replace(/'/g, '');
      const script = tmpl
        .replace('__MARLIN_REF__', () => marlinRef())
        .replace('__PIO_ENV__', () => board.pio_env)
        .replace('__FW_ARTIFACT__', () => board.fw_artifact || 'firmware.bin')
        .replace('__FLASH_HINT__', () => hint)
        .replace('__CONFIG_INI__', () => ini);
      saveBlob(script, outName);
      statusEl.textContent = 'Downloaded ' + outName + ' — ' + howto;
    } catch (e) {
      statusEl.textContent = 'Could not build script: ' + e.message;
    }
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
    if (board.marlin_ref) {
      versionEl.textContent = `builds from ${board.marlin_ref}`
        + (marlinTag ? ` · latest stable ${marlinTag}` : '');
      if (board.marlin_ref_note) versionEl.title = board.marlin_ref_note;
    } else {
      versionEl.textContent = marlinTag
        ? 'latest stable: ' + marlinTag
        : 'version unavailable (offline?) — using bugfix-2.1.x';
    }

    render();

    kinSel.addEventListener('change', render);
    boardSel.addEventListener('change', async () => {
      board = await loadJSON('/boards/' + boardSel.value + '.json');
      render();
    });
    document.getElementById('cfg-save').addEventListener('click', save);
    document.getElementById('cfg-download').addEventListener('click', download);
    document.getElementById('cfg-builder-win').addEventListener('click', () =>
      downloadBuilder('/build-template.ps1', 'angleros-build.ps1', 'right-click it → Run with PowerShell.'));
    document.getElementById('cfg-builder-nix').addEventListener('click', () =>
      downloadBuilder('/build-template.sh', 'angleros-build.sh', 'run: bash angleros-build.sh'));
  }

  init();
})();
