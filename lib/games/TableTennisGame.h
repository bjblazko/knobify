#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>

namespace knobify::games {

// Table Tennis (ADR 0022), as close to the 1972 original as this hardware
// allows -- Magnavox Odyssey's game of that name, which Atari's better
// known machine copied and lost a lawsuit over.
// Pure logic over an explicit clock, like everything else in this project
// that isn't a driver (docs/coding-guidelines.md): no LVGL, no Arduino, so
// the whole game is host-testable under `pio test -e native`.
//
// The court is the largest 4:3 rectangle that fits the round 360px panel
// (288x216 -- 288^2 + 216^2 = 360^2 exactly), and its top and bottom
// edges are the walls the ball bounces off. kCourtHeight below is the
// play area *between* those walls, so the ball's bounce line is a wall's
// inner face rather than somewhere inside it. Coordinates are court-local
// with the origin at the play area's top-left corner; the screen layer
// adds the offset.
//
// Geometry and velocities are integers in 1/16 px (`kUnit`), so a rally is
// bit-for-bit reproducible in a test and cannot drift with the frame rate.
// tick() consumes elapsed milliseconds and splits them into kSubStepMs
// slices, which keeps collision detection a plain overlap test: within one
// slice the ball never moves more than about 2 px, so it cannot tunnel
// through a 6 px paddle even after a long stall.
class TableTennisGame {
 public:
  // --- Court, in whole pixels (the screen layer draws these directly) ---
  static constexpr int kCourtWidth = 288;
  static constexpr int kWallThickness = 4;
  // The inscribed 4:3 rectangle, walls included.
  static constexpr int kCourtOuterHeight = 216;
  static constexpr int kCourtHeight = kCourtOuterHeight - 2 * kWallThickness;
  static constexpr int kBallSize = 8;      // The original's ball is square.
  static constexpr int kPaddleWidth = 6;
  static constexpr int kPaddleHeight = 32;
  static constexpr int kPlayerPaddleX = 8;
  // The paddle is divided into this many zones, each returning the ball
  // at its own angle -- the mechanic the game is actually played with.
  static constexpr int kZones = 8;
  static constexpr int kAiPaddleX = kCourtWidth - 8 - kPaddleWidth;

  static constexpr int kWinningScore = 11;  // The arcade game plays to 11.

  // A detent moves the paddle a whole number of its own zones (see
  // bounceOffPaddle), never a fraction: the original's controller was a
  // continuous potentiometer that could land anywhere on the paddle, and
  // a detented knob cannot, so the two grids are lined up rather than
  // left to straddle each other.
  //
  // Three zones per detent. Tuned on the device in two steps (user,
  // 2026-09-18): one zone per detent put the paddle's 176 px of travel at
  // about 1.5 turns of the ~30-detent encoder and played sluggish, two
  // was better at 22 detents, three lands at 14 -- roughly half a turn
  // end to end, which is where it stopped feeling like winding.
  //
  // The cost is aiming: the paddle can be placed to within three zones
  // rather than one. That is affordable because the ball's own position
  // is continuous and supplies the fine control -- choosing "the end of
  // the paddle" still works, choosing one zone over its neighbour does
  // not, and the first is what the game is actually played with.
  static constexpr int kZonesPerDetent = 3;
  static constexpr int kPixelsPerDetent = kPaddleHeight / kZones * kZonesPerDetent;

  // Sub-pixel resolution: positions and velocities are integers in 1/16
  // px (velocities per second), so a rally is bit-for-bit reproducible.
  static constexpr int32_t kUnit = 16;

