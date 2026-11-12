/* orSend 客户端控制台 */
const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

const state = { snapshot: null, lastLogId: 0, settingsLoaded: false, logLines: 0, connectLoaded: false };

const STATE_TEXT = {
  idle: '未连接',
  connecting: '连接中',
  handshaking: '认证中',
  ready: '已连接',
  backoff: '重连中'
};

/* ---------- 工具 ---------- */
function esc(v) {
  return String(v ?? '').replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  }[c]));
}

function fmtBytes(n) {
  n = Number(n) || 0;
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
  return (i === 0 ? n : n.toFixed(2)) + ' ' + units[i];
}

function fmtDuration(sec) {
  sec = Math.max(0, Number(sec) || 0);
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = Math.floor(sec % 60);
  if (d > 0) return `${d}天${h}小时`;
  if (h > 0) return `${h}小时${m}分`;
  if (m > 0) return `${m}分${s}秒`;
  return `${s}秒`;
}

function fmtTime(ms) {
  if (!ms) return '—';
  const d = new Date(Number(ms));
  const p = (v) => String(v).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

function toast(msg, type = '') {
  const el = document.createElement('div');
  el.className = 'toast ' + type;
  el.textContent = msg;
  $('#toastWrap').appendChild(el);
  setTimeout(() => el.remove(), 3600);
}

async function api(path, method = 'GET', body) {
  const res = await fetch(path, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : {},
    body: body ? JSON.stringify(body) : undefined
  });
  const text = await res.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch { data = null; }
  if (!res.ok) throw new Error((data && data.error) || `HTTP ${res.status}`);
  return data;
}

/* ---------- 标签页 ---------- */
function activateTab(name) {
  const btn = $$('.tab').find((b) => b.dataset.tab === name);
  if (!btn) return;
  $$('.tab').forEach((b) => b.classList.toggle('active', b === btn));
  $$('.tab-panel').forEach((p) => p.classList.toggle('active', p.id === 'tab-' + name));
}

$$('.tab').forEach((btn) => {
  btn.addEventListener('click', () => {
    activateTab(btn.dataset.tab);
    history.replaceState(null, '', '#' + btn.dataset.tab);
  });
});

if (location.hash) activateTab(location.hash.slice(1));

/* ---------- 渲染 ---------- */
function setPill(online, clientState, error) {
  const pill = $('#enginePill');
  let cls = 'pill-idle';
  let text = '未连接';
  if (clientState === 'ready') { cls = 'pill-on'; text = '已连接'; }
  else if (clientState === 'connecting' || clientState === 'handshaking') { cls = 'pill-warn'; text = '连接中'; }
  else if (clientState === 'backoff') { cls = 'pill-err'; text = '重连中'; }
  else if (error) { cls = 'pill-err'; text = '异常'; }
  pill.className = 'pill ' + cls;
  pill.innerHTML = `<i></i>${text}`;

  const connected = clientState === 'ready' || clientState === 'connecting' ||
    clientState === 'handshaking' || clientState === 'backoff';
  const btn = $('#btnToggle');
  btn.textContent = connected ? '断开连接' : '连接服务端';
  btn.className = 'btn ' + (connected ? 'btn-ghost' : 'btn-primary');
}

