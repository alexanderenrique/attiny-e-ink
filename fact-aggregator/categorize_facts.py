#!/usr/bin/env python3
"""Categorize the mixed fun-facts deck with a configurable chat API.

Two phases:
  1. --discover samples reviewed/facts.txt and writes categories.json
  2. default run assigns every line to a category and writes categorized/

Requires FACT_LLM_BASE_URL, FACT_LLM_API_KEY, and FACT_LLM_MODEL (unless
--write-only). OpenAI-compatible: POST {BASE_URL}/chat/completions.

    python3 fact-aggregator/categorize_facts.py --discover
    python3 fact-aggregator/categorize_facts.py --limit 40
    python3 fact-aggregator/categorize_facts.py --write-only
    python3 fact-aggregator/categorize_facts.py --also nature.txt --also space.txt
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from fetch_facts import dedupe_key  # noqa: E402
from review_facts import (  # noqa: E402
    chat_completions,
    content_lines,
    load_deck_lines,
    require_api_config,
)

REVIEWED = ROOT / "reviewed"
SOURCE = REVIEWED / "facts.txt"
CATEGORIES_PATH = ROOT / "categories.json"
CACHE_DIR = ROOT / "category-cache"
CACHE_PATH = CACHE_DIR / "facts.json"
OUT_DIR = ROOT / "categorized"

DEFAULT_BATCH = 20
DISCOVER_SAMPLE = 240
MAX_BATCH_ROUNDS = 8

DISCOVER_SYSTEM = """You survey short trivia facts for an e-ink display.
Suggest broad topic categories that actually appear in the sample.
Prefer a handful of clear buckets (about the grain of space, nature, computers,
food, history, body, animals) over many tiny niches.
Reply with JSON only:
{"suggestions":[{"slug":"space","count":3,"examples":["fact one","fact two"]}]}
slug: lowercase letters, digits, and underscores only. count: how many sample
lines fit. examples: up to two original lines from the sample."""

CONSOLIDATE_SYSTEM = """You consolidate category suggestions into a closed taxonomy
for sorting short trivia facts on an e-ink display.
Produce 8-14 broad categories plus a catch-all "other".
Merge near-duplicates. Prefer clear buckets over niches.
Each category needs:
- slug: lowercase letters, digits, underscores (filesystem-safe)
- title: display heading without a leading # (e.g. "Space Facts")
- definition: one sentence describing what belongs
Always include slug "other" for leftovers.
Reply with JSON only:
{"categories":[{"slug":"space","title":"Space Facts","definition":"..."}]}"""

SORT_SYSTEM_TEMPLATE = """You assign each short trivia fact to exactly one category.
Choose only from this closed list (slug — definition):
{category_list}

