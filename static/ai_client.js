/* ============================================================
   OI 小站 · AI 前端公共脚本
   - Markdown 渲染：markdown-it（本地 vendor，离线可用）
   - 公式渲染：KaTeX + auto-render（本地字体）
   - 流式对话：统一的 SSE 解析（支持工具/触手事件、可中止）
   ============================================================ */
(function () {
  const V = '/static/vendor/';
  const AI = window.AI = window.AI || {};

  AI.esc = function (s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  };

  /* ---------------- 资源加载 ---------------- */
  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = src;
      s.onload = resolve;
      s.onerror = () => reject(new Error('加载失败: ' + src));
      document.head.appendChild(s);
    });
  }
  function loadCss(href) {
    if (document.querySelector('link[href="' + href + '"]')) return;
    const l = document.createElement('link');
    l.rel = 'stylesheet'; l.href = href;
    document.head.appendChild(l);
  }

  AI.ready = (async function () {
    try {
      if (!window.markdownit) await loadScript(V + 'markdown-it.min.js');
      loadCss(V + 'katex/katex.min.css');
      if (!window.katex) await loadScript(V + 'katex/katex.min.js');
      if (!window.renderMathInElement) await loadScript(V + 'katex/auto-render.min.js');
      AI.libOk = !!(window.markdownit && window.katex);
    } catch (e) {
      AI.libOk = false;
      console.warn('AI 渲染库加载失败，退化为纯文本：', e.message);
    }
    AI._md = null;
    return AI.libOk;
  })();

  function md() {
    if (AI._md) return AI._md;
    if (!window.markdownit) return null;
    const m = window.markdownit({
      html: false,
      linkify: true,
      breaks: true,
      typographer: false,
      highlight: null
    });
    // 代码块：加语言标签 + 复制按钮
    m.renderer.rules.fence = function (tokens, idx) {
      const t = tokens[idx];
      const info = (t.info || '').trim().split(/\s+/)[0] || 'code';
      const code = AI.esc(t.content.replace(/\n$/, ''));
      return '<div class="codeblk"><div class="cbhead"><span>' + AI.esc(info) +
        '</span><button type="button" onclick="AI.copyCode(this)">复制</button></div>' +
        '<pre><code>' + code + '</code></pre></div>';
    };
    // 链接新窗口
    const linkOpen = m.renderer.rules.link_open || function (t, i, o, e, s) { return s.renderToken(t, i, o); };
    m.renderer.rules.link_open = function (t, i, o, e, s) {
      t[i].attrSet('target', '_blank');
      t[i].attrSet('rel', 'noopener');
      return linkOpen(t, i, o, e, s);
    };
    AI._md = m;
    return m;
  }

  /* ---------------- markdown / 公式 ---------------- */
  AI.renderMath = function (el) {
    if (!el) return;
    if (!window.renderMathInElement) {
      if (AI.libOk === false) return;
      AI.ready.then(function () { if (window.renderMathInElement) AI.renderMath(el); });
      return;
    }
    try {
      window.renderMathInElement(el, {
        delimiters: [
          { left: '$$', right: '$$', display: true },
          { left: '\\[', right: '\\]', display: true },
          { left: '$', right: '$', display: false },
          { left: '\\(', right: '\\)', display: false }
        ],
        throwOnError: false,
        ignoredTags: ['script', 'noscript', 'style', 'textarea', 'pre', 'code']
      });
    } catch (e) { }
  };

  AI.toHtml = function (text) {
    const m = md();
    if (!m) return '<pre class="ai-plain">' + AI.esc(text) + '</pre>';
    try { return m.render(text == null ? '' : String(text)); }
    catch (e) { return '<pre class="ai-plain">' + AI.esc(text) + '</pre>'; }
  };

  // 把 markdown 渲染进一个元素（并渲染公式）
  AI.paint = function (el, text) {
    if (!el) return;
    el.__aiText = text;
    el.innerHTML = AI.toHtml(text);
    if (!window.markdownit) {
      // 库还没加载完：加载完成后补渲染一次
      AI.ready.then(function () { if (el.__aiText === text) { el.innerHTML = AI.toHtml(text); AI.renderMath(el); } });
      return;
    }
    AI.renderMath(el);
  };

  // 流式过程中的轻量渲染：纯文本 + 保留换行（避免半截 markdown 闪烁）
  AI.paintStreaming = function (el, text) {
    if (!el) return;
    el.innerHTML = '<div class="ai-stream-text">' + AI.esc(text) + '</div>';
  };

  /* ---------------- 复制 ---------------- */
  AI.copy = function (text, btn, back) {
    const done = function () {
      if (btn) {
        const old = btn.textContent;
        btn.textContent = '✓ 已复制';
        setTimeout(() => { btn.textContent = back || old; }, 1200);
      }
      AI.toast && AI.toast('已复制');
    };
    const fallback = function () {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed'; ta.style.opacity = '0';
      document.body.appendChild(ta); ta.select();
      try { document.execCommand('copy'); done(); } catch (e) { }
      ta.remove();
    };
    if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(done).catch(fallback);
    else fallback();
  };
  AI.copyCode = function (btn) {
    const pre = btn.closest('.codeblk').querySelector('pre');
    AI.copy(pre.textContent, btn, '复制');
  };

  /* ---------------- 流式对话 ---------------- */
  // payload: {task, message, problemId, statement, code, extra, level, tools, model, history}
  // handlers: {onChunk, onThinking, onTool, onToolDone, onMeta, onError, onDone, onStart}
  AI.stream = function (payload, handlers) {
    handlers = handlers || {};
    const ctl = new AbortController();
    let finished = false;
    const finish = function (err) {
      if (finished) return;
      finished = true;
      if (err && handlers.onError) handlers.onError(err.msg || String(err), err.code || '');
      if (handlers.onDone) handlers.onDone();
    };

    (async function () {
      let resp;
      try {
        resp = await fetch('/llm/api/chat', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload),
          signal: ctl.signal
        });
      } catch (e) {
        if (e.name === 'AbortError') { finish(); return; }
        finish({ msg: '网络错误：' + e.message });
        return;
      }
      if (!resp.ok) {
        let j = {};
        try { j = await resp.json(); } catch (e) { }
        finish({ msg: j.error || ('请求失败 HTTP ' + resp.status), code: j.code || '', quota: j.quota });
        return;
      }
      if (handlers.onStart) handlers.onStart();

      const reader = resp.body.getReader();
      const dec = new TextDecoder('utf-8');
      let buf = '';
      try {
        while (true) {
          const { done, value } = await reader.read();
          if (done) break;
          buf += dec.decode(value, { stream: true });
          // 按行切分；\r\n 与 \n 都兼容
          let idx;
          while ((idx = buf.indexOf('\n')) !== -1) {
            let line = buf.slice(0, idx);
            buf = buf.slice(idx + 1);
            if (line.endsWith('\r')) line = line.slice(0, -1);
            if (!line || line.startsWith(':')) continue;   // 注释/心跳
            if (line.slice(0, 5) !== 'data:') continue;
            const data = line.slice(5).trim();
            if (!data || data === '[DONE]') continue;
            let j;
            try { j = JSON.parse(data); } catch (e) { continue; }
            if (j.message) {
              if (j.message.thinking && handlers.onThinking) handlers.onThinking(j.message.thinking);
              if (j.message.content && handlers.onChunk) handlers.onChunk(j.message.content);
            } else if (j.type === 'progress') {
              if (handlers.onProgress) handlers.onProgress(j.text || '');
            } else if (j.type === 'tool') {
              if (handlers.onTool) handlers.onTool({ name: j.name, args: j.args });
            } else if (j.type === 'tool_done') {
              if (handlers.onToolDone) handlers.onToolDone({ name: j.name, ok: j.ok === '1', preview: j.preview });
            } else if (j.type === 'truncated') {
              if (handlers.onTruncated) handlers.onTruncated(j.text || '');
            } else if (j.type === 'meta') {
              if (handlers.onMeta) handlers.onMeta(j);
            } else if (j.error) {
              finish({ msg: j.error, code: j.code || '' });
              return;
            }
          }
        }
      } catch (e) {
        if (e.name !== 'AbortError') finish({ msg: '读取流失败：' + e.message });
        finish();
        return;
      }
      finish();
    })();

    return {
      abort: function () { try { ctl.abort(); } catch (e) { } },
      isRunning: function () { return !finished; }
    };
  };

  /* ---------------- 工具卡片 ---------------- */
  AI.toolsHtml = function (tools) {
    if (!tools || !tools.length) return '';
    return '<div class="ai-tools">' + tools.map(function (t) {
      const cls = t.ok === undefined ? 'run' : (t.ok ? 'ok' : 'fail');
      const icon = t.ok === undefined ? '⏳' : (t.ok ? '✅' : '⚠️');
      const args = t.args ? '<span class="ai-arg">' + AI.esc(t.args) + '</span>' : '';
      const prev = t.preview ? '<pre class="ai-prev">' + AI.esc(t.preview) + '</pre>' : '';
      return '<div class="ai-tool ' + cls + '"><div class="ai-th"><b>' + icon + ' ' + AI.esc(t.name) + '</b>' + args + '</div>' + prev + '</div>';
    }).join('') + '</div>';
  };

  AI.toast = AI.toast || function (msg) {
    let t = document.getElementById('ai-toast');
    if (!t) {
      t = document.createElement('div');
      t.id = 'ai-toast';
      t.style.cssText = 'position:fixed;left:50%;bottom:26px;transform:translateX(-50%);background:#1f2937;color:#fff;' +
        'padding:9px 16px;border-radius:99px;font-size:13px;z-index:9999;opacity:0;transition:.25s;pointer-events:none';
      document.body.appendChild(t);
    }
    t.textContent = msg;
    t.style.opacity = '1';
    clearTimeout(t.__tm);
    t.__tm = setTimeout(function () { t.style.opacity = '0'; }, 2000);
  };
})();
