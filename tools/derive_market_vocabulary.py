#!/usr/bin/env python3
"""Derive check E's market vocabulary from git history.  (ENC-1579)

Purity SPEC D11.3: the vocabulary is derived, never a hand list.  Every word
is read out of the market code as it stood in the last krn commit that had
it, by `git show <sha>:<path>`, so it needs this repo's history.

    derive_market_vocabulary.py            print the list
    derive_market_vocabulary.py --write    rewrite tools/market_vocabulary.txt
    derive_market_vocabulary.py --check    exit 1 if the committed list differs

Exit 2 if the history cannot be read or a source yields too few words.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
VOCAB_FILE = os.path.join(HERE, "market_vocabulary.txt")

# b68c7d1 (ENC-1570) deleted connectors/market; its parent is the last commit
# with the market code.  edd9adb (ENC-1573) deleted data/; its parent is the
# last with RecordFieldSource and its k* literals.
MARKET_SHA = "e010bc65c8693ba8c43c9c5b1ae8b1117ac36473"
RECORD_SHA = "c32a60058718c3dfd962787cc15d8a07cf5c17f0"

FIELD_MAP = "connectors/market/include/krn/market/MarketFieldMap.hpp"
CONNECTOR = "connectors/market/src/MarketConnector.cpp"
OB_KEY = "connectors/market/src/ob/ObKey.cpp"
MARKET_TA = "connectors/market/src/MarketTA.cpp"
RECORD_FS = "data/include/krn/data/RecordFieldSource.hpp"
MARKET_DIR = "connectors/market"
ENGINE_DIRS = ("include", "src")

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class DeriveError(Exception):
    pass


def git(*args: str) -> str:
    try:
        res = subprocess.run(["git", "-C", REPO, *args], capture_output=True,
                             text=True, check=False)
    except OSError as exc:
        raise DeriveError(f"cannot run git: {exc}") from exc
    if res.returncode != 0:
        raise DeriveError(f"git {' '.join(args)}: {res.stderr.strip()}")
    return res.stdout


def show(sha: str, path: str) -> str:
    return git("show", f"{sha}:{path}")


def tree(sha: str, prefix: str) -> list[str]:
    return git("ls-tree", "-r", "--name-only", sha, "--", prefix).split()


def literals(text: str) -> list[str]:
    return re.findall(r'"([^"\\\n]*)"', text)


def fieldmap_words() -> set[str]:
    """MarketFieldMap's key table: its keys, and the aliases they default to."""
    hdr = show(MARKET_SHA, FIELD_MAP)
    body = re.search(r"struct MarketFieldMap\s*\{(.*?)\n\};", hdr, re.S)
    if not body:
        raise DeriveError(f"{FIELD_MAP}: no struct MarketFieldMap")
    out = set(re.findall(r"^\s*(?:std::\w+(?:<[^;]*>)?|bool)\s+(\w+)",
                         body.group(1), re.M))
    out |= set(literals(body.group(1)))
    fn = re.search(r"bool applyMarketSourceKey\(.*?\n\}", show(MARKET_SHA,
                                                             CONNECTOR), re.S)
    if not fn:
        raise DeriveError(f"{CONNECTOR}: no applyMarketSourceKey")
    out |= set(re.findall(r'tail == "(\w+)"', fn.group(0)))
    out |= {w for w in literals(fn.group(0)) if w not in ("true", "1", "yes")}
    return out


def obkey_words() -> set[str]:
    """parseObKey's grammar: every token it compares against, and `ob`."""
    src = show(MARKET_SHA, OB_KEY)
    m = re.search(r"\nstd::optional<ObKey> parseObKey\(.*?\n\}", src, re.S)
    if not m:
        raise DeriveError(f"{OB_KEY}: no parseObKey")
    # The helpers it calls (parseSide/parseTarget/parseReduce) hold the rest
    # of the grammar's leaves; they sit in the anonymous namespace above it.
    helpers = re.search(r"namespace \{(.*?)\n\} // anon", src, re.S)
    if not helpers:
        raise DeriveError(f"{OB_KEY}: no anonymous-namespace helpers")
    text = m.group(0) + helpers.group(1)
    return {w for w in literals(text) if w and IDENT.match(w)}


