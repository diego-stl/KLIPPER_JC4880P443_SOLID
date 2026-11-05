#pragma once
#include <stdint.h>
#include <initializer_list>

// Explicit selected-file requests precede optional discovery/list refreshes and
// pinned-job metadata. Printer state itself uses the independent WS worker.
constexpr unsigned nextKlipperQuery(unsigned pending) {
  for (unsigned kind : {8u, 64u, 1u, 4u, 2u, 32u})
    if (pending & kind) return kind;
  return 0;
}
constexpr bool metadataQueryCurrent(uint32_t requested, uint32_t current,
                                    bool background, unsigned pending) {
  return (!requested || requested == current) && (!background || !(pending & 8));
}
