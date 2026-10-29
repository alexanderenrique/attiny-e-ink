#pragma once

// Pure quote-selection helpers — compiles on host and on the MCU.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t QUOTE_LINE_MAX = 240;

// True if a line (without trailing newline) is a blank or a # comment.
// A deck's first # line is the red title; later # lines are comments.
// Both are skipped as quotes.
inline bool quoteLineIsSkippable(const char* line) {
  if (line == nullptr) {
    return true;
  }
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  return *line == '\0' || *line == '#';
}

// True for a deck file in quotes/. Names starting with '.' are hidden files,
// including Mac AppleDouble sidecars such as ._facts.txt on a FAT card.
inline bool quoteDeckFileName(const char* name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '.') {
    return false;
  }
  const size_t n = strlen(name);
  if (n < 5) {
    return false;
  }
  const char* ext = name + (n - 4);
  const char t0 = ext[1] >= 'A' && ext[1] <= 'Z' ? ext[1] - 'A' + 'a' : ext[1];
  const char t1 = ext[2] >= 'A' && ext[2] <= 'Z' ? ext[2] - 'A' + 'a' : ext[2];
  const char t2 = ext[3] >= 'A' && ext[3] <= 'Z' ? ext[3] - 'A' + 'a' : ext[3];
  return ext[0] == '.' && t0 == 't' && t1 == 'x' && t2 == 't';
}

// Split "quote text | Author" on the first " | " marker.
// On success, quoteOut and authorOut are NUL-terminated views into buffer
// (buffer is modified). If there is no marker, authorOut is empty and the
// whole buffer is the quote.
inline void quoteSplitAuthor(char* buffer, char** quoteOut, char** authorOut) {
  static char empty[] = "";
  if (quoteOut == nullptr || authorOut == nullptr) {
    return;
  }
  *quoteOut = buffer != nullptr ? buffer : empty;
  *authorOut = empty;
  if (buffer == nullptr) {
    return;
  }

  char* marker = strstr(buffer, " | ");
  if (marker == nullptr) {
    return;
  }
  *marker = '\0';
  *authorOut = marker + 3;
}

// Advance sequential index with wrap. Returns the index to display this cycle
// and writes the next index into nextIndexOut.
inline uint16_t quoteNextSequentialIndex(uint16_t current, uint16_t count,
                                         uint16_t* nextIndexOut) {
  if (count == 0) {
    if (nextIndexOut != nullptr) {
      *nextIndexOut = 0;
    }
    return 0;
  }
  const uint16_t display = static_cast<uint16_t>(current % count);
  if (nextIndexOut != nullptr) {
    *nextIndexOut = static_cast<uint16_t>((display + 1U) % count);
  }
  return display;
}

// Greatest common divisor. Used to pick a shuffle stride coprime to the deck.
inline uint16_t quoteGcd(uint16_t a, uint16_t b) {
  while (b != 0) {
    const uint16_t t = static_cast<uint16_t>(a % b);
    a = b;
    b = t;
  }
  return a;
}

// A stride in 1..count-1 that shares no factors with count. Walking
// (start + i * stride) mod count then hits every index once.
// preferred is reduced into range; 0 becomes 1. count <= 1 yields 1.
// Stride 1 walks the file in order and count-1 walks it backwards. Those
// are skipped when any other coprime step exists. Only decks of 1, 2, 3,
// 4, or 6 lines have no other step that can reach every line.
inline uint16_t quoteCoprimeStride(uint16_t count, uint16_t preferred) {
  if (count <= 1) {
    return 1;
  }
  uint16_t step = static_cast<uint16_t>(preferred % count);
  if (step == 0) {
    step = 1;
  }
  const uint16_t origin = step;
  if (count > 4) {
    for (uint16_t n = 0; n < count; ++n) {
      if (step != 1 && static_cast<uint16_t>(step + 1U) != count &&
          quoteGcd(step, count) == 1) {
        return step;
      }
      step = static_cast<uint16_t>(step + 1U);
      if (step >= count) {
        step = 1;
      }
    }
    step = origin;
  }
  for (uint16_t n = 0; n < count; ++n) {
    if (quoteGcd(step, count) == 1) {
      return step;
    }
    step = static_cast<uint16_t>(step + 1U);
    if (step >= count) {
      step = 1;
    }
  }
  return 1;
}

inline bool quoteShuffleStrideOk(uint16_t stride, uint16_t count) {
  if (count <= 1) {
    return true;
  }
  return stride >= 1 && stride < count && quoteGcd(stride, count) == 1;
}

// Cursor for one shuffled pass. cursor runs 0..count-1, then a new start
// and stride are chosen so the next pass is a different order.
struct QuoteShuffleState {
  uint16_t cursor = 0;
  uint16_t start = 0;
  uint16_t stride = 1;
};