  // The eight-zone paddle, as a rule on its own: where the ball lands on
  // the paddle sets the return angle, steeply away at the ends and
  // shallow -- but never flat -- at the middle. This is the mechanic the
  // game is actually played with, and the first thing a "nicer" physics model
  // tends to lose, so it is a pure function that can be checked directly
  // rather than something only observable by playing a rally out.
  //
  // contactOffset is the ball's centre relative to the paddle's top edge
  // and speed the ball's horizontal speed, both in the units above; the
  // result is the vertical speed the ball leaves with. The steepest zone
  // trades all of the ball's speed into angle and no more, so no return
  // is ever steeper than 45 degrees.
  static constexpr int32_t deflection(int32_t contactOffset, int32_t speed) {
    const int32_t span = kPaddleHeight * kUnit;
    if (contactOffset < 0) contactOffset = 0;
    if (contactOffset >= span) contactOffset = span - 1;
    const int zone = static_cast<int>(contactOffset * kZones / span);
    // Quarter-steps of the ball's own speed, top to bottom. The middle
    // two are +/-1 rather than 0: a flat ball is the one trajectory the
    // zones cannot answer.
    constexpr int kSteps[kZones] = {-4, -3, -2, -1, 1, 2, 3, 4};
    return speed * kSteps[zone] / 4;
  }

  enum class Phase : uint8_t {
    Ready,    // Before the first serve: waiting for a tap.
    Serving,  // Ball parked at the centre for a beat, then off.
    Rally,    // In play.
    Scored,   // A point just landed; the next serve follows on its own.
    Over,     // Someone reached kWinningScore. A tap starts a new match.
  };

  // The original's three blips. The screen layer drains these each frame
  // and hands them to the tone generator.
  enum class Sound : uint8_t { None, Paddle, Wall, Score };

  // Begins a new match: scores to nil, ball parked at the centre, waiting
  // for a tap. Nothing moves until the player asks it to -- the original
  // wanted a coin and a start button before it served either.
  void start(uint32_t nowMs) {
    playerScore_ = 0;
    aiScore_ = 0;
    playerPaddleY_ = centredPaddleY();
    aiPaddleY_ = centredPaddleY();
    soundCount_ = 0;
    lastTickMs_ = nowMs;
    serveTowardPlayer_ = true;
    serveDown_ = true;
    ballX_ = centredBallX();
    ballY_ = kMaxBallY / 2;
    hits_ = 0;
    phase_ = Phase::Ready;
    phaseStartedMs_ = nowMs;
  }

  // A tap: serves from Ready, and starts a new match from Over. Ignored
  // mid-rally, so a stray touch cannot restart a game being won.
  void tap(uint32_t nowMs) {
    if (phase_ == Phase::Over) {
      start(nowMs);
    } else if (phase_ == Phase::Ready) {
      beginServe(nowMs);
    }
  }

  // Knob detents, signed: clockwise (positive) moves the paddle *up*.
  //
  // That is the opposite sense from every list in this app, where a
  // clockwise detent moves the highlight down. Deliberate, and judged on
  // the device (user, 2026-09-18): a list is read top to bottom, a paddle
  // is held, and the two do not want the same sense from the same knob.
  //
  // Works in every phase, so the player can be in position before the
  // serve rather than scrambling after it.
  void movePlayerPaddle(int detents) {
    playerPaddleY_ =
        clampPaddle(playerPaddleY_ - detents * kPixelsPerDetent * kUnit);
  }

  void tick(uint32_t nowMs) {
    uint32_t dtMs = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;
    // After a stall (a blocking SD read, a screen swap) the clock jumps.
    // Pretend it didn't rather than teleporting the ball, the same way
    // ScreenManager::tickSpectrum() clamps its own delta.
    if (dtMs > kMaxStepMs) dtMs = kMaxStepMs;
    if (dtMs == 0) return;

    switch (phase_) {
      case Phase::Ready:
      case Phase::Over:
        return;
      case Phase::Serving:
        if (nowMs - phaseStartedMs_ >= kServePauseMs) launch();
        return;
      case Phase::Scored:
        if (nowMs - phaseStartedMs_ >= kScoredPauseMs) beginServe(nowMs);
        return;
      case Phase::Rally:
        break;
    }

    while (dtMs > 0) {
      const uint32_t slice = dtMs > kSubStepMs ? kSubStepMs : dtMs;
      advance(static_cast<int32_t>(slice));
      if (phase_ != Phase::Rally) return;  // The slice ended the point.
      dtMs -= slice;
    }
  }

