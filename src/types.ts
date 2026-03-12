// Core line-breaking block
export interface LinebreakBlock {
  width: number;        // visual width of this block
  breakPenalty: number;  // cost of breaking here (+Inf = non-breakable)
  breakWidth: number;    // extra width when broken here (e.g. hyphen width)
  spaceWidth: number;    // stretchable width for justification distribution

  // rendering info
  text: string;
  style: TextStyle;
  isSpace: boolean;
  isHyphen: boolean;
}

export interface TextStyle {
  fontFamily: string;
  fontSize: number;
  fontWeight: number;
  italic: boolean;
}

export function styleKey(s: TextStyle): string {
  return `${s.fontFamily}#${s.fontSize}#${s.fontWeight}#${s.italic ? 'i' : 'r'}`;
}

// AST from parser
export type InlineNode =
  | { type: 'text'; text: string }
  | { type: 'bold'; children: InlineNode[] }
  | { type: 'italic'; children: InlineNode[] };

export interface ParagraphDirective {
  fontSize?: string;
  dropCap?: boolean;
  dropCapMultiplier?: number;
}

export interface Paragraph {
  directive: ParagraphDirective;
  children: InlineNode[];
}

// Line breaking result
export interface LineBreak {
  start: number;
  end: number;
  lineWidth: number;
}
