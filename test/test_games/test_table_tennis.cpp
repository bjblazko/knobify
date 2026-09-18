#include <unity.h>

#include <cstdint>
#include <cstdlib>

#include "TableTennisGame.h"

using knobify::games::TableTennisGame;
using Phase = TableTennisGame::Phase;
using Sound = TableTennisGame::Sound;

void setUp() {}
void tearDown() {}

namespace {

// Runs the clock forward in realistic ~30fps frames, so every test
// exercises the same sub-stepping the device does.
uint32_t run(TableTennisGame &game, uint32_t fromMs, uint32_t forMs,
             uint32_t frameMs = 33) {
  uint32_t now = fromMs;
  const uint32_t until = fromMs + forMs;
  while (now < until) {
    now += frameMs;
    game.tick(now);
  }
  return now;
}

// Starts a match and gets the ball actually moving.
uint32_t serve(TableTennisGame &game) {
  game.start(0);
  game.tap(0);
  return run(game, 0, 1000);
}

// Holds the paddle under the ball, so a rally continues indefinitely.
void trackBallWithPlayerPaddle(TableTennisGame &game) {
  const int wanted = game.ballY() + TableTennisGame::kBallSize / 2 -
                     TableTennisGame::kPaddleHeight / 2;
  const int gap = wanted - game.playerPaddleY();
  if (gap != 0) game.movePlayerPaddle(-gap / TableTennisGame::kPixelsPerDetent);
}

int countSounds(TableTennisGame &game, Sound wanted) {
  int seen = 0;
  for (Sound sound = game.takeSound(); sound != Sound::None;
       sound = game.takeSound()) {
    if (sound == wanted) ++seen;
  }
  return seen;
}

// Keeps the chosen zone (0 = top edge, 7 = bottom edge) lined up with the
// ball all the way in, then returns once the paddle has struck it. Aiming
// once from a distance is not enough: the ball keeps drifting vertically
// over the last stretch, which quietly slides the contact into a
// neighbouring zone.
uint32_t returnBallWithZone(TableTennisGame &game, uint32_t now, int zone,
                            int &paddleHitsOut) {
  paddleHitsOut = 0;
  constexpr int kZoneHeight = TableTennisGame::kPaddleHeight / TableTennisGame::kZones;
  // Keep the ball's centre this far inside the paddle. The paddle can only
  // be placed to within kPixelsPerDetent, so aiming the very edge of it at
  // the ball is a coin toss between a hit and a miss -- and a test that
  // misses is testing the aim, not the deflection.
  constexpr int kMargin = TableTennisGame::kPixelsPerDetent / 2;

  for (int frame = 0; frame < 600 && paddleHitsOut == 0 &&
                      game.phase() == Phase::Rally;
       ++frame) {
    const int ballCentre = game.ballY() + TableTennisGame::kBallSize / 2;
    const int zoneCentre = zone * kZoneHeight + kZoneHeight / 2;
    int wanted = ballCentre - zoneCentre;
    if (wanted > ballCentre - kMargin) wanted = ballCentre - kMargin;
    if (wanted < ballCentre - TableTennisGame::kPaddleHeight + kMargin) {
      wanted = ballCentre - TableTennisGame::kPaddleHeight + kMargin;
    }
    // Round rather than truncate, halving the placement error.
    const int gap = wanted - game.playerPaddleY();
    const int bias = gap >= 0 ? TableTennisGame::kPixelsPerDetent / 2
                              : -TableTennisGame::kPixelsPerDetent / 2;
    game.movePlayerPaddle(-(gap + bias) / TableTennisGame::kPixelsPerDetent);

    now += 8;
    game.tick(now);
    paddleHitsOut += countSounds(game, Sound::Paddle);
  }
  return now;
}

}  // namespace

// --- The court and the serve ---------------------------------------------

