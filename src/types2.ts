/**
 * Core type system for the typesetter.
 * 
 * Uses a class-based styling model where each block carries a bitset of
 * engine-defined classes and a set of dynamically generated class IDs.
 * The span tree builder uses subset/superset relationships to create
 * efficient nested HTML.
 */

// ── Engine-defined base classes (bit indices) ──

export const CLS_LATIN       = 0;
export const CLS_CJK         = 1;
export const CLS_EM          = 2;   // italic
export const CLS_BOLD        = 3;
export const CLS_BOUNDARY    = 4;   // CJK-Latin 1/4em space
export const CLS_HAS_STYLE   = 5;   // has inline style override
export const CLS_CODE        = 6;   // inline code
export const CLS_CJK_EMPH   = 7;   // CJK emphasis dots (text-emphasis)
export const CLS_PUNCT_OPEN  = 8;   // opening punctuation
export const CLS_PUNCT_CLOSE = 9;   // closing punctuation
export const CLS_INDENT      = 10;  // paragraph indent block
export const CLS_HYPHEN      = 11;  // hyphen break point
export const CLS_SPACE       = 12;  // Latin space
export const CLS_LINK        = 13;  // hyperlink
export const CLS_CJK_SPACE   = 14;  // CJK inter-char spacing (0.1em)
export const CLS_PUNCT_SPACE = 15;  // punctuation compressible space

// ── TextStyling ──

export interface InlineStyle {
  [key: string]: string;  // CSS property -> value
}

export class TextStyling {
  baseClass: bigint;           // bitset of engine-defined classes
  classes: Set<number>;        // dynamically generated class IDs
  inlineStyle: InlineStyle | null;

  constructor(
    baseClass: bigint = 0n,
    classes?: Set<number>,
    inlineStyle?: InlineStyle | null,
  ) {
    this.baseClass = baseClass;
    this.classes = classes ?? new Set();
    this.inlineStyle = inlineStyle ?? null;
  }

  /** Check if a base class bit is set */
  has(bit: number): boolean {
    return (this.baseClass & (1n << BigInt(bit))) !== 0n;
  }

  /** Return a new TextStyling with additional base class bits */
  with(...bits: number[]): TextStyling {
    let bc = this.baseClass;
    for (const b of bits) bc |= 1n << BigInt(b);
    return new TextStyling(bc, new Set(this.classes), this.inlineStyle);
  }

  /** Return a new TextStyling without specified base class bits */
  without(...bits: number[]): TextStyling {
    let bc = this.baseClass;
    for (const b of bits) bc &= ~(1n << BigInt(b));
    return new TextStyling(bc, new Set(this.classes), this.inlineStyle);
  }

  /** Return a new TextStyling with an additional dynamic class */
  withClass(id: number): TextStyling {
    const s = new TextStyling(this.baseClass, new Set(this.classes), this.inlineStyle);
    s.classes.add(id);
    return s;
  }

  /** Return a new TextStyling with inline style */
  withInlineStyle(style: InlineStyle): TextStyling {
    const merged = { ...(this.inlineStyle ?? {}), ...style };
    return new TextStyling(
      this.baseClass | (1n << BigInt(CLS_HAS_STYLE)),
      new Set(this.classes),
      merged,
    );
  }

  /** Check if this styling is a subset of other */
  isSubsetOf(other: TextStyling): boolean {
    if ((this.baseClass & other.baseClass) !== this.baseClass) return false;
    for (const c of this.classes) {
      if (!other.classes.has(c)) return false;
    }
    return true;
  }

  /** Check if this styling equals other */
  equals(other: TextStyling): boolean {
    if (this.baseClass !== other.baseClass) return false;
    if (this.classes.size !== other.classes.size) return false;
    for (const c of this.classes) {
      if (!other.classes.has(c)) return false;
    }
    // Inline styles compared by reference (same object = same style)
    if (this.inlineStyle !== other.inlineStyle) {
      if (!this.inlineStyle || !other.inlineStyle) return false;
      const aKeys = Object.keys(this.inlineStyle);
      const bKeys = Object.keys(other.inlineStyle);
      if (aKeys.length !== bKeys.length) return false;
      for (const k of aKeys) {
        if (this.inlineStyle[k] !== other.inlineStyle[k]) return false;
      }
    }
    return true;
  }

  /** Compute the set difference: classes in this but not in other */
  diff(other: TextStyling): TextStyling {
    const bc = this.baseClass & ~other.baseClass;
    const cls = new Set<number>();
    for (const c of this.classes) {
      if (!other.classes.has(c)) cls.add(c);
    }
    // Inline style: include if this has it and other doesn't
    const inl = (this.has(CLS_HAS_STYLE) && !other.has(CLS_HAS_STYLE))
      ? this.inlineStyle
      : null;
    return new TextStyling(bc, cls, inl);
  }

  /** Union of two stylings */
  union(other: TextStyling): TextStyling {
    const bc = this.baseClass | other.baseClass;
    const cls = new Set<number>(this.classes);
    for (const c of other.classes) cls.add(c);
    const inl = other.inlineStyle
      ? { ...(this.inlineStyle ?? {}), ...other.inlineStyle }
      : this.inlineStyle;
    return new TextStyling(bc, cls, inl);
  }

