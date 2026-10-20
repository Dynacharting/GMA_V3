#!/usr/bin/env bash
# ENC-1514 -- the GMA -> KRN rename's false-positive gate.
#
# specs/2026-10-15-gma-to-krn-rename/SPEC.md D5: `gma` is a substring of
# pragma, sigma, gmail, argmax and magma, so a `s/gma/krn/g` sweep turns every
# `#pragma once` in this repo into `#prakrn once`.  Every ticket in that
# project (ENC-1514..ENC-1518) carries the same acceptance criterion: the
# post-change counts of those words are unchanged.  This is that check,
# runnable by each of them.
#
# A corrupted `#pragma` fails loudly at compile time.  A corrupted `sigma`,
# `argmax` or doc mention does not, which is why this exists as a count rather
# than being left to the build.
#
#   tools/check_forbidden_words.sh            # judge this working tree
#   tools/check_forbidden_words.sh --selftest # prove the judgement is not vacuous
#
# Exit 0 clean, 1 on a real corruption, 2 when it could not run.

set -u
cd "$(dirname "$0")/.." || { echo "could not reach repo root" >&2; exit 2; }
git rev-parse --git-dir >/dev/null 2>&1 || { echo "not a git repo" >&2; exit 2; }

# Baseline measured on master 2b405ee (2026-10-20), tracked files only.
# A legitimate new header raises `pragma`; update the number here in the same
# commit that adds it, and say so.  Do not update it to make a sweep pass.
EXPECTED_pragma=107
EXPECTED_sigma=0
EXPECTED_gmail=0
EXPECTED_argmax=8
EXPECTED_magma=0
EXPECTED_enigma=0
EXPECTED_dogma=0
EXPECTED_stigma=0
WORDS="pragma sigma gmail argmax magma enigma dogma stigma"

# What a naive s/gma/krn/g leaves behind.  Zero of these may exist, whatever
# the counts say -- this half survives a baseline somebody edited.
CORRUPTIONS="prakrn sikrn krnil arkrnx makrn enikrn dokrn stikrn"

count() { git grep -I -h -o -i -F -e "$1" -- . 2>/dev/null | wc -l; }

fail=0
for w in $WORDS; do
  eval "want=\$EXPECTED_$w"
  got=$(count "$w")
  if [ "$got" = "$want" ]; then
    printf '  ok    %-8s %s\n' "$w" "$got"
  else
    printf '  FAIL  %-8s %s (expected %s)\n' "$w" "$got" "$want"
    fail=1
  fi
done
for c in $CORRUPTIONS; do
  got=$(count "$c")
  [ "$got" = 0 ] || { printf '  FAIL  %-8s %s occurrence(s) -- a substring sweep ran\n' "$c" "$got"; fail=1; }
done

# `#pragma once` is the one with a second, independent invariant: every file
# that has one has exactly one, so occurrences must equal matching files.
po=$(git grep -I -h -o -F -e '#pragma once' -- . 2>/dev/null | wc -l)
pf=$(git grep -I -l -F -e '#pragma once' -- . 2>/dev/null | wc -l)
if [ "$po" = "$pf" ]; then
  printf '  ok    #pragma once  %s occurrences in %s files\n' "$po" "$pf"
else
  printf '  FAIL  #pragma once  %s occurrences in %s files\n' "$po" "$pf"
  fail=1
fi

if [ "${1-}" = "--selftest" ]; then
  # Positive control.  Without this the check cannot tell a clean tree from an
  # instrument that counts nothing: ENC-1507 is open on exactly that defect in
  # the sibling checker, and ENC-1289 shipped a wrong claim from one empty
  # traversal with no control.
  echo "--- selftest: inject the corruption this gate exists to catch ---"
  victim=$(git grep -I -l -F -e '#pragma once' -- . 2>/dev/null | head -1)
  [ -n "$victim" ] || { echo "  FAIL  selftest found no #pragma once to corrupt"; exit 2; }
  [ -z "$(git status --porcelain)" ] || { echo "  FAIL  selftest needs a clean tree"; exit 2; }
  saved=$(mktemp) || exit 2
  cp "$victim" "$saved"
  sed -i 's/gma/krn/g' "$victim"
  if ! grep -q '^#prakrn once' "$victim"; then
    cp "$saved" "$victim"; rm -f "$saved"
    echo "  FAIL  selftest mutation did not apply -- a no-op mutation reads as a pass"
    exit 2
  fi
  echo "  mutation applied to $victim (#pragma once -> #prakrn once)"
  bad_pragma=$(count pragma)
  bad_corrupt=$(count prakrn)
  cp "$saved" "$victim"; rm -f "$saved"
  [ -z "$(git status --porcelain)" ] || { echo "  FAIL  selftest could not restore $victim"; exit 2; }
  if [ "$bad_pragma" != "$EXPECTED_pragma" ] && [ "$bad_corrupt" != 0 ]; then
    printf '  ok    gate detected it: pragma %s (expected %s), prakrn %s\n' \
      "$bad_pragma" "$EXPECTED_pragma" "$bad_corrupt"
    echo "  restored; tree clean"
  else
    printf '  FAIL  gate did NOT detect it: pragma %s, prakrn %s\n' "$bad_pragma" "$bad_corrupt"
    exit 2
  fi
fi

[ "$fail" = 0 ] && echo "forbidden words: unchanged" || echo "forbidden words: CORRUPTED"
exit "$fail"
