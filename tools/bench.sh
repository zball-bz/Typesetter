#!/usr/bin/env bash
# Perf gate measurement (docs/remediation/PLAN.md §4.5): update and relayout
# latency at 7.8K / 35K / 87K, min of 3 run medians. Prints one compact line
# per (size, mode) plus the 87K phase breakdown; then the user-code variants
# (plan P2-02, MD-04) at 200 sections, update mode (VARIANTS=0 skips them).
set -eu
cd "$(dirname "$0")/.."
RUNS=${RUNS:-3}
line() {
    node tools/bench-edit.mjs --sections "$1" --mode "$2" --variant "$3" --runs "$RUNS" --json |
      node -e 'let d="";process.stdin.on("data",c=>d+=c).on("end",()=>{const r=JSON.parse(d);
        const p=r.phases||{};const f=(x)=>x===undefined?"-":x.toFixed(2);
        const v=r.variant&&r.variant!=="plain"?` ${r.variant}`:"";
        console.log(`${String(r.chars).padStart(6)} ${(r.mode+v).padEnd(8)} median ${f(r.median)} ms  cold ${f(r.cold)} ms` +
          (p.compileMs!==undefined?`  [compile ${f(p.compileMs)} execute ${f(p.executeMs)} ingest ${f(p.ingestMs)} engine ${f(p.engineMs)} render ${f(p.renderMs)}]`:""));});'
}
for s in 18 80 200; do
  for m in update relayout; do line "$s" "$m" plain; done
done
if [ "${VARIANTS:-1}" != 0 ]; then
  for v in splice region let syntax; do line 200 update "$v"; done
fi
