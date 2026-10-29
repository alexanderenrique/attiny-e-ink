#!/usr/bin/env python3
"""Review card decks with a configurable chat API.

Reads firmware/sdcard/quotes/*.txt and does not modify them. Writes decisions
to fact-aggregator/review-cache/, then builds reviewed/ and dropped/ decks.

Requires FACT_LLM_BASE_URL, FACT_LLM_API_KEY, and FACT_LLM_MODEL (unless
--write-only). OpenAI-compatible: POST {BASE_URL}/chat/completions.

    python3 fact-aggregator/review_facts.py --deck facts.txt --limit 20
    python3 fact-aggregator/review_facts.py --batch-size 20
    python3 fact-aggregator/review_facts.py --write-only
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from fetch_facts import acceptable, collapse, dedupe_key  # noqa: E402

REPO = ROOT.parent
QUOTES = REPO / "firmware" / "sdcard" / "quotes"
CACHE = ROOT / "review-cache"
REVIEWED = ROOT / "reviewed"
DROPPED = ROOT / "dropped"

QUOTE_DECKS = frozenset({"stoic.txt", "leadership.txt"})
DEFAULT_BATCH = 20
MAX_RETRIES = 5
AUTHOR_SEP = " | "

FACT_SYSTEM = """You review short facts for an e-ink display.
For each line, choose one action:
- keep: already a coherent display fact; leave the text unchanged
- rewrite: same claim, names, numbers, and dates; fix grammar, punctuation, and wording only
- drop: not a complete coherent fact (fragment, nonsense, question, heading, or unclear)

Return every id exactly once. Do not merge lines. Do not reuse one line's wording for another.
Reply with JSON only:
{"results":[{"id":0,"action":"keep|rewrite|drop","text":"...","reason":"..."}]}
For keep, set text to the original. For drop, set text to "". For rewrite, put the full rewritten fact in text."""

QUOTE_SYSTEM = """You review short quotes for an e-ink display.
For each line, choose one action:
- keep: already clear; leave the text unchanged
- rewrite: same meaning; fix grammar, punctuation, and wording only

