// Stack-Chan Home: panel of the Stack-Chan gateway. Plain ES modules, no build step.
// Everything dynamic goes through esc(); the page CSP forbids inline scripts and styles.

/* ================================== Basics ================================== */

const app = document.getElementById('app');
const modalRoot = document.getElementById('modal-root');
const toasts = document.getElementById('toasts');

const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
const esc = (v) => String(v ?? '').replace(/[&<>"']/g, (c) => ESC[c]);
const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

const ICONS = {
  home: '<path d="M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"/>',
  book: '<path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>',
  music: '<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>',
  spotify: '<circle cx="12" cy="12" r="10"/><path d="M7 9.5c3.5-1 7.5-.6 10.5 1"/><path d="M7.5 12.8c3-.8 6-.4 8.5 1"/><path d="M8 15.8c2.4-.6 4.6-.3 6.5.8"/>',
  plug: '<path d="M9 2v6M15 2v6"/><path d="M6 8h12v4a6 6 0 0 1-12 0z"/><path d="M12 18v4"/>',
  sliders: '<path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/>',
  activity: '<path d="M22 12h-4l-3 9L9 3l-3 9H2"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  edit: '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 1 1 3 3L7 19l-4 1 1-4z"/>',
  trash: '<path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6"/>',
  eye: '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8S1 12 1 12z"/><circle cx="12" cy="12" r="3"/>',
  play: '<path d="M7 4v16l13-8z" fill="currentColor" stroke="none"/>',
  pause: '<path d="M7 4h4v16H7zM13 4h4v16h-4z" fill="currentColor" stroke="none"/>',
  next: '<path d="M5 4l10 8-10 8zM17 5h2v14h-2z" fill="currentColor" stroke="none"/>',
  prev: '<path d="M19 20 9 12l10-8zM5 5h2v14H5z" fill="currentColor" stroke="none"/>',
  upload: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="M17 8l-5-5-5 5"/><path d="M12 3v12"/>',
  refresh: '<path d="M21 12a9 9 0 1 1-2.6-6.4L21 8"/><path d="M21 3v5h-5"/>',
  x: '<path d="M18 6 6 18M6 6l12 12"/>',
  check: '<path d="M20 6 9 17l-5-5"/>',
  copy: '<rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
  info: '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/>',
  alert: '<path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17h.01"/>',
  speaker: '<path d="M11 5 6 9H2v6h4l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7M19 5a10 10 0 0 1 0 14"/>',
  phone: '<rect x="5" y="2" width="14" height="20" rx="2"/><path d="M12 18h.01"/>',
  computer: '<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/>',
  tv: '<rect x="2" y="7" width="20" height="15" rx="2"/><path d="M17 2l-5 5-5-5"/>',
  star: '<path d="M12 2l3.1 6.3 6.9 1-5 4.9 1.2 6.8-6.2-3.2-6.2 3.2L7 14.2 2 9.3l6.9-1z"/>',
  logout: '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4M16 17l5-5-5-5M21 12H9"/>',
  cloud: '<path d="M18 10h-1.3A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"/>',
  robot: '<rect x="3" y="7" width="18" height="13" rx="4"/><path d="M12 3v4M8.5 13h.01M15.5 13h.01M9 17h6"/>',
  shield: '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>',
  search: '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>',
  key: '<circle cx="7.5" cy="15.5" r="5.5"/><path d="m21 2-9.6 9.6M15.5 7.5l3 3L22 7l-3-3"/>',
  sparkle: '<path d="M12 3l1.9 5.1L19 10l-5.1 1.9L12 17l-1.9-5.1L5 10l5.1-1.9z"/>',
  link: '<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.8 1.7"/><path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>',
  arrow: '<path d="M5 12h14M13 6l6 6-6 6"/>',
};

const icon = (name, size = 20) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name] ?? ''}</svg>`;

const face = (cls = '') => `<div class="face ${cls}" aria-hidden="true"><i class="eye"></i><i class="eye"></i><i class="mouth"></i></div>`;

const fmtBytes = (n) => (n > 1048576 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.round(n / 1024))} KB`);
const fmtDur = (s) => `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, '0')}`;
const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;

function rel(ts) {
  if (!ts) return 'nunca';
  const d = (Date.now() - new Date(ts).getTime()) / 1000;
  if (d < 45) return 'agora';
  if (d < 3600) return `há ${Math.round(d / 60)} min`;
  if (d < 86400) return `há ${Math.round(d / 3600)} h`;
  if (d < 172800) return 'ontem';
  return new Date(ts).toLocaleDateString('pt-BR', { day: '2-digit', month: 'short' });
}

const since = (ts) => {
  const d = Math.max(0, (Date.now() - new Date(ts).getTime()) / 1000);
  if (d < 90) return 'há instantes';
  if (d < 3600) return `há ${Math.round(d / 60)} min`;
  if (d < 172800) return `há ${Math.round(d / 3600)} h`;
  return `há ${Math.round(d / 86400)} dias`;
};

/* =================================== API ==================================== */

class ApiError extends Error {
  constructor(status, message, data) {
    super(message);
    this.status = status;
    this.data = data;
  }
}

async function api(method, path, body) {
  const init = { method, headers: {}, credentials: 'same-origin' };
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  const res = await fetch(path, init);
  const data = await res.json().catch(() => ({}));
  if (res.status === 401 && path !== '/api/login') {
    showLogin();
    throw new ApiError(401, 'Faça login de novo');
  }
  if (!res.ok) throw new ApiError(res.status, data.error || `Erro ${res.status}`, data);
  return data;
}

/* ============================ Toasts and dialogs ============================ */

function toast(message, kind = 'ok') {
  const el = document.createElement('div');
  el.className = `toast ${kind}`;
  el.innerHTML = `${icon(kind === 'bad' ? 'alert' : 'check', 18)}<span>${esc(message)}</span>`;
  toasts.append(el);
  setTimeout(() => el.classList.add('out'), 3600);
  setTimeout(() => el.remove(), 3900);
}

const fail = (err) => toast(err?.message || String(err), 'bad');

function openModal({ title, body, actions = '', size = '' }) {
  modalRoot.innerHTML = `
    <div class="backdrop" data-close>
      <div class="modal ${size}" role="dialog" aria-modal="true" aria-label="${esc(title)}">
        <div class="modal-head"><h2>${esc(title)}</h2><button class="btn ghost icon sm" data-close aria-label="Fechar">${icon('x', 18)}</button></div>
        <div class="modal-body">${body}</div>
        ${actions ? `<div class="modal-foot">${actions}</div>` : ''}
      </div>
    </div>`;
  const root = $('.backdrop', modalRoot);
  root.addEventListener('click', (e) => {
    if (e.target === root || e.target.closest('.modal-head [data-close]')) closeModal();
  });
  const first = $('input, textarea, select', root);
  if (first) setTimeout(() => first.focus(), 50);
  return root;
}

function closeModal() {
  modalRoot.innerHTML = '';
}

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && modalRoot.firstChild) closeModal();
});

function confirmDialog(text, okLabel = 'Apagar') {
  return new Promise((resolve) => {
    const root = openModal({
      title: 'Tem certeza?',
      size: 'sm',
      body: `<p class="muted">${esc(text)}</p>`,
      actions: `<button class="btn" data-no>Cancelar</button><button class="btn primary" data-yes>${esc(okLabel)}</button>`,
    });
    $('[data-no]', root).onclick = () => (closeModal(), resolve(false));
    $('[data-yes]', root).onclick = () => (closeModal(), resolve(true));
  });
}

async function busy(btn, run) {
  const html = btn.innerHTML;
  btn.disabled = true;
  btn.innerHTML = `<span class="spin"></span>${btn.textContent.trim() ? '<span>Aguarde…</span>' : ''}`;
  try {
    return await run();
  } finally {
    btn.disabled = false;
    btn.innerHTML = html;
  }
}

/** Widths from data-w (CSP forbids style attributes in markup). */
const applyWidths = (root) => $$('[data-w]', root).forEach((el) => (el.style.width = `${el.dataset.w}%`));

/* ================================== State =================================== */

const state = {
  me: null,
  status: null,
  events: null,
  listeners: { activity: new Set(), status: new Set(), music: new Set() },
  route: '',
  pageCleanup: null,
};

function on(event, fn) {
  state.listeners[event].add(fn);
  return () => state.listeners[event].delete(fn);
}

