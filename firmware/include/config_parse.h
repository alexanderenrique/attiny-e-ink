#pragma once

// Pure config parsing — compiles on host and on the MCU.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum class QuoteOrder : uint8_t {
  Sequential = 0,
  Random = 1,
};

// Product policy cap on the refresh interval (not an RTC hardware limit).
constexpr uint16_t kMaxIntervalHours = 18;

struct QuoteConfig {
  uint16_t intervalHours = 7;
  QuoteOrder order = QuoteOrder::Sequential;
  // Filename inside quotes/, or "*" for every .txt in that folder.
  char deck[32] = "*";
};

inline void configTrimInPlace(char* s) {
  if (s == nullptr) {
    return;
  }
  char* start = s;
  while (*start == ' ' || *start == '\t') {
    ++start;
  }
  if (start != s) {
    memmove(s, start, strlen(start) + 1);
  }
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
    s[--n] = '\0';
  }
}

// Apply one "key=value" line. Returns true if the line was recognized.
inline bool configApplyLine(QuoteConfig* cfg, const char* line) {
  if (cfg == nullptr || line == nullptr) {
    return false;
  }

  // Skip leading whitespace.
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  if (*line == '\0' || *line == '#' || *line == '\r') {
    return false;
  }

  char buf[64];
  size_t n = 0;
  while (line[n] != '\0' && line[n] != '\r' && n + 1 < sizeof(buf)) {
    buf[n] = line[n];
    ++n;
  }
  buf[n] = '\0';

  char* eq = strchr(buf, '=');
  if (eq == nullptr) {
    return false;
  }
  *eq = '\0';
  char* key = buf;
  char* value = eq + 1;
  configTrimInPlace(key);
  configTrimInPlace(value);
  if (key[0] == '\0') {
    return false;
  }

  if (strcmp(key, "interval") == 0) {
    unsigned long v = 0;
    for (const char* p = value; *p; ++p) {
      if (*p < '0' || *p > '9') {
        return false;
      }
      v = v * 10UL + static_cast<unsigned long>(*p - '0');
      if (v > 65535UL) {
        v = 65535UL;
        break;
      }
    }
    if (v == 0) {
      v = 1;
    }
    if (v > kMaxIntervalHours) {
      v = kMaxIntervalHours;
    }
    cfg->intervalHours = static_cast<uint16_t>(v);
    return true;
  }

  if (strcmp(key, "order") == 0) {
    if (strcmp(value, "random") == 0) {
      cfg->order = QuoteOrder::Random;
      return true;
    }
    if (strcmp(value, "sequential") == 0) {
      cfg->order = QuoteOrder::Sequential;
      return true;
    }
    return false;
  }

  if (strcmp(key, "deck") == 0) {
    size_t len = strlen(value);
    if (len == 0 || len >= sizeof(cfg->deck)) {
      return false;
    }
    memcpy(cfg->deck, value, len + 1);
    return true;
  }

  return false;
}
