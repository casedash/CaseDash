#!/usr/bin/env python3
"""Compare inferred symbol sizes between two MSVC linker maps.

The MSVC map records symbol addresses but not exact symbol byte counts. This
tool reuses analyze_link_map's adjacent-symbol estimate, then compares symbols
by normalized name, owning object, and section. Treat the ranking as an
investigation guide rather than exact linker accounting.
"""

from __future__ import annotations

import argparse
import collections
import re
from dataclasses import dataclass
from pathlib import Path

import analyze_link_map


ANONYMOUS_NAMESPACE_HASH_RE = re.compile(r"A0x[0-9A-Fa-f]+")


@dataclass(frozen=True, order=True)
class SymbolKey:
    section: str
    owner: str
    symbol: str


def normalize_symbol_text(value: str, normalize_anonymous_hashes: bool) -> str:
    if not normalize_anonymous_hashes:
        return value
    return ANONYMOUS_NAMESPACE_HASH_RE.sub("A0xHASH", value)


def collect_symbol_sizes(
    path: Path,
    *,
    code_only: bool,
    project_only: bool,
    normalize_anonymous_hashes: bool,
) -> collections.Counter[SymbolKey]:
    sections, symbols = analyze_link_map.read_map(path)
    if not sections:
        raise SystemExit(f"{path}: no section table found")
    if not symbols:
        raise SystemExit(f"{path}: no symbol table found")

    sections_by_index: dict[int, list[analyze_link_map.SectionContribution]] = (
        collections.defaultdict(list)
    )
    for section in sections:
        sections_by_index[section.section].append(section)
    for section_group in sections_by_index.values():
        section_group.sort(key=lambda section: section.start)

    sizes: collections.Counter[SymbolKey] = collections.Counter()
    for size, symbol in analyze_link_map.estimate_symbol_sizes(sections, symbols):
        section_name = symbol_section_name(sections_by_index, symbol)
        if code_only and not section_name.startswith(".text"):
            continue
        if project_only and symbol.library != "<project>":
            continue

        key = SymbolKey(
            section=section_name,
            owner=normalize_symbol_text(symbol.owner, normalize_anonymous_hashes),
            symbol=normalize_symbol_text(symbol.symbol, normalize_anonymous_hashes),
        )
        sizes[key] += size
    return sizes


def symbol_section_name(
    sections_by_index: dict[int, list[analyze_link_map.SectionContribution]],
    symbol: analyze_link_map.Symbol,
) -> str:
    for section in sections_by_index.get(symbol.section, []):
        if section.start <= symbol.offset < section.start + section.length:
            return section.name
    return ""


def format_bytes(value: int) -> str:
    return f"{value:>8} B  {value / 1024:>7.1f} KiB"


def format_delta(value: int) -> str:
    return f"{value:>+8} B  {value / 1024:>+7.1f} KiB"


def short_text(value: str, width: int) -> str:
    if width <= 0 or len(value) <= width:
        return value
    return value[: width - 3] + "..."


def rank_rows(
    left_sizes: collections.Counter[SymbolKey],
    right_sizes: collections.Counter[SymbolKey],
    order: str,
) -> list[tuple[int, int, int, SymbolKey]]:
    rows: list[tuple[int, int, int, SymbolKey]] = []
    for key in set(left_sizes) | set(right_sizes):
        left = left_sizes.get(key, 0)
        right = right_sizes.get(key, 0)
        delta = left - right
        if delta == 0:
            continue
        if order == "left-larger" and delta <= 0:
            continue
        if order == "right-larger" and delta >= 0:
            continue
        rows.append((delta, left, right, key))

    if order == "left-larger":
        rows.sort(key=lambda row: (row[0], row[1], row[3]), reverse=True)
    elif order == "right-larger":
        rows.sort(key=lambda row: (abs(row[0]), row[2], row[3]), reverse=True)
    else:
        rows.sort(key=lambda row: (abs(row[0]), max(row[1], row[2]), row[3]), reverse=True)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare inferred symbol sizes between two MSVC linker maps."
    )
    parser.add_argument("left_map", type=Path)
    parser.add_argument("right_map", type=Path)
    parser.add_argument(
        "--top", type=int, default=10, help="number of symbol differences to print"
    )
    parser.add_argument("--left-label", default="left", help="label for the first map")
    parser.add_argument("--right-label", default="right", help="label for the second map")
    parser.add_argument(
        "--order",
        choices=("absolute", "left-larger", "right-larger"),
        default="absolute",
        help="ranking mode; deltas are always reported as left minus right",
    )
    parser.add_argument(
        "--code-only", action="store_true", help="compare only .text* symbols"
    )
    parser.add_argument(
        "--project-only", action="store_true", help="compare only project-owned symbols"
    )
    parser.add_argument(
        "--keep-anonymous-hashes",
        action="store_true",
        help="do not normalize MSVC anonymous-namespace A0x... hashes",
    )
    parser.add_argument(
        "--symbol-width",
        type=int,
        default=120,
        help="maximum printed symbol width; use 0 for unbounded",
    )
    args = parser.parse_args()

    if args.top < 1:
        raise SystemExit("--top must be at least 1")
    if not args.left_map.exists():
        raise SystemExit(f"{args.left_map}: map file not found")
    if not args.right_map.exists():
        raise SystemExit(f"{args.right_map}: map file not found")

    normalize_anonymous_hashes = not args.keep_anonymous_hashes
    left_sizes = collect_symbol_sizes(
        args.left_map,
        code_only=args.code_only,
        project_only=args.project_only,
        normalize_anonymous_hashes=normalize_anonymous_hashes,
    )
    right_sizes = collect_symbol_sizes(
        args.right_map,
        code_only=args.code_only,
        project_only=args.project_only,
        normalize_anonymous_hashes=normalize_anonymous_hashes,
    )
    rows = rank_rows(left_sizes, right_sizes, args.order)

    left_total = sum(left_sizes.values())
    right_total = sum(right_sizes.values())
    scope = []
    if args.code_only:
        scope.append(".text*")
    if args.project_only:
        scope.append("project")
    scope_text = ", ".join(scope) if scope else "all symbols"

    print(f"Left:  {args.left_label}: {args.left_map}")
    print(f"Right: {args.right_label}: {args.right_map}")
    print(f"Delta: {args.left_label} - {args.right_label}")
    print(f"Scope: {scope_text}")
    print(
        "Estimated symbol bytes: "
        f"{args.left_label} {left_total} B, "
        f"{args.right_label} {right_total} B, "
        f"delta {left_total - right_total:+d} B"
    )
    print("Note: sizes are inferred from adjacent MSVC map addresses.")
    print()
    print(f"Top {min(args.top, len(rows))} symbol differences by {args.order} delta")
    print(
        f"{'Delta':>21}  {args.left_label:>17}  {args.right_label:>17}  "
        "Section    Owner  Symbol"
    )
    for delta, left, right, key in rows[: args.top]:
        symbol = short_text(key.symbol, args.symbol_width)
        print(
            f"{format_delta(delta)}  {format_bytes(left)}  {format_bytes(right)}  "
            f"{key.section:<8}  {key.owner}  {symbol}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