function connectEvents() {
  state.events?.close();
  const es = new EventSource('/api/events');
  es.addEventListener('activity', (e) => state.listeners.activity.forEach((fn) => fn(JSON.parse(e.data))));
  es.addEventListener('status', (e) => {
    const info = JSON.parse(e.data);
    if (state.status) state.status.connection = info;
    renderConnPills();
    state.listeners.status.forEach((fn) => fn(info));
  });
  es.addEventListener('music', (e) => state.listeners.music.forEach((fn) => fn(JSON.parse(e.data))));
  state.events = es;
}

function connInfo(status) {
  if (!status) return { cls: '', text: '…' };
  if (!status.endpointConfigured) return { cls: 'bad', text: 'Sem endereço MCP' };
  const c = status.connection;
  if (c.state === 'connected') return { cls: 'ok', text: 'Conectado' };
  if (c.state === 'standby') return { cls: 'bad', text: 'Em espera' };
  return { cls: 'warn', text: 'Reconectando…' };
}

function renderConnPills() {
  const c = connInfo(state.status);
  $$('[data-conn-pill]').forEach((el) => {
    el.className = `pill ${c.cls}`;
    el.innerHTML = `<span class="dot"></span>${esc(c.text)}`;
  });
}

/* ================================== Login =================================== */

function showLogin() {
  state.events?.close();
  state.me = null;
  app.innerHTML = `
    <main class="login">
      <form class="login-card stack" novalidate>
        ${face('face--lg')}
        <div><h1>Stack-Chan Home</h1><p class="sub">Painel do robô da família</p></div>
        <label class="field">E-mail<input class="input" name="email" type="email" autocomplete="username" required></label>
        <label class="field">Senha<input class="input" name="password" type="password" autocomplete="current-password" required></label>
        <p class="form-error" data-error></p>
        <button class="btn primary block" type="submit">Entrar</button>
      </form>
    </main>`;
  const form = $('form', app);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const btn = $('button[type=submit]', form);
    const error = $('[data-error]', form);
    error.textContent = '';
    try {
      await busy(btn, () =>
        api('POST', '/api/login', { email: form.email.value, password: form.password.value }),
      );
      await boot();
    } catch (err) {
      if (err.status === 429) error.textContent = `Muitas tentativas. Tente de novo em ${Math.ceil((err.data?.retryAfterSec || 900) / 60)} min.`;
      else if (err.status === 401) error.textContent = 'E-mail ou senha incorretos.';
      else error.textContent = err.message;
      $('.face', form).classList.add('is-off');
      setTimeout(() => $('.face', form)?.classList.remove('is-off'), 1200);
    }
  });
}

/* ================================== Shell =================================== */

const NAV = [
  { route: '', label: 'Início', icon: 'home' },
  { route: 'historias', label: 'Histórias', icon: 'book' },
  { route: 'musicas', label: 'Músicas', icon: 'music' },
  { route: 'spotify', label: 'Spotify', icon: 'spotify' },
  { route: 'conexoes', label: 'Conexões MCP', icon: 'plug' },
  { route: 'ferramentas', label: 'Ferramentas', icon: 'sliders' },
  { route: 'atividade', label: 'Atividade', icon: 'activity' },
  { route: 'ajustes', label: 'Ajustes', icon: 'gear' },
];
const TABS = ['', 'historias', 'musicas', 'spotify'];

function renderShell() {
  app.innerHTML = `
    <div class="shell">
      <aside class="sidebar">
        <div class="brand">${face('face--sm')}<div><b>Stack-Chan</b><span>Home Intelligence</span></div></div>
        <nav class="nav">${NAV.map((n) => `<a href="#/${n.route}" data-route="${n.route}">${icon(n.icon)}<span>${esc(n.label)}</span></a>`).join('')}</nav>
        <div class="sidebar-foot">
          <span class="pill" data-conn-pill></span>
          <button class="btn ghost sm" data-logout>${icon('logout', 16)}Sair</button>
        </div>
      </aside>
      <header class="topbar">${face('face--sm')}<b>Stack-Chan</b><span class="pill" data-conn-pill></span></header>
      <main class="main"><div class="page" id="view"></div></main>
      <nav class="tabbar">
        ${TABS.map((r) => NAV.find((n) => n.route === r)).map((n) => `<a href="#/${n.route}" data-route="${n.route}">${icon(n.icon, 22)}${esc(n.label)}</a>`).join('')}
        <a href="#/mais" data-route="mais">${icon('grid', 22)}Mais</a>
      </nav>
    </div>`;
  $('[data-logout]', app).onclick = logout;
  renderConnPills();
}

async function logout() {
  await api('POST', '/api/logout').catch(() => {});
  showLogin();
}

const PAGES = {};

