#!/usr/bin/env bash
# libFuzzer runs (docs/remediation/PLAN.md gate G8, testing.md §6).
#
#   tools/fuzz.sh --smoke        every target for 10 s (per plan step)
#   tools/fuzz.sh --long [MIN]   phase end: MIN minutes total (default 30),
#                                split evenly, at least 3 minutes per target
#   tools/fuzz.sh --target NAME [SECONDS]
#
# Seeds come from test/fixtures (*.tsm for front-end targets, *.ops for the
# ops reader). Working corpora live in .fuzz/ (gitignored); crashes are
# written to .fuzz/crashes/ and must become fixtures before they are fixed.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD=engine/build-fuzz
TARGETS=(fuzz_linepass fuzz_inline fuzz_opreader fuzz_settings fuzz_resanswer fuzz_lower fuzz_fragment)

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN=Ninja
  CC=clang CXX=clang++ cmake -S engine -B "$BUILD" -G "$GEN" -DTSR_FUZZ=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
fi
cmake --build "$BUILD" -j"$(nproc)" --target "${TARGETS[@]}" >/dev/null

seed() {
  local t=$1 dir=".fuzz/corpus/$t"
  mkdir -p "$dir" .fuzz/crashes
  local ext=tsm
  [ "$t" = fuzz_opreader ] && ext=ops
  if [ "$t" = fuzz_resanswer ]; then
    # well-formed answers (the JS codec): empty, a width row, a token row, a failed image
    node --input-type=module -e "
      import { encodeAnswer } from './runtime/src/shared/rescodec.mjs';
      import { writeFileSync } from 'node:fs';
      const seeds = {
        empty: { batch: 1, kinds: {} },
        width: { batch: 1, kinds: { textWidth: [{ resId: 0, px: 12.5 }], fontVmet: [{ resId: 0, asc: 14, desc: 4 }] } },
        tokens: { batch: 1, kinds: { codeTokens: [{ resId: 0, runs: [0, 1, 9, 2, 7, 1] }] } },
        image: { batch: 1, kinds: { boxInfo: [{ resId: 0, failed: true, msg: 'gone' }] } },
      };
      for (const [k, v] of Object.entries(seeds)) writeFileSync('$dir/seed-' + k + '.bin', encodeAnswer(v));
    "
    return
  fi
  if [ "$t" = fuzz_lower ]; then
    # the fixtures' LowerPrograms (tsrc --stage=program)
    local f
    for f in $(find test/fixtures -name "*.tsm" | sort); do
      engine/build/tsrc --stage=program "$f" > "$dir/$(echo "$f" | tr / _).bin" 2>/dev/null || true
    done
    return
  fi
  if [ "$t" = fuzz_settings ]; then
    # settings documents: the profiles, fixture settings and the full dump
    find test/profiles test/fixtures -name "*.json" -exec cp -n {} "$dir/" \; 2>/dev/null || true
    printf '{"host":{"width":420},"doc":{"lang":"en"},"code":{"fontFeaturesByLang":{"js":"\\"liga\\" 1"}}}' > "$dir/seed-mixed.json"
    return
  fi
  find test/fixtures -name "*.$ext" -exec cp -n {} "$dir/" \; 2>/dev/null || true
}

run() {
  local t=$1 secs=$2 rc=0
  seed "$t"
  echo "== $t (${secs}s)"
  "$BUILD/$t" ".fuzz/corpus/$t" -max_total_time="$secs" -timeout=10 -rss_limit_mb=2048 \
    -artifact_prefix=".fuzz/crashes/$t-" -print_final_stats=1 >".fuzz/$t.log" 2>&1 || rc=$?
  grep -E "^(#[0-9]+.*DONE|==[0-9]+==|SUMMARY|stat::number_of_executed_units|Done)" ".fuzz/$t.log" || true
  if [ "$rc" -ne 0 ]; then echo "FUZZ FINDING in $t (exit $rc): see .fuzz/$t.log and .fuzz/crashes/"; fi
  return "$rc"
}

MODE=${1:---smoke}
case "$MODE" in
  --smoke) for t in "${TARGETS[@]}"; do run "$t" 10; done ;;
  --long)
    MIN=${2:-30}
    per=$(( MIN * 60 / ${#TARGETS[@]} ))
    [ "$per" -lt 180 ] && per=180
    for t in "${TARGETS[@]}"; do run "$t" "$per"; done ;;
  --target) run "$2" "${3:-60}" ;;
  *) echo "usage: tools/fuzz.sh --smoke | --long [MIN] | --target NAME [SECONDS]"; exit 2 ;;
esac