inline uint16_t quoteShuffleIndex(uint16_t start, uint16_t cursor, uint16_t stride,
                                  uint16_t count) {
  if (count == 0) {
    return 0;
  }
  const uint32_t sum = static_cast<uint32_t>(start % count) +
                       static_cast<uint32_t>(cursor % count) *
                           static_cast<uint32_t>(stride % count);
  return static_cast<uint16_t>(sum % count);
}

// Start a pass. mix (the deck's byte size) only chooses the first order.
// The same mix always yields the same first pass; later passes call rekey.
inline void quoteShuffleBegin(QuoteShuffleState* s, uint16_t count, uint32_t mix) {
  if (s == nullptr) {
    return;
  }
  s->cursor = 0;
  if (count <= 1) {
    s->start = 0;
    s->stride = 1;
    return;
  }
  s->start = static_cast<uint16_t>(mix % count);
  const uint32_t pref = mix * 0x9E3779B9UL + 0x517CC1B7UL;
  uint16_t preferred = static_cast<uint16_t>(pref % count);
  if (preferred == 0) {
    preferred = 1;
  }
  s->stride = quoteCoprimeStride(count, preferred);
}

// New permutation for the next lap. For count > 2 the stride changes, so the
// relative order is not a rotation of the previous pass. count == 2 has only
// one legal stride, and the start flips instead.
inline void quoteShuffleRekey(QuoteShuffleState* s, uint16_t count) {
  if (s == nullptr) {
    return;
  }
  if (count <= 1) {
    s->cursor = 0;
    s->start = 0;
    s->stride = 1;
    return;
  }
  const uint16_t oldStart = static_cast<uint16_t>(s->start % count);
  const uint16_t oldStride = s->stride;
  uint16_t stride = oldStride;
  for (uint16_t bump = 1; bump < count; ++bump) {
    const uint16_t preferred = static_cast<uint16_t>((oldStride + bump) % count);
    if (preferred == 0) {
      continue;
    }
    stride = quoteCoprimeStride(count, preferred);
    if (stride != oldStride) {
      break;
    }
  }
  s->start = static_cast<uint16_t>((oldStart + 1U) % count);
  s->stride = stride;
  s->cursor = 0;
}

// Index to show now. Advances the cursor and rekeys when the pass ends.
// A stride that cannot cover the deck starts a fresh pass first.
inline uint16_t quoteShuffleTake(QuoteShuffleState* s, uint16_t count) {
  if (s == nullptr || count == 0) {
    return 0;
  }
  if (count == 1) {
    s->cursor = 0;
    s->start = 0;
    s->stride = 1;
    return 0;
  }
  if (!quoteShuffleStrideOk(s->stride, count) || s->start >= count) {
    quoteShuffleBegin(s, count, 0xA5A5U);
  }
  if (s->cursor >= count) {
    s->cursor = 0;
  }
  const uint16_t index = quoteShuffleIndex(s->start, s->cursor, s->stride, count);
  s->cursor = static_cast<uint16_t>(s->cursor + 1U);
  if (s->cursor >= count) {
    quoteShuffleRekey(s, count);
  }
  return index;
}

// Feed bytes through a line counter. Call with each chunk; finish=true on EOF
// flushes a trailing partial line. Returns how many eligible lines were seen
// in this call (usually 0 or 1 per newline, more if the chunk has many).
struct QuoteLineScanner {
  uint16_t eligible = 0;
  bool lineHadContent = false;
  bool lineIsComment = false;
  bool atLineStart = true;
  bool sawHashAtStart = false;

  void reset() {
    eligible = 0;
    lineHadContent = false;
    lineIsComment = false;
    atLineStart = true;
    sawHashAtStart = false;
  }

  uint16_t feed(const char* data, size_t len, bool finish) {
    uint16_t added = 0;
    for (size_t i = 0; i < len; ++i) {
      const char c = data[i];
      if (c == '\n') {
        if (lineHadContent && !lineIsComment) {
          ++eligible;
          ++added;
        }
        lineHadContent = false;
        lineIsComment = false;
        atLineStart = true;
        sawHashAtStart = false;
        continue;
      }
      if (c == '\r') {
        continue;
      }
      if (atLineStart) {
        if (c == ' ' || c == '\t') {
          continue;
        }
        atLineStart = false;
        if (c == '#') {
          lineIsComment = true;
          sawHashAtStart = true;
        }
      }
      if (!lineIsComment) {
        lineHadContent = true;
      } else if (!sawHashAtStart) {
        // unreachable: comment set only at start
        (void)sawHashAtStart;
      }
    }

    if (finish) {
      if (lineHadContent && !lineIsComment) {
        ++eligible;
        ++added;
      }
      lineHadContent = false;
      lineIsComment = false;
      atLineStart = true;
      sawHashAtStart = false;
    }
    return added;
  }
};

