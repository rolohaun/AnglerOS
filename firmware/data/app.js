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

  async function poll() {
    try {
      const r = await fetch('/api/status', { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      const s = await r.json();
      dot.className = 'dot dot-on';
      text.textContent = s.mode === 'sta' ? 'online' : 'setup mode';
      ipEl.textContent = s.ip ? s.ip : '';
      if (s.fw) fwEl.textContent = 'v' + s.fw;
    } catch (e) {
      dot.className = 'dot dot-off';
      text.textContent = 'offline';
      ipEl.textContent = '';
    }
  }

  poll();
  setInterval(poll, 3000);
})();
