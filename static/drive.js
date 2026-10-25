/* OIstation 云盘 v3 —— 接近 Windows 资源管理器的操作体验
   列表/网格 · 搜索 · 排序 · 多选 · 预览(图片/视频/音频/PDF/文本/代码/Markdown/JSON/CSV)
   右键菜单 · 复制/剪切/粘贴 · 快捷键 · 新建 · 属性 · 分享(文件与文件夹) · 共享快捷方式 · 拖拽上传 */
(function () {
  var D = window.DRV || { dir: '', items: [], shortcuts: [] };
  var state = {
    dir: D.dir || '',
    items: D.items || [],
    shortcuts: D.shortcuts || [],
    sel: {},
    lastIdx: -1,
    view: localStorage.getItem('drv.view') || 'list',
    sort: localStorage.getItem('drv.sort') || 'name',
    asc: localStorage.getItem('drv.asc') !== '0',
    q: ''
  };
  var clip = null; // {mode:'copy'|'cut', paths:[]}
  try { var savedClip = sessionStorage.getItem('drv.clip'); if (savedClip) clip = JSON.parse(savedClip); } catch (e) {}
  function setClip(c) { clip = c; try { c ? sessionStorage.setItem('drv.clip', JSON.stringify(c)) : sessionStorage.removeItem('drv.clip'); } catch (e) {} }
  var pickerOn = localStorage.getItem('drv.picker') !== '0';

  function $(id) { return document.getElementById(id); }
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function human(n) {
    n = Number(n) || 0;
    if (n < 1024) return n + ' B';
    if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
    if (n < 1073741824) return (n / 1048576).toFixed(2) + ' MB';
    return (n / 1073741824).toFixed(2) + ' GB';
  }
  function segEnc(p) { return String(p).split('/').map(encodeURIComponent).join('/'); }
  function folderUrl(p) { return p ? '/drive/folder/' + segEnc(p) : '/drive'; }
  function rawUrl(p) { return '/drive/raw/' + segEnc(p); }
  function parentOf(p) { var i = p.lastIndexOf('/'); return i < 0 ? '' : p.substr(0, i); }
  function baseOf(p) { var i = p.lastIndexOf('/'); return i < 0 ? p : p.substr(i + 1); }
  function pathOf(it) { return it.p || it.n; }
  function icon(it) {
    if (it.d) return '📁';
    return { image: '🖼️', video: '🎬', audio: '🎵', pdf: '📕', archive: '🗜️', office: '📊', text: '📄' }[it.k] || '📦';
  }
  function isPreviewable(it) {
    return !it.d && ['image', 'video', 'audio', 'pdf', 'text'].indexOf(it.k) >= 0;
  }

  // ---------- toast ----------
  var toastBox = null;
  function toast(msg, kind, ms) {
    if (!toastBox) {
      toastBox = document.createElement('div');
      toastBox.className = 'drv-toasts';
      document.body.appendChild(toastBox);
    }
    var el = document.createElement('div');
    el.className = 'drv-toast ' + (kind || '');
    el.innerHTML = msg;
    toastBox.appendChild(el);
    setTimeout(function () { el.classList.add('out'); setTimeout(function () { el.remove(); }, 300); }, ms || 2600);
  }

  // ---------- 网络 ----------
  function postForm(url, obj) {
    var body = new URLSearchParams();
    Object.keys(obj).forEach(function (k) { body.append(k, obj[k]); });
    return fetch(url, { method: 'POST', credentials: 'same-origin', body: body,
      headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' } })
      .then(function (r) { return r.text(); });
  }
  function postJSON(url, obj) {
    return postForm(url, obj).then(function (t) { try { return JSON.parse(t); } catch (e) { return { ok: false, msg: t.slice(0, 80) }; } });
  }
  function getJSON(url) { return fetch(url, { credentials: 'same-origin' }).then(function (r) { return r.json(); }); }

  // ---------- 保存到指定位置 ----------
  function anchorSave(blob, name) {
    var a = document.createElement('a');
    var u = URL.createObjectURL(blob);
    a.href = u; a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(function () { URL.revokeObjectURL(u); }, 20000);
  }
  function saveBlobLazy(getBlob, name) {
    var usePicker = pickerOn && !!window.showSaveFilePicker;
    if (!usePicker) {
      toast('正在准备文件…');
      getBlob().then(function (b) { anchorSave(b, name); toast('已开始下载：' + esc(name), 'ok'); })
        .catch(function () { toast('下载失败', 'err'); });
      return;
    }
    window.showSaveFilePicker({ suggestedName: name }).then(function (handle) {
      toast('正在下载…');
      return getBlob().then(function (blob) {
        return handle.createWritable().then(function (w) {
          return w.write(blob).then(function () { return w.close(); });
        });
      }).then(function () { toast('已保存：' + esc(handle.name || name), 'ok'); });
    }).catch(function (e) {
      if (e && e.name === 'AbortError') return;
      getBlob().then(function (b) { anchorSave(b, name); });
    });
  }
  function fetchBlob(url, opts) {
    return fetch(url, Object.assign({ credentials: 'same-origin' }, opts || {})).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.blob();
    });
  }
  function downloadItem(it) {
    if (it.d) return downloadZip([pathOf(it)], it.n + '.zip');
    saveBlobLazy(function () { return fetchBlob(rawUrl(pathOf(it)) + '?download=1'); }, it.n);
  }
  function downloadZip(paths, name) {
    if (!paths.length) { toast('没有选中任何内容'); return; }
    saveBlobLazy(function () {
      return fetch('/drive/zip', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: new URLSearchParams({ dir: state.dir, paths: paths.join('\n'), name: name })
      }).then(function (r) { if (!r.ok) throw new Error('zip'); return r.blob(); });
    }, name);
  }

  // ---------- 渲染 ----------
  function visibleItems() {
    var list = state.items.slice();
    if (state.q) {
      var q = state.q.toLowerCase();
      list = list.filter(function (it) { return it.n.toLowerCase().indexOf(q) >= 0; });
    }
    var dir = state.asc ? 1 : -1;
    list.sort(function (a, b) {
      if (a.d !== b.d) return a.d ? -1 : 1;
      var r = 0;
      if (state.sort === 'name') r = a.n.localeCompare(b.n, 'zh');
      else if (state.sort === 'size') r = a.s - b.s;
      else if (state.sort === 'time') r = String(a.t).localeCompare(String(b.t));
      else r = String(a.k).localeCompare(String(b.k)) || a.n.localeCompare(b.n, 'zh');
      return r * dir;
    });
    return list;
  }
  function selCount() { return Object.keys(state.sel).length; }
  function selPaths() { return Object.keys(state.sel); }
  function selItems() { return state.items.filter(function (it) { return state.sel[pathOf(it)]; }); }

  function renderShortcuts(box) {
    if (state.dir !== '' || !state.shortcuts.length) return;
    var head = document.createElement('div');
    head.className = 'drv-sc-head';
    head.textContent = '📌 共享给我的（快捷方式）';
    box.appendChild(head);
    state.shortcuts.forEach(function (s) {
      var row = document.createElement('div');
      row.className = 'drv-row drv-sc';
      row.innerHTML = '<span class="drv-check"></span><span class="drv-ico">📌</span>' +
        '<span class="drv-name" title="来自 ' + esc(s.owner) + ' 的共享"><a href="/share/' + esc(s.token) + '" style="color:inherit;text-decoration:none;">' + esc(s.name) + '</a></span>' +
        '<span class="drv-size">—</span><span class="drv-time">来自 ' + esc(s.owner) + '</span>' +
        '<span class="drv-acts"><button data-sc="open">打开</button><button data-sc="link">复制链接</button><button data-sc="remove">移除</button></span>';
      row.querySelector('[data-sc="open"]').onclick = function () { location.href = '/share/' + s.token; };
      row.querySelector('[data-sc="link"]').onclick = function () {
        copyText(location.origin + '/share/' + s.token).then(function () { toast('链接已复制', 'ok'); });
      };
      row.querySelector('[data-sc="remove"]').onclick = function () {
        if (!confirm('从我的云盘移除这个快捷方式？（不会删除对方的内容）')) return;
        postJSON('/drive/shortcut', { action: 'remove', token: s.token }).then(function (r) {
          toast(r.msg || '已移除', 'ok');
          state.shortcuts = state.shortcuts.filter(function (x) { return x.token !== s.token; });
          render();
        });
      };
      box.appendChild(row);
    });
  }

  function render() {
    var box = $('drv-list');
    var empty = $('drv-empty');
    var list = visibleItems();
    box.className = 'drv-list ' + (state.view === 'grid' ? 'grid' : 'list');
    box.innerHTML = '';
    renderShortcuts(box);
    if (!list.length) {
      empty.style.display = state.shortcuts.length && state.dir === '' ? 'none' : 'block';
      empty.innerHTML = state.q
        ? '当前目录没有匹配「' + esc(state.q) + '」的内容，试试右上角 🔍 全盘搜索。'
        : '这个文件夹是空的 —— 可以上传文件、新建文件夹，或把文件拖进来。';
    } else {
      empty.style.display = 'none';
    }
    if (state.view === 'grid') {
      list.forEach(function (it) {
        var card = document.createElement('div');
        card.className = 'drv-card' + (state.sel[pathOf(it)] ? ' sel' : '');
        card.dataset.p = pathOf(it);
        card.innerHTML = '<div class="drv-card-ico">' + icon(it) + '</div>' +
          '<div class="drv-card-name" title="' + esc(it.n) + '">' + esc(it.n) + '</div>' +
          '<div class="drv-card-meta">' + (it.d ? '文件夹' : human(it.s)) + '</div>' +
          '<div class="drv-card-acts">' +
            (it.d ? '' : '<button data-act="preview" title="预览">👁</button>') +
            '<button data-act="download" title="下载">⬇</button>' +
            '<button data-act="rename" title="重命名">✏</button>' +
            '<button data-act="delete" title="删除">🗑</button>' +
          '</div>';
        bindRow(card, it);
        box.appendChild(card);
      });
    } else {
      list.forEach(function (it, idx) {
        var row = document.createElement('div');
        row.className = 'drv-row' + (state.sel[pathOf(it)] ? ' sel' : '');
        row.dataset.p = pathOf(it);
        row.dataset.idx = idx;
        var shareBtn = it.sh
          ? '<button data-act="share" title="已分享：点击复制链接" class="on">🔗</button>'
          : '<button data-act="share" title="' + (it.d ? '分享这个文件夹' : '生成分享链接') + '">🔗</button>';
        row.innerHTML =
          '<span class="drv-check"><input type="checkbox"' + (state.sel[pathOf(it)] ? ' checked' : '') + '/></span>' +
          '<span class="drv-ico">' + icon(it) + '</span>' +
          '<span class="drv-name" title="' + esc(it.n) + '">' + esc(it.n) + '</span>' +
          '<span class="drv-size">' + (it.d ? '—' : human(it.s)) + '</span>' +
          '<span class="drv-time">' + esc(it.t || '') + '</span>' +
          '<span class="drv-acts">' +
            (it.d ? '<button data-act="open" title="打开">打开</button>' : '<button data-act="preview" title="预览">👁</button>') +
            '<button data-act="download" title="' + (it.d ? '打包下载' : '下载') + '">⬇</button>' +
            '<button data-act="rename" title="重命名 (F2)">✏</button>' +
            shareBtn +
            '<button data-act="delete" title="删除 (Del)">🗑</button>' +
          '</span>';
        bindRow(row, it);
        box.appendChild(row);
      });
    }
    updateBulk();
  }

  function updateBulk() {
    var n = selCount();
    $('drv-selinfo').textContent = n ? ('已选 ' + n + ' 项') : '未选择';
    var all = $('drv-all');
    var vis = visibleItems();
    all.checked = vis.length > 0 && vis.every(function (it) { return state.sel[pathOf(it)]; });
  }

  function bindRow(el, it) {
    el.addEventListener('click', function (ev) {
      var act = ev.target.closest ? ev.target.closest('[data-act]') : null;
      if (act) {
        ev.stopPropagation();
        var a = act.getAttribute('data-act');
        if (a === 'open') location.href = folderUrl(pathOf(it));
        else if (a === 'preview') openPreview(it);
        else if (a === 'download') downloadItem(it);
        else if (a === 'rename') doRename(it);
        else if (a === 'delete') doDelete([pathOf(it)], it.n);
        else if (a === 'share') doShare(it);
        return;
      }
      if (ev.target.tagName === 'INPUT') { toggleSel(it, ev.target.checked); return; }
      var nameEl = ev.target.closest ? ev.target.closest('.drv-name, .drv-card-name') : null;
      if (nameEl) {
        if (ev.ctrlKey || ev.metaKey || ev.shiftKey) { toggleSel(it, undefined, true); return; }
        open(it);
        return;
      }
      if (ev.ctrlKey || ev.metaKey) toggleSel(it);
      else if (ev.shiftKey) toggleSel(it, true, true);
    });
    el.addEventListener('dblclick', function (ev) {
      if (ev.target.closest && ev.target.closest('[data-act],.drv-check')) return;
      open(it);
    });
    el.addEventListener('contextmenu', function (ev) {
      ev.preventDefault();
      if (!state.sel[pathOf(it)]) { state.sel = {}; state.sel[pathOf(it)] = true; render(); }
      showContextMenu(ev.clientX, ev.clientY, it);
    });
  }

  function open(it) {
    if (it.d) location.href = folderUrl(pathOf(it));
    else if (isPreviewable(it)) openPreview(it);
    else downloadItem(it);
  }
  function toggleSel(it, force, exclusive) {
    var p = pathOf(it);
    if (exclusive && (force === true)) {
      // Shift 点：范围选择
      var vis = visibleItems();
      var idx = vis.findIndex(function (x) { return pathOf(x) === p; });
      if (state.lastIdx >= 0 && idx >= 0) {
        var a = Math.min(state.lastIdx, idx), b = Math.max(state.lastIdx, idx);
        for (var i = a; i <= b; i++) state.sel[pathOf(vis[i])] = true;
        render();
        return;
      }
    }
    var on = force === undefined ? !state.sel[p] : force;
    if (on) state.sel[p] = true; else delete state.sel[p];
    var vis2 = visibleItems();
    state.lastIdx = vis2.findIndex(function (x) { return pathOf(x) === p; });
    render();
  }

  // ---------- 右键菜单 ----------
  function closeMenu() { var m = $('drv-menu'); if (m) m.remove(); }
  function menuItem(label, fn, cls, hint) {
    var b = document.createElement('button');
    b.className = 'drv-mi ' + (cls || '');
    b.innerHTML = '<span>' + label + '</span>' + (hint ? '<i>' + hint + '</i>' : '');
    b.onclick = function () { closeMenu(); fn(); };
    return b;
  }
  function showContextMenu(x, y, it) {
    closeMenu();
    var m = document.createElement('div');
    m.id = 'drv-menu'; m.className = 'drv-menu';
    var n = selCount();
    if (it && !it.__blank) {
      m.appendChild(menuItem(it.d ? '打开' : '预览', function () { open(it); }, '', 'Enter'));
    if (!it.d && it.k === 'text') m.appendChild(menuItem('在 IDE 中打开', function () { location.href = '/run?file=' + encodeURIComponent(pathOf(it)); }));
      m.appendChild(menuItem('下载' + (it.d ? '（打包）' : ''), function () { downloadItem(it); }));
      m.appendChild(menuItem('复制', function () { doClipboard('copy', selPaths()); }, '', 'Ctrl+C'));
      m.appendChild(menuItem('剪切', function () { doClipboard('cut', selPaths()); }, '', 'Ctrl+X'));
      if (n === 1) m.appendChild(menuItem('重命名', function () { doRename(it); }, '', 'F2'));
      m.appendChild(menuItem('分享' + (it.d ? '文件夹' : ''), function () { doShare(it); }));
      m.appendChild(menuItem('属性', function () { doProps(it); }));
      m.appendChild(menuItem('删除', function () { doDelete(selPaths().length ? selPaths() : [pathOf(it)], it.n); }, 'danger', 'Del'));
    } else {
      m.appendChild(menuItem('新建文件夹', doMkdir));
      m.appendChild(menuItem('新建文本文件', doNewFile));
      m.appendChild(menuItem('上传文件', function () { $('drv-file').click(); }));
      m.appendChild(menuItem('上传文件夹', function () { $('drv-dir').click(); }));
      m.appendChild(menuItem('粘贴', doPaste, '', 'Ctrl+V'));
      m.appendChild(menuItem('全选', selectAll, '', 'Ctrl+A'));
      m.appendChild(menuItem('刷新', function () { location.reload(); }, '', 'F5'));
      m.appendChild(menuItem('向上', upOneLevel, '', 'Backspace'));
    }
    m.style.left = Math.min(x, window.innerWidth - 190) + 'px';
    m.style.top = Math.min(y, window.innerHeight - 40 - m.children.length * 34) + 'px';
    document.body.appendChild(m);
  }

  // ---------- 剪贴板 ----------
  function doClipboard(mode, paths) {
    if (!paths.length) { toast('请先选择'); return; }
    setClip({ mode: mode, paths: paths.slice() });
    toast((mode === 'copy' ? '已复制 ' : '已剪切 ') + paths.length + ' 项到剪贴板', 'ok');
  }
  function doPaste() {
    if (!clip) { toast('剪贴板是空的'); return; }
    if (clip.mode === 'copy') {
      postJSON('/drive/copy', { dir: '', paths: clip.paths.join('\n'), to: state.dir }).then(function (r) {
        toast(r.ok ? ('已粘贴 ' + r.copied + ' 项') : ('粘贴失败：' + (r.msg || '')), r.ok ? 'ok' : 'err');
        if (r.ok) setTimeout(function () { location.reload(); }, 400);
      });
    } else {
      var seq = Promise.resolve(), moved = 0;
      clip.paths.forEach(function (p) {
        seq = seq.then(function () {
          return postJSON('/drive/move', { from: p, to: state.dir }).then(function (r) { if (r.ok) moved++; });
        });
      });
      seq.then(function () {
        toast('已移动 ' + moved + ' 项', 'ok');
        setClip(null);
        setTimeout(function () { location.reload(); }, 400);
      });
    }
  }

  // ---------- 操作 ----------
  function doRename(it) {
    var nn = prompt('重命名（含 / 表示移动到子路径）：', it.n);
    if (nn === null) return;
    nn = nn.trim();
    if (!nn || nn === it.n) return;
    if (nn.indexOf('/') >= 0) {
      var parts = nn.split('/');
      var base = parts.pop();
      var to = parts.join('/');
      postJSON('/drive/move', { from: pathOf(it), to: to }).then(function (res) {
        if (!res.ok) { toast(res.msg || '移动失败', 'err'); return; }
        postForm('/drive/rename', { dir: to, file: it.n, newname: base }).then(function () {
          toast('已重命名', 'ok'); location.reload();
        });
      });
      return;
    }
    postForm('/drive/rename', { dir: state.dir, file: it.n, newname: nn }).then(function () {
      toast('已重命名为 ' + esc(nn), 'ok'); setTimeout(function () { location.reload(); }, 300);
    });
  }
  function doDelete(paths, label) {
    var msg = paths.length > 1
      ? '确定删除选中的 ' + paths.length + ' 项吗？文件夹会连同内容一起删除，无法恢复。'
      : '确定删除「' + label + '」吗？无法恢复。';
    if (!confirm(msg)) return;
    postForm('/drive/delete', { dir: state.dir, paths: paths.join('\n') }).then(function () {
      toast('已删除', 'ok');
      state.sel = {};
      setTimeout(function () { location.reload(); }, 300);
    });
  }
  function doShare(it) {
    var token = it.sh || '';
    if (!it.d && token) {
      var link = location.origin + '/share/' + token;
      copyText(link).then(function () { toast('分享链接已复制：' + esc(link), 'ok', 3400); });
      return;
    }
    // 文件夹：弹窗选择协作权限（可编辑 / 只读）
    var old = $('drv-share'); if (old) old.remove();
    var w = document.createElement('div');
    w.id = 'drv-share'; w.className = 'drv-pv';
    w.innerHTML = '<div class="drv-pv-mask"></div>' +
      '<div class="drv-pv-box" style="max-width:540px">' +
      '<div class="drv-pv-head"><span class="drv-pv-title">🔗 ' + (token ? '分享设置' : '分享') + '</span>' +
      '<span class="drv-pv-tools"><button class="drv-pv-btn" data-x="1">✕</button></span></div>' +
      '<div style="padding:16px 18px 6px;font-size:13.5px;line-height:1.7;">' +
      (token ? '<div style="margin-bottom:10px;">链接：<a href="/share/' + esc(token) + '" target="_blank">/share/' + esc(token) + '</a></div>' : '') +
      (it.d
        ? '<label style="display:flex;gap:8px;align-items:flex-start;margin:10px 0;cursor:pointer;"><input type="checkbox" id="shEdit" style="margin-top:4px;"/><span><b>允许协作者编辑</b><br/><span style="color:#8792a0;font-size:12.5px;">登录用户可锁定文件后在线修改（同时只允许一人编辑，避免互相覆盖）</span></span></label>'
        : '<div style="color:#8792a0;font-size:12.5px;">文件分享是只读的，适合外链预览 / Markdown 引用。</div>') +
      '</div>' +
      '<div style="display:flex;gap:8px;justify-content:flex-end;padding:8px 18px 16px;">' +
      '<button class="btn btn-sm btn-outline" id="shCancel">取消</button>' +
      '<button class="btn btn-sm" id="shGo">' + (token ? '保存设置' : '生成链接并复制') + '</button></div></div>';
    document.body.appendChild(w);
    w.querySelector('.drv-pv-mask').onclick = function () { w.remove(); };
    w.querySelector('[data-x]').onclick = function () { w.remove(); };
    w.querySelector('#shCancel').onclick = function () { w.remove(); };
    if (token) {
      getJSON('/drive/props?p=' + encodeURIComponent(pathOf(it))).then(function (p) {
        var c = document.getElementById('shEdit');
        if (c && p && p.perm === 'edit') c.checked = true;
      });
    }
    w.querySelector('#shGo').onclick = function () {
      var btn = this; btn.disabled = true;
      var chk = document.getElementById('shEdit');
      var fd = new URLSearchParams({
        dir: state.dir, action: 'share', file: pathOf(it),
        allowEdit: (it.d && chk && chk.checked) ? '1' : '0'
      });
      fetch('/drive/share', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8', 'X-Requested-With': 'fetch' },
        body: fd
      }).then(function (r) { return r.json(); }).then(function (j) {
        btn.disabled = false;
        if (!j.ok) { toast(j.error || '分享失败', 'err'); return; }
        var link = location.origin + '/share/' + j.token;
        it.sh = j.token;
        copyText(link).then(function () { toast('已分享并复制链接：' + esc(link), 'ok', 3800); });
        w.remove();
        render();
      }).catch(function () { btn.disabled = false; toast('分享失败', 'err'); });
    };
  }
  function copyText(t) {
    if (navigator.clipboard) return navigator.clipboard.writeText(t).catch(function () {});
    var ta = document.createElement('textarea');
    ta.value = t; document.body.appendChild(ta); ta.select();
    try { document.execCommand('copy'); } catch (e) {}
    ta.remove();
    return Promise.resolve();
  }

  function doMkdir() {
    var name = prompt('新建文件夹名称：');
    if (!name) return;
    postForm('/drive/mkdir', { dir: state.dir, name: name }).then(function () { location.reload(); });
  }
  function doNewFile() {
    var name = prompt('新建文件名称（含扩展名，如 notes.md）：', '新建文件.txt');
    if (!name) return;
    postJSON('/drive/newfile', { dir: state.dir, name: name, content: '' }).then(function (r) {
      if (!r.ok) { toast(r.msg || '创建失败', 'err'); return; }
      toast('已创建 ' + esc(name), 'ok');
      setTimeout(function () { location.reload(); }, 300);
    });
  }
  function doProps(it) {
    getJSON('/drive/props?p=' + encodeURIComponent(pathOf(it))).then(function (p) {
      if (!p.ok) { toast(p.msg || '读取失败', 'err'); return; }
      var old = $('drv-props'); if (old) old.remove();
      var w = document.createElement('div');
      w.id = 'drv-props'; w.className = 'drv-pv';
      w.innerHTML = '<div class="drv-pv-mask"></div><div class="drv-pv-box drv-props-box">' +
        '<div class="drv-pv-head"><span class="drv-pv-title">' + (p.isDir ? '📁' : '📄') + ' 属性</span>' +
        '<span class="drv-pv-tools"><button class="drv-pv-btn" data-x="1">✕</button></span></div>' +
        '<div class="drv-props-body">' +
        row('名称', esc(p.name)) + row('路径', esc(p.path)) +
        row('类型', p.isDir ? '文件夹' : (p.ext ? p.ext.toUpperCase() + ' 文件' : '文件')) +
        row('大小', human(p.size) + (p.isDir ? '（含 ' + p.files + ' 个文件 / ' + p.dirs + ' 个子文件夹）' : ' (' + p.size + ' 字节)')) +
        row('创建时间', esc(p.created || '—')) + row('修改时间', esc(p.modified || '—')) +
        row('分享状态', p.share ? ('已分享 · /share/' + esc(p.share)) : '未分享') +
        '</div></div>';
      document.body.appendChild(w);
      w.querySelector('.drv-pv-mask').onclick = function () { w.remove(); };
      w.querySelector('[data-x]').onclick = function () { w.remove(); };
      function row(k, v) { return '<div class="drv-prop-row"><span>' + k + '</span><div>' + v + '</div></div>'; }
    });
  }
  function selectAll() {
    visibleItems().forEach(function (it) { state.sel[pathOf(it)] = true; });
    render();
  }
  function upOneLevel() { location.href = folderUrl(parentOf(state.dir)); }

  // ---------- 上传 ----------
  function uploadFiles(files, relPaths) {
    if (!files || !files.length) return;
    var fd = new FormData();
    fd.append('dir', state.dir);
    for (var i = 0; i < files.length; i++) fd.append('files', files[i], relPaths ? relPaths[i] : files[i].name);
    var bar = document.createElement('div');
    bar.className = 'drv-progress';
    bar.innerHTML = '<div class="drv-progress-txt">上传中 0%</div><div class="drv-progress-bar"><i></i></div>';
    document.body.appendChild(bar);
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/drive/upload');
    xhr.upload.onprogress = function (e) {
      if (!e.lengthComputable) return;
      var pct = Math.round(e.loaded / e.total * 100);
      bar.querySelector('.drv-progress-txt').textContent = '上传中 ' + pct + '%（' + files.length + ' 个文件）';
      bar.querySelector('.drv-progress-bar i').style.width = pct + '%';
    };
    xhr.onload = function () { bar.remove(); location.reload(); };
    xhr.onerror = function () { bar.remove(); toast('上传失败，请重试', 'err'); };
    xhr.send(fd);
  }

  // ---------- 预览 ----------
  var previewList = [], previewIdx = 0;
  function openPreview(it) {
    previewList = state.items.filter(isPreviewable);
    previewIdx = Math.max(0, previewList.findIndex(function (x) { return pathOf(x) === pathOf(it); }));
    showPreview();
  }
  function mdToHtml(md) {
    var out = esc(md);
    out = out.replace(/```([\s\S]*?)```/g, function (_, code) { return '<pre class="md-code">' + code.replace(/^\w*\n/, '') + '</pre>'; });
    out = out.replace(/^###### (.*)$/gm, '<h6>$1</h6>').replace(/^##### (.*)$/gm, '<h5>$1</h5>')
      .replace(/^#### (.*)$/gm, '<h4>$1</h4>').replace(/^### (.*)$/gm, '<h3>$1</h3>')
      .replace(/^## (.*)$/gm, '<h2>$1</h2>').replace(/^# (.*)$/gm, '<h1>$1</h1>');
    out = out.replace(/^&gt; (.*)$/gm, '<blockquote>$1</blockquote>');
    out = out.replace(/^\s*[-*] (.*)$/gm, '<li>$1</li>').replace(/(<li>[\s\S]*?<\/li>)(?!\s*<li>)/g, '<ul>$1</ul>');
    out = out.replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>').replace(/`([^`]+)`/g, '<code>$1</code>');
    out = out.replace(/\[([^\]]+)\]\((https?:\/\/[^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');
    out = out.replace(/^---$/gm, '<hr/>');
    out = out.replace(/\n{2,}/g, '</p><p>').replace(/\n/g, '<br/>');
    return '<div class="md-body"><p>' + out + '</p></div>';
  }
  function csvToHtml(text) {
    var lines = text.split(/\r?\n/).filter(function (l) { return l.length; }).slice(0, 500);
    if (!lines.length) return '<div class="drv-pv-text">空文件</div>';
    var delim = (lines[0].split('\t').length > lines[0].split(',').length) ? '\t' : ',';
    var html = '<div class="drv-csv-wrap"><table class="drv-csv">';
    lines.forEach(function (l, i) {
      var cells = l.split(delim);
      html += '<tr>' + cells.map(function (c) {
        return (i === 0 ? '<th>' : '<td>') + esc(c.replace(/^"|"$/g, '')) + (i === 0 ? '</th>' : '</td>');
      }).join('') + '</tr>';
    });
    return html + '</table></div>';
  }
  function showPreview() {
    var it = previewList[previewIdx];
    if (!it) return;
    var old = $('drv-pv'); if (old) old.remove();
    var wrap = document.createElement('div');
    wrap.id = 'drv-pv';
    wrap.className = 'drv-pv';
    var url = rawUrl(pathOf(it));
    var ext = (it.n.split('.').pop() || '').toLowerCase();
    var body = '';
    if (it.k === 'image') body = '<img src="' + url + '" alt="' + esc(it.n) + '"/>';
    else if (it.k === 'video') body = '<video src="' + url + '" controls autoplay playsinline></video>';
    else if (it.k === 'audio') body = '<div class="drv-pv-audio">🎵<audio src="' + url + '" controls autoplay></audio></div>';
    else if (it.k === 'pdf') body = '<iframe src="' + url + '" title="pdf"></iframe>';
    else if (it.k === 'text') body = '<div class="drv-pv-text">加载中…</div>';
    wrap.innerHTML =
      '<div class="drv-pv-mask"></div>' +
      '<div class="drv-pv-box">' +
        '<div class="drv-pv-head">' +
          '<span class="drv-pv-title">' + icon(it) + ' ' + esc(it.n) + '</span>' +
          '<span class="drv-pv-meta">' + human(it.s) + ' · ' + esc(it.t || '') + '</span>' +
          '<span class="drv-pv-tools">' +
            '<button class="drv-pv-btn" data-pv="prev" title="上一个 (←)">‹</button>' +
            '<button class="drv-pv-btn" data-pv="next" title="下一个 (→)">›</button>' +
            '<button class="drv-pv-btn" data-pv="raw" title="新窗口打开">↗</button>' +
            '<button class="drv-pv-btn" data-pv="dl" title="下载">⬇</button>' +
            '<button class="drv-pv-btn" data-pv="close" title="关闭 (Esc)">✕</button>' +
          '</span>' +
        '</div>' +
        '<div class="drv-pv-body">' + body + '</div>' +
      '</div>';
    document.body.appendChild(wrap);
    wrap.querySelector('.drv-pv-mask').onclick = closePreview;
    wrap.querySelectorAll('[data-pv]').forEach(function (b) {
      b.onclick = function () {
        var a = b.getAttribute('data-pv');
        if (a === 'close') return closePreview();
        if (a === 'dl') return downloadItem(it);
        if (a === 'raw') return window.open(url, '_blank');
        if (a === 'prev') { previewIdx = (previewIdx - 1 + previewList.length) % previewList.length; showPreview(); }
        if (a === 'next') { previewIdx = (previewIdx + 1) % previewList.length; showPreview(); }
      };
    });
    if (it.k === 'text') {
      fetch(url, { credentials: 'same-origin', headers: { Range: 'bytes=0-1048575' } })
        .then(function (r) { return r.text(); })
        .then(function (t) {
          var holder = wrap.querySelector('.drv-pv-text');
          if (ext === 'md' || ext === 'markdown') holder.innerHTML = mdToHtml(t);
          else if (ext === 'csv' || ext === 'tsv') holder.innerHTML = csvToHtml(t);
          else if (ext === 'json') {
            var pretty = t;
            try { pretty = JSON.stringify(JSON.parse(t), null, 2); } catch (e) {}
            holder.innerHTML = '<pre class="drv-pv-pre">' + esc(pretty) + '</pre>';
          } else {
            holder.innerHTML = '<pre class="drv-pv-pre">' + esc(t) + '</pre>';
          }
          if (it.s > 1048576) holder.innerHTML += '<div class="drv-pv-more">…文件较大，仅预览前 1MB，完整内容请下载</div>';
        })
        .catch(function () { wrap.querySelector('.drv-pv-text').textContent = '加载失败'; });
    }
  }
  function closePreview() { var w = $('drv-pv'); if (w) w.remove(); }

  // ---------- 移动 / 复制到 ----------
  function openMove(paths, mode) {
    if (!paths.length) { toast('没有选中任何内容'); return; }
    mode = mode || 'move';
    getJSON('/drive/tree.json').then(function (data) {
      var old = $('drv-mv'); if (old) old.remove();
      var wrap = document.createElement('div');
      wrap.id = 'drv-mv'; wrap.className = 'drv-pv';
      var listHtml = '<div class="drv-mv-item' + (state.dir === '' ? ' cur' : '') + '" data-to="">🏠 云盘根目录</div>';
      (data.folders || []).forEach(function (f) {
        var pad = 16 + f.depth * 18;
        listHtml += '<div class="drv-mv-item' + (f.p === state.dir ? ' cur' : '') + '" data-to="' + esc(f.p) + '" style="padding-left:' + pad + 'px">📁 ' + esc(f.n) + '</div>';
      });
      wrap.innerHTML =
        '<div class="drv-pv-mask"></div>' +
        '<div class="drv-pv-box drv-mv-box">' +
          '<div class="drv-pv-head"><span class="drv-pv-title">' + (mode === 'move' ? '移动 ' : '复制 ') + paths.length + ' 项到…</span>' +
          '<span class="drv-pv-tools"><button class="drv-pv-btn" data-mv="close">✕</button></span></div>' +
          '<div class="drv-mv-list">' + listHtml + '</div>' +
          '<div class="drv-mv-foot">点击目标文件夹完成操作（同名自动加序号）</div>' +
        '</div>';
      document.body.appendChild(wrap);
      wrap.querySelector('.drv-pv-mask').onclick = function () { wrap.remove(); };
      wrap.querySelector('[data-mv="close"]').onclick = function () { wrap.remove(); };
      wrap.querySelectorAll('.drv-mv-item').forEach(function (el) {
        el.onclick = function () {
          var to = el.getAttribute('data-to');
          el.classList.add('busy');
          if (mode === 'copy') {
            postJSON('/drive/copy', { dir: '', paths: paths.join('\n'), to: to }).then(function (r) {
              wrap.remove();
              toast(r.ok ? ('已复制 ' + r.copied + ' 项') : ('失败：' + (r.msg || '')), r.ok ? 'ok' : 'err');
              if (r.ok) setTimeout(function () { location.reload(); }, 400);
            });
            return;
          }
          var seq = Promise.resolve(), moved = 0, failed = 0;
          paths.forEach(function (p) {
            seq = seq.then(function () {
              return postJSON('/drive/move', { from: p, to: to }).then(function (r) { if (r.ok) moved++; else failed++; });
            });
          });
          seq.then(function () {
            wrap.remove();
            toast('已移动 ' + moved + ' 项' + (failed ? '，失败 ' + failed + ' 项' : ''), failed ? 'warn' : 'ok');
            state.sel = {};
            setTimeout(function () { location.reload(); }, 500);
          });
        };
      });
    }).catch(function () { toast('读取文件夹失败', 'err'); });
  }

  // ---------- 全盘搜索 ----------
  function globalSearch() {
    var q = ($('drv-q').value || '').trim();
    if (!q) { render(); return; }
    getJSON('/drive/search.json?q=' + encodeURIComponent(q)).then(function (data) {
      var items = data.items || [];
      if (!items.length) { toast('没有找到匹配项'); return; }
      var box = $('drv-list');
      box.className = 'drv-list list';
      box.innerHTML = '<div class="drv-sr-head">全盘搜索「' + esc(q) + '」· ' + items.length + ' 个结果 ' +
        '<button class="btn btn-sm btn-outline" id="drv-sr-clear">返回目录</button></div>';
      items.forEach(function (it) {
        var row = document.createElement('div');
        row.className = 'drv-row';
        var dirOf = parentOf(it.p);
        row.innerHTML = '<span class="drv-check"></span><span class="drv-ico">' + icon(it) + '</span>' +
          '<span class="drv-name">' + esc(it.n) + '</span>' +
          '<span class="drv-size">' + (it.d ? '—' : human(it.s)) + '</span>' +
          '<span class="drv-parent">📂 ' + esc(dirOf || '根目录') + '</span>' +
          '<span class="drv-acts"><button data-act="open" title="打开 / 定位">打开</button>' +
          '<button data-act="download" title="下载">⬇</button></span>';
        row.querySelector('.drv-name').onclick = function () {
          if (it.d) location.href = folderUrl(it.p);
          else openPreview(it);
        };
        row.querySelector('[data-act="open"]').onclick = function () {
          if (it.d) location.href = folderUrl(it.p);
          else location.href = folderUrl(dirOf);
        };
        row.querySelector('[data-act="download"]').onclick = function () {
          if (it.d) downloadZip([it.p], it.n + '.zip');
          else saveBlobLazy(function () { return fetchBlob(rawUrl(it.p) + '?download=1'); }, it.n);
        };
        box.appendChild(row);
      });
      $('drv-sr-clear').onclick = function () { render(); };
    });
  }

  // ---------- 初始化 ----------
  function init() {
    if (!$('drv-list')) return;
    render();

    $('drv-up-file').onclick = function () { $('drv-file').click(); };
    $('drv-up-dir').onclick = function () { $('drv-dir').click(); };
    $('drv-mkdir').onclick = doMkdir;
    $('drv-refresh').onclick = function () { location.reload(); };
    $('drv-file').onchange = function (e) {
      var fs = Array.prototype.slice.call(e.target.files);
      uploadFiles(fs, fs.map(function (f) { return f.name; }));
      e.target.value = '';
    };
    $('drv-dir').onchange = function (e) {
      var fs = Array.prototype.slice.call(e.target.files);
      var rels = fs.map(function (f) { return f.webkitRelativePath || f.name; });
      uploadFiles(fs, rels);
      e.target.value = '';
    };
    $('drv-sort').value = state.sort;
    $('drv-sort').onchange = function () {
      state.sort = this.value; localStorage.setItem('drv.sort', state.sort); render();
    };
    $('drv-sort-dir').onclick = function () {
      state.asc = !state.asc; localStorage.setItem('drv.asc', state.asc ? '1' : '0'); render();
    };
    function syncViewBtn() { $('drv-view').textContent = state.view === 'grid' ? '▦' : '☰'; }
    syncViewBtn();
    $('drv-view').onclick = function () {
      state.view = state.view === 'grid' ? 'list' : 'grid';
      localStorage.setItem('drv.view', state.view);
      syncViewBtn(); render();
    };
    $('drv-q').oninput = function () { state.q = this.value.trim(); render(); };
    $('drv-q').onkeydown = function (e) {
      if (e.key === 'Enter') globalSearch();
      if (e.key === 'Escape') { this.value = ''; state.q = ''; render(); }
    };
    $('drv-q-all').onclick = globalSearch;

    $('drv-all').onchange = function () {
      var on = this.checked;
      state.sel = {};
      if (on) visibleItems().forEach(function (it) { state.sel[pathOf(it)] = true; });
      render();
    };
    $('drv-dl-sel').onclick = function () {
      var paths = selPaths();
      if (!paths.length) { toast('请先选择文件或文件夹'); return; }
      var name = (paths.length === 1 ? baseOf(paths[0]) : (state.dir ? baseOf(state.dir) : '云盘')) + '.zip';
      downloadZip(paths, name);
    };
    $('drv-mv-sel').onclick = function () { openMove(selPaths(), 'move'); };
    $('drv-cp-sel').onclick = function () { openMove(selPaths(), 'copy'); };
    $('drv-rm-sel').onclick = function () {
      var paths = selPaths();
      if (!paths.length) { toast('请先选择文件或文件夹'); return; }
      doDelete(paths, '');
    };

    // 下载位置开关 + 剪贴板提示
    var bar = document.querySelector('.drv-bar');
    if (bar) {
      if (window.showSaveFilePicker) {
        var lbl = document.createElement('label');
        lbl.className = 'drv-picker';
        lbl.innerHTML = '<input type="checkbox"' + (pickerOn ? ' checked' : '') + '/> 选择保存位置';
        var cb = lbl.querySelector('input');
        cb.onchange = function () {
          pickerOn = cb.checked;
          localStorage.setItem('drv.picker', pickerOn ? '1' : '0');
          toast(pickerOn ? '下载时会弹出保存位置选择' : '下载将直接保存到浏览器默认目录');
        };
        bar.appendChild(lbl);
      }
      if (clip) {
        var chip = document.createElement('span');
        chip.className = 'drv-clip';
        chip.textContent = '剪贴板：' + clip.paths.length + ' 项';
        chip.onclick = doPaste;
        bar.appendChild(chip);
      }
    }

    // 空白处右键
    $('drv-list').addEventListener('contextmenu', function (ev) {
      if (ev.target.closest && ev.target.closest('.drv-row, .drv-card')) return;
      ev.preventDefault();
      state.sel = {};
      render();
      showContextMenu(ev.clientX, ev.clientY, { __blank: true });
    });
    document.addEventListener('click', function (ev) {
      if (!ev.target.closest || !ev.target.closest('#drv-menu')) closeMenu();
    });

    // 拖拽上传
    var drop = $('drv-drop');
    var dragDepth = 0;
    document.addEventListener('dragenter', function (e) {
      if (!e.dataTransfer || Array.prototype.indexOf.call(e.dataTransfer.types, 'Files') < 0) return;
      dragDepth++; drop.classList.add('on');
    });
    document.addEventListener('dragover', function (e) { e.preventDefault(); });
    document.addEventListener('dragleave', function () {
      dragDepth--; if (dragDepth <= 0) { dragDepth = 0; drop.classList.remove('on'); }
    });
    document.addEventListener('drop', function (e) {
      e.preventDefault();
      dragDepth = 0; drop.classList.remove('on');
      var fs = e.dataTransfer && e.dataTransfer.files;
      if (!fs || !fs.length) return;
      uploadFiles(Array.prototype.slice.call(fs), null);
    });

    // 键盘
    document.addEventListener('keydown', function (e) {
      var t = e.target;
      var typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA');
      if (e.key === 'Escape') {
        closePreview();
        var mv = $('drv-mv'); if (mv) mv.remove();
        var pr = $('drv-props'); if (pr) pr.remove();
        closeMenu();
        if (typing && t.id === 'drv-q') { t.value = ''; state.q = ''; render(); }
        return;
      }
      if (typing) return;
      var n = selCount();
      if (e.ctrlKey || e.metaKey) {
        if (e.key === 'c') { e.preventDefault(); doClipboard('copy', selPaths()); }
        else if (e.key === 'x') { e.preventDefault(); doClipboard('cut', selPaths()); }
        else if (e.key === 'v') { e.preventDefault(); doPaste(); }
        else if (e.key === 'a') { e.preventDefault(); selectAll(); }
        else if (e.key === 'f') { e.preventDefault(); $('drv-q').focus(); }
        return;
      }
      if (e.key === 'Delete' && n) { e.preventDefault(); doDelete(selPaths(), ''); }
      else if (e.key === 'F2' && n === 1) { e.preventDefault(); doRename(selItems()[0]); }
      else if (e.key === 'F5') { e.preventDefault(); location.reload(); }
      else if (e.key === 'Backspace') { e.preventDefault(); upOneLevel(); }
      else if (e.key === 'Enter' && n === 1) { e.preventDefault(); var it = selItems()[0]; if (it) open(it); }
      else if ($('drv-pv')) {
        if (e.key === 'ArrowLeft') { previewIdx = (previewIdx - 1 + previewList.length) % previewList.length; showPreview(); }
        if (e.key === 'ArrowRight') { previewIdx = (previewIdx + 1) % previewList.length; showPreview(); }
      }
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
