#include "../components/klipper_client/private/NumericRules.h"
#include <limits>
constexpr bool floats() {
  float out = 42;
  return !KlipperNumeric::asFloat(1e300, out) && out == 42 &&
         KlipperNumeric::asFloat(0, out) && out == 0;
}
constexpr bool geometry() {
  float out = 5;
  return !KlipperNumeric::coordinate(1e300, out) &&
         !KlipperNumeric::coordinate(1000001, out) &&
         !KlipperNumeric::coordinate(-1000001, out) &&
         KlipperNumeric::coordinate(-300, out) && out == -300;
}
constexpr bool time() {
  double out = 1;
  return !KlipperNumeric::seconds(1e300, out) && out == 1 &&
         !KlipperNumeric::seconds(-1, out) &&
         KlipperNumeric::seconds(UINT32_MAX, out) &&
         out == KlipperNumeric::maxPublishedSeconds &&
         uint32_t(float(out)) == 4294967040u;
}
static_assert(floats());
static_assert(geometry());
static_assert(time());
constexpr bool nonfinite() {
  float f = 0;
  double d = 0;
  return !KlipperNumeric::asFloat(std::numeric_limits<double>::infinity(), f) &&
         !KlipperNumeric::coordinate(std::numeric_limits<double>::quiet_NaN(),
                                     f) &&
         !KlipperNumeric::seconds(std::numeric_limits<double>::infinity(), d);
}
static_assert(nonfinite());
