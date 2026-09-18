#include <unity.h>

#include <cstdint>
#include <cstdlib>

#include "FixedTrig.h"
#include "GravityGame.h"
#include "GravityTerrain.h"

using knobify::games::FixedTrig;
using knobify::games::GravityGame;
using knobify::games::GravityTerrain;
using knobify::games::Pad;
using Phase = GravityGame::Phase;
using Sound = GravityGame::Sound;

void setUp() {}
void tearDown() {}

namespace {

// No bezel at all: for tests about generation and flight themselves.
int32_t unlimited(int32_t) { return 10000; }

// The real thing: the visible half-width of a 360px circle at height y.
int32_t roundScreen(int32_t y) {
  const int32_t dy = y - 180;
  const int32_t inside = 180 * 180 - dy * dy;
  if (inside <= 0) return 0;
  int32_t root = 0;
  while ((root + 1) * (root + 1) <= inside) ++root;
  return root;
}

// Flies the clock forward in ~5ms loop steps, the rate the device ticks
// the physics at.
uint32_t fly(GravityGame &game, uint32_t fromMs, uint32_t forMs) {
  uint32_t now = fromMs;
  const uint32_t until = fromMs + forMs;
  while (now < until && game.phase() == Phase::Flying) {
    now += 5;
    game.tick(now);
  }
  return now;
}

uint32_t launch(GravityGame &game, uint32_t seed = 1) {
  game.start(0, seed, unlimited);
  game.tap(0, seed, unlimited);
  return 0;
}

int countSounds(GravityGame &game, Sound wanted) {
  int seen = 0;
  for (Sound s = game.takeSound(); s != Sound::None; s = game.takeSound()) {
    if (s == wanted) ++seen;
  }
  return seen;
}

// The widest pad in this landscape -- the kindest target.
const Pad *widestPad(const GravityGame &game) {
  const GravityTerrain &terrain = game.terrain();
  const Pad *widest = &terrain.pads()[0];
  for (int i = 1; i < terrain.padCount(); ++i) {
    if (terrain.pads()[i].right - terrain.pads()[i].left >
        widest->right - widest->left) {
      widest = &terrain.pads()[i];
    }
  }
  return widest;
}

// A crude autopilot: tilt to steer toward the pad, level off near the
// ground, and brake harder the lower it gets. Deliberately simple -- if
// twenty lines of arithmetic can fly this thing down, a person can; if
// they cannot, the gravity, thrust or tank constants are wrong, and that
// is worth a failing test rather than a shrug.
bool autoLand(GravityGame &game, const Pad &pad, uint32_t &now) {
  const int32_t target = (pad.left + pad.right) / 2;
  for (int step = 0; step < 20000 && game.phase() == Phase::Flying; ++step) {
    int32_t dx = target - game.x();
    const int32_t world = GravityTerrain::kWorldWidth;
    if (dx > world / 2) dx -= world;
    if (dx < -world / 2) dx += world;

    int32_t wantVx = dx / 4;
    if (wantVx > 20) wantVx = 20;
    if (wantVx < -20) wantVx = -20;

    int32_t wantAngle = (wantVx - game.horizontalSpeed()) * 3;
    if (wantAngle > 30) wantAngle = 30;
    if (wantAngle < -30) wantAngle = -30;
    // Upright for the touchdown itself, whatever the steering wanted.
    if (game.altitude() < 24) wantAngle = 0;
    game.rotate((wantAngle - game.angle()) / GravityGame::kDegreesPerDetent);

    // Fall freely while high, brake progressively as the ground nears.
    game.setThrusting(game.verticalSpeed() > 8 + game.altitude() / 6);

    now += 5;
    game.tick(now);
  }
  return game.phase() == Phase::Landed;
}

}  // namespace

// --- Trigonometry ---------------------------------------------------------