async function navigate() {
  if (!state.me) return;
  const [path, qs] = location.hash.replace(/^#\/?/, '').split('?');
  const route = PAGES[path] ? path : '';
  const params = new URLSearchParams(qs || '');
  state.pageCleanup?.();
  state.pageCleanup = null;
  state.route = route;
  closeModal();
  $$('[data-route]').forEach((a) => {
    const active = a.dataset.route === route || (a.dataset.route === 'mais' && ['conexoes', 'ferramentas', 'atividade', 'ajustes'].includes(route));
    a.classList.toggle('active', active);
  });
  const view = $('#view');
  view.innerHTML = '<div class="skeleton"></div>';
  window.scrollTo(0, 0);
  const cleanups = [];
  try {
    await PAGES[route](view, params, (fn) => cleanups.push(fn));
  } catch (err) {
    if (err.status !== 401) {
      view.innerHTML = `<div class="card empty">${face('is-off')}<p>Não consegui carregar esta página.</p><p class="small faint">${esc(err.message)}</p><button class="btn" data-retry>${icon('refresh', 16)}Tentar de novo</button></div>`;
      $('[data-retry]', view).onclick = navigate;
    }
  }
  state.pageCleanup = () => cleanups.forEach((fn) => fn());
  document.title = `${NAV.find((n) => n.route === route)?.label ?? 'Mais'} · Stack-Chan Home`;
}

const head = (title, sub = '', actions = '') =>
  `<div class="page-head"><div><h1>${esc(title)}</h1>${sub ? `<p>${sub}</p>` : ''}</div>${actions ? `<div class="row-wrap">${actions}</div>` : ''}</div>`;

/* ============================== Activity rows =============================== */

const KIND = {
  tool: ['robot', 'accent'],
  robot: ['speaker', 'mint'],
  system: ['sparkle', 'blue'],
  auth: ['key', 'amber'],
  error: ['alert', 'red'],
};

function activityRow(e, isNew = false) {
  const [ic, color] = e.ok === false ? ['alert', 'red'] : (KIND[e.kind] ?? KIND.system);
  return `<div class="item${isNew ? ' new' : ''}">
    <div class="ico ${color}">${icon(ic, 18)}</div>
    <div class="grow"><div class="ellipsis">${esc(e.title)}</div>${e.detail ? `<div class="tiny faint ellipsis mono">${esc(e.detail)}</div>` : ''}</div>
    <span class="when" title="${esc(new Date(e.ts).toLocaleString('pt-BR'))}">${esc(rel(e.ts))}</span>
  </div>`;
}

/* =================================== Home =================================== */

const SAY = [
  ['conta a história do', 'dinossauro sonolento'],
  ['toca', 'Galinha Pintadinha'],
  ['toca música na', 'sala'],
  ['vai chover', 'amanhã?'],
  ['pausa a', 'música'],
  ['qual é a palavra secreta do', 'gateway?'],
];

PAGES[''] = async (view, _params, cleanup) => {
  const [status, act, weather] = await Promise.all([
    api('GET', '/api/status'),
    api('GET', '/api/activity?limit=8'),
    api('GET', '/api/weather').catch(() => ({ configured: false })),
  ]);
  state.status = status;
  renderConnPills();

  const hero = () => {
    const s = state.status;
    const c = connInfo(s);
    const off = c.cls !== 'ok';
    const line = !s.endpointConfigured
      ? 'Configure o endereço MCP do app para ligar as ferramentas extras.'
      : c.cls === 'ok'
        ? `Conectado ao xiaozhi.me ${since(s.connection.since)} · ${plural(s.tools.exposed, 'ferramenta ativa', 'ferramentas ativas')}`
        : 'O robô continua conversando normalmente; só as ferramentas extras ficam indisponíveis.';
    return `${face(`face--lg${off ? ' is-off' : ''}`)}
      <div class="grow">
        <div class="row-wrap"><span class="pill ${c.cls}"><span class="dot"></span>${esc(c.text)}</span>${s.robotLastSeen ? `<span class="pill">${icon('robot', 14)}Robô usou o gateway ${esc(rel(s.robotLastSeen))}</span>` : ''}</div>
        <h2 class="mt">${off ? 'O Stack-Chan está sem as ferramentas extras' : 'O Stack-Chan está pronto'}</h2>
        <p class="meta">${esc(line)}</p>
      </div>`;
  };

  const tiles = [
    ['historias', 'book', status.stories, 'histórias'],
    ['musicas', 'music', status.music, 'músicas no robô'],
    ['ferramentas', 'sliders', status.tools.exposed, 'ferramentas ativas'],
    ['conexoes', 'plug', status.external, 'servidores MCP'],
  ];

  view.innerHTML = `
    ${head(greeting(), 'Tudo o que o Stack-Chan sabe fazer além da conversa, num lugar só.')}
    <div class="stack-lg">
      <section class="card hero" data-hero>${hero()}</section>
      <div class="grid grid-4">
        ${tiles.map(([r, ic, n, l]) => `<a class="card stat" href="#/${r}"><span class="num">${n}</span><span class="lbl">${icon(ic, 15)}${esc(l)}</span></a>`).join('')}
      </div>
      <div class="grid grid-2">
        <section class="card" data-now>${spotifyMiniPlaceholder(status.spotify)}</section>
        <section class="card">${weatherCard(weather)}</section>
      </div>
      <div class="grid grid-2">
        <section class="card flush">
          <div class="card-head pad-head"><h2>${icon('activity', 18)}Agora há pouco</h2></div>
          <div class="list feed" data-feed>${act.entries.length ? act.entries.map((e) => activityRow(e)).join('') : '<div class="empty">Nada por aqui ainda.</div>'}</div>
          <div class="item"><a href="#/atividade" class="small">Ver toda a atividade ${icon('arrow', 14)}</a></div>
        </section>
        <section class="card">
          <div class="card-head"><h2>${icon('sparkle', 18)}Experimente dizer</h2></div>
          <div class="chips">${SAY.map(([a, b]) => `<span class="chip">"Stack-Chan, ${esc(a)} <b>${esc(b)}</b>"</span>`).join('')}</div>
          <p class="small faint mt">Primeiro chame o robô pela palavra de ativação, como sempre.</p>
        </section>
      </div>
    </div>`;

  if (status.spotify.connected) loadMiniPlayer($('[data-now]', view)).catch(() => {});

  cleanup(on('activity', (e) => {
    const feed = $('[data-feed]', view);
    if (!feed) return;
    $('.empty', feed)?.remove();
    feed.insertAdjacentHTML('afterbegin', activityRow(e, true));
    while (feed.children.length > 8) feed.lastElementChild.remove();
    if (e.kind === 'tool' || e.kind === 'robot') {
      state.status.robotLastSeen = e.ts;
      $('[data-hero]', view).innerHTML = hero();
      $('[data-hero] .face', view)?.classList.add('is-busy');
    }
  }));
  cleanup(on('status', () => ($('[data-hero]', view).innerHTML = hero())));
  const tick = setInterval(() => ($('[data-hero]', view).innerHTML = hero()), 60_000);
  cleanup(() => clearInterval(tick));
};

function greeting() {
  const h = new Date().getHours();
  return h < 12 ? 'Bom dia!' : h < 18 ? 'Boa tarde!' : 'Boa noite!';
}

function weatherCard(w) {
  if (!w.configured) {
    return `<div class="card-head"><h2>${icon('cloud', 18)}Clima</h2></div>
      <div class="empty"><p>Defina a cidade de casa para o robô responder "vai chover?".</p><a class="btn sm" href="#/ajustes">Definir cidade</a></div>`;
  }
  if (w.error) return `<div class="card-head"><h2>${icon('cloud', 18)}Clima</h2></div><p class="muted">Previsão indisponível agora.</p>`;
  const f = w.forecast;
  const names = ['Hoje', 'Amanhã'];
  return `<div class="card-head"><h2>${icon('cloud', 18)}Clima</h2><span class="small muted ellipsis">${esc(f.place)}</span></div>
    <div class="weather-now"><span class="emoji">${esc(f.current.emoji)}</span><div><div class="temp">${f.current.temperature}°</div><div class="muted small">${esc(f.current.text)} · sensação ${f.current.feelsLike}°</div></div></div>
    <div class="days">${f.days.map((d, i) => `<div class="day">${esc(names[i] ?? new Date(`${d.date}T12:00`).toLocaleDateString('pt-BR', { weekday: 'short' }))}<span class="e">${esc(d.emoji)}</span><b>${d.max}°</b> ${d.min}°<br>${icon('cloud', 11)} ${d.rainChance}%</div>`).join('')}</div>`;
}

function spotifyMiniPlaceholder(sp) {
  if (!sp.connected) {
    return `<div class="card-head"><h2>${icon('spotify', 18)}Spotify</h2></div>
      <div class="empty"><p>${sp.configured ? 'Conecte sua conta para tocar nos aparelhos da casa.' : 'Ligue o Spotify para pedir músicas por voz em qualquer aparelho da família.'}</p><a class="btn sm spotify" href="#/spotify">${sp.configured ? 'Conectar' : 'Configurar'}</a></div>`;
  }
  return `<div class="card-head"><h2>${icon('spotify', 18)}Tocando agora</h2></div><div class="skeleton"></div>`;
}

async function loadMiniPlayer(box) {
  const sp = await api('GET', '/api/spotify');
  box.innerHTML = `<div class="card-head"><h2>${icon('spotify', 18)}Tocando agora</h2><a class="small" href="#/spotify">Abrir</a></div>${nowPlaying(sp, true)}`;
  bindPlayer(box, () => loadMiniPlayer(box));
}

/* ================================== Spotify ================================= */

const deviceIcon = (type) => ({ smartphone: 'phone', computer: 'computer', tv: 'tv' })[String(type).toLowerCase()] ?? 'speaker';

function nowPlaying(sp, compact = false) {
  const p = sp.playback;
  if (!p || !p.item) {
    return `<div class="now"><div class="cover">${icon('music', 30)}</div><div class="grow"><b>Nada tocando</b><p class="small muted">Peça ao Stack-Chan: "toca Galinha Pintadinha na sala".</p></div></div>`;
  }
  const t = p.item;
  const img = t.album?.images?.[1]?.url || t.album?.images?.[0]?.url;
  const pct = t.duration_ms ? Math.min(100, Math.round(((p.progress_ms || 0) / t.duration_ms) * 100)) : 0;
  return `<div class="now">
    ${img ? `<img class="cover" src="${esc(img)}" alt="">` : `<div class="cover">${icon('music', 30)}</div>`}
    <div class="grow">
      <div class="title ellipsis"><b>${esc(t.name)}</b></div>
      <div class="small muted ellipsis">${esc(t.artists.map((a) => a.name).join(', '))}</div>
      <div class="tiny faint ellipsis">${icon(deviceIcon(p.device?.type), 12)} ${esc(p.device?.name ?? '')}</div>
      ${compact ? '' : `<div class="progress"><i data-w="${pct}"></i></div>`}
      <div class="controls">
        <button class="btn icon round sm" data-ctl="previous" aria-label="Anterior">${icon('prev', 16)}</button>
        <button class="btn icon round primary" data-ctl="${p.is_playing ? 'pause' : 'resume'}" aria-label="${p.is_playing ? 'Pausar' : 'Tocar'}">${icon(p.is_playing ? 'pause' : 'play', 18)}</button>
        <button class="btn icon round sm" data-ctl="next" aria-label="Próxima">${icon('next', 16)}</button>
      </div>
    </div>
  </div>`;
}

function bindPlayer(root, reload) {
  applyWidths(root);
  $$('[data-ctl]', root).forEach((b) => {
    b.onclick = () =>
      busy(b, async () => {
        await api('POST', '/api/spotify/control', { action: b.dataset.ctl });
        setTimeout(reload, 600);
      }).catch(fail);
  });
}

PAGES.spotify = async (view, params, cleanup) => {
  const result = params.get('result');
  if (result) {
    history.replaceState(null, '', '#/spotify');
    if (result === 'ok') toast('Spotify conectado!');
    else toast(`Spotify: ${result}`, 'bad');
  }
  const sp = await api('GET', '/api/spotify');

  if (!sp.configured) {
    view.innerHTML = `${head('Spotify', 'Peça músicas por voz e escolha em qual aparelho da casa tocar.')}
      <div class="grid grid-2">
        <section class="card stack">
          <h2>Ligar o Spotify (uma vez só)</h2>
          <ol class="steps">
            <li>Abra o <a href="https://developer.spotify.com/dashboard" target="_blank" rel="noopener">painel de desenvolvedor do Spotify</a> com a sua conta e clique em <b>Create app</b>.</li>
            <li>Nome e descrição: qualquer um (por exemplo "Stack-Chan"). Em <b>Redirect URI</b>, cole exatamente:
              <div class="copy"><code>${esc(sp.redirectUri)}</code><button class="btn sm icon" data-copy="${esc(sp.redirectUri)}" aria-label="Copiar">${icon('copy', 16)}</button></div></li>
            <li>Em <b>Which API/SDKs</b>, marque <b>Web API</b>, aceite os termos e salve.</li>
            <li>Copie o <b>Client ID</b> do app e cole aqui:
              <form class="row mt" data-cfg><input class="input mono" name="clientId" placeholder="32 caracteres" maxlength="32" required><button class="btn primary">Salvar</button></form></li>
          </ol>
        </section>
        <section class="stack">
          <div class="callout">${icon('info')}<div>Não precisa de senha nem de chave secreta: o login usa PKCE e os tokens ficam criptografados no servidor.</div></div>
          <div class="callout warn">${icon('alert')}<div>Desde 2026 o Spotify exige <b>Premium</b> do dono do app para controlar a reprodução, e libera até 5 usuários por app.</div></div>
          <div class="callout">${icon('speaker')}<div>O Spotify só toca em aparelhos <b>Spotify Connect</b> (celular, computador, Echo, TV, caixas). O alto-falante do robô não é um deles: para músicas no próprio Stack-Chan, use a página <a href="#/musicas">Músicas</a>.</div></div>
        </section>
      </div>`;
    bindCopy(view);
    $('[data-cfg]', view).onsubmit = async (e) => {
      e.preventDefault();
      try {
        await api('PUT', '/api/spotify/config', { clientId: e.target.clientId.value.trim() });
        toast('Client ID salvo');
        navigate();
      } catch (err) {
        fail(err);
      }
    };
    return;
  }

  if (!sp.connected) {
    view.innerHTML = `${head('Spotify')}
      <section class="card empty">
        ${icon('spotify', 44)}
        <h2>Conecte sua conta</h2>
        <p>Você vai para o site do Spotify, autoriza, e volta para cá.</p>
        <button class="btn spotify" data-connect>${icon('spotify', 18)}Conectar ao Spotify</button>
        <button class="btn ghost sm" data-reset>Trocar Client ID</button>
      </section>`;
    $('[data-connect]', view).onclick = (e) =>
      busy(e.currentTarget, async () => (location.href = (await api('POST', '/api/spotify/connect')).url)).catch(fail);
    $('[data-reset]', view).onclick = async () => {
      await api('PUT', '/api/spotify/config', { clientId: '' }).catch(fail);
      navigate();
    };
    return;
  }

  const devices = sp.devices ?? [];
  view.innerHTML = `${head('Spotify', `Conta: <b>${esc(sp.user?.name ?? '')}</b> · modo infantil ${sp.kidMode ? '<span class="badge ok">ligado</span>' : '<span class="badge warn">desligado</span>'}`,
    `<button class="btn sm" data-refresh>${icon('refresh', 16)}Atualizar</button><button class="btn sm danger" data-disconnect>Desconectar</button>`)}
    ${sp.error ? `<div class="callout warn mt">${icon('alert')}<div>${esc(sp.error)}</div></div>` : ''}
    <div class="grid grid-2">
      <section class="card" data-player>
        <div class="card-head"><h2>Tocando agora</h2></div>
        <div data-now>${nowPlaying(sp)}</div>
        <div class="sep"></div>
        <label class="field">Volume <span class="hint">máximo permitido: ${sp.maxVolume}% (em Ajustes)</span>
          <input type="range" min="0" max="${sp.maxVolume}" value="${Math.min(sp.playback?.device?.volume_percent ?? 50, sp.maxVolume)}" data-volume></label>
      </section>
      <section class="card flush">
        <div class="card-head pad-head"><h2>Aparelhos da família</h2><span class="badge">${devices.length}</span></div>
        <div class="list">
          ${devices.length ? devices.map((d) => `<div class="item">
            <div class="ico ${d.is_active ? 'mint' : ''}">${icon(deviceIcon(d.type), 18)}</div>
            <div class="grow"><div class="title ellipsis">${esc(d.name)}</div><div class="tiny faint device-type">${esc(d.type)}${d.is_active ? ' · tocando aqui' : ''}${d.is_restricted ? ' · não controlável' : ''}</div></div>
            <div class="actions">
              <button class="btn sm icon ${sp.defaultDevice === d.name ? 'primary' : 'ghost'}" data-default="${esc(d.name)}" title="Aparelho padrão para pedidos por voz" aria-label="Aparelho padrão">${icon('star', 16)}</button>
              ${d.is_active || d.is_restricted || !d.id ? '' : `<button class="btn sm" data-transfer="${esc(d.id)}">Tocar aqui</button>`}
            </div></div>`).join('') : '<div class="empty"><p>Nenhum aparelho online.</p><p class="small faint">Abra o Spotify no celular, computador ou caixa de som e atualize.</p></div>'}
        </div>
        <div class="item tiny faint">${icon('robot', 14)} O Stack-Chan não aparece aqui: o Spotify só toca em aparelhos Spotify Connect. Músicas no robô ficam em <a href="#/musicas">Músicas</a>.</div>
      </section>
    </div>
    <section class="card mt-lg">
      <div class="card-head"><h2>${icon('search', 18)}Buscar e tocar</h2>
        <select class="input" data-target aria-label="Aparelho">${devices.filter((d) => d.id && !d.is_restricted).map((d) => `<option value="${esc(d.id)}" ${d.is_active ? 'selected' : ''}>${esc(d.name)}</option>`).join('')}</select>
      </div>
      <input class="input" type="search" placeholder="Música, artista, álbum ou playlist" data-q>
      <div data-results class="mt"></div>
    </section>`;

  const reloadNow = async () => {
    const fresh = await api('GET', '/api/spotify').catch(() => null);
    if (!fresh) return;
    const box = $('[data-now]', view);
    if (box) {
      box.innerHTML = nowPlaying(fresh);
      bindPlayer(box, reloadNow);
    }
  };
  bindPlayer($('[data-now]', view), reloadNow);
  const poll = setInterval(reloadNow, 8000);
  cleanup(() => clearInterval(poll));

  $('[data-refresh]', view).onclick = navigate;
  $('[data-disconnect]', view).onclick = async () => {
    if (!(await confirmDialog('O robô deixa de controlar o Spotify até você conectar de novo.', 'Desconectar'))) return;
    await api('POST', '/api/spotify/disconnect').catch(fail);
    navigate();
  };
  $('[data-volume]', view).onchange = (e) =>
    api('POST', '/api/spotify/volume', { percent: Number(e.target.value) }).then((r) => toast(`Volume ${r.volume}%`)).catch(fail);
  $$('[data-transfer]', view).forEach((b) => {
    b.onclick = () =>
      busy(b, async () => {
        const r = await api('POST', '/api/spotify/transfer', { deviceId: b.dataset.transfer });
        toast(`Tocando em ${r.device}`);
        setTimeout(navigate, 800);
      }).catch(fail);
  });
  $$('[data-default]', view).forEach((b) => {
    b.onclick = async () => {
      const name = sp.defaultDevice === b.dataset.default ? '' : b.dataset.default;
      await api('PATCH', '/api/settings', { spotifyDefaultDevice: name }).catch(fail);
      toast(name ? `"${name}" é o aparelho padrão` : 'Sem aparelho padrão');
      navigate();
    };
  });

  let timer;
  const results = $('[data-results]', view);
  $('[data-q]', view).oninput = (e) => {
    clearTimeout(timer);
    const q = e.target.value.trim();
    if (q.length < 2) return void (results.innerHTML = '');
    timer = setTimeout(async () => {
      results.innerHTML = '<div class="bar indeterminate"><i></i></div>';
      try {
        const r = await api('GET', `/api/spotify/search?q=${encodeURIComponent(q)}`);
        const row = (img, title, sub, uri, blocked, kind) => `<div class="item">
          <div class="ico">${img ? `<img src="${esc(img)}" alt="">` : icon(kind === 'track' ? 'music' : 'book', 18)}</div>
          <div class="grow"><div class="title ellipsis">${esc(title)} ${blocked ? '<span class="badge bad">explícita</span>' : ''}</div><div class="tiny faint ellipsis">${esc(sub)}</div></div>
          <button class="btn sm icon round ${blocked ? '' : 'primary'}" data-play="${esc(uri)}" ${blocked ? 'disabled title="Bloqueada pelo modo infantil"' : ''} aria-label="Tocar">${icon('play', 14)}</button></div>`;
        results.innerHTML = `<div class="list card flush">${[
          ...r.tracks.map((t) => row(t.image, t.name, `${t.artists} · ${fmtDur(t.durationMs / 1000)}`, t.uri, t.blocked, 'track')),
          ...r.albums.slice(0, 4).map((a) => row(a.image, a.name, `Álbum · ${a.artists}`, a.uri, false, 'album')),
          ...r.playlists.slice(0, 4).map((p) => row(p.image, p.name, `Playlist · ${p.owner}`, p.uri, false, 'playlist')),
        ].join('') || '<div class="empty">Nada encontrado.</div>'}</div>`;
        $$('[data-play]', results).forEach((b) => {
          b.onclick = () =>
            busy(b, async () => {
              const deviceId = $('[data-target]', view)?.value;
              const res = await api('POST', '/api/spotify/play', { uri: b.dataset.play, deviceId });
              toast(`Tocando em ${res.device}`);
              setTimeout(reloadNow, 900);
            }).catch(fail);
        });
      } catch (err) {
        results.innerHTML = `<p class="form-error">${esc(err.message)}</p>`;
      }
    }, 350);
  };
};

function bindCopy(root) {
  $$('[data-copy]', root).forEach((b) => {
    b.onclick = async () => {
      try {
        await navigator.clipboard.writeText(b.dataset.copy);
        toast('Copiado');
      } catch {
        toast('Não deu para copiar; selecione o texto', 'bad');
      }
    };
  });
}

/* ================================== Stories ================================= */

const countPages = (text) => Math.max(1, Math.ceil(text.replace(/\s+/g, ' ').trim().length / 720));

PAGES.historias = async (view) => {
  const { stories } = await api('GET', '/api/stories');
  view.innerHTML = `${head('Histórias', 'A biblioteca online que o robô lê página por página, palavra por palavra.', `<button class="btn primary" data-new>${icon('plus', 18)}Nova história</button>`)}
    <div class="callout">${icon('info')}<div>Para ouvir, diga: <b>"Stack-Chan, conta a história do…"</b> e o título. O robô também tem as histórias dele no cartão; estas ficam na nuvem e você pode trocar quando quiser.</div></div>
    <section class="card flush mt-lg">
      <div class="list">${stories.length ? stories.map((s) => `<div class="item">
        <div class="ico accent">${icon('book', 18)}</div>
        <div class="grow"><div class="title ellipsis">${esc(s.title)}</div><div class="tiny faint">${plural(s.pages, 'página', 'páginas')} · atualizada ${esc(rel(s.updatedAt))}</div></div>
        <div class="actions">
          <button class="btn sm icon ghost" data-read="${esc(s.id)}" aria-label="Ler">${icon('eye', 18)}</button>
          <button class="btn sm icon ghost" data-edit="${esc(s.id)}" aria-label="Editar">${icon('edit', 18)}</button>
          <button class="btn sm icon ghost danger" data-del="${esc(s.id)}" data-title="${esc(s.title)}" aria-label="Apagar">${icon('trash', 18)}</button>
        </div></div>`).join('') : `<div class="empty">${face()}<p>Nenhuma história ainda.</p></div>`}</div>
    </section>`;

  $('[data-new]', view).onclick = () => storyEditor();
  $$('[data-read]', view).forEach((b) => (b.onclick = () => storyPreview(b.dataset.read).catch(fail)));
  $$('[data-edit]', view).forEach((b) => (b.onclick = () => api('GET', `/api/stories/${b.dataset.edit}`).then(storyEditor).catch(fail)));
  $$('[data-del]', view).forEach((b) => {
    b.onclick = async () => {
      if (!(await confirmDialog(`"${b.dataset.title}" sai da biblioteca do robô.`))) return;
      await api('DELETE', `/api/stories/${b.dataset.del}`).then(() => toast('História apagada')).catch(fail);
      navigate();
    };
  });
};

async function storyPreview(id) {
  const s = await api('GET', `/api/stories/${id}`);
  openModal({
    title: s.title,
    body: `<div class="pages">${s.pages.map((p, i) => `<div class="page-preview"><b>Página ${i + 1} de ${s.pages.length}</b>${esc(p)}</div>`).join('')}</div>`,
  });
}

function storyEditor(story) {
  const root = openModal({
    title: story ? 'Editar história' : 'Nova história',
    body: `<form class="stack" data-form>
      <label class="field">Título<input class="input" name="title" maxlength="120" required value="${esc(story?.title ?? '')}" placeholder="O Dinossauro Sonolento"></label>
      <label class="field">Texto<textarea class="input" name="text" required placeholder="Era uma vez…">${esc(story?.text ?? '')}</textarea>
        <span class="hint" data-count></span></label>
      <label class="btn sm ghost">${icon('upload', 16)}Importar arquivo .txt<input type="file" accept=".txt,text/plain" class="hidden" data-file></label>
      <p class="form-error" data-error></p>
    </form>`,
    actions: `<button class="btn" data-cancel>Cancelar</button><button class="btn primary" data-save>Salvar</button>`,
  });
  const form = $('[data-form]', root);
  const count = () => {
    const t = form.text.value;
    const words = t.trim() ? t.trim().split(/\s+/).length : 0;
    $('[data-count]', root).textContent = words ? `${words} palavras · cerca de ${plural(countPages(t), 'página', 'páginas')} de leitura` : '';
  };
  form.text.oninput = count;
  count();
  $('[data-file]', root).onchange = async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    let text = await file.text();
    const first = text.split('\n', 1)[0];
    if (first.startsWith('# ')) {
      form.title.value ||= first.slice(2).trim();
      text = text.slice(first.length).trim();
    }
    form.title.value ||= file.name.replace(/\.txt$/i, '').replace(/[_-]+/g, ' ');
    form.text.value = text;
    count();
  };
  $('[data-cancel]', root).onclick = closeModal;
  $('[data-save]', root).onclick = (e) =>
    busy(e.currentTarget, async () => {
      const body = { title: form.title.value, text: form.text.value };
      if (!body.title.trim() || !body.text.trim()) return void ($('[data-error]', root).textContent = 'Preencha título e texto.');
      try {
        await (story ? api('PUT', `/api/stories/${story.id}`, body) : api('POST', '/api/stories', body));
        closeModal();
        toast('História salva');
        navigate();
      } catch (err) {
        $('[data-error]', root).textContent = err.message;
      }
    });
}