// Copy the Nth eligible line (0-based) from a byte stream into out.
// Returns true if found. outCap includes room for the trailing NUL.
struct QuoteLinePicker {
  uint16_t target = 0;
  uint16_t seen = 0;
  size_t outLen = 0;
  bool capturing = false;
  bool done = false;
  bool lineIsComment = false;
  bool atLineStart = true;
  bool lineHadContent = false;
  char* out = nullptr;
  size_t outCap = 0;

  void begin(uint16_t targetIndex, char* buffer, size_t cap) {
    target = targetIndex;
    seen = 0;
    outLen = 0;
    capturing = false;
    done = false;
    lineIsComment = false;
    atLineStart = true;
    lineHadContent = false;
    out = buffer;
    outCap = cap;
    if (out != nullptr && outCap > 0) {
      out[0] = '\0';
    }
  }

  bool feed(const char* data, size_t len, bool finish) {
    if (done || out == nullptr || outCap == 0) {
      return done;
    }

    for (size_t i = 0; i < len; ++i) {
      const char c = data[i];
      if (c == '\n') {
        if (capturing) {
          out[outLen < outCap ? outLen : outCap - 1] = '\0';
          done = true;
          return true;
        }
        if (lineHadContent && !lineIsComment) {
          ++seen;
        }
        lineHadContent = false;
        lineIsComment = false;
        atLineStart = true;
        capturing = false;
        continue;
      }
      if (c == '\r') {
        continue;
      }

      if (atLineStart) {
        if (c == ' ' || c == '\t') {
          continue;
        }
        atLineStart = false;
        if (c == '#') {
          lineIsComment = true;
        } else if (seen == target && !lineIsComment) {
          capturing = true;
          outLen = 0;
        }
      }

      if (lineIsComment) {
        continue;
      }

      lineHadContent = true;
      if (capturing) {
        if (outLen + 1 < outCap) {
          out[outLen++] = c;
        }
        // else truncate silently
      }
    }

    if (finish && !done) {
      if (capturing && outLen > 0) {
        out[outLen < outCap ? outLen : outCap - 1] = '\0';
        done = true;
        return true;
      }
      if (lineHadContent && !lineIsComment && seen == target) {
        // Started capturing mid-logic above; if we never set capturing
        // because the line started in a prior chunk, handle final line.
      }
      if (lineHadContent && !lineIsComment) {
        if (seen == target && outLen > 0) {
          out[outLen < outCap ? outLen : outCap - 1] = '\0';
          done = true;
          return true;
        }
      }
    }
    return done;
  }
};

// Capture the deck title from the first non-blank line when it starts with '#'.
// "#Fun Facts" and "# Fun Facts" both yield "Fun Facts". A first line that does
// not start with '#' means the file has no title. Stops after that line.
struct QuoteHeaderReader {
  char* out = nullptr;
  size_t outCap = 0;
  size_t outLen = 0;
  bool done = false;
  bool found = false;
  bool skippingBlank = true;
  bool inHeader = false;
  bool skippingHashWs = false;

  void begin(char* buffer, size_t cap) {
    out = buffer;
    outCap = cap;
    outLen = 0;
    done = false;
    found = false;
    skippingBlank = true;
    inHeader = false;
    skippingHashWs = false;
    if (out != nullptr && outCap > 0) {
      out[0] = '\0';
    }
  }

  void finishHeader() {
    while (outLen > 0 && (out[outLen - 1] == ' ' || out[outLen - 1] == '\t')) {
      --outLen;
    }
    if (out != nullptr && outCap > 0) {
      out[outLen < outCap ? outLen : outCap - 1] = '\0';
    }
    found = outLen > 0;
    done = true;
    inHeader = false;
  }

  bool feed(const char* data, size_t len, bool finish) {
    if (done || out == nullptr || outCap == 0) {
      return done;
    }

    for (size_t i = 0; i < len; ++i) {
      const char c = data[i];
      if (c == '\r') {
        continue;
      }
      if (skippingBlank) {
        if (c == ' ' || c == '\t' || c == '\n') {
          continue;
        }
        skippingBlank = false;
        if (c == '#') {
          inHeader = true;
          skippingHashWs = true;
          continue;
        }
        done = true;
        found = false;
        out[0] = '\0';
        return true;
      }
      if (!inHeader) {
        continue;
      }
      if (c == '\n') {
        finishHeader();
        return true;
      }
      if (skippingHashWs) {
        if (c == ' ' || c == '\t') {
          continue;
        }
        skippingHashWs = false;
      }
      if (outLen + 1 < outCap) {
        out[outLen++] = c;
      }
    }

    if (finish && !done) {
      if (inHeader) {
        finishHeader();
      } else {
        done = true;
        found = false;
        out[0] = '\0';
      }
    }
    return done;
  }
};