void test_the_cardinal_angles_are_exact() {
  TEST_ASSERT_EQUAL_INT32(0, FixedTrig::sinScaled(0));
  TEST_ASSERT_EQUAL_INT32(FixedTrig::kScale, FixedTrig::sinScaled(90));
  TEST_ASSERT_EQUAL_INT32(0, FixedTrig::sinScaled(180));
  TEST_ASSERT_EQUAL_INT32(-FixedTrig::kScale, FixedTrig::sinScaled(270));
  TEST_ASSERT_EQUAL_INT32(FixedTrig::kScale, FixedTrig::cosScaled(0));
  TEST_ASSERT_EQUAL_INT32(0, FixedTrig::cosScaled(90));
  TEST_ASSERT_EQUAL_INT32(-FixedTrig::kScale, FixedTrig::cosScaled(180));
}

void test_angles_wrap_in_both_directions() {
  for (int32_t angle = -720; angle <= 720; ++angle) {
    TEST_ASSERT_EQUAL_INT32(FixedTrig::sinScaled(angle),
                            FixedTrig::sinScaled(angle + 360));
  }
  TEST_ASSERT_EQUAL_INT32(FixedTrig::sinScaled(90), FixedTrig::sinScaled(-270));
}

void test_sine_and_cosine_stay_on_the_unit_circle() {
  // The craft's thrust is split by these two; if they drifted off the
  // circle, thrusting at an angle would be stronger than thrusting up.
  for (int32_t angle = 0; angle < 360; ++angle) {
    const int32_t s = FixedTrig::sinScaled(angle);
    const int32_t c = FixedTrig::cosScaled(angle);
    const int32_t sum = s * s + c * c;
    const int32_t unit = FixedTrig::kScale * FixedTrig::kScale;
    // Table rounding, not drift: well under half a percent.
    TEST_ASSERT_TRUE(std::abs(sum - unit) < unit / 200);
  }
}

void test_sine_rises_over_the_first_quarter_turn() {
  for (int32_t angle = 1; angle <= 90; ++angle) {
    TEST_ASSERT_TRUE(FixedTrig::sinScaled(angle) >=
                     FixedTrig::sinScaled(angle - 1));
  }
}

// --- Terrain --------------------------------------------------------------

void test_the_same_seed_gives_the_same_landscape() {
  // A crash that only happens on one hillside has to be replayable.
  GravityTerrain a, b;
  a.generate(12345, unlimited);
  b.generate(12345, unlimited);
  for (int i = 0; i < GravityTerrain::pointCount(); ++i) {
    TEST_ASSERT_EQUAL_INT16(a.profile()[i].y, b.profile()[i].y);
  }
  TEST_ASSERT_EQUAL_INT(a.padCount(), b.padCount());
}

void test_different_seeds_give_different_landscapes() {
  GravityTerrain a, b;
  a.generate(1, unlimited);
  b.generate(2, unlimited);
  bool differs = false;
  for (int i = 0; i < GravityTerrain::pointCount(); ++i) {
    if (a.profile()[i].y != b.profile()[i].y) differs = true;
  }
  TEST_ASSERT_TRUE(differs);
}

void test_the_profile_spans_the_world_and_closes_at_the_seam() {
  // The world wraps, so the two ends are the same place; a step there
  // would be a visible cliff the craft could fly through.
  GravityTerrain terrain;
  terrain.generate(7, unlimited);
  TEST_ASSERT_EQUAL_INT16(0, terrain.profile()[0].x);
  TEST_ASSERT_EQUAL_INT16(GravityTerrain::kWorldWidth,
                          terrain.profile()[GravityTerrain::pointCount() - 1].x);
  TEST_ASSERT_EQUAL_INT16(terrain.profile()[0].y,
                          terrain.profile()[GravityTerrain::pointCount() - 1].y);
}

