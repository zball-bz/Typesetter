import { flattenToBlocks } from './flatten';
import { breakLines, makeCostFn, CostParams } from './linebreak';
import { TextMeasurer } from './measure';
import { parse } from './parser';
import { initHyphenation } from './hyphenate';

const COST_PARAMS: CostParams = {
  exponent: 3,
  shrinkThreshold: 0.37,
  shrinkCoeff: 0.6,
};
const HYPHEN_PENALTY = 0.70;

const SHORT_TEXT = 'The amount of parallelism that can be achieved depends on the maximum size of integers available in the language and the processor. In JavaScript bit-wise operations currently treat all numbers as 32-bit integers.';

const MEDIUM_TEXT = 'In the beginning was the Word, and the Word was set in lead. For centuries, the craft of typography was the province of specialists: compositors who arranged individual metal sorts into lines, locked them into chases, and impressed them onto paper. The result was text of extraordinary beauty, with even spacing, graceful hyphenation, and a rhythm that guided the eye effortlessly from line to line. This tradition reached its mathematical apotheosis in Donald Knuth\'s TeX system, which formalized the problem of line breaking as an optimization over all possible configurations of a paragraph.';

const LONG_TEXT = `${MEDIUM_TEXT} The web, by contrast, has always taken a greedy approach. Browsers flow text word by word, breaking each line at the first opportunity that avoids overflow. The result is adequate but never beautiful: rivers of whitespace cascade through justified paragraphs, hyphens appear where they shouldn't, and the subtle irregularities accumulate into a reading experience that feels subtly wrong, even to readers who cannot articulate why. This project attempts to bridge that gap. By implementing the Knuth-Plass algorithm in JavaScript, measuring text with subpixel precision using the Canvas API, and applying Liang's hyphenation patterns from TeX itself, we can produce paragraphs on the web that approach the quality of professionally typeset books. The algorithm considers every possible arrangement of words across lines and selects the one that minimizes a global penalty function, trading off inter-word spacing, hyphenation frequency, and the aesthetic cost of consecutive short or long lines.`;

const STRESS_TEXT = 'Electroencephalographically speaking, the psychophysiological characteristics of magnetohydrodynamics remain underdetermined. Nevertheless, the counterrevolutionary implications of such unconstitutionally disproportionate methodologies are well-established in the peer-reviewed literature on otorhinolaryngological spectrophotofluorometry. The fine-grained, well-established, nineteenth-century hand-crafted type-setting traditions of pre-industrial self-taught compositor-printers were quasi-revolutionary in their day-to-day problem-solving approaches to so-called best-practice anti-aliasing.';

interface BenchCase {
  name: string;
  text: string;
  width: number;
}

const cases: BenchCase[] = [
  { name: 'Short / wide (580px)', text: SHORT_TEXT, width: 580 },
  { name: 'Short / narrow (300px)', text: SHORT_TEXT, width: 300 },
  { name: 'Medium / wide (580px)', text: MEDIUM_TEXT, width: 580 },
  { name: 'Medium / narrow (300px)', text: MEDIUM_TEXT, width: 300 },
  { name: 'Long / wide (580px)', text: LONG_TEXT, width: 580 },
  { name: 'Long / narrow (300px)', text: LONG_TEXT, width: 300 },
  { name: 'Long / tiny (180px)', text: LONG_TEXT, width: 180 },
  { name: 'Stress / wide (580px)', text: STRESS_TEXT, width: 580 },
  { name: 'Stress / narrow (300px)', text: STRESS_TEXT, width: 300 },
  { name: 'Stress / tiny (180px)', text: STRESS_TEXT, width: 180 },
];

const WARMUP = 50;
const ITERATIONS = 1000;

// Mock measurer for Node (no Canvas) — approximate char widths
class MockMeasurer {
  measureWord(text: string, style: any): number {
    const size = style.fontSize || 18;
    let w = 0;
    for (const ch of text) {
      if (ch === ' ') w += 0.25;
      else if (ch === 'i' || ch === 'l' || ch === '.' || ch === ',') w += 0.3;
      else if (ch === 'm' || ch === 'w' || ch === 'M' || ch === 'W') w += 0.75;
      else if (ch >= 'A' && ch <= 'Z') w += 0.6;
      else w += 0.5;
    }
    return w * size;
  }
  measureChar(ch: string, style: any): number {
    return this.measureWord(ch, style);
  }
}

async function main() {
  await initHyphenation();
  const measurer = new MockMeasurer() as any;
  const costFn = makeCostFn(COST_PARAMS);

  console.log('=== Line Breaking Benchmark ===');
  console.log(`Iterations: ${ITERATIONS} (warmup: ${WARMUP})`);
  console.log(`Cost params: n=${COST_PARAMS.exponent}, shrink=${COST_PARAMS.shrinkThreshold}, c=${COST_PARAMS.shrinkCoeff}`);
  console.log(`Hyphen penalty: ${HYPHEN_PENALTY}`);
  console.log('');

  // Pre-flatten all cases
  const prepared = cases.map(c => {
    const ast = parse(c.text);
    const allNodes = ast.flatMap(p => p.children);
    const blocks = flattenToBlocks(allNodes, measurer, 18, HYPHEN_PENALTY);
    return { ...c, blocks };
  });

  // Print block stats
  console.log('--- Block Stats ---');
  for (const p of prepared) {
    const hyphens = p.blocks.filter(b => b.isHyphen).length;
    const spaces = p.blocks.filter(b => b.isSpace).length;
    const words = p.blocks.filter(b => !b.isSpace && !b.isHyphen).length;
    console.log(`  ${p.name}: ${p.blocks.length} blocks (${words} phonemes, ${spaces} spaces, ${hyphens} hyphens)`);
  }
  console.log('');

  // Benchmark
  console.log('--- Results ---');
  console.log(`${'Case'.padEnd(30)} ${'Blocks'.padStart(6)} ${'Lines'.padStart(5)} ${'Cost'.padStart(10)} ${'Avg (µs)'.padStart(10)} ${'Min (µs)'.padStart(10)} ${'Max (µs)'.padStart(10)} ${'ops/s'.padStart(10)}`);

  for (const p of prepared) {
    const lineWidthFn = () => p.width;

    // Warmup
    for (let i = 0; i < WARMUP; i++) {
      breakLines(p.blocks, lineWidthFn, costFn);
    }

    // Timed runs
    const times: number[] = [];
    let result: any;
    for (let i = 0; i < ITERATIONS; i++) {
      const t0 = performance.now();
      result = breakLines(p.blocks, lineWidthFn, costFn);
      const t1 = performance.now();
      times.push((t1 - t0) * 1000); // µs
    }

    times.sort((a, b) => a - b);
    const avg = times.reduce((s, t) => s + t, 0) / times.length;
    const min = times[0];
    const max = times[times.length - 1];
    const median = times[Math.floor(times.length / 2)];
    const ops = 1_000_000 / avg;

    console.log(
      `  ${p.name.padEnd(28)} ${String(p.blocks.length).padStart(6)} ${String(result.breakpoints.length).padStart(5)} ${result.cost.toFixed(4).padStart(10)} ${avg.toFixed(1).padStart(10)} ${min.toFixed(1).padStart(10)} ${max.toFixed(1).padStart(10)} ${Math.round(ops).toLocaleString().padStart(10)}`
    );
  }

  console.log('');
  console.log('Done.');
}

main().catch(console.error);