  // One queued blip, oldest first; Sound::None once drained.
  Sound takeSound() {
    if (soundCount_ == 0) return Sound::None;
    const Sound sound = sounds_[0];
    for (uint8_t i = 1; i < soundCount_; ++i) sounds_[i - 1] = sounds_[i];
    --soundCount_;
    return sound;
  }

  Phase phase() const { return phase_; }
  int playerScore() const { return playerScore_; }
  int aiScore() const { return aiScore_; }

  // Whole-pixel court coordinates, for drawing.
  int ballX() const { return ballX_ / kUnit; }
  int ballY() const { return ballY_ / kUnit; }
  int playerPaddleY() const { return playerPaddleY_ / kUnit; }
  int aiPaddleY() const { return aiPaddleY_ / kUnit; }

  // The ball is parked (and so drawn at the centre) outside a rally.
  bool ballVisible() const { return phase_ != Phase::Over; }

  // Ball velocity in 1/16 px per second. Nothing on screen needs this --
  // it is here so the eight-zone deflection, the mechanic the whole game
  // rests on, can be asserted directly in a test rather than inferred
  // from where the ball drifted (which a wall bounce quietly falsifies).
  int32_t ballVelocityX() const { return vx_; }
  int32_t ballVelocityY() const { return vy_; }

 private:
  // Horizontal ball speed per tier, in 1/16 px per second. The original
  // speeds the ball up twice during a rally -- after the 4th hit and again
  // after the 12th -- and resets it on every point.
  static constexpr int32_t kBallSpeeds[3] = {2400, 3100, 3800};
  static constexpr int kSpeedUpAfterHits[2] = {4, 12};

  // The AI's paddle is capped just under the ball's steepest vertical
  // speed (kBallSpeeds[0], the slowest tier's full deflection), so it
  // reaches everything except a hard-angled return. That is what keeps it
  // beatable without making it look broken: it loses to the shot the
  // eight-zone paddle rewards, not to random misses.
  static constexpr int32_t kAiPaddleSpeed = 2000;

  static constexpr uint32_t kServePauseMs = 900;
  static constexpr uint32_t kScoredPauseMs = 1200;
  static constexpr uint32_t kSubStepMs = 8;
  static constexpr uint32_t kMaxStepMs = 100;

  static constexpr int32_t kMaxBallY = (kCourtHeight - kBallSize) * kUnit;
  static constexpr int32_t kMaxPaddleY = (kCourtHeight - kPaddleHeight) * kUnit;

  static constexpr int32_t centredPaddleY() { return kMaxPaddleY / 2; }
  static constexpr int32_t centredBallX() {
    return (kCourtWidth - kBallSize) * kUnit / 2;
  }

  static int32_t clampPaddle(int32_t y) {
    if (y < 0) return 0;
    if (y > kMaxPaddleY) return kMaxPaddleY;
    return y;
  }

  void pushSound(Sound sound) {
    if (soundCount_ < sounds_.size()) sounds_[soundCount_++] = sound;
  }

  void beginServe(uint32_t nowMs) {
    ballX_ = centredBallX();
    ballY_ = kMaxBallY / 2;
    hits_ = 0;
    phase_ = Phase::Serving;
    phaseStartedMs_ = nowMs;
  }

  void launch() {
    const int32_t speed = kBallSpeeds[0];
    vx_ = serveTowardPlayer_ ? -speed : speed;
    // Never perfectly horizontal, not even off the serve -- a flat ball
    // is the one trajectory the paddle's zones cannot answer.
    vy_ = serveDown_ ? speed / 4 : -speed / 4;
    serveDown_ = !serveDown_;
    phase_ = Phase::Rally;
  }

