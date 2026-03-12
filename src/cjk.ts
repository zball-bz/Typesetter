/**
 * CJK character classification and block emission for line breaking.
 * Follows GB/T 15834 punctuation rules for Simplified Chinese horizontal typesetting.
 */

import { LinebreakBlock, TextStyle } from './types';
import { TextMeasurer } from './measure';

// ── Character classification ──

// CJK Unified Ideographs and common extension blocks
function isCJKIdeograph(cp: number): boolean {
  return (
    (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified Ideographs
    (cp >= 0x3400 && cp <= 0x4DBF) ||   // Extension A
    (cp >= 0x20000 && cp <= 0x2A6DF) || // Extension B
    (cp >= 0x2A700 && cp <= 0x2B73F) || // Extension C
    (cp >= 0x2B740 && cp <= 0x2B81F) || // Extension D
    (cp >= 0x2B820 && cp <= 0x2CEAF) || // Extension E
    (cp >= 0x2CEB0 && cp <= 0x2EBEF) || // Extension F
    (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK Compatibility Ideographs
    (cp >= 0x3000 && cp <= 0x303F && !isCJKPunctuation(cp)) || // CJK Symbols (non-punct)
    (cp >= 0x3100 && cp <= 0x312F) ||   // Bopomofo
    (cp >= 0x31A0 && cp <= 0x31BF) ||   // Bopomofo Extended
    (cp >= 0x3200 && cp <= 0x32FF) ||   // Enclosed CJK Letters
    (cp >= 0x3300 && cp <= 0x33FF) ||   // CJK Compatibility
    (cp >= 0xFE30 && cp <= 0xFE4F)      // CJK Compatibility Forms
  );
}

function isCJKPunctuation(cp: number): boolean {
  return isOpeningPunct(cp) || isClosingPunct(cp);
}

// Opening punctuation: left-facing brackets and opening quotes
// U+300A 《  U+3008 〈  U+300C 「  U+300E 『
// U+3010 【  U+3014 〔  U+3016 〖
// U+FF08 （  U+FF3B ［  U+FF5B ｛
// U+201C \u201C  (left double quotation mark)
// U+2018 \u2018  (left single quotation mark)
const OPENING_PUNCT = new Set([
  0x300A, 0x3008, 0x300C, 0x300E,
  0x3010, 0x3014, 0x3016,
  0xFF08, 0xFF3B, 0xFF5B,
  0x201C, 0x2018,
]);

function isOpeningPunct(cp: number): boolean {
  return OPENING_PUNCT.has(cp);
}

// Closing/pause punctuation: right-facing brackets, closing quotes, and stops
// U+300B 》  U+3009 〉  U+300D 」  U+300F 』
// U+3011 】  U+3015 〕  U+3017 〗
// U+FF09 ）  U+FF3D ］  U+FF5D ｝
// U+201D \u201D  (right double quotation mark)
// U+2019 \u2019  (right single quotation mark)
// U+FF0C ，  U+3001 、  U+3002 。  U+FF0E ．
// U+FF01 ！  U+FF1F ？  U+FF1B ；  U+FF1A ：
const CLOSING_PUNCT = new Set([
  0x300B, 0x3009, 0x300D, 0x300F,
  0x3011, 0x3015, 0x3017,
  0xFF09, 0xFF3D, 0xFF5D,
  0x201D, 0x2019,
  0xFF0C, 0x3001, 0x3002, 0xFF0E,
  0xFF01, 0xFF1F, 0xFF1B, 0xFF1A,
]);

function isClosingPunct(cp: number): boolean {
  return CLOSING_PUNCT.has(cp);
}

// Em dash U+2014 and ellipsis U+2026
function isEmDash(cp: number): boolean {
  return cp === 0x2014;
}

function isEllipsis(cp: number): boolean {
  return cp === 0x2026;
}

// Check if a character is CJK (ideograph or CJK punctuation)
export function isCJK(cp: number): boolean {
  return isCJKIdeograph(cp) || isCJKPunctuation(cp) || isEmDash(cp) || isEllipsis(cp);
}

// Check if a character is Latin/non-CJK (excluding whitespace)
function isLatin(cp: number): boolean {
  return !isCJK(cp) && cp !== 0x20 && cp !== 0x0A && cp !== 0x0D && cp !== 0x09;
}

// ── Block emission ──

const INF = 1e18;

export interface CJKConfig {
  fontSize: number;   // in px, 1em = fontSize
  style: TextStyle;
}

/**
 * Emit LinebreakBlocks for a sequence of CJK and mixed CJK/Latin characters.
 * This handles:
 * - CJK ideographs with 0.1em inter-character spacing
 * - Punctuation compression (GB/T 15834)
 * - Em dash and ellipsis as unbreakable pairs
 * - CJK-Latin boundary thin spaces
 * - Paragraph indentation as blocks
 */
export function emitCJKBlocks(
  text: string,
  measurer: TextMeasurer,
  config: CJKConfig,
  indent: boolean = false,
): LinebreakBlock[] {
  const blocks: LinebreakBlock[] = [];
  const { fontSize, style } = config;
  const em = fontSize;

  // Emit indent blocks (2 x 1em unbreakable)
  if (indent) {
    for (let k = 0; k < 2; k++) {
      blocks.push({
        width: em,
        breakPenalty: INF,
        breakWidth: 0,
        spaceWidth: 0,
        text: '\u3000', // ideographic space for rendering
        style,
        isSpace: false,
        isHyphen: false,
      });
    }
  }

  const codepoints: number[] = [];
  for (const ch of text) {
    codepoints.push(ch.codePointAt(0)!);
  }

  let i = 0;
  let prevType: 'cjk' | 'latin' | 'space' | 'none' = 'none';

  while (i < codepoints.length) {
    const cp = codepoints[i];
    const ch = String.fromCodePoint(cp);

    // ── Whitespace ──
    if (cp === 0x20 || cp === 0x09) {
      // Latin space
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
      prevType = 'space';
      i++;
      continue;
    }

    // ── Em dash pair ——  ──
    if (isEmDash(cp) && i + 1 < codepoints.length && isEmDash(codepoints[i + 1])) {
      // Insert CJK-Latin boundary if coming from Latin
      if (prevType === 'latin') {
        emitBoundarySpace(blocks, em, style);
      }

      const dashText = '\u2014\u2014';
      const w = measurer.measureWord(dashText, style);
      blocks.push({
        width: w,
        breakPenalty: INF,
        breakWidth: 0,
        spaceWidth: 0,
        text: dashText,
        style,
        isSpace: false,
        isHyphen: false,
      });
      prevType = 'cjk';
      i += 2;
      continue;
    }

    // ── Ellipsis pair ……  ──
    if (isEllipsis(cp) && i + 1 < codepoints.length && isEllipsis(codepoints[i + 1])) {
      if (prevType === 'latin') {
        emitBoundarySpace(blocks, em, style);
      }

      const ellText = '\u2026\u2026';
      const w = measurer.measureWord(ellText, style);
      blocks.push({
        width: w,
        breakPenalty: INF,
        breakWidth: 0,
        spaceWidth: 0,
        text: ellText,
        style,
        isSpace: false,
        isHyphen: false,
      });
      prevType = 'cjk';
      i += 2;
      continue;
    }

    // ── Opening punctuation ──
    if (isOpeningPunct(cp)) {
      if (prevType === 'latin') {
        emitBoundarySpace(blocks, em, style);
      }

      // Check if previous block is a closing punct's trailing space — compress
      const shouldCompress = shouldCompressWithPrev(blocks);
      if (shouldCompress) {
        // Remove the previous trailing 0.5em breakable space
        blocks.pop();
        // Don't emit the leading 0.5em space either — total compression
        // No breakable between them
      } else {
        // Emit compressible leading space (breakable, so line can start after it)
        blocks.push({
          width: em * 0.5,
          breakPenalty: 0,
          breakWidth: 0,
          spaceWidth: 0,
          text: '',
          style,
          isSpace: false,
          isHyphen: false,
        });
      }

      // The glyph itself (unbreakable)
      blocks.push({
        width: em * 0.5,
        breakPenalty: INF,
        breakWidth: 0,
        spaceWidth: 0,
        text: ch,
        style,
        isSpace: false,
        isHyphen: false,
      });

      prevType = 'cjk';
      i++;
      continue;
    }

    // ── Closing/pause punctuation ──
    if (isClosingPunct(cp)) {
      // The glyph (unbreakable)
      blocks.push({
        width: em * 0.5,
        breakPenalty: INF,
        breakWidth: 0,
        spaceWidth: 0,
        text: ch,
        style,
        isSpace: false,
        isHyphen: false,
      });

      // Peek ahead: if next is also punctuation that compresses, don't emit trailing space
      const nextCp = i + 1 < codepoints.length ? codepoints[i + 1] : -1;
      if (nextCp >= 0 && (isOpeningPunct(nextCp) || isClosingPunct(nextCp))) {
        // Consecutive punctuation: skip trailing space, next punct handles compression
      } else {
        // Emit compressible trailing space (breakable)
        blocks.push({
          width: em * 0.5,
          breakPenalty: 0,
          breakWidth: 0,
          spaceWidth: 0,
          text: '',
          style,
          isSpace: false,
          isHyphen: false,
        });
      }

      prevType = 'cjk';
      i++;
      continue;
    }

    // ── Single em dash or ellipsis (not paired) — treat as CJK char ──
    if (isEmDash(cp) || isEllipsis(cp)) {
      if (prevType === 'latin') {
        emitBoundarySpace(blocks, em, style);
      }

      const w = measurer.measureWord(ch, style);
      blocks.push({
        width: w,
        breakPenalty: 0,
        breakWidth: 0,
        spaceWidth: em * 0.1,
        text: ch,
        style,
        isSpace: false,
        isHyphen: false,
      });
      prevType = 'cjk';
      i++;
      continue;
    }

    // ── CJK ideograph ──
    if (isCJKIdeograph(cp)) {
      if (prevType === 'latin') {
        emitBoundarySpace(blocks, em, style);
      }

      blocks.push({
        width: em, // full-width character = 1em
        breakPenalty: 0,
        breakWidth: 0,
        spaceWidth: em * 0.1,
        text: ch,
        style,
        isSpace: false,
        isHyphen: false,
      });
      prevType = 'cjk';
      i++;
      continue;
    }

    // ── Latin character — collect run ──
    if (isLatin(cp)) {
      if (prevType === 'cjk') {
        emitBoundarySpace(blocks, em, style);
      }

      // Collect the full Latin run (to be processed by Latin flattener)
      const start = i;
      while (i < codepoints.length && isLatin(codepoints[i])) {
        i++;
      }
      const latinRun = String.fromCodePoint(...codepoints.slice(start, i));

      // Mark this as a Latin run — the caller will process it through
      // the existing Latin flattener (hyphenation etc.)
      blocks.push({
        width: -1, // sentinel: needs Latin processing
        breakPenalty: INF,
        breakWidth: 0,
        spaceWidth: 0,
        text: latinRun,
        style,
        isSpace: false,
        isHyphen: false,
        _isLatinRun: true, // internal flag
      } as any);

      prevType = 'latin';
      continue;
    }

    // ── Fallback: unknown character, treat as CJK ──
    {
      const w = measurer.measureWord(ch, style);
      blocks.push({
        width: w,
        breakPenalty: 0,
        breakWidth: 0,
        spaceWidth: 0,
        text: ch,
        style,
        isSpace: false,
        isHyphen: false,
      });
      prevType = 'cjk';
      i++;
    }
  }

  return blocks;
}

// ── Helpers ──

/** Insert a CJK-Latin boundary thin space */
function emitBoundarySpace(blocks: LinebreakBlock[], em: number, style: TextStyle): void {
  blocks.push({
    width: em * 0.25,
    breakPenalty: 0,
    breakWidth: 0,
    spaceWidth: em * 0.25,
    text: '',
    style,
    isSpace: true,
    isHyphen: false,
  });
}

/**
 * Check if the last emitted block is a closing punct's trailing compressible space.
 * If so, we can compress it away when an opening punct follows.
 */
function shouldCompressWithPrev(blocks: LinebreakBlock[]): boolean {
  if (blocks.length < 2) return false;
  const last = blocks[blocks.length - 1];
  const secondLast = blocks[blocks.length - 2];
  // Pattern: [unbreakable glyph][breakable 0.5em space with sw=0]
  // The trailing space of a closing punct
  return (
    last.breakPenalty === 0 &&
    last.spaceWidth === 0 &&
    last.text === '' &&
    !last.isSpace &&
    secondLast.breakPenalty >= INF &&
    secondLast.text.length === 1 &&
    isClosingPunct(secondLast.text.codePointAt(0)!)
  );
}
