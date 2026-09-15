#pragma once

#include <cstdint>

namespace knobify::messaging {

// Screen messages belong to what's on screen (a toggle's feedback) and go
// away when the screen changes or the device locks. System messages
// (e.g. a lost connection) outlive screen changes.
enum class MessageScope { Screen, System };

// When the message area should hide -- see ui_widgets::MessageArea and
// docs/design/ux-guidelines.md §7. One message at a time: a new one
// replaces the current one and restarts the clock. Callers pass
// millis()-style timestamps so this is host-testable.
class MessageTimer {
 public:
  void show(uint32_t nowMs, uint32_t durationMs, MessageScope scope) {
    shownAtMs_ = nowMs;
    durationMs_ = durationMs;
    scope_ = scope;
    visible_ = true;
  }

  // True exactly once, when a visible message's time is up.
  // Signed elapsed time: loop() may tick with a timestamp taken just before
  // the tap that showed the message, and unsigned now - shownAt then wrapped
  // to ~49 days -- every message hid in the same loop (found on the device
  // 2026-09-15). Still correct across millis() wraparound.
  bool expire(uint32_t nowMs) {
    int32_t elapsedMs = static_cast<int32_t>(nowMs - shownAtMs_);
    if (!visible_ || elapsedMs < static_cast<int32_t>(durationMs_)) return false;
    visible_ = false;
    return true;
  }

  // True if a screen message was showing and is now dismissed.
  bool dismissScreenMessage() {
    if (!visible_ || scope_ != MessageScope::Screen) return false;
    visible_ = false;
    return true;
  }

  void hide() { visible_ = false; }
  bool visible() const { return visible_; }

 private:
  uint32_t shownAtMs_ = 0;
  uint32_t durationMs_ = 0;
  MessageScope scope_ = MessageScope::Screen;
  bool visible_ = false;
};

}  // namespace knobify::messaging