void test_the_court_is_the_largest_four_by_three_rect_in_the_circle() {
  // Walls included, the court is 288x216, whose diagonal is exactly the
  // round panel's 360px diameter -- the largest 4:3 field that fits.
  TEST_ASSERT_EQUAL_INT(4 * 72, TableTennisGame::kCourtWidth);
  TEST_ASSERT_EQUAL_INT(3 * 72, TableTennisGame::kCourtOuterHeight);
  TEST_ASSERT_EQUAL_INT(360 * 360,
                        TableTennisGame::kCourtWidth * TableTennisGame::kCourtWidth +
                            TableTennisGame::kCourtOuterHeight *
                                TableTennisGame::kCourtOuterHeight);
  // The play area is what is left between the two walls.
  TEST_ASSERT_EQUAL_INT(TableTennisGame::kCourtOuterHeight - 2 * TableTennisGame::kWallThickness,
                        TableTennisGame::kCourtHeight);
}

void test_a_new_match_waits_for_a_tap() {
  TableTennisGame game;
  game.start(0);
  TEST_ASSERT_TRUE(game.phase() == Phase::Ready);
  const int restingX = game.ballX();

  run(game, 0, 3000);
  TEST_ASSERT_TRUE(game.phase() == Phase::Ready);
  TEST_ASSERT_EQUAL_INT(restingX, game.ballX());
}

void test_a_tap_serves() {
  TableTennisGame game;
  const uint32_t now = serve(game);
  TEST_ASSERT_TRUE(game.phase() == Phase::Rally);
  (void)now;
}

void test_the_serve_is_never_horizontal() {
  // A flat ball is the one trajectory the paddle's zones cannot answer.
  TableTennisGame game;
  game.start(0);
  game.tap(0);
  const uint32_t launched = run(game, 0, 1000);
  const int y = game.ballY();
  run(game, launched, 200);
  TEST_ASSERT_NOT_EQUAL(y, game.ballY());
}

void test_a_tap_mid_rally_does_not_restart_the_match() {
  TableTennisGame game;
  uint32_t now = serve(game);
  game.tap(now);
  TEST_ASSERT_TRUE(game.phase() == Phase::Rally);
}

// --- Walls ---------------------------------------------------------------

void test_the_ball_bounces_off_both_walls_and_stays_in_the_court() {
  TableTennisGame game;
  uint32_t now = serve(game);
  for (int frame = 0; frame < 600; ++frame) {
    now += 33;
    game.tick(now);
    trackBallWithPlayerPaddle(game);
    TEST_ASSERT_TRUE(game.ballY() >= 0);
    TEST_ASSERT_TRUE(game.ballY() <= TableTennisGame::kCourtHeight - TableTennisGame::kBallSize);
  }
}

void test_hitting_a_wall_makes_a_wall_blip() {
  TableTennisGame game;
  uint32_t now = serve(game);
  int walls = 0;
  for (int frame = 0; frame < 300 && walls == 0; ++frame) {
    now += 33;
    game.tick(now);
    trackBallWithPlayerPaddle(game);
    walls += countSounds(game, Sound::Wall);
  }
  TEST_ASSERT_TRUE(walls > 0);
}

// --- The paddle's eight zones --------------------------------------------

void test_the_eight_zones_deflect_from_steeply_up_to_steeply_down() {
  // The rule on its own, at the centre of each zone: the deflection grows
  // steadily from steeply up at the top edge to steeply down at the
  // bottom, and never passes through flat.
  constexpr int32_t kSpeed = 2400;
  constexpr int32_t kZoneSpan = TableTennisGame::kPaddleHeight * TableTennisGame::kUnit /
                                TableTennisGame::kZones;
  int32_t previous = INT32_MIN;
  for (int zone = 0; zone < TableTennisGame::kZones; ++zone) {
    const int32_t vy =
        TableTennisGame::deflection(zone * kZoneSpan + kZoneSpan / 2, kSpeed);
    if (zone < TableTennisGame::kZones / 2) {
      TEST_ASSERT_TRUE(vy < 0);
    } else {
      TEST_ASSERT_TRUE(vy > 0);
    }
    TEST_ASSERT_TRUE(vy > previous);
    previous = vy;
  }
}