/* =================================== Music ================================== */

PAGES.musicas = async (view, _params, cleanup) => {
  const render = async () => {
    const { tracks, jobs } = await api('GET', '/api/music');
    const active = jobs.filter((j) => j.status !== 'done' || Date.now() - new Date(j.startedAt).getTime() < 120_000);
    view.innerHTML = `${head('Músicas do robô', 'Tocam no alto-falante do próprio Stack-Chan.')}
      <div class="grid grid-2">
        <section class="card stack">
          <h2>${icon('upload', 18)}Enviar música</h2>
          <label class="drop" data-drop>${icon('music', 30)}<div><b>Arraste um arquivo</b> ou toque para escolher</div><div class="tiny">MP3, M4A, OGG, WAV, FLAC · até 60 MB</div><input type="file" accept="audio/*,.mp3,.m4a,.ogg,.wav,.flac,.webm" data-file></label>
          <form class="stack hidden" data-form>
            <div class="row"><div class="ico accent">${icon('music', 18)}</div><div class="grow ellipsis small" data-fname></div></div>
            <label class="field">Nome da música<input class="input" name="title" maxlength="120" required></label>
            <label class="field">Artista <span class="hint">opcional</span><input class="input" name="artist" maxlength="120"></label>
            <div class="bar hidden" data-bar><i data-w="0"></i></div>
            <div class="row"><button class="btn primary grow" data-send>Enviar e converter</button><button class="btn" type="button" data-cancel>Cancelar</button></div>
          </form>
          ${active.length ? `<div class="list card soft flush">${active.map((j) => `<div class="item">
            <div class="ico ${j.status === 'error' ? 'red' : j.status === 'done' ? 'mint' : 'accent'}">${icon(j.status === 'error' ? 'alert' : j.status === 'done' ? 'check' : 'music', 18)}</div>
            <div class="grow"><div class="title ellipsis">${esc(j.title)}</div><div class="tiny faint">${j.status === 'processing' ? 'Convertendo para o formato do robô…' : j.status === 'done' ? 'Pronta!' : esc(j.error ?? 'Falhou')}</div>
            ${j.status === 'processing' ? '<div class="bar indeterminate mt"><i></i></div>' : ''}</div></div>`).join('')}</div>` : ''}
        </section>
        <section class="stack">
          <div class="callout">${icon('robot')}<div>Diga <b>"Stack-Chan, toca <i>nome da música</i> no robô"</b>. Ela começa assim que ele terminar de falar; para parar, é só falar com ele.</div></div>
          <div class="callout">${icon('sparkle')}<div>Cada envio é convertido uma vez para o único formato que o robô toca com segurança (Opus 16 kHz, mono), com volume equalizado e graves cortados para o alto-falante pequeno.</div></div>
          <div class="callout warn">${icon('alert')}<div>Tocar no alto-falante do robô precisa do firmware novo (ferramenta <code>self.gateway.play_audio</code>). Use músicas que você comprou ou tem direito de usar.</div></div>
        </section>
      </div>
      <section class="card flush mt-lg">
        <div class="card-head pad-head"><h2>Biblioteca</h2><span class="badge">${tracks.length}</span></div>
        <div class="list">${tracks.length ? tracks.map((t) => `<div class="item" data-track="${esc(t.id)}">
          <button class="btn icon round sm" data-preview="${esc(t.id)}" aria-label="Ouvir">${icon('play', 14)}</button>
          <div class="grow"><div class="title ellipsis">${esc(t.title)}</div><div class="tiny faint ellipsis">${esc(t.artist || 'Artista desconhecido')} · ${fmtDur(t.seconds)} · ${fmtBytes(t.bytes)}</div></div>
          <div class="actions">
            <button class="btn sm icon ghost" data-edit="${esc(t.id)}" aria-label="Editar">${icon('edit', 18)}</button>
            <button class="btn sm icon ghost danger" data-del="${esc(t.id)}" data-title="${esc(t.title)}" aria-label="Apagar">${icon('trash', 18)}</button>
          </div></div>`).join('') : `<div class="empty">${face()}<p>Nenhuma música ainda. Que tal a favorita das crianças?</p></div>`}</div>
      </section>`;
    bindMusic(view, tracks, render);
  };
  await render();
  cleanup(on('music', () => render().catch(() => {})));
};

