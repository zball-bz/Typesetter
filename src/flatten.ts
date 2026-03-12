import { InlineNode, TextStyle, LinebreakBlock } from './types';
import { TextMeasurer } from './measure';
import { getHyphenationPoints } from './hyphenate';

const INF = 1e18;
const HYPHEN_PENALTY = 0.7;
const SHORT_FRAGMENT_PENALTY = 0.3;

const BASE_STYLE: TextStyle = {
  fontFamily: '"Crimson Text", serif',
  fontSize: 18,
  fontWeight: 400,
  italic: false,
};

export function flattenToBlocks(
  nodes: InlineNode[],
  measurer: TextMeasurer,
  baseFontSize?: number,
  hyphenPenalty: number = HYPHEN_PENALTY,
): LinebreakBlock[] {
  const blocks: LinebreakBlock[] = [];
  const baseStyle = { ...BASE_STYLE };
  if (baseFontSize) baseStyle.fontSize = baseFontSize;

  function processNode(node: InlineNode, style: TextStyle): void {
    switch (node.type) {
      case 'text':
        processText(node.text, style);
        break;
      case 'bold':
        const boldStyle = { ...style, fontWeight: 700 };
        for (const child of node.children) processNode(child, boldStyle);
        break;
      case 'italic':
        const italicStyle = { ...style, italic: true };
        for (const child of node.children) processNode(child, italicStyle);
        break;
    }
  }

  function processText(text: string, style: TextStyle): void {
    const tokens = text.split(/(\s+)/);

    for (const token of tokens) {
      if (!token) continue;

      if (/^\s+$/.test(token)) {
        // Space: its own breakable block
        const spaceW = measurer.measureChar(' ', style);
        blocks.push({
          width: spaceW,
          breakPenalty: 0,
          breakWidth: 0,
          spaceWidth: spaceW,
          text: ' ',
          style,
          isSpace: true,
          isHyphen: false,
        });
        continue;
      }

      // Split on existing hyphens (e.g. "bit-wise" → ["bit-", "wise"])
      const hyphenParts = token.split(/(?<=-)(?=\w)/);

      for (let hp = 0; hp < hyphenParts.length; hp++) {
        const subtoken = hyphenParts[hp];
        const hasTrailingHyphen = subtoken.endsWith('-');
        const cleanToken = hasTrailingHyphen ? subtoken.slice(0, -1) : subtoken;

        // Get hyphenation points for this sub-token
        const parts = getHyphenationPoints(cleanToken);
        const hyphenW = measurer.measureWord('-', style);

        for (let p = 0; p < parts.length; p++) {
          const part = parts[p];
          // If last part of a hyphen-ending subtoken, include the hyphen in the text
          const isLastPart = p === parts.length - 1;
          const partText = (isLastPart && hasTrailingHyphen) ? part + '-' : part;
          const w = measurer.measureWord(partText, style);

          // Phoneme: non-breakable
          blocks.push({
            width: w,
            breakPenalty: INF,
            breakWidth: 0,
            spaceWidth: 0,
            text: partText,
            style,
            isSpace: false,
            isHyphen: false,
          });

          // After each phoneme except the last: insert a hyphen break point
          if (p < parts.length - 1) {
            let penalty = hyphenPenalty;
            if (part.length < 3) penalty += SHORT_FRAGMENT_PENALTY;
            if (parts[p + 1].length < 3) penalty += SHORT_FRAGMENT_PENALTY;

            blocks.push({
              width: 0,
              breakPenalty: penalty,
              breakWidth: hyphenW,
              spaceWidth: 0,
              text: '',
              style,
              isSpace: false,
              isHyphen: true,
            });
          }
        }

        // After hyphen-ending subtoken: zero-width break (hyphen already in text)
        if (hasTrailingHyphen && hp < hyphenParts.length - 1) {
          blocks.push({
            width: 0,
            breakPenalty: 0, // free break — hyphen already present
            breakWidth: 0,   // no extra hyphen needed
            spaceWidth: 0,
            text: '',
            style,
            isSpace: false,
            isHyphen: true,
          });
        }
      }
    }
  }

  for (const node of nodes) {
    processNode(node, baseStyle);
  }

  // Ensure the very last block is breakable (paragraph end)
  if (blocks.length > 0 && blocks[blocks.length - 1].breakPenalty >= INF) {
    blocks[blocks.length - 1].breakPenalty = 0;
    blocks[blocks.length - 1].breakWidth = 0;
  }

  return blocks;
}

/**
 * Extract the first character for drop cap.
 */
export function extractDropCap(nodes: InlineNode[]): { dropChar: string; rest: InlineNode[] } {
  function extract(nodes: InlineNode[]): { found: boolean; char: string; nodes: InlineNode[] } {
    for (let i = 0; i < nodes.length; i++) {
      const node = nodes[i];
      if (node.type === 'text' && node.text.length > 0) {
        const char = node.text[0];
        const rest = node.text.slice(1);
        const newNodes = [...nodes];
        if (rest) {
          newNodes[i] = { type: 'text', text: rest };
        } else {
          newNodes.splice(i, 1);
        }
        return { found: true, char, nodes: newNodes };
      }
      if (node.type === 'bold' || node.type === 'italic') {
        const result = extract(node.children);
        if (result.found) {
          const newNodes = [...nodes];
          newNodes[i] = { ...node, children: result.nodes } as InlineNode;
          return { found: true, char: result.char, nodes: newNodes };
        }
      }
    }
    return { found: false, char: '', nodes };
  }

  const result = extract(nodes);
  return { dropChar: result.char, rest: result.nodes };
}