  void advance(int32_t sliceMs) {
    ballX_ += vx_ * sliceMs / 1000;
    ballY_ += vy_ * sliceMs / 1000;

    if (ballY_ < 0) {
      ballY_ = -ballY_;
      vy_ = -vy_;
      pushSound(Sound::Wall);
    } else if (ballY_ > kMaxBallY) {
      ballY_ = 2 * kMaxBallY - ballY_;
      vy_ = -vy_;
      pushSound(Sound::Wall);
    }

    moveAi(sliceMs);

    if (vx_ < 0) {
      const int32_t face = (kPlayerPaddleX + kPaddleWidth) * kUnit;
      if (ballX_ <= face && ballX_ + kBallSize * kUnit > kPlayerPaddleX * kUnit &&
          overlaps(playerPaddleY_)) {
        ballX_ = face;
        bounceOffPaddle(playerPaddleY_);
      }
    } else {
      const int32_t face = kAiPaddleX * kUnit;
      if (ballX_ + kBallSize * kUnit >= face &&
          ballX_ < (kAiPaddleX + kPaddleWidth) * kUnit && overlaps(aiPaddleY_)) {
        ballX_ = face - kBallSize * kUnit;
        bounceOffPaddle(aiPaddleY_);
      }
    }

    if (ballX_ + kBallSize * kUnit < 0) {
      score(/*toPlayer=*/false);
    } else if (ballX_ > kCourtWidth * kUnit) {
      score(/*toPlayer=*/true);
    }
  }

  bool overlaps(int32_t paddleY) const {
    return ballY_ + kBallSize * kUnit > paddleY &&
           ballY_ < paddleY + kPaddleHeight * kUnit;
  }

  void bounceOffPaddle(int32_t paddleY) {
    const int32_t contactOffset = ballY_ + kBallSize * kUnit / 2 - paddleY;

    ++hits_;
    int tier = 0;
    if (hits_ >= kSpeedUpAfterHits[1]) {
      tier = 2;
    } else if (hits_ >= kSpeedUpAfterHits[0]) {
      tier = 1;
    }
    const int32_t speed = kBallSpeeds[tier];

    vx_ = vx_ < 0 ? speed : -speed;
    vy_ = deflection(contactOffset, speed);
    pushSound(Sound::Paddle);
  }

  void moveAi(int32_t sliceMs) {
    // It only chases while the ball is coming its way; otherwise it waits
    // where it is, like the home console's did.
    if (vx_ <= 0) return;
    const int32_t target =
        clampPaddle(ballY_ + kBallSize * kUnit / 2 - kPaddleHeight * kUnit / 2);
    const int32_t reach = kAiPaddleSpeed * sliceMs / 1000;
    const int32_t gap = target - aiPaddleY_;
    if (gap > reach) {
      aiPaddleY_ += reach;
    } else if (gap < -reach) {
      aiPaddleY_ -= reach;
    } else {
      aiPaddleY_ = target;
    }
  }

  void score(bool toPlayer) {
    if (toPlayer) {
      ++playerScore_;
    } else {
      ++aiScore_;
    }
    pushSound(Sound::Score);
    // The loser of the point serves next -- the original's courtesy.
    serveTowardPlayer_ = !toPlayer;
    ballX_ = centredBallX();
    ballY_ = kMaxBallY / 2;
    if (playerScore_ >= kWinningScore || aiScore_ >= kWinningScore) {
      phase_ = Phase::Over;
    } else {
      phase_ = Phase::Scored;
    }
    phaseStartedMs_ = lastTickMs_;
  }

  Phase phase_ = Phase::Ready;
  uint32_t phaseStartedMs_ = 0;
  uint32_t lastTickMs_ = 0;

  int32_t ballX_ = centredBallX();
  int32_t ballY_ = kMaxBallY / 2;
  int32_t vx_ = 0;
  int32_t vy_ = 0;
  int32_t playerPaddleY_ = centredPaddleY();
  int32_t aiPaddleY_ = centredPaddleY();

  int playerScore_ = 0;
  int aiScore_ = 0;
  int hits_ = 0;
  bool serveTowardPlayer_ = true;
  bool serveDown_ = true;

  std::array<Sound, 6> sounds_{};
  uint8_t soundCount_ = 0;
};

}  // namespace knobify::games
