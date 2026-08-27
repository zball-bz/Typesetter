// Typesetter (.tsm) language support + live typeset preview.
// Design: docs/editor-design.md §5. No build step — plain CJS; the engine
// assets are served from the repo checkout or from vendor/ when packaged.
const vscode = require('vscode');
const { tsmTokens, LEGEND } = require('./tokens');
const { assetRoot } = require('./paths');
const { TsmPreview } = require('./preview');

const HEADING = /^(={1,6}) (.*)$/;
const FENCE = /^```/;

function activate(context) {
  let root = null;
  try {
    root = assetRoot(context.extensionPath);
  } catch (e) {
    vscode.window.showWarningMessage(String(e.message ?? e));
  }

  // --- semantic tokens (tree-sitter-tsm; same grammar the engine uses) ---
  if (root) {
    const legend = new vscode.SemanticTokensLegend(LEGEND);
    context.subscriptions.push(
      vscode.languages.registerDocumentSemanticTokensProvider(
        { language: 'tsm' },
        {
          async provideDocumentSemanticTokens(doc) {
            const text = doc.getText();
            const builder = new vscode.SemanticTokensBuilder(legend);
            for (const t of await tsmTokens(root, text)) {
              // VSCode tokens must not cross lines — split multi-line spans
              let from = doc.positionAt(t.s);
              const to = doc.positionAt(t.e);
              while (from.line < to.line) {
                const end = doc.lineAt(from.line).range.end;
                if (end.character > from.character)
                  builder.push(new vscode.Range(from, end), t.type);
                from = new vscode.Position(from.line + 1, 0);
              }
              if (to.character > from.character)
                builder.push(new vscode.Range(from, to), t.type);
            }
            return builder.build();
          },
        },
        legend),
    );
  }

  // --- outline: heading tree ------------------------------------------------
  context.subscriptions.push(
    vscode.languages.registerDocumentSymbolProvider({ language: 'tsm' }, {
      provideDocumentSymbols(doc) {
        const flat = [];
        for (let i = 0; i < doc.lineCount; i++) {
          const m = HEADING.exec(doc.lineAt(i).text);
          if (m) flat.push({ line: i, level: m[1].length, title: m[2].trim() || '(untitled)' });
        }
        const roots = [];
        const stack = [];
        for (const h of flat) {
          const range = new vscode.Range(h.line, 0, h.line, doc.lineAt(h.line).text.length);
          const sym = new vscode.DocumentSymbol(
            h.title, '', vscode.SymbolKind.String, range, range);
          sym._level = h.level;
          while (stack.length && stack[stack.length - 1]._level >= h.level) stack.pop();
          (stack.length ? stack[stack.length - 1].children : roots).push(sym);
          stack.push(sym);
        }
        // extend each symbol's range to the next same-or-higher heading
        const extend = (syms, endLine) => {
          for (let i = 0; i < syms.length; i++) {
            const stop = i + 1 < syms.length ? syms[i + 1].range.start.line - 1 : endLine;
            const line = Math.max(syms[i].range.start.line, stop);
            syms[i].range = new vscode.Range(
              syms[i].range.start, new vscode.Position(line, doc.lineAt(line).text.length));
            extend(syms[i].children, line);
          }
        };
        extend(roots, doc.lineCount - 1);
        return roots;
      },
    }),
  );

  // --- folding: heading sections + fenced blocks ---------------------------
  context.subscriptions.push(
    vscode.languages.registerFoldingRangeProvider({ language: 'tsm' }, {
      provideFoldingRanges(doc) {
        const out = [];
        const headings = [];
        let fenceStart = -1;
        for (let i = 0; i < doc.lineCount; i++) {
          const text = doc.lineAt(i).text;
          if (FENCE.test(text)) {
            if (fenceStart < 0) fenceStart = i;
            else {
              out.push(new vscode.FoldingRange(fenceStart, i));
              fenceStart = -1;
            }
            continue;
          }
          if (fenceStart >= 0) continue;
          const m = HEADING.exec(text);
          if (m) {
            const level = m[1].length;
            while (headings.length && headings[headings.length - 1].level >= level) {
              const h = headings.pop();
              if (i - 1 > h.line) out.push(new vscode.FoldingRange(h.line, i - 1));
            }
            headings.push({ line: i, level });
          }
        }
        for (const h of headings)
          if (doc.lineCount - 1 > h.line)
            out.push(new vscode.FoldingRange(h.line, doc.lineCount - 1));
        return out;
      },
    }),
  );

  // --- completion: region builders + references ----------------------------
  const BUILDERS = ['figure', 'table', 'quote', 'center', 'right', 'columns'];
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider({ language: 'tsm' }, {
      provideCompletionItems(doc, pos) {
        const prefix = doc.lineAt(pos.line).text.slice(0, pos.character);
        const items = [];
        if (/#!?[A-Za-z_]*$/.test(prefix)) {
          for (const b of BUILDERS) {
            const it = new vscode.CompletionItem(`#!${b}`, vscode.CompletionItemKind.Module);
            it.insertText = new vscode.SnippetString(`!${b}\n$0\n#${b}!`);
            it.range = new vscode.Range(pos.translate(0, -1), pos);
            it.filterText = `#!${b}`;
            items.push(it);
          }
        }
        if (/@[A-Za-z0-9_-]*$/.test(prefix)) {
          const labels = new Set();
          for (const m of doc.getText().matchAll(/<([A-Za-z][A-Za-z0-9_-]*)>/g))
            labels.add(m[1]);
          for (const l of labels)
            items.push(new vscode.CompletionItem(`@${l}`, vscode.CompletionItemKind.Reference));
        }
        return items;
      },
    }, '#', '@'),
  );

  // --- preview --------------------------------------------------------------
  const preview = new TsmPreview(context);
  context.subscriptions.push({ dispose: () => preview.dispose() });
  context.subscriptions.push(
    vscode.commands.registerCommand('tsm.openPreview', () => {
      const doc = vscode.window.activeTextEditor?.document;
      if (doc?.languageId === 'tsm') preview.open(doc);
      else vscode.window.showInformationMessage('Open a .tsm file first.');
    }),
    vscode.commands.registerCommand('tsm.printPreview', () => preview.print()),
    vscode.workspace.onDidChangeTextDocument((e) => preview.schedule(e.document)),
    vscode.window.onDidChangeTextEditorVisibleRanges((e) => {
      if (e.textEditor.document === preview.doc && e.visibleRanges.length)
        preview.reveal(e.visibleRanges[0].start.line);
    }),
    vscode.window.onDidChangeActiveTextEditor((ed) => {
      if (ed?.document.languageId === 'tsm' && preview.panel) preview.open(ed.document);
    }),
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
