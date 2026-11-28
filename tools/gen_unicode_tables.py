#!/usr/bin/env python3
"""gen_unicode_tables - emit kernel/unicode/tables.h + tables.cc.

Generates compact binary-search tables for the kernel unicode module
(hype::unicode) from the host Python unicodedata database:

  * simple uppercase / lowercase case mappings
  * full (multi-codepoint) upper / lower / casefold sequences
  * canonical decompositions (NFD building blocks)
  * canonical composition pairs (NFC)
  * canonical combining classes (non-zero only)
  * combining-mark ranges (general categories Mn / Mc / Me)

Coverage (ICU-subset): Latin-1 supplement, Latin Extended-A/B, IPA
extensions, combining diacritical marks, Greek, Cyrillic (+supplement),
Latin Extended Additional, combining marks for symbols, Latin Extended-C,
variation selectors, plus the letterlike singletons U+2126/212A/212B.

Regenerate with:  python tools/gen_unicode_tables.py
"""
import unicodedata as ud
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "kernel" / "unicode" / "tables.h"
OUT_CC = ROOT / "kernel" / "unicode" / "tables.cc"

COVERAGE = [
    (0x00A0, 0x024F),   # Latin-1 supplement + Latin Ext-A + Latin Ext-B
    (0x0250, 0x02AF),   # IPA extensions (case partners of Ext-B)
    (0x0300, 0x036F),   # combining diacritical marks
    (0x0370, 0x03FF),   # Greek and Coptic (monotonic)
    (0x0400, 0x052F),   # Cyrillic + Cyrillic supplement
    (0x1E00, 0x1EFF),   # Latin extended additional
    (0x20D0, 0x20FF),   # combining marks for symbols
    (0x2C60, 0x2C7F),   # Latin extended-C
    (0xFE00, 0xFE0F),   # variation selectors
    (0xFE20, 0xFE2F),   # combining half marks
]
SINGLES = [0x2126, 0x212A, 0x212B]  # OHM / KELVIN / ANGSTROM signs

# cps whose full uppercase/lowercase is multi-codepoint but that carry a
# real *simple* mapping in UnicodeData.txt (everything else multi maps to
# itself in the simple table).
SIMPLE_UPPER_OVERRIDE = {0x01F0: 0x004A}   # U+01F0 J WITH CARON -> 'J'
SIMPLE_LOWER_OVERRIDE = {}


def in_coverage(cp: int) -> bool:
    return any(lo <= cp <= hi for lo, hi in COVERAGE) or cp in SINGLES


def covered_cps():
    for lo, hi in COVERAGE:
        for cp in range(lo, hi + 1):
            yield cp
    yield from SINGLES


def seq_of(s: str):
    return [ord(c) for c in s]


def simple_map(cp: int, full, override):
    if len(full) == 1:
        return ord(full)
    return override.get(cp, cp)


