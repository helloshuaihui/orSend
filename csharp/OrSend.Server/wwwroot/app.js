/* orSend 服务端控制台 */
const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

const state = { snapshot: null, lastLogId: 0, settingsLoaded: false, logLines: 0 };

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

/* ---------- 状态渲染 ---------- */
function setPill(online, error) {
  const pill = $('#enginePill');
  pill.className = 'pill ' + (online ? 'pill-on' : (error ? 'pill-err' : 'pill-idle'));
  pill.innerHTML = `<i></i>${online ? '运行中' : (error ? '异常' : '未运行')}`;
  $('#btnToggle').textContent = online ? '停止服务端' : '启动服务端';
  $('#btnToggle').className = 'btn ' + (online ? 'btn-ghost' : 'btn-primary');
}

function render(snap) {
  state.snapshot = snap;
  const s = snap.status;
  const online = !!snap.engineOnline && !!s;
  setPill(online, snap.lastError);

  const errBox = $('#engineError');
  if (!online && snap.lastError) {
    errBox.hidden = false;
    errBox.textContent = snap.lastError;
  } else {
    errBox.hidden = true;
  }

  const server = (s && s.server) || {};
  const totals = (s && s.totals) || {};

  $('#statAgents').textContent = server.agentCount ?? 0;
  $('#statTunnels').textContent = server.tunnelCount ?? 0;
  $('#statStreams').textContent = server.activeStreams ?? 0;
  $('#statTraffic').textContent = fmtBytes((server.bytesIn || 0) + (server.bytesOut || 0));
  $('#statTrafficHint').textContent = `上行 ${fmtBytes(server.bytesIn)} / 下行 ${fmtBytes(server.bytesOut)}`;

  $('#uptimeText').textContent = server.running ? `已运行 ${fmtDuration(server.uptimeSec)}` : '—';
  $('#infoBind').textContent = server.listen || snap.settings.bind || '—';
  $('#infoPublic').textContent = server.publicHost || snap.settings.publicHost || '—';
  $('#infoControl').textContent = `127.0.0.1:${(s && s.control && s.control.port) || snap.settings.controlPort}`;
  $('#infoVersion').textContent = (s && s.version) || '—';
  $('#exePathText').textContent = snap.exePath ? '引擎: ' + snap.exePath : '未找到引擎可执行文件';

  $('#tokenWarn').hidden = !!snap.settings.token;

  renderAgents(server.agents || []);
  renderTunnels(server.tunnels || []);
  renderTunnelOverview(server.tunnels || []);
  renderForward((s && s.forward) || []);
  renderMapping((s && s.mapping) || []);
  renderRulesOverview((s && s.forward) || [], (s && s.mapping) || []);

  if (!state.settingsLoaded && snap.settings) {
    fillSettings(snap.settings);
    state.settingsLoaded = true;
  }
  $('#engineOutput').textContent = snap.recentOutput || '—';
}

function renderAgents(list) {
  const tbody = $('#agentRows');
  if (!list.length) {
    tbody.innerHTML = '<tr><td colspan="8" class="empty">暂无客户端接入</td></tr>';
    return;
  }
  tbody.innerHTML = list.map((a) => `
    <tr>
      <td class="mono">${esc(a.clientId)}</td>
      <td>${esc(a.name)}</td>
      <td class="mono">${esc(a.remoteAddr)}</td>
      <td>${fmtDuration(a.uptimeSec)}</td>
      <td>${a.streamsTotal}</td>
      <td class="mono">${fmtBytes(a.bytesIn)} / ${fmtBytes(a.bytesOut)}</td>
      <td><div class="tag-list">${(a.tunnels || []).map((t) => `<span class="badge badge-info">${esc(t)}</span>`).join('') || '<span class="muted">—</span>'}</div></td>
      <td><button class="btn btn-ghost btn-sm" data-kick="${esc(a.clientId)}">断开</button></td>
    </tr>`).join('');

  $$('[data-kick]', tbody).forEach((btn) => {
    btn.addEventListener('click', async () => {
      if (!confirm(`确定断开客户端 ${btn.dataset.kick} 吗？`)) return;
      try {
        await api('/api/server/kick', 'POST', { clientId: btn.dataset.kick });
        toast('已断开 ' + btn.dataset.kick, 'ok');
        refresh();
      } catch (e) { toast(e.message, 'err'); }
    });
  });
}

function renderTunnels(list) {
  const tbody = $('#tunnelRows');
  if (!list.length) {
    tbody.innerHTML = '<tr><td colspan="7" class="empty">暂无隧道</td></tr>';
    return;
  }
  tbody.innerHTML = list.map((t) => `
    <tr>
      <td><b>${esc(t.name)}</b></td>
      <td class="mono">${esc(t.clientId)}</td>
      <td class="mono">${esc(t.publicHost)}:${t.publicPort}</td>
      <td class="mono">${esc(t.target)}</td>
      <td>${t.activeStreams}</td>
      <td>${t.connections}</td>
      <td class="mono">${fmtBytes(t.bytesIn)} / ${fmtBytes(t.bytesOut)}</td>
    </tr>`).join('');
}

function renderForward(list) {
  const tbody = $('#forwardRows');
  if (!list.length) {
    tbody.innerHTML = '<tr><td colspan="7" class="empty">暂无转发规则</td></tr>';
    return;
  }
  tbody.innerHTML = list.map((r) => `
    <tr>
      <td><b>${esc(r.name)}</b></td>
      <td class="mono">${esc(r.listen)}</td>
      <td class="mono">${esc(r.target || (r.targets && r.targets[0] && r.targets[0].endpoint) || '')}</td>
      <td>${r.active}</td>
      <td>${r.connections}</td>
      <td class="mono">${fmtBytes(r.bytesIn)} / ${fmtBytes(r.bytesOut)}</td>
      <td><button class="btn btn-ghost btn-sm" data-del-fwd="${esc(r.name)}">删除</button></td>
    </tr>`).join('');

  $$('[data-del-fwd]', tbody).forEach((btn) => {
    btn.addEventListener('click', async () => {
      try {
        await api('/api/forward/remove', 'POST', { name: btn.dataset.delFwd });
        toast('规则已删除', 'ok');
        refresh();
      } catch (e) { toast(e.message, 'err'); }
    });
  });
}