function bindMusic(view, tracks, render) {
  const drop = $('[data-drop]', view);
  const input = $('[data-file]', view);
  const form = $('[data-form]', view);
  let file = null;
  const pick = (f) => {
    if (!f) return;
    file = f;
    $('[data-fname]', view).textContent = `${f.name} · ${fmtBytes(f.size)}`;
    const base = f.name.replace(/\.[^.]+$/, '').replace(/[_]+/g, ' ');
    const [a, b] = base.split(/\s+-\s+/);
    form.title.value = (b ?? a).trim();
    form.artist.value = b ? a.trim() : '';
    drop.classList.add('hidden');
    form.classList.remove('hidden');
    form.title.focus();
  };
  input.onchange = () => pick(input.files[0]);
  ['dragenter', 'dragover'].forEach((ev) => drop.addEventListener(ev, (e) => (e.preventDefault(), drop.classList.add('over'))));
  ['dragleave', 'drop'].forEach((ev) => drop.addEventListener(ev, () => drop.classList.remove('over')));
  drop.addEventListener('drop', (e) => (e.preventDefault(), pick(e.dataTransfer.files[0])));
  $('[data-cancel]', view).onclick = () => render();

  form.onsubmit = (e) => {
    e.preventDefault();
    if (!file || !form.title.value.trim()) return;
    if (file.size > 60 * 1024 * 1024) return toast('Arquivo maior que 60 MB', 'bad');
    const bar = $('[data-bar]', view);
    bar.classList.remove('hidden');
    $('[data-send]', view).disabled = true;
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `/api/music/upload?title=${encodeURIComponent(form.title.value.trim())}&artist=${encodeURIComponent(form.artist.value.trim())}`);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (ev) => {
      if (ev.lengthComputable) $('i', bar).style.width = `${Math.round((ev.loaded / ev.total) * 100)}%`;
    };
    xhr.onload = () => {
      if (xhr.status === 202) {
        toast('Enviada! Convertendo para o robô…');
        render();
      } else {
        let msg = `Erro ${xhr.status}`;
        try {
          msg = JSON.parse(xhr.responseText).error || msg;
        } catch {}
        if (xhr.status === 401) return showLogin();
        toast(msg, 'bad');
        $('[data-send]', view).disabled = false;
      }
    };
    xhr.onerror = () => {
      toast('Falha de rede no envio', 'bad');
      $('[data-send]', view).disabled = false;
    };
    xhr.send(file);
  };

  let player = null;
  $$('[data-preview]', view).forEach((b) => {
    b.onclick = () => {
      const id = b.dataset.preview;
      const playing = player && player.dataset.id === id && !player.paused;
      player?.pause();
      $$('[data-preview]', view).forEach((x) => (x.innerHTML = icon('play', 14)));
      if (playing) return;
      player = new Audio(`/api/music/${id}/audio`);
      player.dataset.id = id;
      player.play().catch(() => toast('Este navegador não toca Ogg Opus', 'bad'));
      b.innerHTML = icon('pause', 14);
      player.onended = () => (b.innerHTML = icon('play', 14));
    };
  });
  $$('[data-edit]', view).forEach((b) => {
    b.onclick = () => {
      const t = tracks.find((x) => x.id === b.dataset.edit);
      const root = openModal({
        title: 'Editar música',
        size: 'sm',
        body: `<form class="stack" data-f><label class="field">Nome<input class="input" name="title" value="${esc(t.title)}" maxlength="120"></label><label class="field">Artista<input class="input" name="artist" value="${esc(t.artist)}" maxlength="120"></label></form>`,
        actions: `<button class="btn" data-x>Cancelar</button><button class="btn primary" data-ok>Salvar</button>`,
      });
      $('[data-x]', root).onclick = closeModal;
      $('[data-ok]', root).onclick = async () => {
        const f = $('[data-f]', root);
        await api('PATCH', `/api/music/${t.id}`, { title: f.title.value, artist: f.artist.value }).then(() => toast('Salvo')).catch(fail);
        closeModal();
        render();
      };
    };
  });
  $$('[data-del]', view).forEach((b) => {
    b.onclick = async () => {
      if (!(await confirmDialog(`"${b.dataset.title}" sai da biblioteca do robô.`))) return;
      await api('DELETE', `/api/music/${b.dataset.del}`).then(() => toast('Música apagada')).catch(fail);
      render();
    };
  });
  applyWidths(view);
}