  /** Check if empty (no classes set) */
  isEmpty(): boolean {
    return this.baseClass === 0n && this.classes.size === 0;
  }
}

// ── Content types ──

/** Inline block content (for math, links, etc.) */
export interface InlineBlock {
  tag: string;              // e.g. 'a', 'math', 'sub', 'sup'
  attrs: Record<string, string>;  // e.g. { href: '...' }
  children: LinebreakBlock[];     // nested blocks
}

export type BlockContent =
  | { type: 'text'; text: string }
  | { type: 'block'; block: InlineBlock };

// ── LinebreakBlock ──

export interface LinebreakBlock {
  width: number;
  breakPenalty: number;
  breakWidth: number;
  spaceWidth: number;
  height: number;           // line height contribution
  styling: TextStyling;
  content: BlockContent;
}

// ── Style Manager ──

let nextClassId = 256;  // dynamic classes start at 256

export class StyleManager {
  private nameToId = new Map<string, number>();
  private idToName = new Map<number, string>();
  private idToStyle = new Map<number, InlineStyle>();

  /** Get or create a class ID for a named style */
  getClassId(name: string, style?: InlineStyle): number {
    let id = this.nameToId.get(name);
    if (id !== undefined) return id;
    id = nextClassId++;
    this.nameToId.set(name, id);
    this.idToName.set(id, name);
    if (style) this.idToStyle.set(id, style);
    return id;
  }

  /** Get the name for a class ID */
  getClassName(id: number): string | undefined {
    return this.idToName.get(id);
  }

  /** Get the inline style for a class ID */
  getClassStyle(id: number): InlineStyle | undefined {
    return this.idToStyle.get(id);
  }

  /** Register a style for an existing class ID */
  setClassStyle(id: number, style: InlineStyle): void {
    this.idToStyle.set(id, style);
  }
}

// ── Helpers for creating common blocks ──

const INF = 1e18;

/** Create a CJK ideograph block */
export function cjkCharBlock(char: string, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: em,
    breakPenalty: 0,
    breakWidth: 0,
    spaceWidth: em * 0.1,
    height: em,
    styling: styling.with(CLS_CJK),
    content: { type: 'text', text: char },
  };
}

/** Create a Latin space block */
export function latinSpaceBlock(width: number, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width,
    breakPenalty: 0,
    breakWidth: 0,
    spaceWidth: width,
    height: em,
    styling: styling.with(CLS_SPACE),
    content: { type: 'text', text: ' ' },
  };
}

/** Create a CJK-Latin boundary block */
export function boundaryBlock(em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: em * 0.25,
    breakPenalty: 0,
    breakWidth: 0,
    spaceWidth: em * 0.25,
    height: em,
    styling: styling.with(CLS_BOUNDARY),
    content: { type: 'text', text: '' },
  };
}

/** Create a punctuation compressible space */
export function punctSpaceBlock(em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: em * 0.5,
    breakPenalty: 0,
    breakWidth: 0,
    spaceWidth: 0,  // NOT stretchable
    height: em,
    styling: styling.with(CLS_PUNCT_SPACE),
    content: { type: 'text', text: '' },
  };
}

/** Create an opening punctuation glyph block */
export function punctOpenBlock(char: string, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: em * 0.5,
    breakPenalty: INF,
    breakWidth: 0,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_CJK, CLS_PUNCT_OPEN),
    content: { type: 'text', text: char },
  };
}

/** Create a closing punctuation glyph block */
export function punctCloseBlock(char: string, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: em * 0.5,
    breakPenalty: INF,
    breakWidth: 0,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_CJK, CLS_PUNCT_CLOSE),
    content: { type: 'text', text: char },
  };
}

/** Create an indent block (1em) */
export function indentBlock(em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: em,
    breakPenalty: INF,
    breakWidth: 0,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_INDENT),
    content: { type: 'text', text: '\u3000' },
  };
}

/** Create an unbreakable text block (e.g. em dash pair, ellipsis pair) */
export function unbreakableBlock(text: string, width: number, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width,
    breakPenalty: INF,
    breakWidth: 0,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_CJK),
    content: { type: 'text', text },
  };
}

/** Create a Latin phoneme block */
export function latinPhonemeBlock(text: string, width: number, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width,
    breakPenalty: INF,
    breakWidth: 0,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_LATIN),
    content: { type: 'text', text },
  };
}

/** Create a hyphen break point block */
export function hyphenBreakBlock(hyphenWidth: number, penalty: number, em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: 0,
    breakPenalty: penalty,
    breakWidth: hyphenWidth,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_HYPHEN),
    content: { type: 'text', text: '' },
  };
}

/** Create an existing-hyphen break point block (e.g. after "bit-" in "bit-wise") */
export function existingHyphenBreakBlock(em: number, styling: TextStyling): LinebreakBlock {
  return {
    width: 0,
    breakPenalty: 0,
    breakWidth: 0,
    spaceWidth: 0,
    height: em,
    styling: styling.with(CLS_HYPHEN),
    content: { type: 'text', text: '' },
  };
}

/** Create a link inline block */
export function linkBlock(href: string, children: LinebreakBlock[]): InlineBlock {
  return {
    tag: 'a',
    attrs: { href },
    children,
  };
}
