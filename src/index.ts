import { parse } from './parser';
import { TextMeasurer } from './measure';
import { initHyphenation } from './hyphenate';
import { flattenToBlocks, extractDropCap } from './flatten';
import { breakLines, dropCapLineWidths, makeCostFn, DEFAULT_COST_PARAMS, CostParams } from './linebreak';
import { renderParagraph } from './renderer';
import { TextStyle } from './types';

const BASE_STYLE: TextStyle = {
  fontFamily: '"Crimson Text", serif',
  fontSize: 18,
  fontWeight: 400,
  italic: false,
};

export interface TypesetConfig {
  costParams?: CostParams;
  hyphenPenalty?: number;
}

export async function typeset(source: string, container: HTMLElement, containerWidth?: number, config?: TypesetConfig): Promise<void> {
  // Initialize hyphenation
  await initHyphenation();

  const measurer = new TextMeasurer();
  const paragraphs = parse(source);
  const width = containerWidth ?? container.clientWidth;

  container.innerHTML = '';

  for (const para of paragraphs) {
    const fontSize = para.directive.fontSize
      ? parseFloat(para.directive.fontSize) * 18  // rough: 1em = 18px base
      : 18;

    let dropCapInfo: { char: string; multiplier: number; width: number; lines: number } | null = null;
    let nodes = para.children;

    if (para.directive.dropCap) {
      const { dropChar, rest } = extractDropCap(para.children);
      if (dropChar) {
        const multiplier = para.directive.dropCapMultiplier ?? 3;
        // Measure at the large size
        const dcStyle = { ...BASE_STYLE, fontSize: fontSize * multiplier };
        let dcWidth = measurer.measureWord(dropChar, dcStyle);
        // Fallback: estimate from base size if measurement seems wrong
        if (dcWidth < 1) {
          dcWidth = measurer.measureWord(dropChar, { ...BASE_STYLE, fontSize }) * multiplier;
        }
        dcWidth += 6; // margin
        const dcLines = Math.round(multiplier * 0.75);
        dropCapInfo = { char: dropChar, multiplier, width: dcWidth, lines: dcLines };
        nodes = rest;
      }
    }

    const blocks = flattenToBlocks(nodes, measurer, fontSize, config?.hyphenPenalty);

    if (blocks.length === 0) continue;

    // Debug: count hyphenation points
    let hyphenCount = 0;
    for (const b of blocks) {
      if (b.isHyphen) hyphenCount++;
    }
    console.log(`[typesetter] paragraph ${paragraphs.indexOf(para)}: ${blocks.length} blocks, ${hyphenCount} hyphen points`);
    if (hyphenCount === 0) {
      // Log first few words to check
      const words = blocks.filter(b => !b.isSpace).slice(0, 5);
      console.log('[typesetter] first words:', words.map(w => w.text));
    }

    const paraIndex = paragraphs.indexOf(para);
    const needsIndent = paraIndex > 0 && !dropCapInfo;
    const indentPx = needsIndent ? fontSize * 1.5 : 0; // 1.5em

    // Determine line widths
    let lineWidthFn: (lineNum: number) => number;
    if (dropCapInfo) {
      lineWidthFn = dropCapLineWidths(width, dropCapInfo.width, dropCapInfo.lines);
    } else if (needsIndent) {
      lineWidthFn = (lineNum: number) => lineNum === 0 ? width - indentPx : width;
    } else {
      lineWidthFn = () => width;
    }

    const baseCostParams = config?.costParams ?? DEFAULT_COST_PARAMS;
    const baseCostFn = makeCostFn(baseCostParams);
    let result = breakLines(blocks, lineWidthFn, baseCostFn);

    // If INF cost, iteratively widen cursor search range
    if (result.cost >= 1e17) {
      const ranges = [10, 20, 50, Infinity];
      for (const r of ranges) {
        result = breakLines(blocks, lineWidthFn, baseCostFn, r);
        console.log(`[typesetter] retry with cursorSearchRange=${r}: cost=${result.cost.toFixed(2)}`);
        if (result.cost < 1e17) break;
      }
    }

    // Debug: check if any breakpoint is at a hyphen block
    for (const bp of result.breakpoints) {
      if (bp > 0 && blocks[bp - 1].isHyphen) {
        console.log('[typesetter] hyphen break at block', bp - 1, blocks[bp - 1].text);
      }
    }
    console.log('[typesetter] lines:', result.breakpoints.length, 'cost:', result.cost.toFixed(2));

    renderParagraph(blocks, result.breakpoints, container, {
      ...(dropCapInfo ? {
        dropCap: {
          char: dropCapInfo.char,
          multiplier: dropCapInfo.multiplier,
          style: { ...BASE_STYLE, fontSize },
          width: dropCapInfo.width,
        },
        dropCapLines: dropCapInfo.lines,
      } : {}),
      indent: needsIndent,
      lineWidthFn,
    });
  }
}

// exported via `export async function typeset` above — esbuild --global-name handles the rest
export { parse } from './parser';
export { TextMeasurer } from './measure';
export { initHyphenation } from './hyphenate';
export { flattenToBlocks, extractDropCap } from './flatten';
export { breakLines, dropCapLineWidths, makeCostFn, DEFAULT_COST_PARAMS } from './linebreak';
export type { CostParams, CostFn } from './linebreak';
export { renderParagraph } from './renderer';
export type { TextStyle, LinebreakBlock } from './types';
