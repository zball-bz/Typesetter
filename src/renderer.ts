import { LinebreakBlock, TextStyle } from './types';

function applyStyleToElement(el: HTMLElement, style: TextStyle): void {
  el.style.fontFamily = style.fontFamily;
  el.style.fontSize = style.fontSize + 'px';
  el.style.fontWeight = String(style.fontWeight);
  if (style.italic) el.style.fontStyle = 'italic';
}

export function renderParagraph(
  blocks: LinebreakBlock[],
  breakpoints: number[],
  container: HTMLElement,
  options?: {
    dropCap?: { char: string; multiplier: number; style: TextStyle; width: number };
    dropCapLines?: number;
    indent?: boolean;
    lineWidthFn?: (lineNum: number) => number;
  }
): void {
  const para = document.createElement('div');
  para.className = 'typeset-paragraph' + (options?.dropCap ? ' has-dropcap' : '');
  para.style.position = 'relative';
  para.style.lineHeight = '1.5';

  // Drop cap: absolutely positioned, not floated
  if (options?.dropCap) {
    const dc = options.dropCap;
    const dcFontSize = dc.style.fontSize * dc.multiplier;
    const dropCapEl = document.createElement('span');
    dropCapEl.className = 'typeset-dropcap';
    dropCapEl.textContent = dc.char;
    dropCapEl.style.position = 'absolute';
    dropCapEl.style.left = '0';
    dropCapEl.style.top = '0';
    dropCapEl.style.fontFamily = dc.style.fontFamily;
    dropCapEl.style.fontSize = dcFontSize + 'px';
    dropCapEl.style.fontWeight = String(dc.style.fontWeight);
    dropCapEl.style.lineHeight = '1';
    dropCapEl.style.color = '#8b4513';
    para.appendChild(dropCapEl);
  }

  const dcLines = options?.dropCapLines ?? 0;
  const dcWidth = options?.dropCap?.width ?? 0;

  let blockIdx = 0;
  for (let lineIdx = 0; lineIdx < breakpoints.length; lineIdx++) {
    const lineEnd = breakpoints[lineIdx];
    const isLastLine = lineIdx === breakpoints.length - 1;

    const lineDiv = document.createElement('div');
    lineDiv.className = 'typeset-line';

    // Indent first line of non-dropcap continuation paragraphs
    if (lineIdx === 0 && options?.indent && !options?.dropCap) {
      lineDiv.style.paddingLeft = '1.5em';
    }

    // Indent lines next to drop cap
    if (lineIdx < dcLines && dcWidth > 0) {
      lineDiv.style.paddingLeft = dcWidth + 'px';
    }

    if (isLastLine) {
      lineDiv.style.textAlign = 'left';
    }

    // Collect blocks for this line
    let lineBlocks: LinebreakBlock[] = [];
    while (blockIdx < lineEnd) {
      lineBlocks.push(blocks[blockIdx]);
      blockIdx++;
    }

    // Trim leading spaces
    while (lineBlocks.length > 0 && lineBlocks[0].isSpace) {
      lineBlocks.shift();
    }

    // Trim trailing spaces
    while (lineBlocks.length > 0 && lineBlocks[lineBlocks.length - 1].isSpace) {
      lineBlocks.pop();
    }

    // Check if line ends at a hyphen break point
    const lastBlock = lineBlocks[lineBlocks.length - 1];
    const endsWithHyphen = lastBlock && lastBlock.isHyphen && !isLastLine;

    // Compute slack and apply word-spacing for ALL lines (including last)
    {
      let contentW = 0;
      let totalSpaceW = 0;
      let spaceCount = 0;
      for (const b of lineBlocks) {
        if (b.isHyphen) continue;
        if (b.isSpace) { totalSpaceW += b.spaceWidth; spaceCount++; }
        else contentW += b.width;
      }
      if (endsWithHyphen) contentW += (lastBlock?.breakWidth ?? 0);

      let availW: number;
      if (options?.lineWidthFn) {
        availW = options.lineWidthFn(lineIdx);
      } else {
        availW = container.clientWidth || 580;
        if (lineIdx === 0 && options?.indent && !options?.dropCap) {
          availW -= parseFloat(getComputedStyle(container).fontSize || '18') * 1.5;
        }
        if (lineIdx < dcLines && dcWidth > 0) {
          availW -= dcWidth;
        }
      }

      const slack = availW - contentW - totalSpaceW;
      if (slack < 0 && spaceCount > 0) {
        lineDiv.style.wordSpacing = (slack / spaceCount) + 'px';
      }
    }

    // Justify non-last lines
    if (!isLastLine) {
      lineDiv.style.textAlign = 'justify';
      lineDiv.style.textAlignLast = 'justify';
    }

    // Render blocks (skip zero-width hyphen points in the middle)
    for (let bi = 0; bi < lineBlocks.length; bi++) {
      const block = lineBlocks[bi];
      if (block.isHyphen) continue; // skip all hyphen markers
      if (!block.text) continue;

      const span = document.createElement('span');
      applyStyleToElement(span, block.style);
      span.textContent = block.text;
      lineDiv.appendChild(span);
    }

    // Add hyphen glyph at end if broken at hyphen point (only if breakWidth > 0, i.e. not an existing hyphen)
    if (endsWithHyphen && lastBlock.breakWidth > 0) {
      const style = lastBlock.style;
      const hyphenSpan = document.createElement('span');
      applyStyleToElement(hyphenSpan, style);
      hyphenSpan.textContent = '-';
      lineDiv.appendChild(hyphenSpan);
    }

    para.appendChild(lineDiv);
  }

  container.appendChild(para);
}
