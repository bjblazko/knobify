#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>

#include "FixedTrig.h"
#include "GravityTerrain.h"

namespace knobify::games {

// Gravity (ADR 0023): set a craft down on a flat pad before the fuel runs
// out. The knob turns it, a held finger fires the engine, and the ground
// is unforgiving in four separate ways.
//
// Descended from Jim Storer's 1969 text game by way of the 1979 arcade
// cabinet, and renamed for the same reason Table Tennis was.
//
// Pure logic over an explicit clock, like TableTennisGame: no LVGL, no
// Arduino, integers in 1/16 px, and tick() split into fixed sub-steps so
// a flight is reproducible in a test and cannot change with the frame
// rate.
class GravityGame {
 public:
  static constexpr int32_t kUnit = 16;

  // The craft, in whole pixels. Narrower than the narrowest pad, or
  // aiming for the narrow one would be a joke rather than a challenge.
  static constexpr int kCraftWidth = 14;
  static constexpr int kCraftHeight = 16;

  // One knob detent. A first guess: Table Tennis's took three goes on the
  // device, so expect to tune this one too.
  static constexpr int kDegreesPerDetent = 6;

  static constexpr int32_t kStartY = 40 * kUnit;
  static constexpr int32_t kFullTank = 1000;

  enum class Phase : uint8_t { Ready, Flying, Landed, Crashed };

  // One-shot events only. The engine is deliberately *not* here: it is a
  // state, not an event, and the screen reads thrusting() to start and
  // stop its noise. Queued, it filled this queue with on/off pairs during
  // a normal descent and pushed the touchdown out of it -- the one sound
  // that had to survive (found by a test, 2026-09-18).
  enum class Sound : uint8_t { None, Touchdown, Crash };

  // Everything the ground refuses you for, kept apart so a near miss can
  // say which one it was rather than just "crashed".
  static constexpr int32_t kMaxLandingAngle = 10;              // degrees
  static constexpr int32_t kMaxLandingDescent = 25 * kUnit;    // px per second
  static constexpr int32_t kMaxLandingDrift = 15 * kUnit;      // px per second

  // `halfWidthAt` is how the round screen is described to the landscape
  // generator; the game itself knows nothing about the display.
  void start(uint32_t nowMs, uint32_t seed,
             GravityTerrain::HalfWidthFn halfWidthAt) {
    terrain_.generate(seed, halfWidthAt);
    x_ = GravityTerrain::kWorldWidth * kUnit / 2;
    y_ = kStartY;
    // A drift to cancel, so the first move is a decision rather than a
    // wait. Which way is the seed's, so a replay is a replay.
    vx_ = (seed & 1u) ? kStartDrift : -kStartDrift;
    vy_ = 0;
    angle_ = 0;
    fuel_ = kFullTank;
    thrusting_ = false;
    remX_ = remY_ = remVx_ = remVy_ = remFuel_ = 0;
    soundCount_ = 0;
    lastTickMs_ = nowMs;
    phase_ = Phase::Ready;
  }

  // A tap: launches from Ready, and starts a fresh landscape once the
  // flight is over. Ignored mid-flight, where the screen is the throttle.
  void tap(uint32_t nowMs, uint32_t seed,
           GravityTerrain::HalfWidthFn halfWidthAt) {
    if (phase_ == Phase::Ready) {
      phase_ = Phase::Flying;
    } else if (phase_ != Phase::Flying) {
      start(nowMs, seed, halfWidthAt);
      phase_ = Phase::Flying;
    }
  }

  // Knob detents, signed: clockwise turns the craft clockwise. Unlike the
  // paddle in Table Tennis there is nothing to invert -- a knob and a
  // rotation are the same gesture.
  //
  // Works before the launch as well as during the flight, like Table
  // Tennis's paddle does: a control that does nothing until you have
  // started reads as a broken control, and setting an attitude on the pad
  // is a legitimate choice anyway.
  void rotate(int detents) {
    if (phase_ != Phase::Flying && phase_ != Phase::Ready) return;
    angle_ = normalize(angle_ + detents * kDegreesPerDetent);
  }

  // The screen is the throttle: held is burning.
  void setThrusting(bool on) {
    if (phase_ != Phase::Flying) on = false;
    if (on && fuel_ <= 0) on = false;  // A dead engine stays dead.
    thrusting_ = on;
  }

  void tick(uint32_t nowMs) {
    uint32_t dtMs = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;
    if (dtMs > kMaxStepMs) dtMs = kMaxStepMs;  // A stall is not a fall.
    if (dtMs == 0 || phase_ != Phase::Flying) return;

    while (dtMs > 0) {
      const uint32_t slice = dtMs > kSubStepMs ? kSubStepMs : dtMs;
      advance(static_cast<int32_t>(slice));
      if (phase_ != Phase::Flying) return;
      dtMs -= slice;
    }
  }

  Sound takeSound() {
    if (soundCount_ == 0) return Sound::None;
    const Sound sound = sounds_[0];
    for (uint8_t i = 1; i < soundCount_; ++i) sounds_[i - 1] = sounds_[i];
    --soundCount_;
    return sound;
  }

  Phase phase() const { return phase_; }
  bool thrusting() const { return thrusting_; }
  int32_t fuel() const { return fuel_; }
  uint32_t score() const { return score_; }
  int32_t angle() const { return angle_; }

  // Whole pixels, for drawing.
  int32_t x() const { return x_ / kUnit; }
  int32_t y() const { return y_ / kUnit; }

  // Whole pixels per second, signed: negative vertical speed is a climb,
  // which is how an altimeter reads and how the arcade shows it.
  int32_t horizontalSpeed() const { return vx_ / kUnit; }
  int32_t verticalSpeed() const { return vy_ / kUnit; }

