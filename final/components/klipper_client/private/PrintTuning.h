#pragma once
#include "../include/klipper_tuning.h"
#include "NumericRules.h"

namespace PrintTuning {
struct Field {
  bool present{}, valid{};
  float percent{};
};
constexpr bool percent(klipper_tuning_t control, unsigned value) {
  return control == K_TUNING_SPEED ? value >= 10 && value <= 200
       : control == K_TUNING_FLOW ? value >= 50 && value <= 150
       : control == K_TUNING_FAN && value <= 100;
}
constexpr unsigned fanPwm(unsigned percent) {
  return percent <= 100 ? (percent * 255 + 50) / 100 : 255;
}
constexpr const char *prefix(klipper_tuning_t control) {
  return control == K_TUNING_SPEED ? "M220 S"
       : control == K_TUNING_FLOW ? "M221 S"
       : control == K_TUNING_FAN ? "M106 S" : "";
}
constexpr unsigned commandValue(klipper_tuning_t control, unsigned percent) {
  return control == K_TUNING_FAN ? fanPwm(percent) : percent;
}
constexpr Field reading(double factor, bool fan) {
  float value{};
  const bool valid = (fan ? factor >= 0 && factor <= 1 : factor > 0 && factor <= FLT_MAX / 100.0) &&
                     KlipperNumeric::asFloat(factor * 100.0, value, 0) &&
                     (fan || value > 0);
  return {true, valid, valid ? value : 0};
}
template <typename Text>
constexpr bool allowed(bool connected, const Text &state, uint32_t generation,
                       uint32_t expected_generation, uint32_t job,
                       uint32_t expected_job, bool reading_valid) {
  return connected && generation && generation == expected_generation &&
         job && job == expected_job && reading_valid &&
         (state == "printing" || state == "paused");
}
template <typename Text>
constexpr bool bindingCurrent(bool connected, const Text &state,
                              const Text &expected_state, uint32_t epoch,
                              uint32_t expected_epoch, uint32_t generation,
                              uint32_t expected_generation, uint32_t job,
                              uint32_t expected_job, bool reading_valid) {
  return state == expected_state && epoch == expected_epoch &&
         allowed(connected, state, generation, expected_generation, job,
                 expected_job, reading_valid);
}
} // namespace PrintTuning
