#pragma once

// Pure quote-selection helpers — compiles on host and on the MCU.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t QUOTE_LINE_MAX = 240;

// True if a line (without trailing newline) is a blank or a # comment.
inline bool quoteLineIsSkippable(const char* line) {
  if (line == nullptr) {
    return true;
  }
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  return *line == '\0' || *line == '#';
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
