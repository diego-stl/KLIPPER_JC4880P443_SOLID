#pragma once
#include <float.h>
#include <stdint.h>

namespace KlipperNumeric {
// Validate in double before conversion: a finite JSON number need not fit
// float.
constexpr bool asFloat(double value, float &out, double low = -FLT_MAX,
                       double high = FLT_MAX) {
  if (!(value >= low && value <= high) || value < -FLT_MAX || value > FLT_MAX)
    return false;
  out = static_cast<float>(value);
  return true;
}
constexpr bool coordinate(double value, float &out) {
  return asFloat(value, out, -1000000.0, 1000000.0);
}
// Largest float below UINT32_MAX: conversion of the published float to uint32_t
// remains defined, including when a server sends the largest accepted duration.
constexpr double maxPublishedSeconds = 4294967040.0;
constexpr bool seconds(double value, double &out) {
  if (!(value >= 0 && value <= double(UINT32_MAX)))
    return false;
  out = value > maxPublishedSeconds ? maxPublishedSeconds : value;
  return true;
}
} // namespace KlipperNumeric