void test_the_ground_stays_inside_its_band() {
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    GravityTerrain terrain;
    terrain.generate(seed, unlimited);
    for (int i = 0; i < GravityTerrain::pointCount(); ++i) {
      TEST_ASSERT_TRUE(terrain.profile()[i].y >= GravityTerrain::kHighestGroundY);
      TEST_ASSERT_TRUE(terrain.profile()[i].y <= GravityTerrain::kLowestGroundY);
    }
  }
}

void test_height_is_interpolated_between_the_points() {
  GravityTerrain terrain;
  terrain.generate(3, unlimited);
  for (int i = 0; i + 1 < GravityTerrain::pointCount(); ++i) {
    const int16_t left = terrain.profile()[i].y;
    const int16_t right = terrain.profile()[i + 1].y;
    const int16_t mid =
        terrain.heightAt(terrain.profile()[i].x + GravityTerrain::kSegmentWidth / 2);
    TEST_ASSERT_TRUE(mid >= (left < right ? left : right));
    TEST_ASSERT_TRUE(mid <= (left > right ? left : right));
  }
}

void test_height_wraps_with_the_world() {
  GravityTerrain terrain;
  terrain.generate(9, unlimited);
  TEST_ASSERT_EQUAL_INT16(terrain.heightAt(5),
                          terrain.heightAt(5 + GravityTerrain::kWorldWidth));
  TEST_ASSERT_EQUAL_INT16(terrain.heightAt(5),
                          terrain.heightAt(5 - GravityTerrain::kWorldWidth));
}

void test_every_pad_is_actually_flat() {
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    GravityTerrain terrain;
    terrain.generate(seed, unlimited);
    for (int i = 0; i < terrain.padCount(); ++i) {
      const Pad &pad = terrain.pads()[i];
      for (int16_t x = pad.left; x <= pad.right; ++x) {
        TEST_ASSERT_EQUAL_INT16(pad.y, terrain.heightAt(x));
      }
    }
  }
}

void test_pads_never_touch_each_other() {
  // Two pads run together would read as one wide one, and pay the wrong
  // multiplier for the width the player sees.
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    GravityTerrain terrain;
    terrain.generate(seed, unlimited);
    for (int i = 0; i < terrain.padCount(); ++i) {
      for (int j = i + 1; j < terrain.padCount(); ++j) {
        const Pad &a = terrain.pads()[i];
        const Pad &b = terrain.pads()[j];
        TEST_ASSERT_TRUE(a.right < b.left || b.right < a.left);
      }
    }
  }
}

void test_no_pad_is_hidden_by_the_round_bezel() {
  // The rule the round screen imposes: a pad you cannot see is a pad you
  // cannot aim at.
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    GravityTerrain terrain;
    terrain.generate(seed, roundScreen);
    for (int i = 0; i < terrain.padCount(); ++i) {
      const Pad &pad = terrain.pads()[i];
      const int32_t halfWidth = roundScreen(pad.y);
      const int32_t centre = GravityTerrain::kWorldWidth / 2;
      TEST_ASSERT_TRUE(pad.left >= centre - halfWidth);
      TEST_ASSERT_TRUE(pad.right <= centre + halfWidth);
    }
  }
}

void test_a_landscape_still_offers_somewhere_to_land() {
  // The bezel rule rejects placements; it must not reject all of them.
  int totalPads = 0;
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    GravityTerrain terrain;
    terrain.generate(seed, roundScreen);
    TEST_ASSERT_TRUE(terrain.padCount() >= 1);
    totalPads += terrain.padCount();
  }
  // And usually more than the bare minimum.
  TEST_ASSERT_TRUE(totalPads >= 200);
}

