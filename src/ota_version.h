#pragma once
// ota_version — pure, host-testable version-tag logic for the OTA updater.
// Header-only and free of Arduino dependencies so it compiles in the native
// test environment (see test/test_version). This is the boot-loop-critical
// comparison: a bug here could make a remote device auto-update in a loop.
#include <cstddef>

namespace ota_version {

// Parse a "vMAJOR.MINOR" tag into integers. Tolerant of a leading 'v'/'V', a
// missing minor component, and trailing junk (e.g. the auto-deploy 'a' suffix,
// which is intentionally ignored). Returns false if no digits were found.
inline bool parse(const char* s, long& maj, long& min) {
  maj = 0;
  min = 0;
  if (!s) return false;
  size_t i = 0;
  if (s[i] == 'v' || s[i] == 'V') i++;
  long* cur = &maj;
  bool any = false;
  for (; s[i]; i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') {
      *cur = (*cur) * 10 + (c - '0');
      any = true;
    } else if (c == '.' && cur == &maj) {
      cur = &min;
    } else {
      break;  // stop at first unexpected char (e.g. 'a')
    }
  }
  return any;
}

// True if `candidate` is a strictly newer version than `current`.
// Non-numeric / unparseable inputs are treated as "not newer" (fail safe).
inline bool is_newer(const char* candidate, const char* current) {
  long cMaj, cMin, rMaj, rMin;
  if (!parse(candidate, cMaj, cMin)) return false;
  if (!parse(current, rMaj, rMin)) return false;
  if (cMaj != rMaj) return cMaj > rMaj;
  return cMin > rMin;
}

// True if `tag` is an auto-deploy tag (ends in a lowercase 'a', e.g. "v0.7a").
inline bool is_auto_tag(const char* tag) {
  if (!tag || !tag[0]) return false;
  size_t n = 0;
  while (tag[n]) n++;
  return tag[n - 1] == 'a';
}

}  // namespace ota_version
