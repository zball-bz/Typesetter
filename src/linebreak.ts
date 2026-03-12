import { LinebreakBlock } from './types';

const INF = 1e18;

export interface BreakResult {
  breakpoints: number[];
  cost: number;
}

export interface CostParams {
  exponent: number;       // n in f(x)^n
  shrinkThreshold: number; // max negative ratio before INF (e.g. 0.2)
  shrinkCoeff: number;     // c multiplier on the mapped shrink value
}

export const DEFAULT_COST_PARAMS: CostParams = {
  exponent: 3,
  shrinkThreshold: 0.37,
  shrinkCoeff: 0.6,
};

export type CostFn = (totalWidth: number, lineWidth: number, totalSpace: number) => number;

export function makeCostFn(params: CostParams): CostFn {
  const { exponent, shrinkThreshold, shrinkCoeff } = params;
  return function(totalWidth: number, lineWidth: number, totalSpace: number): number {
    const slack = lineWidth - totalWidth;
    const space = Math.max(totalSpace, 0.001);
    const x = slack / space; // positive = stretch, negative = shrink
    if (x < -shrinkThreshold) return INF;
    let mapped: number;
    if (x < 0) {
      // shrink: map to equivalent stretch perception
      // 1/(1+x) - 1, e.g. x=-0.1 -> 0.1111
      mapped = shrinkCoeff * (1 / (1 + x) - 1);
    } else {
      mapped = x;
    }
    return Math.pow(mapped, exponent);
  };
}

export const defaultCostFn = makeCostFn(DEFAULT_COST_PARAMS);

export function breakLines(
  blocks: LinebreakBlock[],
  lineWidths: (lineNum: number) => number,
  costFn: CostFn = defaultCostFn,
  cursorSearchRange: number = 5
): BreakResult {
  const n = blocks.length;
  if (n === 0) return { breakpoints: [], cost: 0 };

  // Prepend virtual start block
  const vBlock: LinebreakBlock = {
    width: 0, breakPenalty: 0, breakWidth: 0, spaceWidth: 0,
    text: '', style: blocks[0].style, isSpace: false, isHyphen: false,
  };
  const allBlocks = [vBlock, ...blocks];
  const N = allBlocks.length;

  // Prefix sums
  const widthPsum = new Float64Array(N + 1);
  const swidthPsum = new Float64Array(N + 1);
  for (let i = 0; i < N; i++) {
    widthPsum[i + 1] = widthPsum[i] + allBlocks[i].width;
    swidthPsum[i + 1] = swidthPsum[i] + allBlocks[i].spaceWidth;
  }

  const dp: Array<Array<{ line: number; val: number }>> = new Array(N);
  const dpRes: Array<Array<{ line: number; parent: number }>> = new Array(N);
  for (let i = 0; i < N; i++) {
    dp[i] = [];
    dpRes[i] = [];
  }

  // Base: virtual block 0
  dp[0] = [{ line: 0, val: 0 }];

  // Precompute breakable positions
  const bkIdx: number[] = [];
  for (let i = 0; i < N; i++) {
    if (allBlocks[i].breakPenalty < INF) bkIdx.push(i);
  }
  const B = bkIdx.length;

  let cursorB = 0; // cursor in breakable-index space

  for (let bi = 1; bi < B; bi++) {
    const i = bkIdx[bi];

    const loB = Math.max(0, cursorB - cursorSearchRange);
    const hiB = Math.min(bi - 1, cursorB + cursorSearchRange);

    const dps = new Map<number, number>();
    const dpp = new Map<number, number>();
    let dpmin = INF;
    let dpminLine = -1;
    let dpminParentB = cursorB;

    for (let bj = loB; bj <= hiB; bj++) {
      const j = bkIdx[bj];
      if (dp[j].length === 0) continue;

      for (const { line: k, val } of dp[j]) {
        const contentW = widthPsum[i + 1] - widthPsum[j + 1] + allBlocks[i].breakWidth;
        const L = lineWidths(k);
        const totalSpace = swidthPsum[i + 1] - swidthPsum[j + 1];

        const lineCost = costFn(contentW, L, totalSpace);
        if (lineCost >= INF) continue;

        const dpNext = val + lineCost + allBlocks[i].breakPenalty;
        const nextLine = k + 1;

        const existing = dps.get(nextLine);
        if (existing === undefined || existing > dpNext) {
          dps.set(nextLine, dpNext);
          dpp.set(nextLine, j);
          if (dpNext < dpmin) {
            dpmin = dpNext;
            dpminLine = nextLine;
            dpminParentB = bj;
          }
        }
      }
    }

    // Prune: keep line counts within ±1 of best
    if (dpminLine >= 0) {
      for (const [k, val] of dps) {
        if (k >= dpminLine - 1 && k <= dpminLine + 1) {
          dp[i].push({ line: k, val });
          dpRes[i].push({ line: k, parent: dpp.get(k)! });
        }
      }
      cursorB = dpminParentB;
    }
  }

  // Find best endpoint
  let bestVal = INF;
  let bestIdx = -1;
  let bestLine = -1;

  for (let i = N - 1; i >= 1; i--) {
    if (allBlocks[i].breakPenalty >= INF) continue;
    if (dp[i].length === 0) continue;

    for (const { line: k, val } of dp[i]) {
      const remainW = widthPsum[N] - widthPsum[i + 1];
      const L = lineWidths(k);
      if (i === N - 1 || remainW <= L) {
        if (val < bestVal) {
          bestVal = val;
          bestIdx = i;
          bestLine = k;
        }
      }
    }
  }

  if (bestIdx < 0) {
    return { breakpoints: [n], cost: INF };
  }

  // Traceback: convert from allBlocks indices (offset by 1) to original block indices
  const breaks: number[] = [];

  // Last line: from bestIdx+1 to N-1 (in allBlocks), which is bestIdx..n-1 in original
  if (bestIdx < N - 1) {
    breaks.push(n); // end of original blocks
  }
  breaks.push(bestIdx); // in allBlocks, = original index bestIdx-1+1 = bestIdx

  let curIdx = bestIdx;
  let curLine = bestLine;

  while (curLine > 0) {
    const entry = dpRes[curIdx].find(e => e.line === curLine);
    if (!entry || entry.parent <= 0) break;
    curIdx = entry.parent;
    curLine--;
    breaks.push(curIdx); // allBlocks index = original index + 1... but 0 is virtual
  }

  breaks.reverse();

  // Convert allBlocks indices to original block indices:
  // allBlocks[i] corresponds to original blocks[i-1]
  // A break "after allBlocks[i]" means items up to original blocks[i-1] inclusive
  // So breakpoint in original = i (number of original blocks consumed)
  const breakpoints = [...new Set(breaks)].sort((a, b) => a - b);

  // Ensure ends with n
  if (breakpoints[breakpoints.length - 1] !== n) {
    breakpoints.push(n);
  }

  return { breakpoints, cost: bestVal };
}

export function dropCapLineWidths(
  containerWidth: number,
  dropCapWidth: number,
  dropCapLines: number
): (lineNum: number) => number {
  return (lineNum: number) => {
    if (lineNum < dropCapLines) {
      return containerWidth - dropCapWidth;
    }
    return containerWidth;
  };
}
