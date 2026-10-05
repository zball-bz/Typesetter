#!/usr/bin/env bash
# Perf gate measurement (docs/remediation/PLAN.md §4.5): update and relayout
# latency at 7.8K / 35K / 87K, min of 3 run medians. Prints one compact line
# per (size, mode) plus the 87K phase breakdown.
set -eu
cd "$(dirname "$0")/.."
RUNS=${RUNS:-3}
for s in 18 80 200; do
  for m in update relayout; do
    node tools/bench-edit.mjs --sections "$s" --mode "$m" --runs "$RUNS" --json |
      node -e 'let d="";process.stdin.on("data",c=>d+=c).on("end",()=>{const r=JSON.parse(d);
        const p=r.phases||{};const f=(x)=>x===undefined?"-":x.toFixed(2);
        console.log(`${String(r.chars).padStart(6)} ${r.mode.padEnd(8)} median ${f(r.median)} ms  cold ${f(r.cold)} ms` +
          (p.compileMs!==undefined?`  [compile ${f(p.compileMs)} execute ${f(p.executeMs)} ingest ${f(p.ingestMs)} engine ${f(p.engineMs)} render ${f(p.renderMs)}]`:""));});'
  done
done
