/* ============================================================
   站内 @ 提及补全
   - 适用于所有 textarea/input[name=content] 以及私信输入框 #chatText
   - 输入 @ 后弹出用户列表，↑↓ 选择，Enter/Tab 确认，Esc 关闭
   ============================================================ */
(function () {
  var SEL = 'textarea[name=content],input[name=content],#chatText';
  var box = null, list = [], idx = 0, target = null, atPos = -1, timer = null;

  function ensureBox() {
    if (box) return box;
    box = document.createElement('div');
    box.className = 'mention-box';
    box.style.display = 'none';
    document.body.appendChild(box);
    box.addEventListener('mousedown', function (e) {
      var el = e.target.closest('[data-name]');
      if (el) { e.preventDefault(); insert(el.getAttribute('data-name')); }
    });
    return box;
  }

  function hide() { if (box) box.style.display = 'none'; list = []; idx = 0; atPos = -1; target = null; }

  function place(el) {
    var r = el.getBoundingClientRect();
    box.style.left = Math.max(8, r.left + 12) + 'px';
    box.style.top = Math.min(window.innerHeight - 180, r.bottom + 4) + 'px';
    box.style.width = Math.max(180, Math.min(260, r.width - 24)) + 'px';
  }

  function render() {
    if (!list.length) { box.style.display = 'none'; return; }
    box.innerHTML = list.map(function (n, i) {
      return '<div class="mb-item' + (i === idx ? ' on' : '') + '" data-name="' + n.replace(/"/g, '&quot;') + '">@' + n + '</div>';
    }).join('');
    box.style.display = 'block';
  }

  function insert(name) {
    var el = target;
    if (!el) return;
    var v = el.value, caret = el.selectionStart;
    var head = v.slice(0, atPos), tail = v.slice(caret);
    var text = '@' + name + ' ';
    hide();
    el.value = head + text + tail;
    var pos = head.length + text.length;
    try { el.selectionStart = el.selectionEnd = pos; } catch (e) { }
    try { el.focus(); } catch (e) { }
    try { el.dispatchEvent(new Event('input', { bubbles: true })); } catch (e) { }
  }

  function findMention(el) {
    var caret = el.selectionStart;
    if (caret == null) return null;
    var before = el.value.slice(0, caret);
    var m = /(^|[\s(（\[【])@([A-Za-z0-9_\u4e00-\u9fa5]{0,20})$/.exec(before);
    if (!m) return null;
    return { q: m[2], pos: caret - m[2].length - 1 };
  }

  async function query(q) {
    try {
      var r = await fetch('/api/users/suggest?q=' + encodeURIComponent(q));
      var j = await r.json();
      if (!Array.isArray(j)) return [];
      return j.slice(0, 8);
    } catch (e) { return []; }
  }

  document.addEventListener('input', function (e) {
    var el = e.target;
    if (!el || !el.matches || !el.matches(SEL)) return;
    var info = findMention(el);
    if (!info) { hide(); return; }
    target = el; atPos = info.pos;
    clearTimeout(timer);
    timer = setTimeout(async function () {
      list = await query(info.q);
      idx = 0;
      ensureBox(); place(el); render();
    }, 120);
  });

  document.addEventListener('keydown', function (e) {
    if (!box || box.style.display === 'none' || !list.length) return;
    if (e.target !== target) return;
    if (e.key === 'ArrowDown') { e.preventDefault(); idx = (idx + 1) % list.length; render(); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); idx = (idx - 1 + list.length) % list.length; render(); }
    else if (e.key === 'Enter' || e.key === 'Tab') { e.preventDefault(); insert(list[idx]); }
    else if (e.key === 'Escape') { e.preventDefault(); hide(); }
  }, true);

  document.addEventListener('click', function (e) {
    if (box && !box.contains(e.target) && e.target !== target) hide();
  });
})();