def k_literal_words() -> set[str]:
    """RecordFieldSource's k* constants: the names and the spellings."""
    out: set[str] = set()
    for name, value in re.findall(
            r'inline const std::string (k\w+)\s*=\s*"([^"]+)";',
            show(RECORD_SHA, RECORD_FS)):
        out |= {name, value}
    return out


def atomic_name_words() -> set[str]:
    """The names MarketTickComputer publishes and skips: the atomic keys the
    market put next to FunctionMap's.  A key built as "sma_" + period is
    recorded as the prefix `sma_`."""
    src = show(MARKET_SHA, MARKET_TA)
    out = set(re.findall(r'emplace_back\("(\w+)"', src))
    out |= set(re.findall(r'store->set\(symbol,\s*"(\w+)"', src))
    skip = re.search(r"void initSkipFields\(.*?\n\}", src, re.S)
    if not skip:
        raise DeriveError(f"{MARKET_TA}: no initSkipFields")
    out |= set(literals(skip.group(0)))
    return out


DEF_RE = re.compile(
    r"^(?:template\s*<[^>]*>\s*)?(?:class|struct|enum\s+class|enum)\s+"
    r"(?:\w+::)*(\w+)\b[^;{]*\{", re.M)


def defined_types(sha: str, paths: list[str]) -> set[str]:
    out: set[str] = set()
    for p in paths:
        if p.endswith((".hpp", ".cpp", ".h")):
            out |= set(DEF_RE.findall(show(sha, p)))
    return out


def market_type_words() -> set[str]:
    """Types the market code defines and the engine did not, at MARKET_SHA.
    The title of check E ("names no market-data type") needs these; the
    subtraction keeps a forward declaration or a shared name out."""
    market = defined_types(MARKET_SHA, tree(MARKET_SHA, MARKET_DIR))
    engine = defined_types(MARKET_SHA, [p for d in ENGINE_DIRS
                                        for p in tree(MARKET_SHA, d)])
    return market - engine


# (tag, function, floor).  A floor is about two thirds of the count measured
# at the SHAs above, so a broken regex is caught, not a reworded comment.
SOURCES = (
    ("fieldmap", fieldmap_words, 12),
    ("obkey", obkey_words, 18),
    ("k_literal", k_literal_words, 8),
    ("atomic", atomic_name_words, 14),
    ("type", market_type_words, 25),
)


def derive() -> dict[str, set[str]]:
    """word -> the source tags it came from."""
    words: dict[str, set[str]] = {}
    for tag, fn, floor in SOURCES:
        got = {w for w in fn() if IDENT.match(w)}
        if len(got) < floor:
            raise DeriveError(f"source {tag} yielded {len(got)} words "
                              f"(floor {floor}) -- the extraction is broken")
        for w in got:
            words.setdefault(w, set()).add(tag)
    return words


def render(words: dict[str, set[str]]) -> str:
    lines = [
        "# check E's market vocabulary (ENC-1579).  GENERATED -- do not edit;",
        "# tools/derive_market_vocabulary.py --write regenerates it, and",
        "# check_engine_isolation.py --selftest fails if it differs.",
        f"# market code: krn {MARKET_SHA} (b68c7d1^)",
        f"# k* literals: krn {RECORD_SHA} (edd9adb^)",
        "# word<TAB>sources.  A word ending in _ is a prefix (sma_ -> sma_20).",
    ]
    for w in sorted(words, key=lambda s: (s.lower(), s)):
        lines.append(f"{w}\t{','.join(sorted(words[w]))}")
    return "\n".join(lines) + "\n"


def drift(path: str, derived: str) -> str | None:
    """Why the file at `path` is not `derived`, or None when it is."""
    try:
        with open(path, encoding="utf-8") as fh:
            committed = fh.read()
    except OSError as exc:
        return f"cannot read {path}: {exc}"
    if committed == derived:
        return None
    have = {ln for ln in committed.split("\n") if not ln.startswith("#")}
    want = {ln for ln in derived.split("\n") if not ln.startswith("#")}
    return (f"{path} differs from its derivation: missing "
            f"{sorted(want - have)[:5]}, extra {sorted(have - want)[:5]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    try:
        text = render(derive())
    except DeriveError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if args.write:
        with open(VOCAB_FILE, "w", encoding="utf-8") as fh:
            fh.write(text)
        return 0
    if args.check:
        why = drift(VOCAB_FILE, text)
        if why:
            print(why, file=sys.stderr)
            return 1
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