void test_pad_widths_carry_the_multipliers_they_are_worth() {
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    GravityTerrain terrain;
    terrain.generate(seed, unlimited);
    for (int i = 0; i < terrain.padCount(); ++i) {
      const Pad &pad = terrain.pads()[i];
      const int segments = (pad.right - pad.left) / GravityTerrain::kSegmentWidth;
      bool matched = false;
      for (int k = 0; k < GravityTerrain::kMaxPads; ++k) {
        if (GravityTerrain::kPadSegments[k] == segments) {
          TEST_ASSERT_EQUAL_UINT8(GravityTerrain::kPadMultipliers[k],
                                  pad.multiplier);
          matched = true;
        }
      }
      TEST_ASSERT_TRUE(matched);
      // Narrower pays more, always.
      for (int j = 0; j < terrain.padCount(); ++j) {
        const Pad &other = terrain.pads()[j];
        if (other.right - other.left < pad.right - pad.left) {
          TEST_ASSERT_TRUE(other.multiplier > pad.multiplier);
        }
      }
    }
  }
}

void test_pad_lookup_answers_only_inside_a_pad() {
  GravityTerrain terrain;
  terrain.generate(11, unlimited);
  TEST_ASSERT_TRUE(terrain.padCount() > 0);
  const Pad &pad = terrain.pads()[0];
  TEST_ASSERT_TRUE(terrain.padAt(pad.left) == &pad);
  TEST_ASSERT_TRUE(terrain.padAt(pad.right) == &pad);
  TEST_ASSERT_TRUE(terrain.padAt((pad.left + pad.right) / 2) == &pad);
  TEST_ASSERT_TRUE(terrain.padAt(pad.left - 1) != &pad);
  TEST_ASSERT_TRUE(terrain.padAt(pad.right + 1) != &pad);
}

// --- Getting off the ground ----------------------------------------------

void test_a_new_flight_waits_for_a_tap() {
  GravityGame game;
  game.start(0, 1, unlimited);
  TEST_ASSERT_TRUE(game.phase() == Phase::Ready);
  const int32_t restingY = game.y();
  fly(game, 0, 2000);
  TEST_ASSERT_TRUE(game.phase() == Phase::Ready);
  TEST_ASSERT_EQUAL_INT32(restingY, game.y());
}

void test_a_full_tank_and_a_drift_to_cancel() {
  GravityGame game;
  launch(game);
  TEST_ASSERT_EQUAL_INT32(GravityGame::kFullTank, game.fuel());
  // Something to do from the first second, rather than a wait.
  TEST_ASSERT_TRUE(game.horizontalSpeed() != 0);
}

// --- Gravity and thrust ---------------------------------------------------

void test_left_alone_it_falls_and_crashes() {
  GravityGame game;
  launch(game);
  const uint32_t now = fly(game, 0, 20000);
  TEST_ASSERT_TRUE(game.phase() == Phase::Crashed);
  TEST_ASSERT_EQUAL_INT(1, countSounds(game, Sound::Crash));
  (void)now;
}

void test_thrust_straight_up_beats_gravity() {
  GravityGame game;
  launch(game);
  game.setThrusting(true);
  const int32_t before = game.y();
  fly(game, 0, 800);
  TEST_ASSERT_TRUE(game.phase() == Phase::Flying);
  // Three times gravity: after the initial fall is cancelled it climbs.
  TEST_ASSERT_TRUE(game.verticalSpeed() < 0);
  TEST_ASSERT_TRUE(game.y() <= before);
}

void test_a_tilted_engine_pushes_sideways() {
  GravityGame game;
  launch(game);
  game.rotate(15);  // 90 degrees: straight sideways.
  TEST_ASSERT_EQUAL_INT32(90, game.angle());
  const int32_t before = game.horizontalSpeed();
  game.setThrusting(true);
  fly(game, 0, 500);
  TEST_ASSERT_TRUE(game.horizontalSpeed() > before);
}

void test_the_knob_turns_the_craft_before_it_launches() {
  // A control that does nothing until you have started reads as a broken
  // control -- and on the device it did, because nothing turned until a
  // tap (user, 2026-09-18).
  GravityGame game;
  game.start(0, 1, unlimited);
  TEST_ASSERT_TRUE(game.phase() == Phase::Ready);
  game.rotate(2);
  TEST_ASSERT_EQUAL_INT32(2 * GravityGame::kDegreesPerDetent, game.angle());
}