def main() -> int:
    case_map = {}       # cp -> (simple_upper, simple_lower)
    upper_seq = {}      # cp -> [cps]   (full upper, only when != [simple])
    lower_seq = {}      # cp -> [cps]   (full lower, only when != [simple])
    fold_seq = {}       # cp -> [cps]   (full fold,  only when != [lower])
    decomp = {}         # cp -> [cps]   (canonical decomposition)
    ccc = {}            # cp -> combining class (>0)
    marks = []          # cps in Mn / Mc / Me
    compose = {}        # (first, second) -> composed

    for cp in covered_cps():
        ch = chr(cp)
        su = simple_map(cp, ch.upper(), SIMPLE_UPPER_OVERRIDE)
        sl = simple_map(cp, ch.lower(), SIMPLE_LOWER_OVERRIDE)
        if su != cp or sl != cp:
            case_map[cp] = (su, sl)
        fu = seq_of(ch.upper())
        fl = seq_of(ch.lower())
        ff = seq_of(ch.casefold())
        if fu != [su]:
            upper_seq[cp] = fu
        if fl != [sl]:
            lower_seq[cp] = fl
        if ff != [sl]:
            fold_seq[cp] = ff
        d = ud.decomposition(ch)
        if d and not d.startswith("<"):
            seq = [int(t, 16) for t in d.split()]
            assert len(seq) <= 3, hex(cp)
            decomp[cp] = seq
        if ud.combining(ch) > 0:
            ccc[cp] = ud.combining(ch)

    # ccc + mark coverage is global (BMP): grapheme advance and canonical
    # ordering must classify marks in every script, even ones outside the
    # case/decomposition coverage above.
    for cp in range(0x10000):
        ch = chr(cp)
        if ud.combining(ch) > 0:
            ccc[cp] = ud.combining(ch)
        if ud.category(ch) in ("Mn", "Mc", "Me"):
            marks.append(cp)

    # closure: outputs may reference cps whose ccc/mark status matters even
    # if the cp itself is not in the primary coverage (e.g. decomp targets).
    for seq in list(upper_seq.values()) + list(lower_seq.values()) + \
            list(fold_seq.values()) + list(decomp.values()):
        for cp in seq:
            if ud.combining(chr(cp)) > 0:
                ccc[cp] = ud.combining(chr(cp))
            if ud.category(chr(cp)) in ("Mn", "Mc", "Me") and cp not in marks:
                marks.append(cp)
            d2 = ud.decomposition(chr(cp))
            if d2 and not d2.startswith("<"):
                assert in_coverage(cp), (
                    "decomp target U+%04X outside coverage has own decomp" % cp)

    # composition pairs from canonical decompositions (skip exclusion
    # cases, which unicodedata tells us via the NFC round-trip).
    for cp, seq in decomp.items():
        recomposed = ud.normalize("NFC", "".join(chr(c) for c in seq))
        if len(recomposed) == 1 and ord(recomposed) == cp:
            if len(seq) == 2:
                compose[(seq[0], seq[1])] = cp
            elif len(seq) == 3:
                head = ud.normalize("NFC", "".join(chr(c) for c in seq[:2]))
                assert len(head) == 1, hex(cp)
                compose[(ord(head), seq[2])] = cp
    for (a, b), c in compose.items():
        assert ud.normalize("NFC", chr(a) + chr(b)) == chr(c), \
            (hex(a), hex(b), hex(c))
        assert all(x < 0x10000 for x in (a, b, c))

    # merge consecutive marks into ranges
    marks.sort()
    mark_ranges = []
    for cp in marks:
        if mark_ranges and cp == mark_ranges[-1][1] + 1:
            mark_ranges[-1][1] = cp
        else:
            mark_ranges.append([cp, cp])

    # shared sequence pool for upper/lower/fold/decomp
    pool = []
    seq_maps = {"upper_seq": {}, "lower_seq": {}, "fold_seq": {},
                "decomp": {}}
    for name, table in (("upper_seq", upper_seq),
                        ("lower_seq", lower_seq),
                        ("fold_seq", fold_seq), ("decomp", decomp)):
        for cp in sorted(table):
            seq_maps[name][cp] = (len(pool), len(table[cp]))
            pool += table[cp]
    assert all(x < 0x10000 for x in pool)

    stats = (f"// Coverage U+00A0-024F, U+0250-02AF, U+0300-036F, "
             f"U+0370-03FF, U+0400-052F, U+1E00-1EFF, U+20D0-20FF, "
             f"U+2C60-2C7F, U+FE00-FE0F, U+FE20-FE2F + U+2126/212A/212B.\n"
             f"// Unicode data {ud.unidata_version}. "
             f"case={len(case_map)} upper_seq={len(upper_seq)} "
             f"lower_seq={len(lower_seq)} fold_seq={len(fold_seq)} "
             f"decomp={len(decomp)} compose={len(compose)} ccc={len(ccc)} "
             f"mark_ranges={len(mark_ranges)} pool={len(pool)}")

    def emit_array(f, decl, values, per_line=12, fmt=str):
        f.write(decl + " = {\n")
        for i in range(0, len(values), per_line):
            f.write("    " + ", ".join(fmt(v) for v in
                                       values[i:i + per_line]) + ",\n")
        f.write("};\n\n")

    hdr = []
    hdr += [
        "#pragma once",
        "",
        "// Generated by tools/gen_unicode_tables.py - do not edit.",
        stats,
        "",
        '#include "hcpp/types.hpp"',
        "",
        "namespace hype::unicode {",
        "",
        "    struct CaseEntry {",
        "        hcpp::u16 cp;",
        "        hcpp::u16 upper;",
        "        hcpp::u16 lower;",
        "    };",
        "",
        "    // cp -> pool[off .. off+len): a multi-codepoint sequence.",
        "    struct SeqEntry {",
        "        hcpp::u16 cp;",
        "        hcpp::u16 off;",
        "        hcpp::u8 len;",
        "    };",
        "",
        "    struct ComposeEntry {",
        "        hcpp::u16 first;",
        "        hcpp::u16 second;",
        "        hcpp::u16 composed;",
        "    };",
        "",
        "    struct CccEntry {",
        "        hcpp::u16 cp;",
        "        hcpp::u8 ccc;",
        "    };",
        "",
        "    struct RangeEntry {",
        "        hcpp::u16 first;",
        "        hcpp::u16 last;",
        "    };",
        "",
        "    extern const CaseEntry unicode_case_map[];",
        "    extern const SeqEntry unicode_upper_seq[];",
        "    extern const SeqEntry unicode_lower_seq[];",
        "    extern const SeqEntry unicode_fold_seq[];",
        "    extern const SeqEntry unicode_decomp[];",
        "    extern const ComposeEntry unicode_compose[];",
        "    extern const CccEntry unicode_ccc[];",
        "    extern const RangeEntry unicode_marks[];",
        "    extern const hcpp::u16 unicode_seq_pool[];",
        "",
    ]
    for name in ("case_map", "upper_seq", "lower_seq", "fold_seq",
                 "decomp", "compose", "ccc", "marks", "seq_pool"):
        count = {"case_map": len(case_map), "upper_seq": len(upper_seq),
                 "lower_seq": len(lower_seq), "fold_seq": len(fold_seq),
                 "decomp": len(decomp), "compose": len(compose),
                 "ccc": len(ccc), "marks": len(mark_ranges),
                 "seq_pool": len(pool)}[name]
        hdr.append(f"    constexpr hcpp::u32 unicode_{name}_count = {count};")
    hdr += ["", "}  // namespace hype::unicode", ""]
    OUT_H.write_text("\n".join(hdr), encoding="ascii")

    with OUT_CC.open("w", encoding="ascii", newline="\n") as f:
        f.write("// Generated by tools/gen_unicode_tables.py - do not edit.\n")
        f.write(stats + "\n\n")
        f.write('#include "kernel/unicode/tables.h"\n\n')
        f.write("namespace hype::unicode {\n\n")
        emit_array(f, "const CaseEntry unicode_case_map[]",
                   [(cp, *case_map[cp]) for cp in sorted(case_map)],
                   per_line=6,
                   fmt=lambda v: "{0x%04X, 0x%04X, 0x%04X}" % v)
        for name, table in (("upper_seq", upper_seq),
                            ("lower_seq", lower_seq),
                            ("fold_seq", fold_seq),
                            ("decomp", decomp)):
            emit_array(f, f"const SeqEntry unicode_{name}[]",
                       [(cp, seq_maps[name][cp][0],
                         seq_maps[name][cp][1])
                        for cp in sorted(table)],
                       per_line=8,
                       fmt=lambda v: "{0x%04X, %d, %d}" % v)
        emit_array(f, "const ComposeEntry unicode_compose[]",
                   [(a, b, c) for (a, b), c in sorted(compose.items())],
                   per_line=6,
                   fmt=lambda v: "{0x%04X, 0x%04X, 0x%04X}" % v)
        emit_array(f, "const CccEntry unicode_ccc[]",
                   [(cp, c) for cp, c in sorted(ccc.items())],
                   per_line=12,
                   fmt=lambda v: "{0x%04X, %d}" % v)
        emit_array(f, "const RangeEntry unicode_marks[]",
                   [(a, b) for a, b in mark_ranges],
                   per_line=8,
                   fmt=lambda v: "{0x%04X, 0x%04X}" % v)
        emit_array(f, "const hcpp::u16 unicode_seq_pool[]",
                   pool, per_line=16, fmt=lambda v: "0x%04X" % v)
        f.write("}  // namespace hype::unicode\n")

    print("wrote", OUT_H)
    print("wrote", OUT_CC)
    print(stats.replace("// ", ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
