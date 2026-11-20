#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
exe=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/nfcidm-test.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP TERM

run() {
  expected=$1
  shift
  rc=0
  "$exe" "$@" >"$work/out" 2>"$work/err" || rc=$?
  if [ "$rc" -ne "$expected" ]; then
    cat "$work/err" >&2
    echo "CLI test failed: expected $expected, got $rc ($*)" >&2
    exit 1
  fi
}

run 0
test "$(cat "$work/out")" = 0123456789ABCDEF
test ! -s "$work/err"
test "$(wc -c <"$work/out" | tr -d ' ')" = 17
run 0 --version
test "$(cat "$work/out")" = 'nfcidm 0.5.3'
run 64 --timeout -1
run 64 --reader 0
run 64 unexpected-argument
run 64 --timeout 999999999999999999999999
run 0 --help
run 0 --timeout=5
run 0 -t5
run 0 --reader=1
run 0 -r1
run 0 -v
# Verbose messages go only to stderr, not the captured IDm.
test "$(cat "$work/out")" = 0123456789ABCDEF
test -s "$work/err"
run 64 --timeout=nope
test ! -s "$work/out"
test -s "$work/err"
run 64 --list --reader=1
test ! -s "$work/out"

export NFCIDM_TEST_MODE=none
run 3
test ! -s "$work/out"
test -s "$work/err"
run 0 --list
test ! -s "$work/out"

export NFCIDM_TEST_MODE=multiple
run 64
test ! -s "$work/out"
run 0 --reader 2
test "$(cat "$work/out")" = 0123456789ABCDEF
run 64 --reader 3
run 0 --list
test "$(wc -l <"$work/out" | tr -d ' ')" = 2

export NFCIDM_TEST_MODE=bad-card
run 4
test ! -s "$work/out"
test -s "$work/err"

export NFCIDM_TEST_MODE=timeout
run 2 --timeout 1
test ! -s "$work/out"
test -s "$work/err"

# The fake backend ignores ordinary cancellation, as a stuck system driver
# might. The process-wide watchdog still has to return an error.
export NFCIDM_TEST_MODE=stall
run 2 --timeout 1
test ! -s "$work/out"
test -s "$work/err"
"$exe" --timeout 0 >"$work/out" 2>"$work/err" &
pid=$!
sleep 0.1
kill -INT "$pid"
rc=0
wait "$pid" || rc=$?
test "$rc" = 130
test ! -s "$work/out"

echo 'CLI tests passed'
