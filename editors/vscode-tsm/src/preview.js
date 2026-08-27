// Live typeset preview: webview panel → iframe → loopback server →
// shell.mjs/worker (editor-design.md §5). The extension debounces document
// changes into handle.update() messages; results carry diagnostics and
// phase timings back. Double-click in the preview jumps to source; editor
// scroll reveals the corresponding paragraph in the preview.
const vscode = require('vscode');
const path = require('node:path');
const { startServer } = require('./server');
const { assetRoot } = require('./paths');

// per-document line → byte-offset prefix table (engine spans are UTF-8)
function lineByteStarts(text) {
  const lines = text.split('\n');
  const starts = new Array(lines.length);
  let at = 0;
  for (let i = 0; i < lines.length; i++) {
    starts[i] = at;
    at += Buffer.byteLength(lines[i], 'utf8') + 1;
  }
  return starts;
}

class TsmPreview {
  constructor(context) {
    this.context = context;
    this.panel = null;
    this.server = null;
    this.doc = null;          // previewed TextDocument
    this.version = 0;         // last version sent
    this.byteStarts = null;   // for the sent version
    this.timer = null;
    this.diags = vscode.languages.createDiagnosticCollection('tsm');
    this.status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 90);
    this.ready = false;
  }

  async open(doc) {
    this.doc = doc;
    if (!this.server) {
      this.docRoots = new Map();
      const root = assetRoot(this.context.extensionPath);
      this.server = await startServer({
        assetRoot: root,
        mediaDir: path.join(this.context.extensionPath, 'media'),
        docRoots: this.docRoots,
      });
    }
    // relative image srcs in the document resolve against its own folder
    this.docRoots.set('/__doc/', path.dirname(doc.uri.fsPath) + path.sep);
    this.docRoots.set('/__fallback/', path.dirname(doc.uri.fsPath) + path.sep);
    if (!this.panel) {
      this.panel = vscode.window.createWebviewPanel(
        'tsmPreview', 'TSM Preview', vscode.ViewColumn.Beside,
        { enableScripts: true, retainContextWhenHidden: true });
      this.panel.iconPath = undefined;
      this.panel.onDidDispose(() => {
        this.panel = null;
        this.ready = false;
        this.status.hide();
      });
      this.panel.webview.onDidReceiveMessage((m) => this.onMessage(m));
      const src = `http://127.0.0.1:${this.server.port}/preview.html`;
      this.panel.webview.html = [
        '<!doctype html><html><head><meta charset="utf-8">',
        `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; frame-src http://127.0.0.1:*; script-src 'unsafe-inline'; style-src 'unsafe-inline';">`,
        '<style>html,body{margin:0;padding:0;height:100%;overflow:hidden}iframe{border:0;width:100%;height:100%}</style>',
        `</head><body><iframe id="f" src="${src}"></iframe><script>`,
        'const vscode = acquireVsCodeApi();',
        'const f = document.getElementById("f");',
        'window.addEventListener("message", (e) => {',
        '  if (e.source === f.contentWindow) vscode.postMessage(e.data);',
        '  else f.contentWindow.postMessage(e.data, "*");',
        '});',
        '</script></body></html>',
      ].join('\n');
    } else {
      this.panel.reveal(undefined, true);
    }
    this.panel.title = `TSM: ${path.basename(doc.uri.fsPath)}`;
    if (this.ready) this.send();
  }

  onMessage(m) {
    if (m?.type === 'ready') {
      this.ready = true;
      this.send();
    } else if (m?.type === 'state') {
      if (m.version !== this.version || !this.doc) return;
      this.publishDiags(m.diags ?? '', m.error);
      const ms = m.ms?.toFixed(0);
      this.status.text = m.error ? 'tsm $(error)' : `tsm $(zap) ${ms}ms`;
      this.status.tooltip = m.timings ? JSON.stringify(m.timings) : undefined;
      this.status.show();
    } else if (m?.type === 'jump') {
      this.jumpToSource(m.offset);
    }
  }

  publishDiags(text, error) {
    if (!this.doc) return;
    const out = [];
    const starts = this.byteStarts ?? [];
    const posOf = (byte) => {
      let lo = 0, hi = starts.length - 1;
      while (lo < hi) {
        const mid = (lo + hi + 1) >> 1;
        if (starts[mid] <= byte) lo = mid;
        else hi = mid - 1;
      }
      return new vscode.Position(lo, 0);
    };
    for (const line of text.split('\n')) {
      const m = /^(error|warning|info) ([\w-]+) @\[(\d+),(\d+)\) (.*)$/.exec(line);
      if (!m) continue;
      const sev = m[1] === 'error' ? vscode.DiagnosticSeverity.Error
        : m[1] === 'warning' ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Information;
      const from = posOf(+m[3]);
      const to = this.doc.lineAt(Math.min(from.line, this.doc.lineCount - 1)).range.end;
      const d = new vscode.Diagnostic(new vscode.Range(from, to), m[5], sev);
      d.code = m[2];
      d.source = 'tsm';
      out.push(d);
    }
    if (error) {
      out.push(new vscode.Diagnostic(new vscode.Range(0, 0, 0, 1),
        `typeset failed: ${error}`, vscode.DiagnosticSeverity.Error));
    }
    this.diags.set(this.doc.uri, out);
  }

  jumpToSource(byteOffset) {
    if (!this.doc || !this.byteStarts) return;
    let lo = 0, hi = this.byteStarts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (this.byteStarts[mid] <= byteOffset) lo = mid;
      else hi = mid - 1;
    }
    const pos = new vscode.Position(lo, 0);
    const editor = vscode.window.visibleTextEditors.find(
      (e) => e.document === this.doc);
    const show = editor
      ? Promise.resolve(editor)
      : vscode.window.showTextDocument(this.doc, vscode.ViewColumn.One);
    Promise.resolve(show).then((ed) => {
      ed.selection = new vscode.Selection(pos, pos);
      ed.revealRange(new vscode.Range(pos, pos), vscode.TextEditorRevealType.InCenter);
    });
  }

  // debounced re-typeset on change; immediate on preview open
  schedule(doc) {
    if (!this.panel || doc !== this.doc) return;
    clearTimeout(this.timer);
    const delay = vscode.workspace.getConfiguration('tsm.preview').get('debounceMs', 150);
    this.timer = setTimeout(() => this.send(), delay);
  }

  send() {
    if (!this.panel || !this.doc || !this.ready) return;
    const cfgw = vscode.workspace.getConfiguration('tsm.preview');
    let text = this.doc.getText();
    // SSG front matter is not markup: blank it out LINE BY LINE, so the
    // engine never sees it while every line keeps its number — diagnostics
    // and jump/reveal offsets are computed against this transformed text
    const fm = /^---\r?\n[\s\S]*?\r?\n---(\r?\n|$)/.exec(text);
    if (fm) {
      const blanked = fm[0].replace(/[^\n]/g, '');
      text = blanked + text.slice(fm[0].length);
    }
    this.version = this.doc.version;
    this.byteStarts = lineByteStarts(text);
    this.panel.webview.postMessage({
      type: 'src',
      text,
      version: this.version,
      cfg: {
        fontFamily: cfgw.get('fontFamily'),
        cjkFontFamily: cfgw.get('cjkFontFamily'),
        baseSizePx: cfgw.get('baseSizePx', 18),
      },
    });
  }

  reveal(line) {
    if (!this.panel || !this.ready || !this.byteStarts) return;
    const byte = this.byteStarts[Math.min(line, this.byteStarts.length - 1)];
    this.panel.webview.postMessage({ type: 'reveal', offset: byte });
  }

  print() {
    this.panel?.webview.postMessage({ type: 'print' });
  }

  dispose() {
    clearTimeout(this.timer);
    this.panel?.dispose();
    this.server?.close();
    this.diags.dispose();
    this.status.dispose();
  }
}

module.exports = { TsmPreview };
