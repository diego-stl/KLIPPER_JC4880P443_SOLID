#pragma once
#include <stdint.h>

// Shared by the backend and its clock-driven regression tests. Only a pause
// reached from observed printing is debounced; reconnect snapshots are a baseline.
template <typename Text> struct PauseFilter {
  Text visible{};
  bool pending{};
  uint64_t since{};
  bool timelapse_active{}, signal_seen{}, extended{};
  uint64_t signal_at{}, grace_until{};
  constexpr void signal(bool active, uint64_t now) {
    signal_seen = true;
    signal_at = now;
    timelapse_active = active;
    if (!active && pending && extended && now + 1000 < grace_until)
      grace_until = now + 1000;
    if (active && pending && now - since <= 2500) {
      extended = true;
      grace_until = since + 15000;
    }
  }
  constexpr Text update(Text raw, uint64_t now, bool baseline = false) {
    if (baseline || raw != "paused" || (visible != "printing" && !pending)) {
      visible = raw;
      pending = false;
      extended = false;
    } else {
      if (!pending) {
        pending = true; since = now;
        extended = signal_seen && timelapse_active && now - signal_at <= 2500;
        grace_until = since + 15000;
      }
      if (now - since >= 2500 && (!extended || now >= grace_until)) {
        visible = raw; pending = false; extended = false;
      }
    }
    return visible;
  }
};

template <typename Text> constexpr bool terminalJob(Text state) {
  return state == "complete" || state == "cancelled" || state == "error";
}
template <typename Text> constexpr bool resetJobAllowed(
    bool connected, Text raw, uint32_t currentJob, uint32_t expectedJob,
    uint32_t currentGeneration, uint32_t expectedGeneration) {
  return connected && terminalJob(raw) && currentJob != 0 &&
         currentJob == expectedJob && currentGeneration == expectedGeneration;
}