void test_the_knob_does_nothing_once_the_flight_is_over() {
  GravityGame game;
  launch(game);
  fly(game, 0, 20000);
  TEST_ASSERT_TRUE(game.phase() == Phase::Crashed);
  const int32_t settled = game.angle();
  game.rotate(4);
  TEST_ASSERT_EQUAL_INT32(settled, game.angle());
}

void test_the_knob_turns_the_craft_both_ways_and_wraps() {
  GravityGame game;
  launch(game);
  game.rotate(1);
  TEST_ASSERT_EQUAL_INT32(GravityGame::kDegreesPerDetent, game.angle());
  game.rotate(-2);
  TEST_ASSERT_EQUAL_INT32(-GravityGame::kDegreesPerDetent, game.angle());
  // All the way round and back to where it started.
  game.rotate(60);
  TEST_ASSERT_EQUAL_INT32(-GravityGame::kDegreesPerDetent, game.angle());
}

// --- Fuel -----------------------------------------------------------------

void test_fuel_burns_only_while_thrusting() {
  GravityGame game;
  launch(game);
  fly(game, 0, 1000);
  TEST_ASSERT_EQUAL_INT32(GravityGame::kFullTank, game.fuel());

  game.setThrusting(true);
  fly(game, 1000, 500);
  TEST_ASSERT_TRUE(game.fuel() < GravityGame::kFullTank);

  const int32_t left = game.fuel();
  game.setThrusting(false);
  fly(game, 1500, 500);
  TEST_ASSERT_EQUAL_INT32(left, game.fuel());
}

void test_an_empty_tank_stops_the_engine_and_says_so() {
  GravityGame game;
  launch(game);
  // Point sideways so thrust cannot fly it into the ceiling or the ground
  // before the tank runs dry.
  game.setThrusting(true);
  fly(game, 0, 20000);
  TEST_ASSERT_EQUAL_INT32(0, game.fuel());
  TEST_ASSERT_FALSE(game.thrusting());
}

void test_a_dead_engine_cannot_be_restarted() {
  GravityGame game;
  launch(game);
  game.setThrusting(true);
  fly(game, 0, 20000);
  game.setThrusting(false);
  game.setThrusting(true);
  TEST_ASSERT_FALSE(game.thrusting());
}

// --- The four ways the ground refuses you --------------------------------

void test_a_good_landing_is_accepted_and_scored() {
  GravityGame game;
  launch(game);
  const Pad *pad = widestPad(game);
  const uint8_t multiplier = pad->multiplier;

  uint32_t now = 0;
  TEST_ASSERT_TRUE(autoLand(game, *pad, now));
  TEST_ASSERT_TRUE(game.phase() == Phase::Landed);
  TEST_ASSERT_TRUE(game.overPad());
  TEST_ASSERT_EQUAL_INT(1, countSounds(game, Sound::Touchdown));
  TEST_ASSERT_TRUE(game.score() >= multiplier * 50u);
}

void test_the_tank_is_big_enough_to_reach_any_pad() {
  // A landscape whose pads cannot be reached on one tank is not a
  // landscape, it is a loading screen.
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    GravityGame game;
    game.start(0, seed, roundScreen);
    game.tap(0, seed, roundScreen);
    const Pad *pad = widestPad(game);
    uint32_t now = 0;
    TEST_ASSERT_TRUE(autoLand(game, *pad, now));
  }
}

void test_landing_off_a_pad_is_a_crash() {
  // Every condition but the pad satisfied.
  GravityGame game;
  launch(game, 4);
  uint32_t now = 0;
  for (int step = 0; step < 4000 && game.phase() == Phase::Flying; ++step) {
    game.setThrusting(game.verticalSpeed() > 10);
    now += 5;
    game.tick(now);
  }
  // Whatever it settled on, the verdict must agree with the pad question.
  if (game.phase() == Phase::Landed) {
    TEST_ASSERT_TRUE(game.overPad());
  } else {
    TEST_ASSERT_TRUE(!game.overPad() || !game.uprightEnough() ||
                     !game.slowEnoughDown() || !game.slowEnoughSideways());
  }
}