/* ================================ MCP servers =============================== */

PAGES.conexoes = async (view) => {
  const { servers } = await api('GET', '/api/mcp');
  view.innerHTML = `${head('Conexões MCP', 'Ligue servidores MCP da internet e escolha quais ferramentas o robô pode usar.', `<button class="btn primary" data-add>${icon('plus', 18)}Adicionar servidor</button>`)}
    <div class="callout warn">${icon('shield')}<div>Ferramentas externas começam <b>desligadas</b>: ligue só o que for seguro para as crianças usarem por voz. Só servidores na internet pública são aceitos, e o painel nunca executa programas no servidor.</div></div>
    <div class="stack mt-lg">${servers.length ? servers.map(serverCard).join('') : `<section class="card empty">${face()}<p>Nenhum servidor conectado.</p><p class="small faint">Ex.: um MCP de agenda, de casa inteligente ou de notícias que ofereça endereço HTTP.</p></section>`}</div>`;

  $('[data-add]', view).onclick = () => addServer();
  $$('[data-server]', view).forEach((card) => {
    const id = card.dataset.server;
    const server = servers.find((s) => s.id === id);
    $('[data-enabled]', card).onchange = (e) =>
      api('PATCH', `/api/mcp/${id}`, { enabled: e.target.checked }).then(() => (toast('Atualizado'), navigate())).catch(fail);
    $$('[data-tool]', card).forEach((cb) => {
      cb.onchange = () => {
        const enabledTools = $$('[data-tool]', card).filter((x) => x.checked).map((x) => x.dataset.tool);
        api('PATCH', `/api/mcp/${id}`, { enabledTools }).then(() => toast(cb.checked ? 'Ferramenta ligada' : 'Ferramenta desligada')).catch(fail);
      };
    });
    $('[data-refresh]', card).onclick = (e) =>
      busy(e.currentTarget, () => api('POST', `/api/mcp/${id}/refresh`)).then(navigate).catch(fail);
    $('[data-remove]', card).onclick = async () => {
      if (!(await confirmDialog(`Remover "${server.name}" e as ferramentas dele?`, 'Remover'))) return;
      await api('DELETE', `/api/mcp/${id}`).catch(fail);
      navigate();
    };
  });
};

