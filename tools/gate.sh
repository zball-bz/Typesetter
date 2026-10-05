#!/usr/bin/env bash
# Remediation gates (docs/remediation/PLAN.md §4.3).
#
#   tools/gate.sh --quick        G1 G2 G3 (+ G8 G9 G10 once their tools exist)
#   tools/gate.sh --full         every gate, including wasm, e2e and corpora
#   tools/gate.sh --only G1,G5   just the named gates
#
# Gates whose tool does not exist yet (G6 review-corpus, G8 fuzz, G9 gen-all,
# G10 lint-arch) are reported as "n/a" until the plan step that adds them.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2
# shellcheck disable=SC1091
source "$ROOT/tools/env.sh"

MODE=quick
ONLY=""
for a in "$@"; do
  case "$a" in
    --quick) MODE=quick ;;
    --full) MODE=full ;;
    --only) MODE=only ;;
    G*) ONLY="$a" ;;
    *) echo "usage: tools/gate.sh [--quick|--full|--only G1,G2,...]"; exit 2 ;;
  esac
done

declare -a RESULTS=()
FAIL=0

want() {
  local g=$1
  case "$MODE" in
    full) return 0 ;;
    quick) case "$g" in G1|G2|G3|G7|G8|G9|G10) return 0 ;; *) return 1 ;; esac ;;
    only) [[ ",$ONLY," == *",$g,"* ]] ;;
  esac
}

run() {
  local g=$1; shift
  local desc=$1; shift
  want "$g" || return 0
  local t0=$SECONDS
  local log="$ROOT/.gate-logs/$g.log"
  mkdir -p "$ROOT/.gate-logs"
  if "$@" >"$log" 2>&1; then
    RESULTS+=("PASS $g $desc ($((SECONDS - t0))s)")
  else
    RESULTS+=("FAIL $g $desc ($((SECONDS - t0))s) -> $log")
    FAIL=1
    tail -n 30 "$log"
  fi
}

na() { want "$1" && RESULTS+=("n/a  $1 $2"); return 0; }

g1() {
  cmake --build engine/build && ./engine/build/tsr_tests . || return 1
  node tools/check-tsrc.mjs --check  # tsrc --profile=golden reproduces every golden (P1-03)
}
g2() {
  grep -q '^CMAKE_BUILD_TYPE:STRING=Debug$' engine/build-debug/CMakeCache.txt ||
    { echo "engine/build-debug is not a Debug (sanitizer) build"; return 1; }
  cmake --build engine/build-debug -j"$(nproc)" && ./engine/build-debug/tsr_tests .
}
g3() { node tools/record-fixtures.mjs --check; }
g4() {
  cmake --build engine/build-wasm || return 1
  node tools/wasm-goldens.mjs --check || return 1  # WASM breaks == native goldens (P0-12)
  node tools/check-domains.mjs --check               # JS value validators == C++ (P1-02)
}
g5() {
  [ -d runtime/assets/hl ] || npm run hl-assets
  npx playwright test
}
g6() {
  npm run corpus || return 1
  if [ -f tools/review-corpus.mjs ]; then node tools/review-corpus.mjs --check; fi
}

run G1 "native build + goldens (+ contract checks G7)" g1
run G2 "ASan/UBSan debug goldens" g2
run G3 "recordings current" g3
run G4 "wasm build + WASM/native parity" g4
run G5 "playwright e2e" g5
run G6 "corpus + review corpus" g6
if [ -x tools/fuzz.sh ]; then run G8 "fuzz smoke" tools/fuzz.sh --smoke; else na G8 "fuzz smoke"; fi
if [ -f tools/gen-all.mjs ]; then run G9 "generated files fresh" node tools/gen-all.mjs --check; else na G9 "generated files fresh"; fi
if [ -f tools/lint-arch.mjs ]; then run G10 "architecture lint" node tools/lint-arch.mjs; else na G10 "architecture lint"; fi

printf '%s\n' "${RESULTS[@]}"
exit $FAIL
