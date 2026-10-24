#!/usr/bin/env python3
"""Move quotes longer than a word limit out of sdcard/quotes/.

Each .txt deck is one quote per line. Blank lines and # comments stay in the
source file. A line with more than --max-words words in the quote body (text
before an optional " | Author") is removed from the deck and appended to the
matching file under sdcard/Long-Quotes/. The deck's # title is copied once so
the long-quote file stays readable.
"""

from __future__ import annotations

import argparse
from pathlib import Path

AUTHOR_MARK = " | "
DEFAULT_MAX_WORDS = 22


def quote_body(line: str) -> str:
    text = line.strip()
    mark = text.find(AUTHOR_MARK)
    if mark != -1:
        text = text[:mark]
    return text.strip()


def word_count(line: str) -> int:
    body = quote_body(line)
    if not body:
        return 0
    return len(body.split())


def is_skippable(line: str) -> bool:
    stripped = line.strip()
    return stripped == "" or stripped.startswith("#")


def title_line(lines: list[str]) -> str | None:
    for line in lines:
        stripped = line.strip()
        if stripped == "":
            continue
        if stripped.startswith("#"):
            return line if line.endswith("\n") else line + "\n"
        return None
    return None


def split_deck(lines: list[str], max_words: int) -> tuple[list[str], list[str]]:
    kept: list[str] = []
    moved: list[str] = []
    for line in lines:
        if is_skippable(line) or word_count(line) <= max_words:
            kept.append(line if line.endswith("\n") else line + "\n")
            continue
        moved.append(line if line.endswith("\n") else line + "\n")
    return kept, moved


def write_long_file(dest: Path, source_lines: list[str], moved: list[str]) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    existing = dest.read_text(encoding="utf-8") if dest.exists() else ""
    chunks: list[str] = []
    if not existing:
        header = title_line(source_lines)
        if header is not None:
            chunks.append(header)
    elif not existing.endswith("\n"):
        chunks.append("\n")
    chunks.extend(moved)
    with dest.open("a", encoding="utf-8") as handle:
        handle.write("".join(chunks))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    firmware = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--quotes",
        type=Path,
        default=firmware / "sdcard" / "quotes",
        help="Folder of quote decks (default: firmware/sdcard/quotes)",
    )
    parser.add_argument(
        "--dest",
        type=Path,
        default=firmware / "sdcard" / "Long-Quotes",
        help="Folder for quotes over the word limit",
    )
    parser.add_argument("--max-words", type=int, default=DEFAULT_MAX_WORDS)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would move without writing files",
    )
    args = parser.parse_args()

    decks = sorted(args.quotes.glob("*.txt"))
    if not decks:
        raise SystemExit(f"No .txt decks in {args.quotes}")

    total_kept = 0
    total_moved = 0
    for deck in decks:
        lines = deck.read_text(encoding="utf-8").splitlines(keepends=True)
        kept, moved = split_deck(lines, args.max_words)
        kept_quotes = sum(1 for line in kept if not is_skippable(line))
        total_kept += kept_quotes
        total_moved += len(moved)
        print(f"{deck.name}: keep {kept_quotes}, move {len(moved)}")
        if args.dry_run or not moved:
            continue
        write_long_file(args.dest / deck.name, lines, moved)
        deck.write_text("".join(kept), encoding="utf-8")

    print(f"total: keep {total_kept}, move {total_moved} (cutoff {args.max_words} words)")


if __name__ == "__main__":
    main()
