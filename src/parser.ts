import { InlineNode, Paragraph, ParagraphDirective } from './types';

function parseInline(text: string): InlineNode[] {
  const nodes: InlineNode[] = [];
  let i = 0;

  while (i < text.length) {
    // Bold: **...**
    if (text[i] === '*' && text[i + 1] === '*') {
      const end = text.indexOf('**', i + 2);
      if (end !== -1) {
        nodes.push({ type: 'bold', children: parseInline(text.slice(i + 2, end)) });
        i = end + 2;
        continue;
      }
    }
    // Italic: *...*
    if (text[i] === '*') {
      const end = text.indexOf('*', i + 1);
      if (end !== -1) {
        nodes.push({ type: 'italic', children: parseInline(text.slice(i + 1, end)) });
        i = end + 1;
        continue;
      }
    }
    // Plain text: accumulate until next marker
    let j = i;
    while (j < text.length) {
      if (text[j] === '*') break;
      j++;
    }
    if (j > i) {
      nodes.push({ type: 'text', text: text.slice(i, j) });
    }
    i = j;
  }

  return nodes;
}

function parseDirective(line: string): ParagraphDirective {
  const directive: ParagraphDirective = {};
  // Remove brackets
  const inner = line.slice(1, -1).trim();
  const parts = inner.split(',').map(s => s.trim());
  for (const part of parts) {
    const [key, val] = part.split('=').map(s => s.trim());
    if (key === 'font_size') directive.fontSize = val;
    if (key === 'drop_cap') directive.dropCap = val === 'true';
    if (key === 'drop_cap_multiplier') directive.dropCapMultiplier = parseFloat(val);
  }
  return directive;
}

export function parse(source: string): Paragraph[] {
  const paragraphs: Paragraph[] = [];
  const blocks = source.split(/\n\n+/);

  for (const block of blocks) {
    const trimmed = block.trim();
    if (!trimmed) continue;

    const lines = trimmed.split('\n');
    let directive: ParagraphDirective = {};
    let textLines: string[] = [];

    for (const line of lines) {
      const l = line.trim();
      if (l.startsWith('[') && l.endsWith(']')) {
        directive = { ...directive, ...parseDirective(l) };
      } else {
        textLines.push(l);
      }
    }

    const fullText = textLines.join(' ');
    if (fullText) {
      paragraphs.push({
        directive,
        children: parseInline(fullText),
      });
    }
  }

  return paragraphs;
}
