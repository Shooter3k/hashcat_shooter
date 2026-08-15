#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <hashcat executable>" >&2
  exit 2
fi

binary="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"

if [[ ! -x "$binary" ]]; then
  echo "executable not found: $binary" >&2
  exit 1
fi

test_dir="$(mktemp -d)"

cleanup ()
{
  rm -rf -- "$test_dir"
}

trap cleanup EXIT

cd "$test_dir"

secret_equals='automatic-report-secret-equals'
secret_separate='automatic-report-secret-separate'

set +e
"$binary" "--brain-password=$secret_equals" --brain-password "$secret_separate" --definitely-invalid > console.txt 2>&1
error_rc=$?
set -e

if [[ $error_rc -eq 0 ]]; then
  echo "invalid command unexpectedly succeeded" >&2
  exit 1
fi

shopt -s nullglob
reports=(shooter_hashcat-error-*.log)
shopt -u nullglob

if [[ ${#reports[@]} -ne 1 ]]; then
  echo "expected one error report, found ${#reports[@]}" >&2
  cat console.txt >&2
  exit 1
fi

report="${reports[0]}"

grep -Fq 'Error report saved to:' console.txt
grep -Fq 'shooter_hashcat automatic error report' "$report"
grep -Fq 'Version: ' "$report"
grep -Fq 'Platform: ' "$report"
grep -Fq 'Arguments:' "$report"
grep -Fq 'Recent warnings before the first error' "$report"
grep -Fq '[REDACTED]' "$report"
grep -Fq 'Unknown or invalid command-line option: --definitely-invalid' "$report"

if grep -Fq "$secret_equals" "$report" || grep -Fq "$secret_separate" "$report"; then
  echo "secret command-line value was not redacted" >&2
  exit 1
fi

report_count_before=${#reports[@]}

"$binary" --version > version.txt

shopt -s nullglob
reports_after_success=(shooter_hashcat-error-*.log)
shopt -u nullglob

if [[ ${#reports_after_success[@]} -ne $report_count_before ]]; then
  echo "a successful command created an error report" >&2
  exit 1
fi

echo "Automatic error report test passed: $report"