void test_coming_down_too_fast_is_a_crash() {
  GravityGame game;
  launch(game);
  // No braking at all: the descent limit alone is enough to refuse it,
  // whatever it happens to come down on.
  fly(game, 0, 20000);
  TEST_ASSERT_TRUE(game.phase() == Phase::Crashed);
  TEST_ASSERT_EQUAL_UINT32(0u, game.score());
}

void test_the_landing_limits_are_separate_questions() {
  // Each is answerable on its own, which is what lets a near miss say
  // which one it was rather than just "crashed".
  GravityGame game;
  launch(game);
  TEST_ASSERT_TRUE(game.uprightEnough());
  game.rotate(3);  // 18 degrees, past the limit.
  TEST_ASSERT_FALSE(game.uprightEnough());
  game.rotate(-3);
  TEST_ASSERT_TRUE(game.uprightEnough());

  // Falling long enough breaks the descent limit but not the others.
  fly(game, 0, 1500);
  TEST_ASSERT_FALSE(game.slowEnoughDown());
  TEST_ASSERT_TRUE(game.uprightEnough());
}

// --- Flight envelope ------------------------------------------------------

void test_the_craft_wraps_around_the_world() {
  GravityGame game;
  launch(game);
  game.rotate(15);  // Sideways.
  game.setThrusting(true);
  for (int step = 0; step < 2000 && game.phase() == Phase::Flying; ++step) {
    game.tick(static_cast<uint32_t>(step * 5));
    TEST_ASSERT_TRUE(game.x() >= 0);
    TEST_ASSERT_TRUE(game.x() < GravityTerrain::kWorldWidth);
  }
}

void test_there_is_a_ceiling_rather_than_an_open_sky() {
  GravityGame game;
  launch(game);
  game.setThrusting(true);
  fly(game, 0, 5000);
  TEST_ASSERT_TRUE(game.y() >= 0);
}

void test_altitude_is_measured_to_the_ground_below() {
  GravityGame game;
  launch(game);
  const int32_t ground = game.terrain().heightAt(game.x());
  TEST_ASSERT_EQUAL_INT32(ground - game.y() - GravityGame::kCraftHeight / 2,
                          game.altitude());
  TEST_ASSERT_TRUE(game.altitude() > 0);
}

void test_a_long_stall_does_not_drop_the_craft_through_the_ground() {
  GravityGame game;
  launch(game);
  uint32_t now = 0;
  for (int step = 0; step < 40 && game.phase() == Phase::Flying; ++step) {
    now += 500;  // A blocking SD read, a screen swap.
    game.tick(now);
    TEST_ASSERT_TRUE(game.y() <= game.terrain().heightAt(game.x()));
  }
}

void test_a_finished_flight_stays_finished_until_tapped() {
  GravityGame game;
  launch(game);
  fly(game, 0, 20000);
  TEST_ASSERT_TRUE(game.phase() == Phase::Crashed);
  const int32_t restingY = game.y();
  fly(game, 20000, 5000);
  TEST_ASSERT_EQUAL_INT32(restingY, game.y());

  game.tap(25000, 2, unlimited);
  TEST_ASSERT_TRUE(game.phase() == Phase::Flying);
  TEST_ASSERT_EQUAL_INT32(GravityGame::kFullTank, game.fuel());
}

void test_the_knob_and_the_throttle_do_nothing_once_it_is_down() {
  GravityGame game;
  launch(game);
  fly(game, 0, 20000);
  const int32_t angle = game.angle();
  game.rotate(5);
  TEST_ASSERT_EQUAL_INT32(angle, game.angle());
  game.setThrusting(true);
  TEST_ASSERT_FALSE(game.thrusting());
}

