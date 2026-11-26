#include "tap_detector.h"

bool TapDetector::update(bool down, uint32_t nowMs) {
  if (!down) {
    if (pressed_) {
      pressed_ = false;
      releaseAtMs_ = nowMs;
    }
    return false;
  }
  if (pressed_) return false;
  pressed_ = true;
  if (hadTap_ && nowMs - releaseAtMs_ < kReleaseMs) return false;  // same press, brief dropout
  if (hadTap_ && nowMs - lastTapMs_ < kLockoutMs) return false;
  hadTap_ = true;
  lastTapMs_ = nowMs;
  return true;
}
