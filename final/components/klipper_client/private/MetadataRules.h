#pragma once
#include <stddef.h>
namespace MetadataRules {
struct ToolWeight {
  bool has_weight, referenced;
  float grams;
  constexpr bool visible() const { return !has_weight || grams > 0 || referenced; }
};
constexpr ToolWeight normalizeToolWeight(bool has_weight, float grams, bool explicit_reference) {
  return {has_weight, explicit_reference, has_weight ? grams : 0};
}
constexpr bool hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
constexpr bool color(const char *s) {
  if (!s || *s++ != '#') return false;
  for (unsigned i = 0; i < 6; ++i) if (!hex(*s++)) return false;
  return *s == 0;
}
constexpr unsigned thumbnailRank(unsigned w, unsigned h) {
  return w == 300 && h == 300 ? 2 : w == 100 && h == 100 ? 1 : 0;
}
// Printer configuration can override visibility on public names. Private
// helpers remain private even with a marker, and are filtered BEFORE the cap.
constexpr bool macroVisible(const char *name, bool markerPresent, bool marker) {
  return name && *name && *name != '_' && (!markerPresent || marker);
}
}
