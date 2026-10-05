#!/usr/bin/env bash
# Toolchain environment for the remediation gates (docs/remediation/PLAN.md §4.3).
# Usage: source tools/env.sh
if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
  # shellcheck disable=SC1091
  source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