function render(snap) {
  state.snapshot = snap;
  const s = snap.status;
  const client = (s && s.client) || {};
  const cs = client.state || 'idle';

  setPill(!!snap.engineOnline, cs, snap.lastError);

  const errBox = $('#engineError');
  if (!snap.engineOnline && snap.lastError) {
    errBox.hidden = false;
    errBox.textContent = snap.lastError;
  } else {
    errBox.hidden = true;
  }

  $('#statState').textContent = snap.engineOnline ? (STATE_TEXT[cs] || cs) : '引擎未运行';
  $('#statStateHint').textContent = client.lastError
    ? client.lastError
    : (client.server ? '服务端 ' + client.server : (snap.settings.server || '—'));
  $('#statUptime').textContent = cs === 'ready' ? fmtDuration(client.uptimeSec) : '—';
  $('#statStreams').textContent = client.activeStreams || 0;

  const tunnels = snap.engineOnline ? (client.tunnels || []) : (snap.settings.tunnels || []);
  const registered = tunnels.filter((t) => t.registered || (!snap.engineOnline && t.localPort)).length;
  $('#statTunnels').textContent = registered;
  $('#uptimeText').textContent = cs === 'ready' ? `已连接 ${fmtDuration(client.uptimeSec)}` : '';
  $('#exePathText').textContent = snap.exePath ? '引擎: ' + snap.exePath : '未找到引擎可执行文件';

  renderTunnels(tunnels, snap.engineOnline);
  renderOverview(tunnels, cs);

  if (!state.connectLoaded && snap.settings) {
    const f = $('#connectForm');
    f.server.value = snap.settings.server || '';
    f.token.value = snap.settings.token || '';
    f.clientId.value = snap.settings.clientId || '';
    f.name.value = snap.settings.name || '';
    state.connectLoaded = true;
  }

  if (!state.settingsLoaded && snap.settings) {
    const f = $('#settingsForm');
    f.controlPort.value = snap.settings.controlPort || 7402;
    f.logLevel.value = snap.settings.logLevel || 'info';
    f.autoConnect.checked = !!snap.settings.autoConnect;
    state.settingsLoaded = true;
  }

  $('#engineOutput').textContent = snap.recentOutput || '—';
}

function renderTunnels(list, online) {
  const tbody = $('#tunnelRows');
  if (!list.length) {
    tbody.innerHTML = '<tr><td colspan="6" class="empty">暂无隧道，先在上方添加一条</td></tr>';
    return;
  }
  tbody.innerHTML = list.map((t) => {
    // 在线时用引擎返回的结构，离线时用本地配置的结构
    const name = t.name || '';
    const target = t.target || `${t.localHost || '127.0.0.1'}:${t.localPort || 0}`;
    const publicPort = t.publicPort || t.remotePort || 0;
    const registered = online ? !!t.registered : false;
    const statusBadge = !online
      ? '<span class="badge">未启动</span>'
      : (registered ? '<span class="badge badge-on">已注册</span>' : '<span class="badge badge-off">未注册</span>');
    const err = t.lastError ? `<div class="muted" style="font-size:12px">${esc(t.lastError)}</div>` : '';
    return `
      <tr>
        <td><b>${esc(name)}</b>${err}</td>
        <td class="mono">${esc(target)}</td>
        <td class="mono">${publicPort ? publicPort : '自动分配'}</td>
        <td>${statusBadge}</td>
        <td>${t.connections || 0}</td>
        <td><button class="btn btn-ghost btn-sm" data-del="${esc(name)}">删除</button></td>
      </tr>`;
  }).join('');

  $$('[data-del]', tbody).forEach((btn) => {
    btn.addEventListener('click', async () => {
      if (!confirm(`确定删除隧道 ${btn.dataset.del} 吗？`)) return;
      try {
        await api('/api/tunnel/remove', 'POST', { name: btn.dataset.del });
        toast('隧道已删除', 'ok');
        refresh();
      } catch (e) { toast(e.message, 'err'); }
    });
  });
}

function renderOverview(list, cs) {
  const box = $('#tunnelOverview');
  if (!list.length) {
    box.innerHTML = '<div class="empty">暂无隧道</div>';
    return;
  }
  box.innerHTML = list.map((t) => {
    const target = t.target || `${t.localHost || '127.0.0.1'}:${t.localPort || 0}`;
    const port = t.publicPort || t.remotePort || 0;
    const ok = cs === 'ready' && t.registered;
    return `
      <div class="mini-row">
        <span><span class="badge ${ok ? 'badge-on' : ''}">${ok ? '在线' : '待连接'}</span>
          <span class="name">${esc(t.name)}</span></span>
        <span class="meta">${esc(target)} → 公网 ${port || '自动'}</span>
      </div>`;
  }).join('');
}