Never drop a line because it is a quote or opinion. Return every id exactly once.
Do not merge lines. Do not reuse one line's wording for another.
Reply with JSON only:
{"results":[{"id":0,"action":"keep|rewrite","text":"...","reason":"..."}]}
For keep, set text to the original. For rewrite, put the rewritten quote body in text (no author suffix)."""


def is_comment(line: str) -> bool:
    return line.lstrip().startswith("#")


def split_author(line: str) -> tuple[str, str]:
    if AUTHOR_SEP not in line:
        return line, ""
    body, author = line.rsplit(AUTHOR_SEP, 1)
    return body.rstrip(), AUTHOR_SEP + author


def join_author(body: str, suffix: str) -> str:
    if not suffix:
        return body
    return body.rstrip() + suffix


def load_deck_lines(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return text.splitlines()


def content_lines(lines: list[str]) -> list[tuple[int, str]]:
    """Return (line_index, text) for non-blank, non-comment lines."""
    out: list[tuple[int, str]] = []
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or is_comment(stripped):
            continue
        out.append((index, stripped))
    return out


def cache_path(deck_name: str) -> Path:
    stem = deck_name[:-4] if deck_name.endswith(".txt") else deck_name
    return CACHE / f"{stem}.json"


def load_cache(deck_name: str) -> dict[str, dict[str, Any]]:
    path = cache_path(deck_name)
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        return {}
    entries = data.get("entries")
    if not isinstance(entries, dict):
        return {}
    return {str(key): value for key, value in entries.items() if isinstance(value, dict)}


def save_cache(deck_name: str, entries: dict[str, dict[str, Any]]) -> None:
    CACHE.mkdir(parents=True, exist_ok=True)
    payload = {"deck": deck_name, "entries": entries}
    cache_path(deck_name).write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def require_api_config() -> tuple[str, str, str]:
    base = (os.environ.get("FACT_LLM_BASE_URL") or "").rstrip("/")
    key = os.environ.get("FACT_LLM_API_KEY") or ""
    model = os.environ.get("FACT_LLM_MODEL") or ""
    missing = [
        name
        for name, value in (
            ("FACT_LLM_BASE_URL", base),
            ("FACT_LLM_API_KEY", key),
            ("FACT_LLM_MODEL", model),
        )
        if not value
    ]
    if missing:
        raise SystemExit(
            "Set these environment variables before reviewing: "
            + ", ".join(missing)
        )
    return base, key, model


def extract_json_object(text: str) -> dict[str, Any]:
    text = text.strip()
    if text.startswith("```"):
        text = re.sub(r"^```(?:json)?\s*", "", text)
        text = re.sub(r"\s*```$", "", text)
    try:
        data = json.loads(text)
        if isinstance(data, dict):
            return data
    except json.JSONDecodeError:
        pass
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end <= start:
        raise ValueError("model reply was not JSON")
    data = json.loads(text[start : end + 1])
    if not isinstance(data, dict):
        raise ValueError("model JSON was not an object")
    return data


def chat_completions(
    base_url: str,
    api_key: str,
    model: str,
    system: str,
    user: str,
) -> dict[str, Any]:
    url = f"{base_url}/chat/completions"
    body: dict[str, Any] = {
        "model": model,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "response_format": {"type": "json_object"},
    }
    # Some models (e.g. certain OpenAI reasoning models) only allow the default
    # temperature and reject 0. Set FACT_LLM_TEMPERATURE only when the model
    # accepts it.
    temp = 1
    payload = json.dumps(body).encode("utf-8")
    delay = 2.0
    for attempt in range(MAX_RETRIES):
        request = urllib.request.Request(
            url,
            data=payload,
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
                "Accept": "application/json",
                "User-Agent": "attiny-e-ink-fact-review/1.0",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                raw = response.read().decode("utf-8")
            data = json.loads(raw)
            choices = data.get("choices") or []
            if not choices:
                raise ValueError("empty choices in chat response")
            content = choices[0].get("message", {}).get("content") or ""
            return extract_json_object(content)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            if exc.code in (408, 429, 500, 502, 503, 504, 520, 521, 522, 523, 524) and attempt + 1 < MAX_RETRIES:
                time.sleep(delay)
                delay = min(delay * 2, 60)
                continue
            raise SystemExit(f"API HTTP {exc.code}: {detail[:500]}") from exc
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, ValueError) as exc:
            if attempt + 1 < MAX_RETRIES:
                time.sleep(delay)
                delay = min(delay * 2, 60)
                continue
            raise SystemExit(f"API request failed: {exc}") from exc
    raise SystemExit("API request failed after retries")


def build_user_prompt(
    items: list[dict[str, Any]],
    *,
    allow_drop: bool,
    length_feedback: dict[int, str] | None = None,
) -> str:
    lines = [
        "Review each item. Return JSON with a results array covering every id.",
        "Constraints: body must be 3-22 words and at most 140 characters.",
    ]
    if not allow_drop:
        lines.append("Actions allowed: keep, rewrite only.")
    if length_feedback:
        lines.append("These rewrites failed the length check; shorten without changing meaning:")
        for item_id, message in length_feedback.items():
            lines.append(f"- id {item_id}: {message}")
    lines.append("Items:")
    for item in items:
        lines.append(json.dumps({"id": item["id"], "text": item["body"]}, ensure_ascii=False))
    return "\n".join(lines)


def parse_results(data: dict[str, Any]) -> dict[int, dict[str, Any]]:
    results = data.get("results")
    if not isinstance(results, list):
        raise ValueError("missing results array")
    by_id: dict[int, dict[str, Any]] = {}
    for entry in results:
        if not isinstance(entry, dict):
            continue
        try:
            item_id = int(entry["id"])
        except (KeyError, TypeError, ValueError):
            continue
        action = str(entry.get("action") or "").casefold().strip()
        text = entry.get("text")
        reason = str(entry.get("reason") or "").strip()
        if action not in {"keep", "rewrite", "drop"}:
            continue
        by_id[item_id] = {
            "action": action,
            "text": "" if text is None else str(text).strip(),
            "reason": reason,
        }
    return by_id


def finalize_entry(
    original: str,
    body: str,
    author_suffix: str,
    decision: dict[str, Any],
    *,
    allow_drop: bool,
) -> tuple[dict[str, Any] | None, str | None]:
    """Return (cache_entry, length_error). length_error means retry rewrite."""
    action = decision["action"]
    reason = decision.get("reason") or ""

    if action == "drop":
        if not allow_drop:
            return {
                "original": original,
                "action": "keep",
                "text": original,
                "reason": reason or "quote decks do not drop",
            }, None
        return {
            "original": original,
            "action": "drop",
            "text": "",
            "reason": reason or "dropped",
        }, None

    if action == "keep":
        return {
            "original": original,
            "action": "keep",
            "text": original,
            "reason": reason or "kept",
        }, None

    rewritten = collapse(decision.get("text") or "")
    if not rewritten:
        return None, "rewrite returned empty text"

    checked = acceptable(rewritten)
    if checked is None:
        words = len(rewritten.split())
        return None, (
            f"rewrite failed length/coherence check "
            f"({words} words, {len(rewritten)} chars): {rewritten}"
        )

    final_text = join_author(checked, author_suffix)
    return {
        "original": original,
        "action": "rewrite",
        "text": final_text,
        "reason": reason or "rewritten",
    }, None


def review_batch(
    base_url: str,
    api_key: str,
    model: str,
    items: list[dict[str, Any]],
    *,
    allow_drop: bool,
) -> dict[int, dict[str, Any]]:
    """Review items; return map from batch id to finalized cache entry.

    Retries missing ids and length failures in smaller batches.
    """
    pending = {item["id"]: item for item in items}
    finalized: dict[int, dict[str, Any]] = {}
    length_attempts: dict[int, int] = {}
    length_feedback: dict[int, str] = {}
    rounds = 0

    while pending and rounds < 8:
        rounds += 1
        batch_items = [pending[i] for i in sorted(pending)]
        system = FACT_SYSTEM if allow_drop else QUOTE_SYSTEM
        user = build_user_prompt(
            batch_items,
            allow_drop=allow_drop,
            length_feedback=length_feedback or None,
        )
        try:
            data = chat_completions(base_url, api_key, model, system, user)
            by_id = parse_results(data)
        except (ValueError, SystemExit) as exc:
            if isinstance(exc, SystemExit):
                raise
            if len(batch_items) == 1:
                item = batch_items[0]
                if allow_drop:
                    finalized[item["id"]] = {
                        "original": item["original"],
                        "action": "drop",
                        "text": "",
                        "reason": f"invalid model reply: {exc}",
                    }
                else:
                    finalized[item["id"]] = {
                        "original": item["original"],
                        "action": "keep",
                        "text": item["original"],
                        "reason": f"invalid model reply; kept original: {exc}",
                    }
                del pending[item["id"]]
                length_feedback.pop(item["id"], None)
                continue
            # Split the batch and retry halves.
            mid = len(batch_items) // 2
            left = review_batch(
                base_url, api_key, model, batch_items[:mid], allow_drop=allow_drop
            )
            right = review_batch(
                base_url, api_key, model, batch_items[mid:], allow_drop=allow_drop
            )
            finalized.update(left)
            finalized.update(right)
            return finalized

        length_feedback = {}
        for item_id, item in list(pending.items()):
            decision = by_id.get(item_id)
            if decision is None:
                continue
            entry, length_error = finalize_entry(
                item["original"],
                item["body"],
                item["author_suffix"],
                decision,
                allow_drop=allow_drop,
            )
            if length_error:
                length_attempts[item_id] = length_attempts.get(item_id, 0) + 1
                if length_attempts[item_id] >= 2:
                    if allow_drop:
                        finalized[item_id] = {
                            "original": item["original"],
                            "action": "drop",
                            "text": "",
                            "reason": f"rewrite still too long: {length_error}",
                        }
                    else:
                        finalized[item_id] = {
                            "original": item["original"],
                            "action": "keep",
                            "text": item["original"],
                            "reason": f"rewrite still too long; kept original: {length_error}",
                        }
                    del pending[item_id]
                else:
                    length_feedback[item_id] = length_error
                continue
            assert entry is not None
            finalized[item_id] = entry
            del pending[item_id]

        if pending and not length_feedback and set(pending) <= set(by_id):
            # All returned but somehow still pending — should not happen.
            break
        if pending and not by_id:
            # Empty parse: shrink if needed.
            if len(pending) == 1:
                item = next(iter(pending.values()))
                if allow_drop:
                    finalized[item["id"]] = {
                        "original": item["original"],
                        "action": "drop",
                        "text": "",
                        "reason": "empty model results",
                    }
                else:
                    finalized[item["id"]] = {
                        "original": item["original"],
                        "action": "keep",
                        "text": item["original"],
                        "reason": "empty model results; kept original",
                    }
                pending.clear()
            else:
                mid = len(batch_items) // 2
                left_ids = {item["id"] for item in batch_items[:mid]}
                left_items = [pending[i] for i in sorted(pending) if i in left_ids]
                right_items = [pending[i] for i in sorted(pending) if i not in left_ids]
                pending.clear()
                finalized.update(
                    review_batch(
                        base_url, api_key, model, left_items, allow_drop=allow_drop
                    )
                )
                finalized.update(
                    review_batch(
                        base_url, api_key, model, right_items, allow_drop=allow_drop
                    )
                )
                return finalized

    for item_id, item in pending.items():
        if allow_drop:
            finalized[item_id] = {
                "original": item["original"],
                "action": "drop",
                "text": "",
                "reason": "missing from model reply after retries",
            }
        else:
            finalized[item_id] = {
                "original": item["original"],
                "action": "keep",
                "text": item["original"],
                "reason": "missing from model reply after retries; kept original",
            }
    return finalized


def write_outputs(
    deck_name: str,
    source_lines: list[str],
    entries: dict[str, dict[str, Any]],
    *,
    allow_drop: bool,
) -> tuple[int, int]:
    REVIEWED.mkdir(parents=True, exist_ok=True)
    DROPPED.mkdir(parents=True, exist_ok=True)

    reviewed_lines: list[str] = []
    dropped_lines: list[str] = []
    seen: set[str] = set()
    kept = 0
    dropped = 0

    for line in source_lines:
        stripped = line.strip()
        if not stripped:
            continue
        if is_comment(stripped):
            reviewed_lines.append(stripped)
            continue

        entry = entries.get(stripped)
        if entry is None:
            # Unreviewed: keep original so --write-only / partial runs stay usable.
            key = dedupe_key(stripped)
            if key in seen and allow_drop:
                dropped_lines.append(stripped)
                dropped_lines.append("# duplicate of an earlier kept line")
                dropped += 1
                continue
            if allow_drop:
                seen.add(key)
            reviewed_lines.append(stripped)
            kept += 1
            continue

        action = entry.get("action")
        if action == "drop":
            dropped_lines.append(stripped)
            reason = str(entry.get("reason") or "dropped").strip()
            dropped_lines.append(f"# {reason}")
            dropped += 1
            continue

        text = str(entry.get("text") or stripped).strip() or stripped
        key = dedupe_key(text)
        if allow_drop and key in seen:
            dropped_lines.append(stripped)
            dropped_lines.append("# duplicate of an earlier kept or rewritten line")
            dropped += 1
            continue
        if allow_drop:
            seen.add(key)
        reviewed_lines.append(text)
        kept += 1

    (REVIEWED / deck_name).write_text("\n".join(reviewed_lines) + "\n", encoding="utf-8")
    dropped_path = DROPPED / deck_name
    if dropped_lines:
        dropped_path.write_text("\n".join(dropped_lines) + "\n", encoding="utf-8")
    else:
        title = next((line for line in source_lines if line.strip().startswith("#")), f"#{deck_name}")
        dropped_path.write_text(title.strip() + "\n", encoding="utf-8")
    return kept, dropped


def review_deck(
    path: Path,
    *,
    batch_size: int,
    limit: int | None,
    write_only: bool,
    api: tuple[str, str, str] | None,
) -> None:
    deck_name = path.name
    allow_drop = deck_name not in QUOTE_DECKS
    source_lines = load_deck_lines(path)
    entries = load_cache(deck_name)

    contents = content_lines(source_lines)
    pending_originals: list[str] = []
    for _index, text in contents:
        if text not in entries:
            pending_originals.append(text)

    if limit is not None:
        pending_originals = pending_originals[:limit]

    if write_only:
        kept, dropped = write_outputs(
            deck_name, source_lines, entries, allow_drop=allow_drop
        )
        print(
            f"{deck_name}: write-only "
            f"(cached {len(entries)}, kept {kept}, dropped {dropped})"
        )
        return

    assert api is not None
    base_url, api_key, model = api
    if not pending_originals:
        kept, dropped = write_outputs(
            deck_name, source_lines, entries, allow_drop=allow_drop
        )
        print(
            f"{deck_name}: nothing new "
            f"(cached {len(entries)}, kept {kept}, dropped {dropped})"
        )
        return

    print(f"{deck_name}: reviewing {len(pending_originals)} lines "
          f"(batch {batch_size}, cached {len(entries)})")

    for start in range(0, len(pending_originals), batch_size):
        chunk = pending_originals[start : start + batch_size]
        items: list[dict[str, Any]] = []
        for batch_id, original in enumerate(chunk):
            body, author_suffix = split_author(original)
            items.append(
                {
                    "id": batch_id,
                    "original": original,
                    "body": body,
                    "author_suffix": author_suffix,
                }
            )
        results = review_batch(
            base_url, api_key, model, items, allow_drop=allow_drop
        )
        for batch_id, item in enumerate(items):
            entry = results.get(batch_id)
            if entry is None:
                continue
            entries[item["original"]] = entry
        save_cache(deck_name, entries)
        done = min(start + batch_size, len(pending_originals))
        print(f"  {deck_name}: {done}/{len(pending_originals)}")

    kept, dropped = write_outputs(
        deck_name, source_lines, entries, allow_drop=allow_drop
    )
    print(f"{deck_name}: done (kept {kept}, dropped {dropped})")


def list_decks(selected: str | None) -> list[Path]:
    if not QUOTES.is_dir():
        raise SystemExit(f"missing quotes folder: {QUOTES}")
    if selected:
        name = selected if selected.endswith(".txt") else f"{selected}.txt"
        path = QUOTES / name
        if not path.is_file():
            raise SystemExit(f"deck not found: {path}")
        return [path]
    decks = sorted(path for path in QUOTES.glob("*.txt") if path.is_file())
    if not decks:
        raise SystemExit(f"no .txt decks in {QUOTES}")
    return decks


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Review firmware/sdcard/quotes decks with a chat API."
    )
    parser.add_argument(
        "--deck",
        help="Limit to one file name, e.g. facts.txt",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=DEFAULT_BATCH,
        help=f"Lines per API call (default {DEFAULT_BATCH})",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Review only the first N unfinished lines per selected deck",
    )
    parser.add_argument(
        "--write-only",
        action="store_true",
        help="Rebuild reviewed/ and dropped/ from cache; no API calls",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    if args.batch_size < 1:
        raise SystemExit("--batch-size must be at least 1")
    if args.limit is not None and args.limit < 1:
        raise SystemExit("--limit must be at least 1")

    api = None if args.write_only else require_api_config()
    decks = list_decks(args.deck)
    for path in decks:
        review_deck(
            path,
            batch_size=args.batch_size,
            limit=args.limit,
            write_only=args.write_only,
            api=api,
        )


if __name__ == "__main__":
    main()