void test_the_steepest_return_is_forty_five_degrees() {
  // The outermost zones trade all of the ball's speed into angle and no
  // more: the original never sends the ball straight up the court.
  constexpr int32_t kSpeed = 2400;
  constexpr int32_t kSpan = TableTennisGame::kPaddleHeight * TableTennisGame::kUnit;
  TEST_ASSERT_EQUAL_INT32(-kSpeed, TableTennisGame::deflection(0, kSpeed));
  TEST_ASSERT_EQUAL_INT32(kSpeed, TableTennisGame::deflection(kSpan - 1, kSpeed));

  // And nothing in between is steeper than that, at any contact point.
  for (int32_t offset = 0; offset < kSpan; ++offset) {
    const int32_t vy = TableTennisGame::deflection(offset, kSpeed);
    TEST_ASSERT_TRUE(vy <= kSpeed);
    TEST_ASSERT_TRUE(vy >= -kSpeed);
    TEST_ASSERT_NOT_EQUAL(0, vy);
  }
}

void test_a_contact_off_the_paddle_is_clamped_to_its_nearest_end() {
  // Defensive: the ball's centre can sit a hair outside the paddle on the
  // frame the overlap test accepts, and a zone index off the end of the
  // table would be a memory bug, not a gameplay one.
  constexpr int32_t kSpeed = 2400;
  constexpr int32_t kSpan = TableTennisGame::kPaddleHeight * TableTennisGame::kUnit;
  TEST_ASSERT_EQUAL_INT32(TableTennisGame::deflection(0, kSpeed),
                          TableTennisGame::deflection(-500, kSpeed));
  TEST_ASSERT_EQUAL_INT32(TableTennisGame::deflection(kSpan - 1, kSpeed),
                          TableTennisGame::deflection(kSpan + 500, kSpeed));
}

void test_a_detent_is_a_whole_number_of_paddle_zones() {
  // The two grids stay lined up: a detent may not leave the paddle
  // straddling a zone boundary, whatever the speed is tuned to.
  const int zoneHeight = TableTennisGame::kPaddleHeight / TableTennisGame::kZones;
  TEST_ASSERT_EQUAL_INT(0, TableTennisGame::kPixelsPerDetent % zoneHeight);
  TEST_ASSERT_EQUAL_INT(TableTennisGame::kZonesPerDetent,
                        TableTennisGame::kPixelsPerDetent / zoneHeight);
}

void test_aiming_high_or_low_on_the_paddle_returns_the_ball_that_way() {
  // The rule above, reached through the knob: the two ends of the paddle
  // are far enough apart to be chosen deliberately even though the knob
  // places it only to within kZonesPerDetent.
  TableTennisGame high;
  int hits = 0;
  returnBallWithZone(high, serve(high), 0, hits);
  TEST_ASSERT_TRUE(hits > 0);
  TEST_ASSERT_TRUE(high.ballVelocityY() < 0);

  TableTennisGame low;
  hits = 0;
  returnBallWithZone(low, serve(low), TableTennisGame::kZones - 1, hits);
  TEST_ASSERT_TRUE(hits > 0);
  TEST_ASSERT_TRUE(low.ballVelocityY() > 0);
}

void test_a_returned_ball_travels_back_up_the_court() {
  TableTennisGame game;
  uint32_t now = serve(game);
  int paddleHits = 0;
  for (int frame = 0; frame < 400 && paddleHits == 0; ++frame) {
    now += 8;
    game.tick(now);
    trackBallWithPlayerPaddle(game);
    paddleHits += countSounds(game, Sound::Paddle);
  }
  TEST_ASSERT_TRUE(paddleHits > 0);

  const int before = game.ballX();
  run(game, now, 100, 8);
  TEST_ASSERT_TRUE(game.ballX() > before);
}

// --- Scoring -------------------------------------------------------------

void test_a_missed_ball_scores_for_the_other_side() {
  TableTennisGame game;
  game.start(0);
  game.tap(0);
  uint32_t now = run(game, 0, 1000);
  // Park the player's paddle at a wall; the serve heads for the middle.
  game.movePlayerPaddle(-100);

  while (game.phase() == Phase::Rally) {
    now += 33;
    game.tick(now);
  }
  TEST_ASSERT_EQUAL_INT(1, game.aiScore());
  TEST_ASSERT_EQUAL_INT(0, game.playerScore());
  TEST_ASSERT_TRUE(game.phase() == Phase::Scored);
  TEST_ASSERT_EQUAL_INT(1, countSounds(game, Sound::Score));
}

