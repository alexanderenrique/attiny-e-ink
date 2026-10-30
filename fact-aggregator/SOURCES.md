# Fact sources

This folder collects short facts from public lists. It does not copy anything into `firmware/sdcard/quotes/`. Those decks stay on the card and are not committed. After a fetch, copy a deck from `out/` onto the card by hand if you want the display to use it.

A fact is kept only when it is **22 words or fewer** (the same cutoff as `firmware/scripts/split_long_quotes.py`) and **140 characters or fewer** (the length the e-ink layout can show). Questions are dropped, and so are headings of one or two words, lines that are still wiki markup, lines that start with a dash, and lines cut off on `and`, `or`, or `the`. A leading year label such as `1749:` is removed before that check. The same sentence is kept once, compared case-insensitively after whitespace is collapsed. When a longer science fact is written as `short fact - explanation`, the lead clause is kept if that clause itself fits and does not end on a comma, colon, or semicolon.

Refresh the decks with:

```bash
python3 fact-aggregator/fetch_facts.py
```

Downloads are cached in `raw/` and are gitignored. The script writes:

- `out/nature.txt` — animals, plants, oceans, insects, earth, weather, fossils, and the animal CSV
- `out/space.txt` — astronomy and space
- `out/fun_facts.txt` — food, the body, inventions, and the other short general facts
- `out/cat_facts.txt` — Cat Facts API only
- `out/attribution.csv` — `deck,text,source_url,license` for every kept line

Each `.txt` file starts with a `#` title, then one fact per line, matching the firmware decks. Attribution stays in the CSV so it does not take a line on the panel. Cat Facts are added last, so a sentence already kept from another source is not repeated in `cat_facts.txt`.

## Fetched

### Science facts JSON

- URL: <https://github.com/Royal-lobster/science-facts-project>
- File: <https://raw.githubusercontent.com/Royal-lobster/science-facts-project/main/facts.json>
- License: MIT, as published on the repository
- About 10,003 sourced facts. Fields used: `text`, `category`, `source_url` (falling back to `source`).
- A count of the file found about 6,680 facts already at or under 20 words. Large nature slices include marine biology, zoology, ornithology, botany, and paleontology. Astronomy is large enough for its own deck.
- Nature categories are animals, biology, zoology, botany, mycology, entomology, arachnology, herpetology, ichthyology, ornithology, marine biology, oceanography, parasitology, microbiology, evolution, genetics, dinosaurs, paleontology, geology, ecology, geography, glaciers, lakes, waterfalls, climatology, and meteorology.
- Space categories are astronomy, astrobiology, cosmology, space, and spacecraft.
- Everything else that passes the length filter goes to `fun_facts.txt`.

### Animal fun-facts CSV

- URL: <https://github.com/ekohrt/animal-fun-facts-dataset>
- File: <https://raw.githubusercontent.com/ekohrt/animal-fun-facts-dataset/main/animal-fun-facts-dataset.csv>
- License recorded on each row: MIT, which is the license of that dataset repository
- 7,734 rows with `animal_name`, `source`, `text`, `media_link`, and `wikipedia_link`. About 3,400 are at or under 20 words once internal line breaks are collapsed.
- The sentences were scraped from FactAnimal, A-Z Animals, SeaWorld, Animal Facts Encyclopedia, and Reddit. Each kept line stores that page in `source_url`. They all join `nature.txt`.

### Cat Facts API

- API: <https://catfact.ninja/facts?limit=1000&max_length=140>
- Catalog: <https://github.com/alexwohlbruck/cat-facts>
- License: Apache-2.0
- This is the full catalog, not a random draw. With the 140-character cap the list is about 241 facts. `source_url` for each line is `https://catfact.ninja/`.

### FactFusion

