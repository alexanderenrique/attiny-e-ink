#!/usr/bin/env python3
"""Download public fact lists and write short display decks.

Facts longer than 22 words or 140 characters are dropped. A longer science
fact written as "short fact - explanation" keeps the lead clause when that
clause fits on its own. Questions and duplicate sentences are dropped.

Output is fact-aggregator/out/. This does not touch firmware/sdcard/quotes/.
Raw downloads are cached in fact-aggregator/raw/.

    python3 fact-aggregator/fetch_facts.py
"""

from __future__ import annotations

import csv
import io
import json
import re
import urllib.request
from collections import Counter
from pathlib import Path

MAX_WORDS = 22
MIN_WORDS = 3
MAX_CHARS = 140
USER_AGENT = "attiny-e-ink-fact-aggregator/1.0"

ROOT = Path(__file__).resolve().parent
RAW = ROOT / "raw"
OUT = ROOT / "out"

SCIENCE_URL = (
    "https://raw.githubusercontent.com/Royal-lobster/"
    "science-facts-project/main/facts.json"
)
ANIMAL_CSV_URL = (
    "https://raw.githubusercontent.com/ekohrt/animal-fun-facts-dataset/"
    "main/animal-fun-facts-dataset.csv"
)
CAT_FACTS_URL = "https://catfact.ninja/facts?limit=1000&max_length=140"
FACTFUSION_URL = "https://factfusionai.com/wp-json/factfusion/v1/facts?per_page=50"
CAT_SOURCE = "https://catfact.ninja/"

DECK_FILES = {
    "nature": "nature.txt",
    "space": "space.txt",
    "fun": "fun_facts.txt",
    "cat": "cat_facts.txt",
}
DECK_TITLES = {
    "nature": "#Nature Facts",
    "space": "#Space Facts",
    "fun": "#Fun Facts",
    "cat": "#Cat Facts",
}
DECK_ORDER = ("nature", "space", "fun", "cat")

NATURE_PREFIXES = (
    "animal",
    "arachnolog",
    "biolog",
    "botany",
    "climat",
    "dinosaur",
    "earth",
    "ecolog",
    "entomolog",
    "evolution",
    "genetic",
    "geograph",
    "geolog",
    "glacier",
    "herpetolog",
    "ichthyolog",
    "lakes",
    "marine",
    "meteorolog",
    "microbiolog",
    "mycolog",
    "oceanograph",
    "ornitholog",
    "paleoclimate",
    "paleontolog",
    "parasitolog",
    "waterfall",
    "zoolog",
)
SPACE_PREFIXES = ("astronom", "astrobiolog", "cosmolog", "space", "spacecraft")
FUSION_NATURE = {
    "nature",
    "animal",
    "ocean",
    "geography",
    "dinosaurs-fossils",
    "tiny-life",
}
LEAD_SEPARATORS = (" - ", " – ", " — ")
INCOMPLETE_ENDINGS = set(",:;")
DANGLING_ENDINGS = {"and", "or", "but", "nor", "the", "a", "an", "also"}
MARKUP_LINK = re.compile(r"\[([^\]]+)\]\([^)]+\)")
YEAR_LABEL = re.compile(r"^\d{3,4}:\s+")


class Fact:
    def __init__(self, deck: str, text: str, source_url: str, license_name: str) -> None:
        self.deck = deck
        self.text = text
        self.source_url = source_url
        self.license_name = license_name


def collapse(text: str) -> str:
    return " ".join(text.split())


def word_count(text: str) -> int:
    return len(text.split())


def last_token(text: str) -> str:
    cleaned = text.replace("’", "'").replace("‘", "'")
    return cleaned.split()[-1].strip("“”\"'()[]{}.,;:!?…").casefold()


def normalize_fact(text: str) -> str:
    text = MARKUP_LINK.sub(r"\1", text)
    text = YEAR_LABEL.sub("", collapse(text))
    return collapse(text)


def is_broken(text: str) -> bool:
    lowered = text.casefold()
    if "http://" in lowered or "https://" in lowered or "](" in text or 'title="' in text:
        return True
    if "===" in text or text.startswith(("-", "–", "—")):
        return True
    return bool(text) and last_token(text) in DANGLING_ENDINGS


def fits(text: str) -> bool:
    return (
        word_count(text) >= MIN_WORDS
        and word_count(text) <= MAX_WORDS
        and len(text) <= MAX_CHARS
        and any(ch.isalpha() for ch in text)
        and not is_broken(text)
    )


def lead_clause(text: str) -> str | None:
    for separator in LEAD_SEPARATORS:
        if separator not in text:
            continue
        lead = text.split(separator, 1)[0].strip()
        if not lead or lead[-1] in INCOMPLETE_ENDINGS:
            return None
        return lead
    return None


def acceptable(text: str) -> str | None:
    """Return the fact to keep, or None when it should be dropped."""
    if not isinstance(text, str):
        return None
    cleaned = normalize_fact(text)
    if not cleaned or "?" in cleaned:
        return None
    if fits(cleaned):
        return cleaned
    lead = lead_clause(cleaned)
    if lead is None:
        return None
    lead = collapse(lead)
    if not lead or "?" in lead or lead[-1] in INCOMPLETE_ENDINGS:
        return None
    if fits(lead):
        return lead
    return None


def dedupe_key(text: str) -> str:
    return collapse(text).casefold().rstrip(".! ")


def science_deck(category: str) -> str:
    name = (category or "").casefold()
    if name.startswith(SPACE_PREFIXES):
        return "space"
    if name.startswith(NATURE_PREFIXES):
        return "nature"
    return "fun"