void test_the_next_serve_goes_to_whoever_was_scored_on() {
  TableTennisGame game;
  game.start(0);
  game.tap(0);
  uint32_t now = run(game, 0, 1000);
  game.movePlayerPaddle(-100);
  while (game.phase() == Phase::Rally) {
    now += 33;
    game.tick(now);
  }
  // The player conceded, so the ball comes back at the player.
  now = run(game, now, 2500);
  TEST_ASSERT_TRUE(game.phase() == Phase::Rally);
  const int before = game.ballX();
  run(game, now, 100);
  TEST_ASSERT_TRUE(game.ballX() < before);
}

void test_the_match_ends_at_eleven() {
  TableTennisGame game;
  game.start(0);
  game.tap(0);
  uint32_t now = 0;
  game.movePlayerPaddle(-100);
  for (int frame = 0; frame < 4000 && game.phase() != Phase::Over; ++frame) {
    now += 33;
    game.tick(now);
    game.movePlayerPaddle(-100);  // Never defend.
  }
  TEST_ASSERT_TRUE(game.phase() == Phase::Over);
  TEST_ASSERT_EQUAL_INT(TableTennisGame::kWinningScore, game.aiScore());
}

void test_a_tap_after_the_match_starts_a_new_one() {
  TableTennisGame game;
  game.start(0);
  game.tap(0);
  uint32_t now = 0;
  for (int frame = 0; frame < 4000 && game.phase() != Phase::Over; ++frame) {
    now += 33;
    game.tick(now);
    game.movePlayerPaddle(-100);
  }
  TEST_ASSERT_TRUE(game.phase() == Phase::Over);

  game.tap(now);
  TEST_ASSERT_TRUE(game.phase() == Phase::Ready);
  TEST_ASSERT_EQUAL_INT(0, game.aiScore());
  TEST_ASSERT_EQUAL_INT(0, game.playerScore());
}

// --- The paddles ---------------------------------------------------------

void test_a_clockwise_detent_moves_the_paddle_up() {
  // The opposite sense from a list, where clockwise moves the highlight
  // down. Deliberate (ADR 0022), so it is pinned rather than left to be
  // "fixed" by someone matching it to the lists.
  TableTennisGame game;
  game.start(0);
  const int start = game.playerPaddleY();
  game.movePlayerPaddle(3);
  TEST_ASSERT_EQUAL_INT(start - 3 * TableTennisGame::kPixelsPerDetent,
                        game.playerPaddleY());
}

void test_the_knob_stops_the_paddle_at_the_walls() {
  TableTennisGame game;
  game.start(0);
  game.movePlayerPaddle(1000);
  TEST_ASSERT_EQUAL_INT(0, game.playerPaddleY());
  game.movePlayerPaddle(-1000);
  TEST_ASSERT_EQUAL_INT(TableTennisGame::kCourtHeight - TableTennisGame::kPaddleHeight,
                        game.playerPaddleY());
}

void test_the_whole_court_is_about_one_sweep_of_the_knob() {
  // ~30 detents per revolution, so this is half a turn to one turn end to
  // end. Pinning the range is what stops a later tweak to either grid
  // from making the paddle slow again (it was, at one zone per detent) or
  // so twitchy that a single click crosses half the court.
  const int travel = TableTennisGame::kCourtHeight - TableTennisGame::kPaddleHeight;
  const int detentsForFullTravel = travel / TableTennisGame::kPixelsPerDetent;
  TEST_ASSERT_TRUE(detentsForFullTravel >= 12);
  TEST_ASSERT_TRUE(detentsForFullTravel <= 30);
  // And a single detent never moves the paddle by more than its own body.
  TEST_ASSERT_TRUE(TableTennisGame::kPixelsPerDetent <
                   TableTennisGame::kPaddleHeight / 2);
}

void test_the_ai_paddle_never_moves_faster_than_its_cap() {
  // Its cap is what makes it beatable: it cannot answer a hard angle.
  TableTennisGame game;
  uint32_t now = serve(game);
  int previous = game.aiPaddleY();
  for (int frame = 0; frame < 600; ++frame) {
    now += 33;
    game.tick(now);
    trackBallWithPlayerPaddle(game);
    const int moved = std::abs(game.aiPaddleY() - previous);
    // 2000/16 px per second over a 33ms frame is ~4px; allow rounding.
    TEST_ASSERT_TRUE(moved <= 6);
    previous = game.aiPaddleY();
  }
}

