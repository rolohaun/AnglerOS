// AnglerOS SPA — Phase 0 shell.
// Handles tab switching and polls /api/status for live board state.

(function () {
  const views = {
    dashboard: 'Dashboard',
    configuration: 'Configuration',
  };

  // --- Tab navigation ---
  const navItems = document.querySelectorAll('.nav-item');
  const title = document.getElementById('view-title');

  function show(view) {
    document.querySelectorAll('.view').forEach((v) => v.classList.remove('active'));
    document.getElementById('view-' + view).classList.add('active');
    navItems.forEach((n) => n.classList.toggle('active', n.dataset.view === view));
    title.textContent = views[view] || view;
    try { localStorage.setItem('angleros.view', view); } catch (e) {}
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

  async function poll() {
    try {
      const r = await fetch('/api/status', { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      const s = await r.json();
      dot.className = 'dot dot-on';
      text.textContent = s.mode === 'sta' ? 'online' : 'setup mode';
      ipEl.textContent = s.ip ? s.ip : '';
      if (s.fw) fwEl.textContent = 'v' + s.fw;
      // Reveal the Wi-Fi setup overlay only while in SoftAP mode.
      if (!submitting) setup.hidden = s.mode !== 'ap';
    } catch (e) {
      dot.className = 'dot dot-off';
      text.textContent = 'offline';
      ipEl.textContent = '';
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
    msg.textContent = 'Saving…';
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
        'Saved. Rebooting and joining your network — reconnect your device to ' +
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