function serverCard(s) {
  const status = s.lastError ? `<span class="pill bad"><span class="dot"></span>Erro</span>` : s.enabled ? `<span class="pill ok"><span class="dot"></span>Ativo</span>` : '<span class="pill">Desligado</span>';
  return `<section class="card" data-server="${esc(s.id)}">
    <div class="card-head">
      <div class="grow"><h2>${icon('plug', 18)}${esc(s.name)}</h2><div class="tiny faint mono ellipsis">${esc(s.url)}</div></div>
      ${status}
      <label class="switch" title="Ligar/desligar servidor"><input type="checkbox" data-enabled ${s.enabled ? 'checked' : ''}><span></span></label>
    </div>
    ${s.lastError ? `<div class="callout warn">${icon('alert')}<div>${esc(s.lastError)}</div></div>` : ''}
    <div class="list card soft flush mt">${s.discovered.length ? s.discovered.map((t) => `<div class="item">
      <div class="grow"><div class="title mono small">${esc(t.name)}</div>${t.description ? `<div class="tool-desc">${esc(t.description)}</div>` : ''}</div>
      <label class="switch"><input type="checkbox" data-tool="${esc(t.name)}" ${s.enabledTools.includes(t.name) ? 'checked' : ''} ${s.enabled ? '' : 'disabled'}><span></span></label>
    </div>`).join('') : '<div class="empty small">Nenhuma ferramenta descoberta.</div>'}</div>
    <div class="row between mt"><span class="tiny faint">${s.headerNames.length ? `${icon('key', 12)} cabeçalhos: ${esc(s.headerNames.join(', '))} · ` : ''}verificado ${esc(rel(s.lastCheckedAt))}</span>
      <div class="row"><button class="btn sm" data-refresh>${icon('refresh', 14)}Atualizar</button><button class="btn sm danger" data-remove>${icon('trash', 14)}Remover</button></div></div>
  </section>`;
}

function addServer() {
  const root = openModal({
    title: 'Adicionar servidor MCP',
    body: `<form class="stack" data-f>
      <label class="field">Nome<input class="input" name="name" maxlength="40" placeholder="Agenda da família"></label>
      <label class="field">Endereço (HTTP streamable ou SSE)<input class="input mono" name="url" type="url" required placeholder="https://exemplo.com/mcp"></label>
      <div class="stack" data-headers></div>
      <button type="button" class="btn sm ghost" data-addh>${icon('key', 16)}Adicionar cabeçalho de autenticação</button>
      <p class="hint">Valores de cabeçalhos (como "Authorization: Bearer …") ficam criptografados e nunca voltam para a tela.</p>
      <p class="form-error" data-error></p>
    </form>`,
    actions: `<button class="btn" data-x>Cancelar</button><button class="btn primary" data-ok>Conectar e descobrir</button>`,
  });
  const headers = $('[data-headers]', root);
  $('[data-addh]', root).onclick = () =>
    headers.insertAdjacentHTML('beforeend', `<div class="row"><input class="input mono" placeholder="Authorization" data-hn><input class="input mono" type="password" placeholder="valor" data-hv><button type="button" class="btn icon ghost" data-rm aria-label="Remover">${icon('x', 16)}</button></div>`);
  headers.onclick = (e) => e.target.closest('[data-rm]')?.parentElement.remove();
  $('[data-x]', root).onclick = closeModal;
  $('[data-ok]', root).onclick = (e) =>
    busy(e.currentTarget, async () => {
      const f = $('[data-f]', root);
      const hs = $$('.row', headers).map((r) => ({ name: $('[data-hn]', r).value.trim(), value: $('[data-hv]', r).value }));
      try {
        const { server } = await api('POST', '/api/mcp', { name: f.name.value, url: f.url.value.trim(), headers: hs });
        closeModal();
        toast(server.lastError ? 'Salvo, mas o servidor não respondeu' : `${server.discovered.length} ferramentas encontradas`, server.lastError ? 'bad' : 'ok');
        navigate();
      } catch (err) {
        $('[data-error]', root).textContent = err.message;
      }
    });
}

/* =================================== Tools ================================== */

const SOURCE_NAMES = { diagnostics: 'Diagnóstico', stories: 'Histórias', music: 'Música', spotify: 'Controle do Spotify', weather: 'Clima' };

PAGES.ferramentas = async (view) => {
  const data = await api('GET', '/api/tools');
  const disabled = new Set(data.tools.filter((t) => !t.enabled && !data.modules.find((m) => m.name === t.source && !m.enabled)).map((t) => t.name));
  const groups = new Map();
  for (const t of data.tools) {
    if (!groups.has(t.source)) groups.set(t.source, []);
    groups.get(t.source).push(t);
  }
  const extName = (src) => data.external.find((e) => `mcp:${e.id}` === src)?.name;
  view.innerHTML = `${head('Ferramentas', 'O que a IA do robô pode usar. Mudanças chegam ao robô em poucos segundos.')}
    <section class="card flush">
      <div class="card-head pad-head"><h2>Módulos</h2><span class="badge">${data.tools.filter((t) => t.enabled).length} ativas</span></div>
      <div class="list">${data.modules.map((m) => `<div class="item">
        <div class="grow"><div class="title">${esc(m.title)}</div><div class="tool-desc">${esc(m.description)}</div></div>
        <label class="switch"><input type="checkbox" data-module="${esc(m.name)}" ${m.enabled ? 'checked' : ''}><span></span></label></div>`).join('')}</div>
    </section>
    ${[...groups.entries()].map(([src, tools]) => `<section class="card flush mt-lg">
      <div class="card-head pad-head"><h2>${esc(SOURCE_NAMES[src] ?? extName(src) ?? src)}</h2>${src.startsWith('mcp:') ? '<span class="badge blue">externo</span>' : ''}</div>
      <div class="list">${tools.map((t) => `<div class="item">
        <div class="grow"><div class="title mono small">${esc(t.name)}</div>
          <details class="more"><summary>O que a IA lê</summary><p>${esc(t.description)}</p></details></div>
        <label class="switch"><input type="checkbox" data-tool="${esc(t.name)}" ${!disabled.has(t.name) ? 'checked' : ''} ${data.modules.find((m) => m.name === src && !m.enabled) ? 'disabled' : ''}><span></span></label></div>`).join('')}</div>
    </section>`).join('')}`;

  $$('[data-module]', view).forEach((cb) => {
    cb.onchange = () =>
      api('PATCH', '/api/tools', { modules: { [cb.dataset.module]: cb.checked } })
        .then(() => (toast(cb.checked ? 'Módulo ligado' : 'Módulo desligado'), navigate()))
        .catch(fail);
  });
  $$('[data-tool]', view).forEach((cb) => {
    cb.onchange = () => {
      if (cb.checked) disabled.delete(cb.dataset.tool);
      else disabled.add(cb.dataset.tool);
      api('PATCH', '/api/tools', { disabledTools: [...disabled] }).then(() => toast('Atualizado')).catch(fail);
    };
  });
};

/* ================================== Activity ================================ */

