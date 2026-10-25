/* OI 小站 · 前端脚本（前后端分离） */
(function () {
  var THEME_KEY = 'theme';

  function applyTheme(theme) {
    var root = document.documentElement;
    if (theme === 'dark') { root.setAttribute('data-theme', 'dark'); }
    else { root.removeAttribute('data-theme'); }
    // 清除旧版内联深色覆盖，交由 CSS 变量接管
    if (document.body) {
      document.body.style.backgroundColor = '';
      document.body.style.color = '';
    }
  }

  function readTheme() {
    try {
      var m = document.cookie.split('; ').filter(function (c) { return c.indexOf(THEME_KEY + '=') === 0; });
      if (m.length) return m[0].split('=')[1];
    } catch (e) {}
    return 'light';
  }

  window.toggleTheme = function () {
    var next = readTheme() === 'dark' ? 'light' : 'dark';
    try { document.cookie = THEME_KEY + '=' + next + '; path=/; max-age=31536000'; } catch (e) {}
    applyTheme(next);
  };

  // 背景切换（预设几款渐变/图案，可自行追加 url 到 BG_KEY）
  var BG_KEY = 'site_bg';
  var BG_PRESETS = [
    { name: '纯色', bg: 'none' },
    { name: '蓝天', bg: 'linear-gradient(160deg,#dfeaf6 0%,#eef4fb 60%,#f7fafd 100%)' },
    { name: '青绿', bg: 'linear-gradient(160deg,#dcefe6 0%,#eef6f1 100%)' },
    { name: '暖阳', bg: 'linear-gradient(160deg,#f3ecdd 0%,#faf6ee 100%)' }
  ];
  function applyBg(bg) {
    document.documentElement.style.setProperty('--body-bg-image', bg || 'none');
  }
  function readBg() { try { var c = document.cookie.split('; ').filter(function(x){return x.indexOf(BG_KEY+'=')===0;}); if(c.length) return decodeURIComponent(c[0].split('=')[1]); } catch(e){} return ''; }
  window.pickBg = function (i) {
    if (i < 0 || i >= BG_PRESETS.length) return;
    var bg = BG_PRESETS[i].bg;
    try { document.cookie = BG_KEY + '=' + encodeURIComponent(bg) + '; path=/; max-age=31536000'; } catch(e) {}
    applyBg(bg);
    var sel = document.getElementById('bgSel');
    if (sel) sel.classList.remove('open');
  };
  window.toggleBgSel = function () { var sel = document.getElementById('bgSel'); if (sel) sel.classList.toggle('open'); };

  // 返回顶部按钮
  function initBackToTop() {
    var btn = document.createElement('button');
    btn.className = 'back-to-top';
    btn.setAttribute('aria-label', '返回顶部');
    btn.textContent = '↑';
    btn.addEventListener('click', function () { window.scrollTo({ top: 0, behavior: 'smooth' }); });
    document.body.appendChild(btn);
    var ticking = false;
    function onScroll() {
      if (ticking) return;
      ticking = true;
      requestAnimationFrame(function () {
        btn.classList.toggle('show', window.scrollY > 360);
        ticking = false;
      });
    }
    window.addEventListener('scroll', onScroll, { passive: true });
  }

  // ---------- 编辑器增强（缩进 / 括号匹配 / 自动补全） ----------
  // 通过事件委托统一增强所有 <textarea>
  var EDITOR_PAIRS = { '(': ')', '[': ']', '{': '}', '"': '"', "'": "'", '`': '`' };

  function isCodeArea(t) {
    return t && t.tagName === 'TEXTAREA';
  }

  // 在指定位置插入文本，并返回新光标位置
  function insertText(ta, text, selStart, selEnd) {
    ta.setRangeText(text, selStart, selEnd, 'end');
  }

  function getLineStart(ta, pos) {
    var s = ta.value;
    var i = pos;
    while (i > 0 && s[i - 1] !== '\n') i--;
    return i;
  }

  function getIndent(line) {
    var m = line.match(/^[ \t]*/);
    return m ? m[0] : '';
  }

  // Tab / Shift+Tab 缩进处理
  function handleTabKey(ta, e) {
    var start = ta.selectionStart, end = ta.selectionEnd;
    var INDENT = '  '; // 两空格缩进
    var s = ta.value;

    if (start === end) {
      // 无选区：插入缩进
      if (e.shiftKey) {
        // Shift+Tab 反缩进：删除当前行行首缩进
        var ls = getLineStart(ta, start);
        var indent = getIndent(s.slice(ls, start));
        if (indent.length > 0) {
          var rm = Math.min(2, indent.length);
          insertText(ta, '', ls, ls + rm);
          ta.setSelectionRange(ls, ls);
        }
      } else {
        insertText(ta, INDENT, start, start);
      }
      return true;
    }

    // 多行选区：整行缩进/反缩进
    e.preventDefault();
    var firstLine = getLineStart(ta, start);
    // 如果选区结尾在某行行首且非空选区，则回退到上一行
    var lastLineStart = getLineStart(ta, end);
    var block = s.slice(firstLine, lastLineStart + (s[lastLineStart] === '\n' ? 1 : 0));
    var lines = block.split('\n');
    var out = [], newStart = firstLine, newEnd = 0;
    for (var i = 0; i < lines.length; i++) {
      if (e.shiftKey) {
        var ind = getIndent(lines[i]);
        out.push(ind.length >= 2 ? lines[i].slice(2) : (ind.length === 1 ? lines[i].slice(1) : lines[i]));
      } else {
        out.push(INDENT + lines[i]);
      }
    }
    var replaced = out.join('\n');
    insertText(ta, replaced, firstLine, lastLineStart + (s[lastLineStart] === '\n' ? 1 : 0));
    newEnd = firstLine + replaced.length;
    ta.setSelectionRange(newStart, newEnd);
    return true;
  }

  // 括号自动补全
  function handlePairKey(ta, e) {
    var ch = e.key;
    var start = ta.selectionStart, end = ta.selectionEnd;
    var close = EDITOR_PAIRS[ch];
    var s = ta.value;
    var next = s[end] || '';

    // 输入右半：若下一字符已是配对右半，跳过
    if (ch === ')' || ch === ']' || ch === '}' || ch === '"' || ch === "'" || ch === '`') {
      if (next === ch) { e.preventDefault(); ta.setSelectionRange(end + 1, end + 1); return true; }
      return false;
    }

    // 有选区：用配对包裹
    if (start !== end && close) {
      e.preventDefault();
      insertText(ta, ch + s.slice(start, end) + close, start, end);
      ta.setSelectionRange(start + 1, end + 1);
      return true;
    }

    // 无选区且为括号：插入左右并光标居中（引号除外，避免误触发）
    if (close && ch !== '"' && ch !== "'" && ch !== '`') {
      e.preventDefault();
      insertText(ta, ch + close, start, start);
      ta.setSelectionRange(start + 1, start + 1);
      return true;
    }
    return false;
  }

  // 回车自动缩进跟随
  function handleEnterKey(ta, e) {
    var start = ta.selectionStart;
    var s = ta.value;
    var ls = getLineStart(ta, start);
    var indent = getIndent(s.slice(ls, start));
    if (!indent) return false;
    e.preventDefault();
    insertText(ta, '\n' + indent, start, start);
    ta.setSelectionRange(start + 1 + indent.length, start + 1 + indent.length);
    return true;
  }

  // 括号匹配高亮（用单字符宽度背景块，需等宽字体）
  var matchOpen = '([{', matchClose = ')]}';
  function findMatch(ta, pos, open) {
    var s = ta.value, i, depth = 0;
    if (open) {
      var close = EDITOR_PAIRS[s[pos]];
      for (i = pos + 1; i < s.length; i++) {
        if (s[i] === s[pos]) depth++;
        else if (s[i] === close) { if (depth === 0) return i; depth--; }
      }
    } else {
      var c = s[pos];
      for (i = pos - 1; i >= 0; i--) {
        if (s[i] === c) depth++;
        else if (s[i] === EDITOR_PAIRS[c]) { if (depth === 0) return i; depth--; }
      }
    }
    return -1;
  }

  function hlBand(from, to) {
    return 'linear-gradient(rgba(52,152,219,0.30) 0, rgba(52,152,219,0.30) 100%) ' +
      from + 'ch/' + (to - from) + 'ch no-repeat';
  }
  function updateBracketHighlight(ta) {
    var pos = ta.selectionStart;
    var s = ta.value;
    var chars = '()[]{}';
    if (pos > 0 && chars.indexOf(s[pos - 1]) !== -1) pos = pos - 1;
    if (pos >= s.length || pos < 0 || chars.indexOf(s[pos]) === -1) { removeHighlight(ta); return; }
    var open = matchOpen.indexOf(s[pos]) !== -1;
    var mate = findMatch(ta, pos, open);
    if (mate === -1) { removeHighlight(ta); return; }
    var lo = Math.min(pos, mate), hi = Math.max(pos, mate);
    ta.style.backgroundImage = hlBand(lo, lo + 1) + ', ' + hlBand(hi, hi + 1);
    ta.style.backgroundRepeat = 'no-repeat';
    ta._hl = true;
  }
  function removeHighlight(ta) {
    if (ta._hl) { ta.style.backgroundImage = ''; ta._hl = false; }
  }

  // 绑定编辑器增强（事件委托到 document，对 textarea 生效）
  function enhanceEditor(ta) {
    ta.addEventListener('keydown', function (e) {
      if (!isCodeArea(ta)) return;
      if (e.key === 'Tab') { e.preventDefault(); handleTabKey(ta, e); return; }
      if (e.key === 'Enter') { if (handleEnterKey(ta, e)) return; }
      if (e.key.length === 1 && EDITOR_PAIRS[e.key]) { if (handlePairKey(ta, e)) return; }
    });
    ta.addEventListener('keyup', function () { updateBracketHighlight(ta); });
    ta.addEventListener('click', function () { updateBracketHighlight(ta); });
    ta.addEventListener('blur', function () { removeHighlight(ta); });
  }

  // 初始化
  applyTheme(readTheme());
  applyBg(readBg());
  window.addEventListener('DOMContentLoaded', function () {
    var btn = document.getElementById('themeBtn');
    if (btn) btn.addEventListener('click', function (e) { e.preventDefault(); window.toggleTheme(); });

    // 标记当前激活的导航链接（支持 /problem/xxx 等子路径）
    var p = location.pathname;
    var bestLink = null, bestLen = -1;
    document.querySelectorAll('.side-sub .side-link, .side-menu > .side-link').forEach(function (a) {
      var href = a.getAttribute('href');
      if (!href) return;
      var hit = false;
      if (href === '/') hit = (p === '/');
      else if (p === href || p.indexOf(href + '/') === 0 || p.indexOf(href + '?') === 0) hit = true;
      // 前缀误判修正：/ai 不应吃掉 /ai/debug
      if (hit && href.length > bestLen) { bestLen = href.length; bestLink = a; }
    });
    if (bestLink) {
      bestLink.classList.add('active');
      var grp = bestLink.closest('.side-sub');
      if (grp) {
        grp.classList.add('open');
        var head = document.querySelector('.side-grp[data-grp="' + grp.getAttribute('data-sub') + '"]');
        if (head) head.classList.add('open');
      }
    }

    // 没有精确命中时，按路径前缀猜一个大栏展开
    if (!bestLink) {
      var guessGroup = null;
      var gmap = { learn: ['/problem/', '/submission/', '/status', '/run', '/set/'],
                   comm: ['/post/', '/blog', '/discuss', '/forum'],
                   ai: ['/llm'], me: ['/user/', '/messages', '/shop', '/drive', '/admin', '/tasks'] };
      Object.keys(gmap).forEach(function (k) {
        gmap[k].forEach(function (pre) { if (p.indexOf(pre) === 0) guessGroup = k; });
      });
      if (guessGroup) {
        var sub2 = document.querySelector('.side-sub[data-sub="' + guessGroup + '"]');
        var head2 = document.querySelector('.side-grp[data-grp="' + guessGroup + '"]');
        if (sub2) sub2.classList.add('open');
        if (head2) head2.classList.add('open');
      }
    }

    // 大栏点击展开/收起小栏
    document.querySelectorAll('.side-grp').forEach(function (g) {
      g.addEventListener('click', function (e) {
        e.preventDefault();
        var key = g.getAttribute('data-grp');
        var sub = document.querySelector('.side-sub[data-sub="' + key + '"]');
        if (!sub) return;
        var open = sub.classList.toggle('open');
        g.classList.toggle('open', open);
      });
    });

    initBackToTop();

    // ---------- KaTeX 公式渲染（按需动态加载） ----------
    function unescapeHtml(s) {
      var d = document.createElement('div');
      d.innerHTML = s;
      return d.textContent || d.innerText || '';
    }
    var __mathBusy = false;
    function renderMath() {
      // 只处理还没渲染过的（避免 MutationObserver 死循环）
      var all = document.querySelectorAll('.md-math');
      var els = [];
      for (var i = 0; i < all.length; i++) {
        if (all[i].getAttribute('data-math-done') !== '1') els.push(all[i]);
      }
      if (!els.length) return;
      if (window.katex) {
        if (__mathBusy) return;
        __mathBusy = true;
        els.forEach(function (el) {
          if (el.getAttribute('data-math-done') === '1') return;
          el.setAttribute('data-math-done', '1');
          try { window.katex.render(unescapeHtml(el.getAttribute('data-latex') || ''), el, { throwOnError: false }); }
          catch (e) {}
        });
        __mathBusy = false;
        return;
      }
      // 动态加载 KaTeX（本地 vendor，无外网也能渲染）
      function loadKatex() {
        if (!document.querySelector('link[data-katex]')) {
          var css = document.createElement('link');
          css.rel = 'stylesheet';
          css.setAttribute('data-katex', '1');
          css.href = '/static/vendor/katex/katex.min.css';
          document.head.appendChild(css);
        }
        if (window.__katexLoading) return;
        window.__katexLoading = true;
        var js = document.createElement('script');
        js.src = '/static/vendor/katex/katex.min.js';
        js.onload = function () { renderMath(); };
        document.head.appendChild(js);
      }
      loadKatex();
    }
    renderMath();
    var __mathTimer = null;
    var mathMo = new MutationObserver(function () {
      if (__mathTimer) return;
      __mathTimer = setTimeout(function () { __mathTimer = null; renderMath(); }, 120);
    });
    mathMo.observe(document.body, { childList: true, subtree: true });

    // 增强所有现有 textarea，并监控动态添加的 textarea
    document.querySelectorAll('textarea').forEach(enhanceEditor);
    var mo = new MutationObserver(function (muts) {
      muts.forEach(function (m) {
        m.addedNodes.forEach(function (n) {
          if (n.nodeType === 1) {
            if (n.tagName === 'TEXTAREA') enhanceEditor(n);
            if (n.querySelectorAll) n.querySelectorAll('textarea').forEach(enhanceEditor);
          }
        });
      });
    });
    mo.observe(document.body, { childList: true, subtree: true });

    // 自动隐藏提示条（5 秒后淡出，仅对非错误提示）
    document.querySelectorAll('.alert-success, .alert-info').forEach(function (al) {
      setTimeout(function () {
        al.style.transition = 'opacity .6s';
        al.style.opacity = '0';
        setTimeout(function () { if (al.parentNode) al.parentNode.removeChild(al); }, 700);
      }, 5000);
    });
  });
})();
