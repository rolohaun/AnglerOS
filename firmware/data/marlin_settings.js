// Pure Marlin runtime-settings parser and command builder. Kept separate from
// the UI so representative M503 reports can be tested without a printer.
(function (root) {
  const COMMAND_FIELDS = {
    m92: ['X', 'Y', 'Z', 'E'],
    m301: ['P', 'I', 'D'],
    m304: ['P', 'I', 'D'],
    m851: ['X', 'Y', 'Z'],
    m900: ['K'],
    m906: ['X', 'Y', 'Z', 'E'],
    m420: ['S', 'Z'],
  };

  function blank(fields) {
    const out = {};
    fields.forEach((field) => { out[field] = null; });
    return out;
  }

  function createState() {
    const values = {};
    Object.keys(COMMAND_FIELDS).forEach((key) => { values[key] = blank(COMMAND_FIELDS[key]); });
    return { values, supported: {} };
  }

  function parseWords(text) {
    const words = {};
    const re = /(?:^|\s)([A-Z])\s*(-?(?:\d+(?:\.\d*)?|\.\d+))/gi;
    let match;
    while ((match = re.exec(text))) words[match[1].toUpperCase()] = Number(match[2]);
    return words;
  }

  function copyFields(target, words, fields) {
    fields.forEach((field) => {
      if (Number.isFinite(words[field])) target[field] = words[field];
    });
  }

  function parseLine(state, rawLine) {
    const line = String(rawLine || '').replace(/^\s*(?:ok\s+)?(?:echo:\s*)?/i, '').trim();
    const command = line.match(/\b(M92|M301|M304|M420|M851|M900|M906)\b/i);
    if (command) {
      const key = command[1].toLowerCase();
      const words = parseWords(line.slice(command.index + command[1].length));

      if (key === 'm906') {
        // M906 may report primary and secondary motors on separate lines. Keep
        // only I0 / E0 values in this first-version editor.
        if (!Number.isFinite(words.I) || words.I === 0) {
          copyFields(state.values.m906, words, ['X', 'Y', 'Z']);
        }
        if (!Number.isFinite(words.T) || words.T === 0) {
          copyFields(state.values.m906, words, ['E']);
        }
      } else {
        copyFields(state.values[key], words, COMMAND_FIELDS[key]);
      }
      state.supported[key] = true;
      return key;
    }

    const leveling = line.match(/(?:Bed\s+)?Leveling(?:\s+is)?\s+(ON|OFF)/i);
    if (leveling) {
      state.values.m420.S = leveling[1].toUpperCase() === 'ON' ? 1 : 0;
      state.supported.m420 = true;
      return 'm420';
    }
    return null;
  }

  function finite(value) {
    if (value === null || value === undefined || value === '') return null;
    const number = typeof value === 'number' ? value : Number(value);
    return Number.isFinite(number) ? number : null;
  }

  function format(value) {
    const number = finite(value);
    if (number === null) return null;
    return String(Math.round(number * 1000000) / 1000000);
  }

  function command(code, values, fields) {
    const words = [];
    fields.forEach((field) => {
      const value = format(values[field]);
      if (value !== null) words.push(field + value);
    });
    return words.length ? code.toUpperCase() + ' ' + words.join(' ') : null;
  }

  function buildCommands(state) {
    const commands = [];
    const supported = state.supported || {};
    const values = state.values || {};

    if (supported.m92) commands.push(command('M92', values.m92, ['X', 'Y', 'Z', 'E']));
    if (supported.m301) commands.push(command('M301', values.m301, ['P', 'I', 'D']));
    if (supported.m304) commands.push(command('M304', values.m304, ['P', 'I', 'D']));
    if (supported.m851) commands.push(command('M851', values.m851, ['X', 'Y', 'Z']));
    if (supported.m900) commands.push(command('M900', values.m900, ['K']));
    if (supported.m906) commands.push(command('M906', values.m906, ['X', 'Y', 'Z', 'E']));
    if (supported.m420) {
      const levelState = finite(values.m420.S);
      const leveling = {
        S: levelState === null ? null : (levelState ? 1 : 0),
        Z: values.m420.Z,
      };
      commands.push(command('M420', leveling, ['S', 'Z']));
    }

    const valid = commands.filter(Boolean);
    if (valid.length) valid.push('M500');
    return valid;
  }

  function supportedCount(state) {
    return Object.keys(COMMAND_FIELDS).filter((key) => state.supported[key]).length;
  }

  root.AnglerMarlinSettings = {
    commandFields: COMMAND_FIELDS,
    createState,
    parseLine,
    buildCommands,
    supportedCount,
    format,
  };
})(typeof window !== 'undefined' ? window : globalThis);
