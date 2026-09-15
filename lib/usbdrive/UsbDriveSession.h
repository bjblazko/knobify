#pragma once

#include <cstdint>

#include "UsbStorage.h"

namespace knobify::usbdrive {

// Why the last session ended.
enum class UsbDriveEnd { None, Ejected, HostGone, Done };

enum class UsbDrivePhase {
  Off,
  // Card exported, no computer has configured the device yet.
  WaitingForHost,
  // A computer is attached and can read and write the card.
  Connected,
};

// One "USB drive" session (ADR 0016): the SD card is handed to a computer
// as a mass storage device, and the firmware must not touch it until the
// session ends. It ends when the computer ejects the drive, when the
// computer goes away (unplugged, asleep) for kHostGoneMs, or when the user
// taps Done. Pure logic over UsbStorage and an explicit clock, like
// SleepTimer.
class UsbDriveSession {
 public:
  // Rides out the brief detach/reattach of a USB bus reset.
  static constexpr uint32_t kHostGoneMs = 1500;

  explicit UsbDriveSession(UsbStorage &storage) : storage_(storage) {}

  // False (and stays Off) when the card can't be exported.
  bool start(uint32_t nowMs) {
    if (phase_ != UsbDrivePhase::Off) return true;
    if (!storage_.startExport()) return false;
    phase_ = UsbDrivePhase::WaitingForHost;
    hostSeenMs_ = nowMs;
    finished_ = false;
    return true;
  }

  void tick(uint32_t nowMs) {
    if (phase_ == UsbDrivePhase::Off) return;
    if (storage_.takeEjectRequest()) {
      end(UsbDriveEnd::Ejected);
      return;
    }
    const bool attached = storage_.hostAttached();
    if (attached) hostSeenMs_ = nowMs;
    if (phase_ == UsbDrivePhase::WaitingForHost) {
      if (attached) phase_ = UsbDrivePhase::Connected;
    } else if (nowMs - hostSeenMs_ >= kHostGoneMs) {
      end(UsbDriveEnd::HostGone);
    }
  }

  // The user leaves the screen. Anything the computer hadn't flushed yet
  // is lost -- the screen asks to eject first.
  void finish() { end(UsbDriveEnd::Done); }

  UsbDrivePhase phase() const { return phase_; }
  bool active() const { return phase_ != UsbDrivePhase::Off; }
  UsbDriveEnd lastEnd() const { return lastEnd_; }

  // True once after a session ended: the card may have changed, so the
  // library needs a rescan.
  bool takeFinished() {
    bool finished = finished_;
    finished_ = false;
    return finished;
  }

 private:
  void end(UsbDriveEnd reason) {
    if (phase_ == UsbDrivePhase::Off) return;
    storage_.stopExport();
    phase_ = UsbDrivePhase::Off;
    lastEnd_ = reason;
    finished_ = true;
  }

  UsbStorage &storage_;
  UsbDrivePhase phase_ = UsbDrivePhase::Off;
  uint32_t hostSeenMs_ = 0;
  bool finished_ = false;
  UsbDriveEnd lastEnd_ = UsbDriveEnd::None;
};

}  // namespace knobify::usbdrive
