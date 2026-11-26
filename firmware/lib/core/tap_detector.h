// Turns raw "is touched" samples into single tap events, ignoring controller glitches.
#pragma once
#include <cstdint>

class TapDetector {
 public:
  static constexpr uint32_t kReleaseMs = 80;   // a release shorter than this is a glitch, not a new tap
  static constexpr uint32_t kLockoutMs = 250;  // minimum time between taps
  // Returns true exactly once per real press.
  bool update(bool down, uint32_t nowMs);

 private:
  bool pressed_ = false;
  bool hadTap_ = false;
  uint32_t releaseAtMs_ = 0;
  uint32_t lastTapMs_ = 0;
};