PAGES.atividade = async (view, _params, cleanup) => {
  const { entries } = await api('GET', '/api/activity?limit=300');
  let filter = 'all';
  const list = [...entries];
  const FILTERS = [['all', 'Tudo'], ['tool', 'Robô'], ['system', 'Sistema'], ['auth', 'Login'], ['error', 'Erros']];
  const match = (e) => filter === 'all' || (filter === 'tool' ? e.kind === 'tool' || e.kind === 'robot' : filter === 'error' ? e.ok === false || e.kind === 'error' : e.kind === filter);
  const draw = () => {
    const shown = list.filter(match);
    $('[data-list]', view).innerHTML = shown.length ? shown.map((e) => activityRow(e)).join('') : '<div class="empty">Nada por aqui.</div>';
  };
  view.innerHTML = `${head('Atividade', 'Tudo o que o robô pediu ao gateway e o que mudou no painel. Sem gravar o que as crianças falam.', `<button class="btn sm danger" data-clear>${icon('trash', 16)}Limpar</button>`)}
    <div class="segmented" data-filters>${FILTERS.map(([k, l]) => `<button data-f="${k}" class="${k === 'all' ? 'on' : ''}">${l}</button>`).join('')}</div>
    <section class="card flush mt"><div class="list feed" data-list></div></section>`;
  draw();
  $('[data-filters]', view).onclick = (e) => {
    const b = e.target.closest('[data-f]');
    if (!b) return;
    filter = b.dataset.f;
    $$('[data-f]', view).forEach((x) => x.classList.toggle('on', x === b));
    draw();
  };
  $('[data-clear]', view).onclick = async () => {
    if (!(await confirmDialog('Apagar todo o histórico de atividade?'))) return;
    await api('DELETE', '/api/activity').catch(fail);
    list.length = 0;
    draw();
  };
  cleanup(on('activity', (e) => {
    list.unshift(e);
    if (match(e)) {
      const box = $('[data-list]', view);
      $('.empty', box)?.remove();
      box.insertAdjacentHTML('afterbegin', activityRow(e, true));
    }
  }));
};

/* ================================== Settings ================================ */

PAGES.ajustes = async (view) => {
  const [s, { sessions }] = await Promise.all([api('GET', '/api/settings'), api('GET', '/api/sessions')]);
  const TARGETS = [['auto', 'Automático'], ['robot', 'Robô primeiro'], ['spotify', 'Spotify primeiro']];
  view.innerHTML = `${head('Ajustes')}
    <div class="grid grid-2">
      <section class="card stack">
        <h2>${icon('home', 18)}Casa</h2>
        <form class="stack" data-city>
          <label class="field">Cidade (para o clima)<div class="row"><input class="input" name="city" value="${esc(s.homeCity)}" placeholder="São Paulo"><button class="btn">Salvar</button></div>
          <span class="hint">${s.home ? `Encontrada: ${esc(s.home.name)}` : 'Ainda não definida'}</span></label>
        </form>
        <div class="sep"></div>
        <h2>${icon('music', 18)}"Toca…" sem dizer onde</h2>
        <div class="segmented" data-target>${TARGETS.map(([k, l]) => `<button data-v="${k}" class="${s.musicDefaultTarget === k ? 'on' : ''}">${l}</button>`).join('')}</div>
        <p class="small muted">Automático: se a música estiver na biblioteca do robô, toca nele; senão, no Spotify (aparelho padrão).</p>
      </section>
      <section class="card stack">
        <h2>${icon('spotify', 18)}Spotify e crianças</h2>
        <div class="row between"><div><b>Modo infantil</b><p class="small muted">Nunca tocar músicas explícitas (nem álbuns e playlists que tenham alguma).</p></div>
          <label class="switch"><input type="checkbox" data-kid ${s.kidMode ? 'checked' : ''}><span></span></label></div>
        <label class="field">Volume máximo pelo robô: <b data-volv>${s.spotifyMaxVolume}%</b><input type="range" min="10" max="100" step="5" value="${s.spotifyMaxVolume}" data-vol></label>
        <p class="small muted">Aparelho padrão: <b>${esc(s.spotifyDefaultDevice || 'o que estiver tocando')}</b> · troque na página Spotify (estrela).</p>
      </section>
      <section class="card stack">
        <h2>${icon('shield', 18)}Segurança</h2>
        <form class="stack" data-pass>
          <label class="field">Senha atual<input class="input" type="password" name="current" autocomplete="current-password" required></label>
          <label class="field">Nova senha <span class="hint">mínimo 10 caracteres</span><input class="input" type="password" name="next" autocomplete="new-password" minlength="10" required></label>
          <p class="form-error" data-error></p>
          <button class="btn">Trocar senha</button>
        </form>
      </section>
      <section class="card flush">
        <div class="card-head pad-head"><h2>${icon('key', 18)}Sessões abertas</h2><button class="btn sm" data-others>Sair dos outros</button></div>
        <div class="list">${sessions.map((x) => `<div class="item"><div class="ico ${x.current ? 'mint' : ''}">${icon(/mobile|iphone|android/i.test(x.ua) ? 'phone' : 'computer', 18)}</div>
          <div class="grow"><div class="ellipsis small">${esc(x.ua || 'Navegador')}</div><div class="tiny faint">${esc(x.ip)} · ${x.current ? 'esta sessão' : `ativa ${esc(rel(x.lastSeen))}`}</div></div></div>`).join('')}</div>
        <div class="item"><button class="btn sm ghost" data-logout2>${icon('logout', 16)}Sair deste aparelho</button></div>
      </section>
    </div>
    <p class="tiny faint center mt-lg">Stack-Chan Gateway ${esc(state.me?.version ?? '')} · ${esc(s.publicUrl)} · fuso ${esc(s.timezone)}${s.secretsAvailable ? '' : ' · ⚠ SECRETS_KEY ausente'}</p>`;

  $('[data-city]', view).onsubmit = (e) => {
    e.preventDefault();
    busy($('button', e.target), () => api('PATCH', '/api/settings', { homeCity: e.target.city.value }))
      .then((r) => (toast(r.home ? `Cidade: ${r.home.name}` : 'Cidade removida'), navigate()))
      .catch(fail);
  };
  $('[data-target]', view).onclick = (e) => {
    const b = e.target.closest('[data-v]');
    if (!b) return;
    $$('[data-v]', view).forEach((x) => x.classList.toggle('on', x === b));
    api('PATCH', '/api/settings', { musicDefaultTarget: b.dataset.v }).then(() => toast('Salvo')).catch(fail);
  };
  $('[data-kid]', view).onchange = (e) =>
    api('PATCH', '/api/settings', { kidMode: e.target.checked }).then(() => toast(e.target.checked ? 'Modo infantil ligado' : 'Modo infantil desligado')).catch(fail);
  $('[data-vol]', view).oninput = (e) => ($('[data-volv]', view).textContent = `${e.target.value}%`);
  $('[data-vol]', view).onchange = (e) =>
    api('PATCH', '/api/settings', { spotifyMaxVolume: Number(e.target.value) }).then(() => toast('Volume máximo salvo')).catch(fail);
  $('[data-pass]', view).onsubmit = (e) => {
    e.preventDefault();
    const f = e.target;
    $('[data-error]', view).textContent = '';
    busy($('button', f), () => api('POST', '/api/settings/password', { current: f.current.value, next: f.next.value }))
      .then(() => (toast('Senha trocada. Outras sessões foram encerradas.'), f.reset()))
      .catch((err) => ($('[data-error]', view).textContent = err.message));
  };
  $('[data-others]', view).onclick = () =>
    api('POST', '/api/sessions/logout-others').then((r) => (toast(`${plural(r.removed, 'sessão encerrada', 'sessões encerradas')}`), navigate())).catch(fail);
  $('[data-logout2]', view).onclick = logout;
};

/* ==================================== More ================================== */

PAGES.mais = async (view) => {
  view.innerHTML = `${head('Mais')}
    <section class="card flush"><div class="list">
      ${NAV.filter((n) => !TABS.includes(n.route)).map((n) => `<a class="item" href="#/${n.route}"><div class="ico accent">${icon(n.icon, 18)}</div><div class="grow title">${esc(n.label)}</div>${icon('arrow', 16)}</a>`).join('')}
      <button class="item btn ghost block" data-out>${icon('logout', 18)}Sair</button>
    </div></section>`;
  $('[data-out]', view).onclick = logout;
};

/* ==================================== Boot ================================== */

async function boot() {
  try {
    state.me = await api('GET', '/api/me');
  } catch (err) {
    if (err.status !== 401) {
      app.innerHTML = `<div class="boot"><div class="stack center">${face('face--lg is-off')}<p>Não consegui falar com o gateway.</p><button class="btn" data-r>Tentar de novo</button></div></div>`;
      $('[data-r]', app).onclick = boot;
    }
    return;
  }
  state.status = await api('GET', '/api/status').catch(() => null);
  renderShell();
  connectEvents();
  await navigate();
}

window.addEventListener('hashchange', navigate);
boot();