  // Height of the craft's feet above the ground directly beneath them.
  int32_t altitude() const {
    return (groundBeneath() - (y_ + kCraftHeight * kUnit / 2)) / kUnit;
  }

  const GravityTerrain &terrain() const { return terrain_; }

  // Whether this landing would be accepted, for the four separate reasons
  // it might not be. Public so the screen can say which one went wrong
  // and a test can fail them one at a time.
  bool overPad() const { return terrain_.padAt(x_ / kUnit) != nullptr; }
  bool uprightEnough() const { return std::abs(angle_) <= kMaxLandingAngle; }
  bool slowEnoughDown() const { return std::abs(vy_) <= kMaxLandingDescent; }
  bool slowEnoughSideways() const { return std::abs(vx_) <= kMaxLandingDrift; }

 private:
  // Lunar, not terrestrial: a fall you can still think your way out of.
  static constexpr int32_t kGravity = 30 * kUnit;      // px/s^2
  static constexpr int32_t kThrust = 90 * kUnit;       // px/s^2, three times
  static constexpr int32_t kBurnPerSecond = 200;       // tank units
  static constexpr int32_t kStartDrift = 12 * kUnit;   // px/s

  static constexpr uint32_t kSubStepMs = 8;
  static constexpr uint32_t kMaxStepMs = 100;

  // Integer integration that keeps what it could not divide.
  //
  // A drift of 10 px/s is 160 sub-units per second, and a 5 ms slice of
  // that is 0.8 -- which truncates to zero, every slice, forever. Traced
  // on a flight 2026-09-18: the craft's horizontal drift simply did not
  // exist, and a gentle descent hovered until the tank ran dry. Table
  // Tennis never hit this because a ball moves an order of magnitude
  // faster than a lander does; a game about arriving slowly lives exactly
  // in the range that rounds away.
  //
  // Carrying the remainder makes the sum exact over time while staying in
  // integers, which every test here depends on.
  static int32_t integrate(int32_t ratePerSecond, int32_t sliceMs,
                           int32_t &remainder) {
    const int32_t total = ratePerSecond * sliceMs + remainder;
    remainder = total % 1000;
    return total / 1000;
  }

  static constexpr int32_t normalize(int32_t degrees) {
    int32_t a = degrees % 360;
    if (a > 180) a -= 360;
    if (a < -180) a += 360;
    return a;
  }

  void pushSound(Sound sound) {
    if (soundCount_ < sounds_.size()) sounds_[soundCount_++] = sound;
  }

  int32_t groundBeneath() const {
    return terrain_.heightAt(x_ / kUnit) * kUnit;
  }

  void advance(int32_t sliceMs) {
    // One acceleration, integrated once: splitting thrust and gravity into
    // separate steps would round each of them on its own.
    int32_t ax = 0;
    int32_t ay = kGravity;

    if (thrusting_) {
      if (fuel_ > 0) {
        // The engine pushes along the craft's own heading: upright is
        // straight up, and a tilt trades lift for sideways travel. That
        // trade is the whole game.
        ax = kThrust * FixedTrig::sinScaled(angle_) / FixedTrig::kScale;
        ay -= kThrust * FixedTrig::cosScaled(angle_) / FixedTrig::kScale;
        fuel_ -= integrate(kBurnPerSecond, sliceMs, remFuel_);
        if (fuel_ <= 0) {
          fuel_ = 0;
          thrusting_ = false;
        }
      } else {
        thrusting_ = false;
      }
    }

    vx_ += integrate(ax, sliceMs, remVx_);
    vy_ += integrate(ay, sliceMs, remVy_);
    x_ += integrate(vx_, sliceMs, remX_);
    y_ += integrate(vy_, sliceMs, remY_);

    const int32_t world = GravityTerrain::kWorldWidth * kUnit;
    while (x_ < 0) x_ += world;
    while (x_ >= world) x_ -= world;

    // A ceiling rather than an open sky: a craft flown off the top is
    // gone, and there is nothing up there to go to.
    if (y_ < 0) {
      y_ = 0;
      if (vy_ < 0) vy_ = 0;
    }

    if (y_ + kCraftHeight * kUnit / 2 >= groundBeneath()) land();
  }

  void land() {
    y_ = groundBeneath() - kCraftHeight * kUnit / 2;
    thrusting_ = false;
    const Pad *pad = terrain_.padAt(x_ / kUnit);
    const bool safe = pad != nullptr && uprightEnough() && slowEnoughDown() &&
                      slowEnoughSideways();
    vx_ = 0;
    vy_ = 0;
    if (safe) {
      // What is left in the tank is what the landing was worth beyond
      // simply surviving it, and the narrow pad multiplies both.
      score_ += static_cast<uint32_t>(pad->multiplier) *
                static_cast<uint32_t>(50 + fuel_ / 10);
      phase_ = Phase::Landed;
      pushSound(Sound::Touchdown);
    } else {
      phase_ = Phase::Crashed;
      pushSound(Sound::Crash);
    }
  }

  GravityTerrain terrain_;
  Phase phase_ = Phase::Ready;
  uint32_t lastTickMs_ = 0;

  int32_t x_ = 0;
  int32_t y_ = kStartY;
  int32_t vx_ = 0;
  int32_t vy_ = 0;
  int32_t angle_ = 0;
  int32_t fuel_ = kFullTank;
  uint32_t score_ = 0;
  bool thrusting_ = false;
  // See integrate(): what each sum could not divide this slice.
  int32_t remX_ = 0, remY_ = 0, remVx_ = 0, remVy_ = 0, remFuel_ = 0;

  std::array<Sound, 6> sounds_{};
  uint8_t soundCount_ = 0;
};

}  // namespace knobify::games