/* ---------- 交互 ---------- */
$('#btnToggle').addEventListener('click', async () => {
  const btn = $('#btnToggle');
  const client = (state.snapshot && state.snapshot.status && state.snapshot.status.client) || {};
  const connected = ['ready', 'connecting', 'handshaking', 'backoff'].includes(client.state);
  btn.disabled = true;
  try {
    if (connected) {
      await api('/api/client/stop', 'POST', {});
      toast('已断开', 'ok');
    } else {
      await api('/api/client/start', 'POST', {});
      toast('正在连接服务端…', 'ok');
    }
    await refresh();
  } catch (e) {
    toast(e.message, 'err');
  } finally {
    btn.disabled = false;
  }
});

$('#connectForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = e.target;
  const btn = f.querySelector('button[type=submit]');
  btn.disabled = true;
  btn.textContent = '连接中…';
  try {
    await api('/api/client/start', 'POST', {
      server: f.server.value.trim(),
      token: f.token.value,
      clientId: f.clientId.value.trim(),
      name: f.name.value.trim()
    });
    toast('配置已保存，正在连接', 'ok');
    await refresh();
  } catch (err) {
    toast(err.message, 'err');
  } finally {
    btn.disabled = false;
    btn.textContent = '保存并连接';
  }
});

$('#tunnelForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = e.target;
  try {
    await api('/api/tunnel/add', 'POST', {
      name: f.name.value.trim(),
      localHost: f.localHost.value.trim() || '127.0.0.1',
      localPort: Number(f.localPort.value),
      remotePort: Number(f.remotePort.value) || 0
    });
    f.reset();
    f.localHost.value = '127.0.0.1';
    f.remotePort.value = '';
    toast('隧道已添加', 'ok');
    refresh();
  } catch (err) { toast(err.message, 'err'); }
});

$('#settingsForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = e.target;
  const btn = f.querySelector('button[type=submit]');
  btn.disabled = true;
  btn.textContent = '重启中…';
  try {
    await api('/api/settings', 'POST', {
      controlPort: Number(f.controlPort.value) || 7402,
      logLevel: f.logLevel.value,
      autoConnect: f.autoConnect.checked
    });
    toast('设置已保存，引擎已重启', 'ok');
    state.settingsLoaded = false;
    await refresh();
  } catch (err) {
    toast(err.message, 'err');
  } finally {
    btn.disabled = false;
    btn.textContent = '保存并重启引擎';
  }
});

$('#btnClearLogs').addEventListener('click', () => {
  $('#logView').innerHTML = '';
  state.logLines = 0;
});

/* ---------- 日志 ---------- */
function appendLogs(entries) {
  if (!entries || !entries.length) return;
  const view = $('#logView');
  const frag = document.createDocumentFragment();
  entries.forEach((e) => {
    const line = document.createElement('div');
    line.className = 'log-line';
    line.innerHTML = `<span class="log-time">${fmtTime(e.time).slice(11)}</span>` +
      `<span class="log-level lv-${esc(e.level.toLowerCase())}">${esc(e.level)}</span>` +
      `<span class="log-tag">${esc(e.tag)}</span>` +
      `<span class="log-msg">${esc(e.message)}</span>`;
    frag.appendChild(line);
  });
  view.appendChild(frag);
  state.logLines += entries.length;
  while (state.logLines > 800 && view.firstChild) {
    view.removeChild(view.firstChild);
    state.logLines--;
  }
  if ($('#autoScroll').checked) view.scrollTop = view.scrollHeight;
}

async function pollLogs() {
  if (!state.snapshot || !state.snapshot.engineOnline) return;
  try {
    const r = await api(`/api/logs?sinceId=${state.lastLogId}&limit=200`);
    if (r && r.entries && r.entries.length) {
      state.lastLogId = r.lastId || r.entries[r.entries.length - 1].id;
      appendLogs(r.entries);
    } else if (r && r.lastId) {
      state.lastLogId = r.lastId;
    }
  } catch { /* 忽略 */ }
}

/* ---------- 主循环 ---------- */
async function refresh() {
  try {
    const snap = await api('/api/status');
    render(snap);
  } catch (e) {
    setPill(false, 'idle', e.message);
  }
}

refresh();
setInterval(refresh, 2000);
setInterval(pollLogs, 2000);
