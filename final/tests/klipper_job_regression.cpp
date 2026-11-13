// Compile with the project's C++20 compiler: static_assert executes the exact
// production pause/command policy with a monotonic synthetic clock.
#include "../components/klipper_client/private/JobPolicy.h"
#include "../components/klipper_client/private/MetadataRules.h"
#include "../components/klipper_client/private/QueryPolicy.h"
#include <string_view>
using Text = std::string_view;

constexpr bool shortPauseIsSilent() {
  PauseFilter<Text> p;
  p.update("printing", 0, true);
  if (p.update("paused", 100) != "printing" || !p.pending) return false;
  if (p.update("paused", 2000) != "printing") return false;
  return p.update("printing", 2100) == "printing" && !p.pending;
}
constexpr bool repeatedTicksDoNotRestartTimer() {
  PauseFilter<Text> p;
  p.update("printing", 0, true);
  p.update("paused", 10);
  for (unsigned now = 11; now < 2510; ++now)
    if (p.update("paused", now) != "printing") return false;
  return p.update("paused", 2510) == "paused" && !p.pending;
}
constexpr bool baselineAndTerminals() {
  PauseFilter<Text> p;
  if (p.update("paused", 0, true) != "paused" || p.pending) return false;
  for (Text terminal : {Text("error"), Text("complete"), Text("cancelled"), Text("shutdown")}) {
    p.update("printing", 10, true); p.update("paused", 11);
    if (p.update(terminal, 12) != terminal || p.pending) return false;
  }
  return true;
}
constexpr bool timelapseBoundedAndFresh() {
  PauseFilter<Text> p;
  p.update("printing", 0, true);
  p.signal(true, 100);
  p.update("paused", 200);
  if (p.update("paused", 10000) != "printing") return false;
  // Repeated true flags cannot postpone the hard deadline indefinitely.
  p.signal(true, 12000);
  if (p.update("paused", 15199) != "printing") return false;
  if (p.update("paused", 15200) != "paused") return false;
  p = {}; p.update("printing", 0, true); p.signal(true, 1);
  p.update("paused", 3000);
  return p.update("paused", 5500) == "paused"; // stale flag does not extend
}
constexpr bool timelapseEndGraceAndRealResume() {
  PauseFilter<Text> p;
  p.update("printing", 0, true); p.signal(true, 1); p.update("paused", 10);
  p.signal(false, 9000);
  if (p.update("paused", 9500) != "printing") return false;
  if (p.update("printing", 9600) != "printing" || p.pending) return false;
  p.update("paused", 9700);
  return p.update("paused", 12200) == "paused";
}
constexpr bool resetGuards() {
  if (!resetJobAllowed(true, Text("complete"), 2, 2, 7, 7)) return false;
  if (!resetJobAllowed(true, Text("cancelled"), 2, 2, 7, 7)) return false;
  if (!resetJobAllowed(true, Text("error"), 2, 2, 7, 7)) return false;
  for (Text active : {Text("printing"), Text("paused"), Text("standby"), Text("startup")})
    if (resetJobAllowed(true, active, 2, 2, 7, 7)) return false;
  return !resetJobAllowed(false, Text("complete"), 2, 2, 7, 7) &&
         !resetJobAllowed(true, Text("complete"), 3, 2, 7, 7) &&
         !resetJobAllowed(true, Text("complete"), 2, 2, 8, 7) &&
         !resetJobAllowed(true, Text("complete"), 0, 0, 7, 7);
}
static_assert(shortPauseIsSilent());
static_assert(repeatedTicksDoNotRestartTimer());
static_assert(baselineAndTerminals());
static_assert(timelapseBoundedAndFresh());
static_assert(timelapseEndGraceAndRealResume());
static_assert(resetGuards());
static_assert(MetadataRules::color("#09aBfF"));
static_assert(!MetadataRules::color("red") && !MetadataRules::color("#123") &&
              !MetadataRules::color("#1234567") && !MetadataRules::color("#12345G"));
static_assert(MetadataRules::thumbnailRank(300, 300) > MetadataRules::thumbnailRank(100, 100));
static_assert(!MetadataRules::thumbnailRank(48, 48) && !MetadataRules::thumbnailRank(300, 100));
static_assert(!MetadataRules::macroVisible("_HELPER", false, true));
static_assert(!MetadataRules::macroVisible("_HELPER", true, true));
static_assert(!MetadataRules::macroVisible("PUBLIC", true, false));
static_assert(MetadataRules::macroVisible("PUBLIC", false, false));
static_assert(MetadataRules::macroVisible("PUBLIC", true, true));

constexpr bool zeroWeightsRequireExplicitReference() {
  constexpr float weights[] = {0, 12, 0, 3};
  unsigned visible_without_refs = 0, visible_with_refs = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const auto absent = MetadataRules::normalizeToolWeight(true, weights[i], false);
    const auto referenced = MetadataRules::normalizeToolWeight(true, weights[i], i == 0);
    if (absent.referenced) return false;
    visible_without_refs += absent.visible();
    visible_with_refs += referenced.visible();
  }
  const auto unknown = MetadataRules::normalizeToolWeight(false, 0, false);
  return visible_without_refs == 2 && visible_with_refs == 3 &&
         !unknown.has_weight && unknown.visible();
}
static_assert(zeroWeightsRequireExplicitReference());

constexpr bool selectionPrecedesBackgroundQueries() {
  unsigned pending = 1 | 2 | 4 | 8 | 32 | 64;
  constexpr unsigned order[] = {8, 64, 1, 4, 2, 32};
  for (unsigned expected : order) {
    const auto work = nextKlipperQuery(pending);
    if (work != expected) return false;
    pending &= ~work;
  }
  return nextKlipperQuery(pending) == 0 && nextKlipperQuery(16) == 0;
}
constexpr bool staleSelectionAndBackgroundYield() {
  return metadataQueryCurrent(4, 4, false, 0) &&
         !metadataQueryCurrent(4, 5, false, 8) &&
         !metadataQueryCurrent(0, 5, true, 8) &&
         metadataQueryCurrent(0, 5, true, 32) &&
         metadataQueryCurrent(5, 5, false, 32);
}
static_assert(selectionPrecedesBackgroundQueries());
static_assert(staleSelectionAndBackgroundYield());