void test_each_sound_is_handed_out_exactly_once() {
  GravityGame game;
  launch(game);
  fly(game, 0, 20000);
  TEST_ASSERT_EQUAL_INT(1, countSounds(game, Sound::Crash));
  TEST_ASSERT_TRUE(game.takeSound() == Sound::None);
}

void test_the_engine_is_a_state_the_screen_reads_not_a_queued_event() {
  // A long descent toggles the throttle constantly. None of that may
  // crowd out the one sound that has to arrive.
  GravityGame game;
  launch(game);
  const Pad *pad = widestPad(game);
  uint32_t now = 0;
  TEST_ASSERT_TRUE(autoLand(game, *pad, now));
  TEST_ASSERT_EQUAL_INT(1, countSounds(game, Sound::Touchdown));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_the_cardinal_angles_are_exact);
  RUN_TEST(test_angles_wrap_in_both_directions);
  RUN_TEST(test_sine_and_cosine_stay_on_the_unit_circle);
  RUN_TEST(test_sine_rises_over_the_first_quarter_turn);
  RUN_TEST(test_the_same_seed_gives_the_same_landscape);
  RUN_TEST(test_different_seeds_give_different_landscapes);
  RUN_TEST(test_the_profile_spans_the_world_and_closes_at_the_seam);
  RUN_TEST(test_the_ground_stays_inside_its_band);
  RUN_TEST(test_height_is_interpolated_between_the_points);
  RUN_TEST(test_height_wraps_with_the_world);
  RUN_TEST(test_every_pad_is_actually_flat);
  RUN_TEST(test_pads_never_touch_each_other);
  RUN_TEST(test_no_pad_is_hidden_by_the_round_bezel);
  RUN_TEST(test_a_landscape_still_offers_somewhere_to_land);
  RUN_TEST(test_pad_widths_carry_the_multipliers_they_are_worth);
  RUN_TEST(test_pad_lookup_answers_only_inside_a_pad);
  RUN_TEST(test_a_new_flight_waits_for_a_tap);
  RUN_TEST(test_a_full_tank_and_a_drift_to_cancel);
  RUN_TEST(test_left_alone_it_falls_and_crashes);
  RUN_TEST(test_thrust_straight_up_beats_gravity);
  RUN_TEST(test_a_tilted_engine_pushes_sideways);
  RUN_TEST(test_the_knob_turns_the_craft_before_it_launches);
  RUN_TEST(test_the_knob_does_nothing_once_the_flight_is_over);
  RUN_TEST(test_the_knob_turns_the_craft_both_ways_and_wraps);
  RUN_TEST(test_fuel_burns_only_while_thrusting);
  RUN_TEST(test_an_empty_tank_stops_the_engine_and_says_so);
  RUN_TEST(test_a_dead_engine_cannot_be_restarted);
  RUN_TEST(test_a_good_landing_is_accepted_and_scored);
  RUN_TEST(test_the_tank_is_big_enough_to_reach_any_pad);
  RUN_TEST(test_landing_off_a_pad_is_a_crash);
  RUN_TEST(test_coming_down_too_fast_is_a_crash);
  RUN_TEST(test_the_landing_limits_are_separate_questions);
  RUN_TEST(test_the_craft_wraps_around_the_world);
  RUN_TEST(test_there_is_a_ceiling_rather_than_an_open_sky);
  RUN_TEST(test_altitude_is_measured_to_the_ground_below);
  RUN_TEST(test_a_long_stall_does_not_drop_the_craft_through_the_ground);
  RUN_TEST(test_a_finished_flight_stays_finished_until_tapped);
  RUN_TEST(test_the_knob_and_the_throttle_do_nothing_once_it_is_down);
  RUN_TEST(test_each_sound_is_handed_out_exactly_once);
  RUN_TEST(test_the_engine_is_a_state_the_screen_reads_not_a_queued_event);
  return UNITY_END();
}
