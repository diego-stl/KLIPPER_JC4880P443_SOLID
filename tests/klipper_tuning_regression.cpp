// C++20 compile-time regression of the production tuning conversion and guard.
#include "../components/klipper_client/private/MoonrakerStatus.h"
#include <limits>
#include <string_view>
using Text = std::string_view;

constexpr bool exactCommandsAndBounds() {
  if (Text(PrintTuning::prefix(K_TUNING_SPEED)) != "M220 S" ||
      Text(PrintTuning::prefix(K_TUNING_FLOW)) != "M221 S" ||
      Text(PrintTuning::prefix(K_TUNING_FAN)) != "M106 S") return false;
  for (unsigned value = 0; value < 256; ++value) {
    if (PrintTuning::percent(K_TUNING_SPEED, value) != (value >= 10 && value <= 200) ||
        PrintTuning::percent(K_TUNING_FLOW, value) != (value >= 50 && value <= 150) ||
        PrintTuning::percent(K_TUNING_FAN, value) != (value <= 100)) return false;
    if (PrintTuning::commandValue(K_TUNING_SPEED, value) != value ||
        PrintTuning::commandValue(K_TUNING_FLOW, value) != value) return false;
  }
  if (PrintTuning::percent(static_cast<klipper_tuning_t>(99), 100) ||
      PrintTuning::percent(K_TUNING_SPEED, UINT32_MAX) ||
      PrintTuning::percent(K_TUNING_FLOW, UINT32_MAX) ||
      PrintTuning::percent(K_TUNING_FAN, UINT32_MAX)) return false;
  for (unsigned value = 0; value <= 100; ++value) {
    const unsigned pwm = PrintTuning::commandValue(K_TUNING_FAN, value);
    const int error = int(pwm * 100) - int(value * 255);
    if (pwm > 255 || error < -50 || error > 50) return false;
  }
  return PrintTuning::fanPwm(0) == 0 && PrintTuning::fanPwm(50) == 128 &&
         PrintTuning::fanPwm(100) == 255;
}
constexpr bool readingsAndDeltas() {
  MoonrakerStatus<Text> status;
  if (status.speedFactorValid || status.flowFactorValid || status.fanSpeedValid) return false;
  MoonrakerStatusDelta<Text> delta;
  delta.speedPercent = PrintTuning::reading(1.25, false);
  delta.flowPercent = PrintTuning::reading(.95, false);
  delta.fanPercent = PrintTuning::reading(.5, true);
  status.apply(delta);
  if (!status.speedFactorValid || status.speedPercent != 125 ||
      !status.flowFactorValid || status.flowPercent != 95 ||
      !status.fanSpeedValid || status.fanPercent != 50) return false;
  delta = {}; delta.fanPercent = PrintTuning::reading(0, true);
  status.apply(delta);
  if (!status.fanSpeedValid || status.fanPercent != 0 ||
      status.speedPercent != 125 || status.flowPercent != 95) return false;
  delta = {}; delta.speedPercent = {true, false, 0};
  status.apply(delta);
  if (status.speedFactorValid || status.speedPercent != 0 ||
      !status.flowFactorValid || !status.fanSpeedValid) return false;
  status = {}; status.apply({}); // fresh subscription/endpoint has no invented readings
  if (status.speedFactorValid || status.flowFactorValid || status.fanSpeedValid) return false;
  constexpr double invalid[] = {-1, std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max()};
  for (double value : invalid)
    if (PrintTuning::reading(value, false).valid || PrintTuning::reading(value, true).valid) return false;
  return !PrintTuning::reading(0, false).valid &&
         !PrintTuning::reading(1.01, true).valid && PrintTuning::reading(1, true).percent == 100;
}
constexpr bool staleWorkIsRejected() {
  const auto check = [](bool connected, Text state, Text expected_state,
                        uint32_t epoch, uint32_t gen, uint32_t job, bool valid) {
    return PrintTuning::bindingCurrent(connected, state, expected_state,
                                      epoch, 4, gen, 2, job, 7, valid);
  };
  if (!check(true, "printing", "printing", 4, 2, 7, true) ||
      !check(true, "paused", "paused", 4, 2, 7, true)) return false;
  for (Text state : {Text("standby"), Text("complete"), Text("cancelled"), Text("error")})
    if (check(true, state, state, 4, 2, 7, true)) return false;
  return !check(false, "printing", "printing", 4, 2, 7, true) &&
         !check(true, "printing", "printing", 4, 3, 7, true) &&
         !check(true, "printing", "printing", 4, 2, 8, true) &&
         !check(true, "printing", "printing", 4, 2, 0, true) &&
         !check(true, "paused", "printing", 4, 2, 7, true) &&
         !check(true, "printing", "printing", 5, 2, 7, true) &&
         !check(true, "printing", "printing", 4, 2, 7, false);
}
static_assert(exactCommandsAndBounds());
static_assert(readingsAndDeltas());
static_assert(staleWorkIsRejected());
