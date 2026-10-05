// Typesetter (.tsm) language support + live typeset preview.
// Design: docs/editor-design.md §5. No build step — plain CJS; the engine
// assets are served from the repo checkout or from vendor/ when packaged.
// Tokens, outline, folding and completion come from the engine running in
// the extension host (src/engine.js, plan P1-09); until it has loaded, the
// tree-sitter grammar colors the first paint.
const vscode = require('vscode');
const engine = require('./engine');
const { headingTree, foldingRanges, regionNames, labelNames } = require('./features');
const { tsmTokens, LEGEND, TYPE_OF } = require('./tokens');
const { assetRoot } = require('./paths');
const { TsmPreview } = require('./preview');

function activate(context) {
  let root = null;
  try {
    root = assetRoot(context.extensionPath);
  } catch (e) {
    vscode.window.showWarningMessage(String(e.message ?? e));
  }
  const tokensChanged = new vscode.EventEmitter();
  context.subscriptions.push(tokensChanged);
  const loaded = root
    ? engine.load(root).then(() => tokensChanged.fire(), (e) => {
      vscode.window.showWarningMessage(`tsm: engine unavailable (${e.message ?? e})`);
    })
    : Promise.resolve();
  // the engine's outline of a document (null without an engine)
  const outlineOf = async (doc) => {
    await loaded;
    return engine.ready() ? engine.outline(doc.getText()) : null;
  };
  const lineOf = (doc) => (i) => doc.positionAt(i).line;

  // --- semantic tokens -------------------------------------------------------
  if (root) {
    const legend = new vscode.SemanticTokensLegend(LEGEND);
    context.subscriptions.push(
      vscode.languages.registerDocumentSemanticTokensProvider(
        { language: 'tsm' },
        {
          onDidChangeSemanticTokens: tokensChanged.event,
          async provideDocumentSemanticTokens(doc) {
            const text = doc.getText();
            const toks = engine.ready()
              ? engine.tokens(text).map((t) => ({ s: t.s, e: t.e, type: TYPE_OF[t.tag] })).filter((t) => t.type)
              : await tsmTokens(root, text);  // cold start
            const builder = new vscode.SemanticTokensBuilder(legend);
            for (const t of toks) {
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
      async provideDocumentSymbols(doc) {
        const o = await outlineOf(doc);
        if (!o) return [];
        const symbol = (h) => {
          const range = new vscode.Range(h.line, 0, h.endLine, doc.lineAt(h.endLine).text.length);
          const head = new vscode.Range(h.line, 0, h.line, doc.lineAt(h.line).text.length);
          const sym = new vscode.DocumentSymbol(h.title, '', vscode.SymbolKind.String, range, head);
          sym.children = h.children.map(symbol);
          return sym;
        };
        return headingTree(o, lineOf(doc), doc.lineCount - 1).map(symbol);
      },
    }),
  );

  // --- folding: heading sections, fences, regions ---------------------------
  context.subscriptions.push(
    vscode.languages.registerFoldingRangeProvider({ language: 'tsm' }, {
      async provideFoldingRanges(doc) {
        const o = await outlineOf(doc);
        if (!o) return [];
        return foldingRanges(o, lineOf(doc), doc.lineCount - 1)
          .map((r) => new vscode.FoldingRange(r.start, r.end));
      },
    }),
  );

  // --- completion: region names + references --------------------------------
  // Region names are the manifest's Body constructors (plan P2-03), the
  // handlers the document registers ($.region("name", …)) and its regions.
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider({ language: 'tsm' }, {
      async provideCompletionItems(doc, pos) {
        const prefix = doc.lineAt(pos.line).text.slice(0, pos.character);
        const o = await outlineOf(doc);
        if (!o) return [];
        const items = [];
        if (/#!?[A-Za-z_]*$/.test(prefix)) {
          for (const b of regionNames(o, engine.manifest(), doc.getText())) {
            const it = new vscode.CompletionItem(`#!${b}`, vscode.CompletionItemKind.Module);
            it.insertText = new vscode.SnippetString(`!${b}\n$0\n#${b}!`);
            it.range = new vscode.Range(pos.translate(0, -1), pos);
            it.filterText = `#!${b}`;
            items.push(it);
          }
        }
        if (/@\[?[^\s\]]*$/.test(prefix))
          for (const l of labelNames(o))
            items.push(new vscode.CompletionItem(`@${l}`, vscode.CompletionItemKind.Reference));
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