- API: <https://factfusionai.com/wp-json/factfusion/v1/facts?per_page=50>
- Docs: <https://factfusionai.com/facts-api/>
- License: CC BY 4.0. The fact's own page URL is stored in `source_url`.
- About 300 verified facts. The script walks every page (the limit is 100 requests an hour; one walk is a handful of calls) and uses the `claim` field.
- `nature`, `animal`, `ocean`, `geography`, `dinosaurs-fossils`, and `tiny-life` join `nature.txt`. `space` joins `space.txt`. The rest join `fun_facts.txt`.

## Review

`review_facts.py` scores the master decks in `firmware/sdcard/quotes/` (not `out/`) and does not change those files. It sends batches of lines to an OpenAI-compatible chat API, stores decisions in `review-cache/`, then writes display decks and dropped lists:

- `reviewed/<deck>.txt` — `#` titles and comments kept, then kept or grammar-rewritten lines in the original order
- `dropped/<deck>.txt` — original lines the model rejected, each followed by a `#` reason

Fact decks (`facts.txt`, `nature.txt`, `space.txt`, `cat_facts.txt`) may be kept, rewritten for grammar only, or dropped. Quote decks (`stoic.txt`, `leadership.txt`) are kept or rewritten only; a ` | author` suffix is left alone. Rewrites still have to pass the same 3–22 word and 140-character checks as `fetch_facts.py`.

Set these before a live run:

- `FACT_LLM_BASE_URL` — prefix such as `https://api.openai.com/v1` (the script posts to `{BASE_URL}/chat/completions`)
- `FACT_LLM_API_KEY`
- `FACT_LLM_MODEL`
- `FACT_LLM_TEMPERATURE` — optional; omitted by default so models that only allow the API default still work. Set e.g. `0` when the model accepts it.

```bash
python3 fact-aggregator/review_facts.py --deck facts.txt --limit 20
python3 fact-aggregator/review_facts.py --batch-size 20
python3 fact-aggregator/review_facts.py --write-only
```

Default batch size is 20 lines per call. A rerun skips lines already in `review-cache/<deck>.json`. Copy a file from `reviewed/` onto the card by hand when you want the display to use it.

## Categorize

`categorize_facts.py` sorts the mixed fun-facts deck in `reviewed/facts.txt` into topic decks. It does not change that file, and it does not merge into `reviewed/nature.txt` or `reviewed/space.txt`. Facts are copied verbatim; only the category label is chosen.

1. **Discover** — stride-sample about 240 lines, ask the model for category suggestions per batch, then consolidate into 8–14 broad categories plus `other`. Writes `categories.json`. If that file already exists, refuse to overwrite unless `--force` is set, so hand edits are kept. Edit slugs, titles, and definitions before sorting.

2. **Sort** — requires `categories.json`. Assigns every line to one slug, caches decisions in `category-cache/facts.json`, and writes `categorized/<slug>.txt` (each with a `#` title and facts in original order).

```bash
python3 fact-aggregator/categorize_facts.py --discover
python3 fact-aggregator/categorize_facts.py --limit 40
python3 fact-aggregator/categorize_facts.py --write-only
```

Uses the same `FACT_LLM_*` environment variables as review. A rerun skips lines already in the category cache. Copy a file from `categorized/` onto the card by hand when you want the display to use it.

## Skipped

These showed up in the survey and are not fetched:

- **Useless Facts** (`https://uselessfacts.jsph.pl`) is CC BY-SA, but the API only has `/random` and `/today`. There is no full dump.
- **DriveBird** and **Animality** are random endpoints with no complete list.
- **[kinduff/dog-api](https://github.com/kinduff/dog-api)** `db/seeds.rb` has no license, and most entries are paragraphs.
- **[enviro-app/facts](https://github.com/enviro-app/facts)** `facts.json` has no license file.
- **Open Trivia DB** (Science & Nature, Animals) is questions, not statements.
- **[vadimdemedes/cat-facts](https://github.com/vadimdemedes/cat-facts)** (MIT) and **[meowfacts](https://github.com/wh-iterabb-it/meowfacts)** `eng-us.js` (MIT) overlap the Cat Facts API. The API catalog is the one that is fetched.