Return every id exactly once. Prefer a specific category over other when it fits.
Reply with JSON only:
{{"results":[{{"id":0,"category":"space"}}]}}"""


def slugify(value: str) -> str:
    text = value.strip().casefold()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    text = re.sub(r"_+", "_", text).strip("_")
    return text or "other"


def stride_sample(items: list[str], target: int) -> list[str]:
    if not items:
        return []
    if len(items) <= target:
        return list(items)
    # Evenly spaced indices across the full deck.
    step = len(items) / target
    chosen: list[str] = []
    seen: set[int] = set()
    for i in range(target):
        index = min(int(i * step), len(items) - 1)
        if index in seen:
            continue
        seen.add(index)
        chosen.append(items[index])
    return chosen


def load_categories() -> list[dict[str, str]]:
    if not CATEGORIES_PATH.is_file():
        raise SystemExit(
            f"missing {CATEGORIES_PATH}; run with --discover first "
            "(then edit categories.json if needed)"
        )
    data = json.loads(CATEGORIES_PATH.read_text(encoding="utf-8"))
    raw = data.get("categories") if isinstance(data, dict) else None
    if not isinstance(raw, list) or not raw:
        raise SystemExit(f"{CATEGORIES_PATH} has no categories array")
    categories: list[dict[str, str]] = []
    seen: set[str] = set()
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        slug = slugify(str(entry.get("slug") or ""))
        title = str(entry.get("title") or "").strip() or slug.replace("_", " ").title()
        definition = str(entry.get("definition") or "").strip()
        if not definition or slug in seen:
            continue
        seen.add(slug)
        categories.append(
            {"slug": slug, "title": title, "definition": definition}
        )
    if not categories:
        raise SystemExit(f"{CATEGORIES_PATH}: no valid categories")
    if "other" not in seen:
        categories.append(
            {
                "slug": "other",
                "title": "Other Facts",
                "definition": "Facts that do not fit the other categories.",
            }
        )
    return categories


def save_categories(categories: list[dict[str, str]], *, force: bool) -> None:
    if CATEGORIES_PATH.exists() and not force:
        raise SystemExit(
            f"{CATEGORIES_PATH} already exists; edit it by hand or pass --force"
        )
    payload = {"categories": categories}
    CATEGORIES_PATH.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def load_sort_cache() -> dict[str, dict[str, Any]]:
    if not CACHE_PATH.exists():
        return {}
    data = json.loads(CACHE_PATH.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        return {}
    entries = data.get("entries")
    if not isinstance(entries, dict):
        return {}
    return {str(key): value for key, value in entries.items() if isinstance(value, dict)}


def save_sort_cache(entries: dict[str, dict[str, Any]]) -> None:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    payload = {"deck": "facts.txt", "entries": entries}
    CACHE_PATH.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def parse_suggestions(data: dict[str, Any]) -> list[dict[str, Any]]:
    raw = data.get("suggestions")
    if not isinstance(raw, list):
        raise ValueError("missing suggestions array")
    out: list[dict[str, Any]] = []
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        slug = slugify(str(entry.get("slug") or ""))
        try:
            count = int(entry.get("count") or 0)
        except (TypeError, ValueError):
            count = 0
        examples_raw = entry.get("examples") or []
        examples: list[str] = []
        if isinstance(examples_raw, list):
            for item in examples_raw[:2]:
                text = str(item).strip()
                if text:
                    examples.append(text)
        if slug:
            out.append({"slug": slug, "count": max(count, 0), "examples": examples})
    return out


def parse_consolidated(data: dict[str, Any]) -> list[dict[str, str]]:
    raw = data.get("categories")
    if not isinstance(raw, list):
        raise ValueError("missing categories array")
    categories: list[dict[str, str]] = []
    seen: set[str] = set()
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        slug = slugify(str(entry.get("slug") or ""))
        title = str(entry.get("title") or "").strip()
        definition = str(entry.get("definition") or "").strip()
        if not slug or not definition or slug in seen:
            continue
        if not title:
            title = slug.replace("_", " ").title() + " Facts"
        seen.add(slug)
        categories.append(
            {"slug": slug, "title": title, "definition": definition}
        )
    if not categories:
        raise ValueError("no valid categories in consolidation reply")
    if "other" not in seen:
        categories.append(
            {
                "slug": "other",
                "title": "Other Facts",
                "definition": "Facts that do not fit the other categories.",
            }
        )
    # Soft-clamp: if the model returned a huge set, keep first 14 non-other + other.
    others = [c for c in categories if c["slug"] == "other"]
    main = [c for c in categories if c["slug"] != "other"]
    if len(main) > 14:
        main = main[:14]
    return main + others


def parse_sort_results(data: dict[str, Any]) -> dict[int, str]:
    raw = data.get("results")
    if not isinstance(raw, list):
        raise ValueError("missing results array")
    by_id: dict[int, str] = {}
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        try:
            item_id = int(entry["id"])
        except (KeyError, TypeError, ValueError):
            continue
        category = slugify(str(entry.get("category") or ""))
        if category:
            by_id[item_id] = category
    return by_id


def discover_batch_prompt(facts: list[str]) -> str:
    lines = [
        "Suggest broad categories that appear in these sample facts.",
        "Return JSON with a suggestions array.",
        "Facts:",
    ]
    for fact in facts:
        lines.append(json.dumps(fact, ensure_ascii=False))
    return "\n".join(lines)


def consolidate_prompt(suggestions: list[dict[str, Any]]) -> str:
    lines = [
        "Consolidate these batch suggestions into 8-14 broad categories plus other.",
        "Return JSON with a categories array.",
        "Suggestions:",
        json.dumps(suggestions, ensure_ascii=False, indent=2),
    ]
    return "\n".join(lines)


def sort_prompt(
    items: list[dict[str, Any]],
    categories: list[dict[str, str]],
    *,
    retry_feedback: dict[int, str] | None = None,
) -> tuple[str, str]:
    category_list = "\n".join(
        f"- {c['slug']}: {c['definition']}" for c in categories
    )
    lines = [
        "Assign each fact to exactly one category slug from the closed list.",
        "Allowed slugs: " + ", ".join(c["slug"] for c in categories),
        "Return JSON with a results array covering every id.",
    ]
    if retry_feedback:
        lines.append("These assignments were invalid; pick a listed slug:")
        for item_id, message in retry_feedback.items():
            lines.append(f"- id {item_id}: {message}")
    lines.append("Items:")
    for item in items:
        lines.append(
            json.dumps({"id": item["id"], "text": item["text"]}, ensure_ascii=False)
        )
    return "\n".join(lines), category_list


def run_discover(
    *,
    batch_size: int,
    sample_size: int,
    force: bool,
    api: tuple[str, str, str],
) -> None:
    if CATEGORIES_PATH.exists() and not force:
        raise SystemExit(
            f"{CATEGORIES_PATH} already exists; edit it by hand or pass --force"
        )
    if not SOURCE.is_file():
        raise SystemExit(f"missing source deck: {SOURCE}")

    base_url, api_key, model = api
    facts = [text for _index, text in content_lines(load_deck_lines(SOURCE))]
    sample = stride_sample(facts, sample_size)
    print(f"discover: sampling {len(sample)} of {len(facts)} facts")

    all_suggestions: list[dict[str, Any]] = []
    for start in range(0, len(sample), batch_size):
        chunk = sample[start : start + batch_size]
        user = discover_batch_prompt(chunk)
        data = chat_completions(
            base_url, api_key, model, DISCOVER_SYSTEM, user
        )
        suggestions = parse_suggestions(data)
        all_suggestions.extend(suggestions)
        done = min(start + batch_size, len(sample))
        print(f"  discover batch {done}/{len(sample)} ({len(suggestions)} suggestions)")

    if not all_suggestions:
        raise SystemExit("discover produced no suggestions")

    print("discover: consolidating suggestions")
    consolidated = chat_completions(
        base_url,
        api_key,
        model,
        CONSOLIDATE_SYSTEM,
        consolidate_prompt(all_suggestions),
    )
    categories = parse_consolidated(consolidated)
    save_categories(categories, force=True)
    print(f"wrote {CATEGORIES_PATH} ({len(categories)} categories):")
    for entry in categories:
        print(f"  {entry['slug']}: {entry['title']} — {entry['definition']}")


def sort_batch(
    base_url: str,
    api_key: str,
    model: str,
    items: list[dict[str, Any]],
    categories: list[dict[str, str]],
) -> dict[int, str]:
    """Return map from batch id to category slug."""
    allowed = {c["slug"] for c in categories}
    pending = {item["id"]: item for item in items}
    finalized: dict[int, str] = {}
    retry_feedback: dict[int, str] = {}
    rounds = 0

    while pending and rounds < MAX_BATCH_ROUNDS:
        rounds += 1
        batch_items = [pending[i] for i in sorted(pending)]
        user, category_list = sort_prompt(
            batch_items, categories, retry_feedback=retry_feedback or None
        )
        system = SORT_SYSTEM_TEMPLATE.format(category_list=category_list)
        try:
            data = chat_completions(base_url, api_key, model, system, user)
            by_id = parse_sort_results(data)
        except (ValueError, SystemExit) as exc:
            if isinstance(exc, SystemExit):
                raise
            if len(batch_items) == 1:
                item = batch_items[0]
                finalized[item["id"]] = "other"
                del pending[item["id"]]
                retry_feedback.pop(item["id"], None)
                continue
            mid = len(batch_items) // 2
            left = sort_batch(
                base_url, api_key, model, batch_items[:mid], categories
            )
            right = sort_batch(
                base_url, api_key, model, batch_items[mid:], categories
            )
            finalized.update(left)
            finalized.update(right)
            return finalized

        retry_feedback = {}
        for item_id, item in list(pending.items()):
            slug = by_id.get(item_id)
            if slug is None:
                retry_feedback[item_id] = "missing from model reply"
                continue
            if slug not in allowed:
                if rounds >= MAX_BATCH_ROUNDS - 1:
                    finalized[item_id] = "other"
                    del pending[item_id]
                else:
                    retry_feedback[item_id] = f"unknown slug {slug!r}"
                continue
            finalized[item_id] = slug
            del pending[item_id]

        if not retry_feedback:
            break
        # Next loop only retries the feedback ids; drop resolved ones already handled.
        pending = {i: pending[i] for i in retry_feedback if i in pending}

    for item_id in list(pending):
        finalized[item_id] = "other"
    return finalized


def resolve_reviewed_deck(name: str) -> Path:
    raw = Path(name)
    candidates = [raw] if raw.is_absolute() else [REVIEWED / raw.name, raw]
    for path in candidates:
        if path.is_file():
            return path
    raise SystemExit(f"missing source deck: {name}")


def write_categorized(
    sources: list[list[str]],
    entries: dict[str, dict[str, Any]],
    categories: list[dict[str, str]],
) -> dict[str, int]:
    by_slug: dict[str, list[str]] = {c["slug"]: [] for c in categories}
    title_by_slug = {c["slug"]: c["title"] for c in categories}
    if "other" not in by_slug:
        by_slug["other"] = []
        title_by_slug["other"] = "Other Facts"

    counts: dict[str, int] = {slug: 0 for slug in by_slug}
    uncached = 0
    skipped_dupes = 0
    seen: set[str] = set()
    for source_lines in sources:
        for _index, text in content_lines(source_lines):
            key = dedupe_key(text)
            if key in seen:
                skipped_dupes += 1
                continue
            entry = entries.get(text)
            if not entry:
                uncached += 1
                continue
            seen.add(key)
            slug = slugify(str(entry.get("category") or "other"))
            if slug not in by_slug:
                slug = "other"
            by_slug[slug].append(text)
            counts[slug] = counts.get(slug, 0) + 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # Remove prior decks so renamed/removed categories do not linger.
    for old in OUT_DIR.glob("*.txt"):
        old.unlink()

    for slug, facts in by_slug.items():
        title = title_by_slug.get(slug, slug.replace("_", " ").title())
        if not title.startswith("#"):
            title = "#" + title
        body = [title, *facts]
        (OUT_DIR / f"{slug}.txt").write_text(
            "\n".join(body) + "\n", encoding="utf-8"
        )

    if skipped_dupes:
        print(f"skipped {skipped_dupes} duplicate lines already kept in an earlier deck")
    if uncached:
        print(f"warning: {uncached} lines not in category cache (omitted from decks)")
    return counts


def load_sort_sources(also: list[str]) -> tuple[list[Path], list[list[str]], list[str]]:
    if not SOURCE.is_file():
        raise SystemExit(f"missing source deck: {SOURCE}")
    paths = [SOURCE]
    seen_paths = {SOURCE.resolve()}
    for name in also:
        path = resolve_reviewed_deck(name)
        resolved = path.resolve()
        if resolved in seen_paths:
            continue
        seen_paths.add(resolved)
        paths.append(path)
    sources = [load_deck_lines(path) for path in paths]
    facts: list[str] = []
    seen_text: set[str] = set()
    for source_lines in sources:
        for _index, text in content_lines(source_lines):
            if text in seen_text:
                continue
            seen_text.add(text)
            facts.append(text)
    return paths, sources, facts


def run_sort(
    *,
    batch_size: int,
    limit: int | None,
    write_only: bool,
    api: tuple[str, str, str] | None,
    also: list[str],
) -> None:
    categories = load_categories()
    paths, sources, facts = load_sort_sources(also)
    entries = load_sort_cache()

    pending = [text for text in facts if text not in entries]
    if limit is not None:
        pending = pending[:limit]

    deck_names = ", ".join(path.name for path in paths)

    if write_only:
        counts = write_categorized(sources, entries, categories)
        print(
            f"write-only: cached {len(entries)} / {len(facts)} "
            f"from {deck_names}; wrote {OUT_DIR}"
        )
        for slug, count in sorted(counts.items(), key=lambda pair: (-pair[1], pair[0])):
            print(f"  {slug}: {count}")
        return

    assert api is not None
    base_url, api_key, model = api

    if not pending:
        counts = write_categorized(sources, entries, categories)
        print(
            f"nothing new: cached {len(entries)} / {len(facts)} "
            f"from {deck_names}; wrote {OUT_DIR}"
        )
        for slug, count in sorted(counts.items(), key=lambda pair: (-pair[1], pair[0])):
            print(f"  {slug}: {count}")
        return

    print(
        f"sort: assigning {len(pending)} lines from {deck_names} "
        f"(batch {batch_size}, cached {len(entries)}, "
        f"{len(categories)} categories)"
    )
    for start in range(0, len(pending), batch_size):
        chunk = pending[start : start + batch_size]
        items = [
            {"id": batch_id, "text": text}
            for batch_id, text in enumerate(chunk)
        ]
        results = sort_batch(base_url, api_key, model, items, categories)
        for batch_id, text in enumerate(chunk):
            slug = results.get(batch_id, "other")
            entries[text] = {"category": slug, "original": text}
        save_sort_cache(entries)
        done = min(start + batch_size, len(pending))
        print(f"  sort: {done}/{len(pending)}")

    counts = write_categorized(sources, entries, categories)
    print(f"done: wrote {OUT_DIR}")
    for slug, count in sorted(counts.items(), key=lambda pair: (-pair[1], pair[0])):
        print(f"  {slug}: {count}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Discover categories and sort reviewed/facts.txt."
    )
    parser.add_argument(
        "--discover",
        action="store_true",
        help="Sample the deck and write categories.json; do not sort",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite categories.json during --discover",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=DEFAULT_BATCH,
        help=f"Lines per API call (default {DEFAULT_BATCH})",
    )
    parser.add_argument(
        "--sample-size",
        type=int,
        default=DISCOVER_SAMPLE,
        help=f"Facts to sample during --discover (default {DISCOVER_SAMPLE})",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Sort only the first N unfinished lines",
    )
    parser.add_argument(
        "--write-only",
        action="store_true",
        help="Rebuild categorized/ from cache; no API calls",
    )
    parser.add_argument(
        "--also",
        action="append",
        default=[],
        metavar="DECK",
        help="Also sort this reviewed deck and merge it into categorized/ "
        "(repeatable; facts.txt is always included and wins on duplicates)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    if args.batch_size < 1:
        raise SystemExit("--batch-size must be at least 1")
    if args.sample_size < 1:
        raise SystemExit("--sample-size must be at least 1")
    if args.limit is not None and args.limit < 1:
        raise SystemExit("--limit must be at least 1")
    if args.discover and args.write_only:
        raise SystemExit("use either --discover or --write-only, not both")

    if args.discover:
        api = require_api_config()
        run_discover(
            batch_size=args.batch_size,
            sample_size=args.sample_size,
            force=args.force,
            api=api,
        )
        return

    api = None if args.write_only else require_api_config()
    run_sort(
        batch_size=args.batch_size,
        limit=args.limit,
        write_only=args.write_only,
        api=api,
        also=args.also,
    )


if __name__ == "__main__":
    main()