def fusion_deck(category: object) -> str:
    slug = ""
    if isinstance(category, dict):
        slug = str(category.get("slug") or "")
    elif isinstance(category, str):
        slug = category
    slug = slug.casefold()
    if slug == "space":
        return "space"
    if slug in FUSION_NATURE:
        return "nature"
    return "fun"


def fetch(url: str) -> tuple[bytes, dict[str, str]]:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "application/json, text/csv, */*",
        },
    )
    with urllib.request.urlopen(request, timeout=90) as response:
        headers = {key: value for key, value in response.headers.items()}
        return response.read(), headers


def save_raw(name: str, payload: bytes) -> None:
    RAW.mkdir(parents=True, exist_ok=True)
    (RAW / name).write_bytes(payload)


def add_fact(
    records: list[Fact],
    seen: set[str],
    counts: Counter[str],
    deck: str,
    text: str,
    source_url: str,
    license_name: str,
    source_name: str,
) -> None:
    kept = acceptable(text)
    if kept is None:
        counts[f"{source_name} dropped"] += 1
        return
    key = dedupe_key(kept)
    if key in seen:
        counts[f"{source_name} duplicate"] += 1
        return
    seen.add(key)
    records.append(Fact(deck, kept, source_url or "", license_name))
    counts[f"{source_name} kept"] += 1


def load_science(records: list[Fact], seen: set[str], counts: Counter[str]) -> None:
    payload, _headers = fetch(SCIENCE_URL)
    save_raw("science-facts.json", payload)
    facts = json.loads(payload.decode("utf-8"))
    if not isinstance(facts, list):
        raise SystemExit("Science facts response was not a list")
    for fact in facts:
        if not isinstance(fact, dict):
            continue
        source = fact.get("source_url") or fact.get("source") or ""
        add_fact(
            records,
            seen,
            counts,
            science_deck(str(fact.get("category") or "")),
            str(fact.get("text") or ""),
            str(source),
            "MIT",
            "science",
        )


def load_animals(records: list[Fact], seen: set[str], counts: Counter[str]) -> None:
    payload, _headers = fetch(ANIMAL_CSV_URL)
    save_raw("animal-fun-facts.csv", payload)
    text = payload.decode("utf-8-sig", errors="replace")
    for row in csv.DictReader(io.StringIO(text)):
        add_fact(
            records,
            seen,
            counts,
            "nature",
            row.get("text") or "",
            row.get("source") or "",
            "MIT",
            "animals",
        )


def load_cats(records: list[Fact], seen: set[str], counts: Counter[str]) -> None:
    page = 1
    pages: list[object] = []
    while page < 20:
        url = f"{CAT_FACTS_URL}&page={page}"
        payload, _headers = fetch(url)
        body = json.loads(payload.decode("utf-8"))
        pages.append(body)
        data = body.get("data") if isinstance(body, dict) else None
        if not isinstance(data, list) or not data:
            break
        for item in data:
            if not isinstance(item, dict):
                continue
            add_fact(
                records,
                seen,
                counts,
                "cat",
                str(item.get("fact") or ""),
                CAT_SOURCE,
                "Apache-2.0",
                "cats",
            )
        last_page = int(body.get("last_page") or page)
        if page >= last_page:
            break
        page += 1
    save_raw("cat-facts.json", json.dumps(pages).encode("utf-8"))


def load_factfusion(records: list[Fact], seen: set[str], counts: Counter[str]) -> None:
    page = 1
    collected: list[object] = []
    while page < 40:
        url = f"{FACTFUSION_URL}&page={page}"
        payload, headers = fetch(url)
        body = json.loads(payload.decode("utf-8"))
        if isinstance(body, dict):
            batch = body.get("facts") or body.get("data") or []
        elif isinstance(body, list):
            batch = body
        else:
            batch = []
        if not isinstance(batch, list) or not batch:
            break
        collected.extend(batch)
        for fact in batch:
            if not isinstance(fact, dict):
                continue
            add_fact(
                records,
                seen,
                counts,
                fusion_deck(fact.get("category")),
                str(fact.get("claim") or ""),
                str(fact.get("url") or ""),
                "CC BY 4.0",
                "factfusion",
            )
        total_pages = headers.get("X-WP-TotalPages") or headers.get("x-wp-totalpages")
        if total_pages and page >= int(total_pages):
            break
        if len(batch) < 50:
            break
        page += 1
    save_raw("factfusion.json", json.dumps(collected).encode("utf-8"))


def write_outputs(records: list[Fact]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    by_deck: dict[str, list[Fact]] = {deck: [] for deck in DECK_ORDER}
    for record in records:
        by_deck[record.deck].append(record)

    for deck in DECK_ORDER:
        lines = [DECK_TITLES[deck], * (record.text for record in by_deck[deck])]
        path = OUT / DECK_FILES[deck]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    with (OUT / "attribution.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["deck", "text", "source_url", "license"])
        for deck in DECK_ORDER:
            filename = DECK_FILES[deck]
            for record in by_deck[deck]:
                writer.writerow([filename, record.text, record.source_url, record.license_name])


def main() -> None:
    records: list[Fact] = []
    seen: set[str] = set()
    counts: Counter[str] = Counter()
    load_science(records, seen, counts)
    load_animals(records, seen, counts)
    load_factfusion(records, seen, counts)
    load_cats(records, seen, counts)
    write_outputs(records)

    by_deck = Counter(record.deck for record in records)
    print(f"kept {len(records)} facts")
    for deck in DECK_ORDER:
        print(f"  {DECK_FILES[deck]}: {by_deck[deck]}")
    for name in ("science", "animals", "factfusion", "cats"):
        print(
            f"  {name}: kept {counts[f'{name} kept']}, "
            f"dropped {counts[f'{name} dropped']}, "
            f"duplicate {counts[f'{name} duplicate']}"
        )


if __name__ == "__main__":
    main()