function renderMapping(list) {
  const tbody = $('#mappingRows');
  if (!list.length) {
    tbody.innerHTML = '<tr><td colspan="7" class="empty">暂无映射规则</td></tr>';
    return;
  }
  const stratName = { roundrobin: '轮询', leastconn: '最少连接', failover: '主备', random: '随机' };
  tbody.innerHTML = list.map((r) => `
    <tr>
      <td><b>${esc(r.name)}</b></td>
      <td class="mono">${esc(r.listen)}</td>
      <td>${stratName[r.strategy] || esc(r.strategy)}</td>
      <td><div class="tag-list">${(r.targets || []).map((t) =>
        `<span class="badge ${t.up ? 'badge-on' : 'badge-off'}" title="${esc(t.lastError || '')}">${esc(t.endpoint)}</span>`).join('')}</div></td>
      <td>${r.active}</td>
      <td>${r.connections}</td>
      <td><button class="btn btn-ghost btn-sm" data-del-map="${esc(r.name)}">删除</button></td>
    </tr>`).join('');

  $$('[data-del-map]', tbody).forEach((btn) => {
    btn.addEventListener('click', async () => {
      try {
        await api('/api/mapping/remove', 'POST', { name: btn.dataset.delMap });
        toast('规则已删除', 'ok');
        refresh();
      } catch (e) { toast(e.message, 'err'); }
    });
  });
}

function renderTunnelOverview(list) {
  const box = $('#tunnelOverview');
  if (!list.length) {
    box.innerHTML = '<div class="empty">暂无隧道</div>';
    return;
  }
  box.innerHTML = list.map((t) => `
    <div class="mini-row">
      <span><span class="badge ${t.activeStreams > 0 ? 'badge-on' : 'badge-info'}">${t.activeStreams > 0 ? '活跃' : '空闲'}</span>
        <span class="name">${esc(t.name)}</span></span>
      <span class="meta">${esc(t.publicHost)}:${t.publicPort} → ${esc(t.target)}</span>
      <span class="muted">${t.connections} 次连接</span>
    </div>`).join('');
}

function renderRulesOverview(forward, mapping) {  const box = $('#ruleOverview');
  const rows = [];
  forward.forEach((r) => rows.push(`
    <div class="mini-row">
      <span><span class="badge">转发</span> <span class="name">${esc(r.name)}</span></span>
      <span class="meta">${esc(r.listen)} → ${esc(r.target || '')}</span>
      <span class="muted">${r.active} 活跃</span>
    </div>`));
  mapping.forEach((r) => rows.push(`
    <div class="mini-row">
      <span><span class="badge badge-info">映射</span> <span class="name">${esc(r.name)}</span></span>
      <span class="meta">${esc(r.listen)} → ${(r.targets || []).length} 个目标</span>
      <span class="muted">${r.active} 活跃</span>
    </div>`));
  box.innerHTML = rows.length ? rows.join('') : '<div class="empty">暂无规则</div>';
}

function fillSettings(st) {
  const f = $('#settingsForm');
  f.bind.value = st.bind || '';
  f.publicHost.value = st.publicHost || '';
  f.token.value = st.token || '';
  f.controlPort.value = st.controlPort || 7401;
  f.logLevel.value = st.logLevel || 'info';
  f.autoStart.checked = !!st.autoStart;
}

/* ---------- 交互 ---------- */
$('#btnToggle').addEventListener('click', async () => {
  const btn = $('#btnToggle');
  const online = state.snapshot && state.snapshot.engineOnline;
  btn.disabled = true;
  try {
    if (online) {
      await api('/api/server/stop', 'POST', {});
      toast('服务端已停止', 'ok');
    } else {
      await api('/api/server/start', 'POST', {});
      toast('服务端已启动', 'ok');
    }
    await refresh();
  } catch (e) {
    toast(e.message, 'err');
  } finally {
    btn.disabled = false;
  }
});

$('#forwardForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = e.target;
  try {
    await api('/api/forward/add', 'POST', {
      name: f.name.value.trim(), listen: f.listen.value.trim(), target: f.target.value.trim()
    });
    f.reset();
    toast('转发规则已添加', 'ok');
    refresh();
  } catch (err) { toast(err.message, 'err'); }
});

$('#mappingForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = e.target;
  const targets = f.targets.value.split(',').map((x) => x.trim()).filter(Boolean);
  try {
    await api('/api/mapping/add', 'POST', {
      name: f.name.value.trim(), listen: f.listen.value.trim(), targets,
      strategy: f.strategy.value, healthCheck: true
    });
    f.reset();
    toast('映射规则已添加', 'ok');
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
      bind: f.bind.value.trim(),
      publicHost: f.publicHost.value.trim(),
      token: f.token.value,
      controlPort: Number(f.controlPort.value) || 7401,
      logLevel: f.logLevel.value,
      autoStart: f.autoStart.checked
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
  } catch { /* 忽略轮询错误 */ }
}

/* ---------- 主循环 ---------- */
async function refresh() {
  try {
    const snap = await api('/api/status');
    render(snap);
  } catch (e) {
    setPill(false, e.message);
  }
}

refresh();
setInterval(refresh, 2000);
setInterval(pollLogs, 2000);