void test_the_ai_paddle_stays_inside_the_court() {
  TableTennisGame game;
  uint32_t now = serve(game);
  for (int frame = 0; frame < 600; ++frame) {
    now += 33;
    game.tick(now);
    trackBallWithPlayerPaddle(game);
    TEST_ASSERT_TRUE(game.aiPaddleY() >= 0);
    TEST_ASSERT_TRUE(game.aiPaddleY() <=
                     TableTennisGame::kCourtHeight - TableTennisGame::kPaddleHeight);
  }
}

void test_the_ai_can_be_beaten_with_a_hard_angle() {
  // The AI is capped just under the ball's steepest vertical speed, so a
  // return off the end of the paddle gets past it. That is the whole
  // point of the eight zones: the game rewards aim, not reflexes.
  TableTennisGame game;
  uint32_t now = serve(game);
  int zone = 0;
  for (int rally = 0; rally < 40 && game.playerScore() == 0; ++rally) {
    int paddleHits = 0;
    now = returnBallWithZone(game, now, zone, paddleHits);
    if (paddleHits == 0) break;
    // Alternate ends, so whichever way the AI is caught out, it is caught.
    zone = zone == 0 ? TableTennisGame::kZones - 1 : 0;
    // Let the return play out until the ball comes back or a point lands.
    for (int frame = 0; frame < 400 && game.ballVelocityX() > 0 &&
                        game.phase() == Phase::Rally;
         ++frame) {
      now += 8;
      game.tick(now);
    }
    if (game.phase() != Phase::Rally) now = run(game, now, 2500);
  }
  TEST_ASSERT_TRUE(game.playerScore() > 0);
}

void test_the_ai_returns_a_ball_played_down_the_middle() {
  // It must not be a pushover either: a lazy centre return comes back.
  TableTennisGame game;
  uint32_t now = serve(game);
  int paddleHits = 0;
  now = returnBallWithZone(game, now, TableTennisGame::kZones / 2, paddleHits);
  TEST_ASSERT_TRUE(paddleHits > 0);

  int aiHits = 0;
  for (int frame = 0; frame < 600 && aiHits == 0 && game.phase() == Phase::Rally;
       ++frame) {
    now += 8;
    game.tick(now);
    aiHits += countSounds(game, Sound::Paddle);
  }
  TEST_ASSERT_TRUE(aiHits > 0);
  TEST_ASSERT_EQUAL_INT(0, game.playerScore());
}

// --- Robustness ----------------------------------------------------------

void test_a_long_stall_does_not_teleport_the_ball_through_a_paddle() {
  TableTennisGame game;
  uint32_t now = serve(game);
  // A blocking SD read, a screen swap: the clock jumps by half a second.
  for (int frame = 0; frame < 40; ++frame) {
    trackBallWithPlayerPaddle(game);
    now += 500;
    game.tick(now);
    TEST_ASSERT_TRUE(game.ballX() >= -TableTennisGame::kBallSize);
    TEST_ASSERT_TRUE(game.ballX() <= TableTennisGame::kCourtWidth);
  }
}

void test_each_blip_is_handed_out_exactly_once() {
  TableTennisGame game;
  uint32_t now = serve(game);
  int total = 0;
  for (int frame = 0; frame < 200; ++frame) {
    now += 33;
    game.tick(now);
    trackBallWithPlayerPaddle(game);
    for (Sound sound = game.takeSound(); sound != Sound::None;
         sound = game.takeSound()) {
      ++total;
    }
  }
  TEST_ASSERT_TRUE(total > 0);
  TEST_ASSERT_TRUE(game.takeSound() == Sound::None);
}

void test_the_ball_speeds_up_during_a_long_rally() {
  // The original steps the ball up after the 4th hit and again after the
  // 12th. Compare the speed it leaves the paddle with, early and late --
  // timing whole legs would instead measure the serve's half-court start.
  TableTennisGame game;
  uint32_t now = serve(game);

  int32_t earlySpeed = 0;
  int32_t lateSpeed = 0;
  int hits = 0;
  for (int rally = 0; rally < 40 && hits < 14 && game.phase() == Phase::Rally;
       ++rally) {
    int paddleHits = 0;
    now = returnBallWithZone(game, now, TableTennisGame::kZones / 2, paddleHits);
    if (paddleHits == 0) break;
    hits += paddleHits;
    if (hits == 1) earlySpeed = game.ballVelocityX();
    if (hits >= 13 && lateSpeed == 0) lateSpeed = game.ballVelocityX();

    for (int frame = 0; frame < 600 && game.ballVelocityX() > 0 &&
                        game.phase() == Phase::Rally;
         ++frame) {
      now += 8;
      game.tick(now);
      hits += countSounds(game, Sound::Paddle);  // the AI's returns count too
    }
  }

  TEST_ASSERT_TRUE(earlySpeed > 0);
  TEST_ASSERT_TRUE(lateSpeed > 0);
  TEST_ASSERT_TRUE(lateSpeed > earlySpeed);
}

void test_the_ball_speed_resets_on_a_new_point() {
  TableTennisGame game;
  uint32_t now = serve(game);
  int paddleHits = 0;
  now = returnBallWithZone(game, now, TableTennisGame::kZones / 2, paddleHits);
  const int32_t firstHitSpeed = game.ballVelocityX();

  // Concede the point, then read the speed off the first hit of the next.
  game.movePlayerPaddle(-100);
  while (game.phase() == Phase::Rally) {
    now += 33;
    game.tick(now);
  }
  now = run(game, now, 2500);
  TEST_ASSERT_TRUE(game.phase() == Phase::Rally);
  paddleHits = 0;
  returnBallWithZone(game, now, TableTennisGame::kZones / 2, paddleHits);
  TEST_ASSERT_TRUE(paddleHits > 0);
  TEST_ASSERT_EQUAL_INT32(firstHitSpeed, std::abs(game.ballVelocityX()));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_the_court_is_the_largest_four_by_three_rect_in_the_circle);
  RUN_TEST(test_a_new_match_waits_for_a_tap);
  RUN_TEST(test_a_tap_serves);
  RUN_TEST(test_the_serve_is_never_horizontal);
  RUN_TEST(test_a_tap_mid_rally_does_not_restart_the_match);
  RUN_TEST(test_the_ball_bounces_off_both_walls_and_stays_in_the_court);
  RUN_TEST(test_hitting_a_wall_makes_a_wall_blip);
  RUN_TEST(test_the_eight_zones_deflect_from_steeply_up_to_steeply_down);
  RUN_TEST(test_a_contact_off_the_paddle_is_clamped_to_its_nearest_end);
  RUN_TEST(test_a_detent_is_a_whole_number_of_paddle_zones);
  RUN_TEST(test_the_steepest_return_is_forty_five_degrees);
  RUN_TEST(test_aiming_high_or_low_on_the_paddle_returns_the_ball_that_way);
  RUN_TEST(test_a_returned_ball_travels_back_up_the_court);
  RUN_TEST(test_a_missed_ball_scores_for_the_other_side);
  RUN_TEST(test_the_next_serve_goes_to_whoever_was_scored_on);
  RUN_TEST(test_the_match_ends_at_eleven);
  RUN_TEST(test_a_tap_after_the_match_starts_a_new_one);
  RUN_TEST(test_a_clockwise_detent_moves_the_paddle_up);
  RUN_TEST(test_the_knob_stops_the_paddle_at_the_walls);
  RUN_TEST(test_the_whole_court_is_about_one_sweep_of_the_knob);
  RUN_TEST(test_the_ai_paddle_never_moves_faster_than_its_cap);
  RUN_TEST(test_the_ai_paddle_stays_inside_the_court);
  RUN_TEST(test_the_ai_can_be_beaten_with_a_hard_angle);
  RUN_TEST(test_the_ai_returns_a_ball_played_down_the_middle);
  RUN_TEST(test_a_long_stall_does_not_teleport_the_ball_through_a_paddle);
  RUN_TEST(test_each_blip_is_handed_out_exactly_once);
  RUN_TEST(test_the_ball_speeds_up_during_a_long_rally);
  RUN_TEST(test_the_ball_speed_resets_on_a_new_point);
  return UNITY_END();
}
