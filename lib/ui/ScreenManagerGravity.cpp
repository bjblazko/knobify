// ScreenManager's Gravity screen (ADR 0023) -- one file per game, beside
// ScreenManagerTableTennis.cpp.
//
// Drawn entirely in lines rather than rectangles, for a reason that turns
// out to be the faithful one: LVGL 8 applies transform_angle only to
// images, so a craft that rotates cannot be an lv_obj (ADR 0018 found
// this on the wordmark's dial) -- and the 1979 machine this descends from
// is a vector display, so line art is what it actually looked like.
//
// Colours come from GameScreenStyle.h, where the games' exception to the
// palette is argued.

#include <lvgl.h>

#include <cstdio>

#include "FixedTrig.h"
#include "GameScreenStyle.h"
#include "GravityGame.h"
#include "GravitySounds.h"
#include "Theme.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"

namespace knobify::ui {

using games::FixedTrig;
using games::GravityGame;
using games::GravityTerrain;
using games::Pad;

namespace {

// The craft, in its own coordinates about its centre, closed back to the
// start. A body with splayed legs: enough to read as a lander at 16px,
// and few enough points to rotate every frame without thinking about it.
constexpr lv_point_t kCraftOutline[] = {
    {0, -8}, {5, -3}, {5, 2}, {7, 8}, {-7, 8}, {-5, 2}, {-5, -3}, {0, -8},
};
constexpr int kCraftPointCount =
    static_cast<int>(sizeof(kCraftOutline) / sizeof(kCraftOutline[0]));

// The exhaust, in the same coordinates, pointing away from the craft's
// heading. Its length flickers so the engine reads as running rather than
// as a fixed spike bolted to the hull.
constexpr lv_point_t kFlameOutline[] = {{-4, 6}, {0, 15}, {4, 6}};
constexpr int kFlamePointCount = 3;

// Instruments, two rows of two, top-centre. Both rows sit where the round
// bezel is still wide enough for two columns -- the very top of this
// screen is not (ux-guidelines §7).
constexpr lv_coord_t kInstrumentTop = 44;
constexpr lv_coord_t kInstrumentRowGap = 20;
constexpr lv_coord_t kInstrumentColumnWidth = 110;
constexpr lv_coord_t kHintTop = 312;
constexpr lv_coord_t kHintWidth = 220;
// The controls, in the empty sky between the instruments and the ground,
// where the circle is at its widest. Only before the launch: there is
// nothing else to look at then, and nothing to read once there is.
constexpr lv_coord_t kLegendTop = 160;
constexpr lv_coord_t kLegendWidth = 240;
constexpr lv_coord_t kPadLabelHeight = 16;
// Narrow enough that a column's step stays under the ridge line's own
// width on the gentlest-to-steepest slopes the generator now produces.
constexpr int kFillColumns = 90;
static_assert(kFillColumns * 4 == drivers::kLcdHorRes,
              "the columns must tile the screen exactly, with no seam");
constexpr lv_coord_t kFillColumnWidth = drivers::kLcdHorRes / kFillColumns;

// Low in the sky, clear of the instruments above and the ground below.
constexpr ui_widgets::MessageAnchor kGravityMessageAnchor{
    drivers::kLcdHorRes / 2, 150};

// How much of the round screen is visible either side of centre at this
// height. The landscape generator takes this, so a pad is never put where
// the bezel would hide it.
int32_t visibleHalfWidth(int32_t y) {
  constexpr int32_t kRadius = drivers::kLcdHorRes / 2;
  const int32_t dy = y - kRadius;
  const int32_t inside = kRadius * kRadius - dy * dy;
  if (inside <= 0) return 0;
  int32_t root = 0;
  while ((root + 1) * (root + 1) <= inside) ++root;
  // A margin, so a pad's ends are comfortably shown rather than just
  // barely -- the bezel clips harder than the framebuffer suggests.
  return root > 12 ? root - 12 : 0;
}

// Rotates a point about the craft's centre and drops it at (originX, originY).
lv_point_t place(lv_point_t local, int32_t angleDeg, int32_t originX,
                 int32_t originY) {
  const int32_t s = FixedTrig::sinScaled(angleDeg);
  const int32_t c = FixedTrig::cosScaled(angleDeg);
  const int32_t x = (local.x * c - local.y * s) / FixedTrig::kScale;
  const int32_t y = (local.x * s + local.y * c) / FixedTrig::kScale;
  return lv_point_t{static_cast<lv_coord_t>(originX + x),
                    static_cast<lv_coord_t>(originY + y)};
}

}  // namespace

void ScreenManager::renderGravity() {
  // Modal like the other game: no back button, no caption, no mini-bar,
  // and a swipe is the way out.
  lv_obj_set_style_bg_color(screen_, game_style::vacuum(), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

  // The whole screen is the throttle. PRESS_LOST as well as RELEASED: a
  // finger that slides off the edge must not leave the engine burning.
  lv_obj_t *throttle = lv_obj_create(screen_);
  lv_obj_set_size(throttle, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_set_pos(throttle, 0, 0);
  lv_obj_set_style_bg_opa(throttle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(throttle, 0, 0);
  lv_obj_clear_flag(throttle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(throttle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(throttle, onGravityPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(throttle, onGravityReleased, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(throttle, onGravityReleased, LV_EVENT_PRESS_LOST, this);
  lv_obj_add_event_cb(throttle, onGravityTapped, LV_EVENT_CLICKED, this);

  gravity_.start(lv_tick_get(), lv_tick_get(), visibleHalfWidth);

  GravityWidgets &w = gravityWidgets_;

  // The ground mass first, so the ridge line drawn next sits on top of it
  // and hides the columns' steps.
  for (int i = 0; i < kFillColumns; ++i) {
    w.fill[i] = game_style::makeBlock(screen_, i * kFillColumnWidth, 0,
                                      kFillColumnWidth, 1);
    lv_obj_set_style_bg_color(w.fill[i], game_style::ground(), 0);
  }
  w.terrain = game_style::makeLine(screen_, 2);
  rebuildGravityTerrain();

  // What each pad pays, written above it in the sky rather than on the
  // ground, so it stays white like the ridge it belongs to.
  for (int i = 0; i < GravityTerrain::kMaxPads; ++i) {
    w.padLabels[i] = game_style::makeLabel(screen_, 0, 0, 40,
                                           &knobify_text_font_14);
    lv_obj_add_flag(w.padLabels[i], LV_OBJ_FLAG_HIDDEN);
  }
  rebuildGravityTerrain();

  w.craft = game_style::makeLine(screen_, 2);
  w.flame = game_style::makeLine(screen_, 2);

  const lv_coord_t leftColumn = drivers::kLcdHorRes / 2 - kInstrumentColumnWidth;
  const lv_coord_t rightColumn = drivers::kLcdHorRes / 2;
  w.altitude = game_style::makeLabel(screen_, leftColumn, kInstrumentTop,
                                     kInstrumentColumnWidth,
                                     &knobify_text_font_14);
  w.vertical = game_style::makeLabel(screen_, rightColumn, kInstrumentTop,
                                     kInstrumentColumnWidth,
                                     &knobify_text_font_14);
  w.horizontal = game_style::makeLabel(
      screen_, leftColumn, kInstrumentTop + kInstrumentRowGap,
      kInstrumentColumnWidth, &knobify_text_font_14);
  w.fuel = game_style::makeLabel(screen_, rightColumn,
                                 kInstrumentTop + kInstrumentRowGap,
                                 kInstrumentColumnWidth, &knobify_text_font_14);

  // Below the ground line, so it lies on the grey mass rather than the
  // sky -- and is therefore set in ink rather than white. The one place
  // in either game where the text inverts, because it is the one place
  // the background does.
  w.hint = game_style::makeLabel(screen_,
                                 (drivers::kLcdHorRes - kHintWidth) / 2,
                                 kHintTop, kHintWidth, &knobify_text_font_14);
  lv_obj_set_style_text_color(w.hint, game_style::vacuum(), 0);

  // Three controls is two more than a knob and a screen suggest on their
  // own: turning does not look like steering, and a screen you hold is
  // not a screen you tap. Said plainly, once, while there is time to read
  // it (user, 2026-09-18).
  w.legend = game_style::makeLabel(
      screen_, (drivers::kLcdHorRes - kLegendWidth) / 2, kLegendTop,
      kLegendWidth, &knobify_text_font_14);
  lv_label_set_text(w.legend, "TURN TO STEER\nHOLD TO THRUST");

  // Said once, on arriving: this board has no button and the game has no
  // back button, so the way out would otherwise be unannounced
  // (ux-guidelines §7, the same lesson as ADR 0022).
  messages_.show("Swipe right to leave", kGravityMessageAnchor, lv_tick_get());

  shownGravityPhase_ = GravityGame::Phase::Flying;  // Force the first hint.
  lastGravityDrawMs_ = lv_tick_get();
  applyGravityScene();
}

void ScreenManager::onGravityPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (self && self->gravityWidgets_.craft) self->gravity_.setThrusting(true);
}

void ScreenManager::onGravityReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (self && self->gravityWidgets_.craft) self->gravity_.setThrusting(false);
}

void ScreenManager::onGravityTapped(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->gravityWidgets_.craft) return;
  // A fresh landscape every attempt, seeded from the clock.
  self->gravity_.tap(lv_tick_get(), lv_tick_get(), visibleHalfWidth);
  self->rebuildGravityTerrain();
  self->applyGravityScene();
}

// The landscape only changes when a flight starts, so its line is rebuilt
// there rather than every frame.
void ScreenManager::rebuildGravityTerrain() {
  GravityWidgets &w = gravityWidgets_;
  if (!w.terrain) return;
  const GravityTerrain &terrain = gravity_.terrain();
  for (int i = 0; i < GravityTerrain::pointCount(); ++i) {
    w.terrainPoints[i] = lv_point_t{terrain.profile()[i].x,
                                    terrain.profile()[i].y};
  }
  lv_line_set_points(w.terrain, w.terrainPoints, GravityTerrain::pointCount());

  // Each column runs from the ground at its own centre to the bottom of
  // the framebuffer; the bezel crops whatever of that it does not show.
  for (int i = 0; i < kFillColumns; ++i) {
    if (!w.fill[i]) continue;
    const lv_coord_t top =
        terrain.heightAt(i * kFillColumnWidth + kFillColumnWidth / 2);
    lv_obj_set_pos(w.fill[i], i * kFillColumnWidth, top);
    lv_obj_set_size(w.fill[i], kFillColumnWidth,
                    static_cast<lv_coord_t>(drivers::kLcdVerRes - top));
  }

  for (int i = 0; i < GravityTerrain::kMaxPads; ++i) {
    if (!w.padLabels[i]) continue;
    if (i >= terrain.padCount()) {
      lv_obj_add_flag(w.padLabels[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const Pad &pad = terrain.pads()[i];
    char text[8];
    std::snprintf(text, sizeof(text), "\xC3\x97%u", pad.multiplier);
    lv_label_set_text(w.padLabels[i], text);
    lv_obj_set_pos(w.padLabels[i], (pad.left + pad.right) / 2 - 20,
                   pad.y - kPadLabelHeight - 4);
    lv_obj_clear_flag(w.padLabels[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void ScreenManager::onGravityKnob(int16_t delta) {
  if (!gravityWidgets_.craft) return;
  gravity_.rotate(delta);
  // Redraw at once rather than waiting for the frame gate: a control you
  // have to wait for is a control you do not trust.
  applyGravityScene();
}

void ScreenManager::applyGravityScene() {
  GravityWidgets &w = gravityWidgets_;
  if (!w.craft) return;

  const int32_t angle = gravity_.angle();
  const int32_t x = gravity_.x();
  const int32_t y = gravity_.y();
  for (int i = 0; i < kCraftPointCount; ++i) {
    w.craftPoints[i] = place(kCraftOutline[i], angle, x, y);
  }
  lv_line_set_points(w.craft, w.craftPoints, kCraftPointCount);

  if (gravity_.thrusting()) {
    // A flicker, so the engine reads as burning rather than as a spike
    // welded to the hull.
    const int32_t flicker = static_cast<int32_t>(lv_tick_get() / 60) % 3;
    for (int i = 0; i < kFlamePointCount; ++i) {
      lv_point_t local = kFlameOutline[i];
      if (i == 1) local.y = static_cast<lv_coord_t>(local.y - flicker * 2);
      w.flamePoints[i] = place(local, angle, x, y);
    }
    lv_line_set_points(w.flame, w.flamePoints, kFlamePointCount);
    lv_obj_clear_flag(w.flame, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(w.flame, LV_OBJ_FLAG_HIDDEN);
  }

  char text[20];
  std::snprintf(text, sizeof(text), "ALT %3d", static_cast<int>(gravity_.altitude()));
  lv_label_set_text(w.altitude, text);
  std::snprintf(text, sizeof(text), "VS %+4d", static_cast<int>(gravity_.verticalSpeed()));
  lv_label_set_text(w.vertical, text);
  std::snprintf(text, sizeof(text), "HS %+4d", static_cast<int>(gravity_.horizontalSpeed()));
  lv_label_set_text(w.horizontal, text);
  std::snprintf(text, sizeof(text), "FUEL %3d",
                static_cast<int>(gravity_.fuel() * 100 / GravityGame::kFullTank));
  lv_label_set_text(w.fuel, text);

  // The one number you act on, and the only signal colour on this screen:
  // red once the descent is past what the ground will accept.
  lv_obj_set_style_text_color(
      w.vertical,
      gravity_.slowEnoughDown() ? game_style::phosphor() : theme::warning(), 0);

  const GravityGame::Phase phase = gravity_.phase();
  if (phase == shownGravityPhase_) return;
  shownGravityPhase_ = phase;

  // The legend belongs to the one moment there is nothing else to look at.
  if (phase == GravityGame::Phase::Ready) {
    lv_obj_clear_flag(w.legend, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(w.legend, LV_OBJ_FLAG_HIDDEN);
  }

  char landed[32];
  const char *hint = nullptr;
  switch (phase) {
    case GravityGame::Phase::Ready:
      hint = "TAP TO LAUNCH";
      break;
    case GravityGame::Phase::Landed:
      std::snprintf(landed, sizeof(landed), "DOWN  SCORE %u",
                    static_cast<unsigned>(gravity_.score()));
      hint = landed;
      break;
    case GravityGame::Phase::Crashed:
      // Which of the four it was: "crashed" alone teaches nothing, and
      // each condition is a separate question the game can answer.
      hint = gravityFailureText();
      break;
    case GravityGame::Phase::Flying:
      break;
  }

  if (hint) {
    lv_label_set_text(w.hint, hint);
    lv_obj_clear_flag(w.hint, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(w.hint, LV_OBJ_FLAG_HIDDEN);
  }
}

const char *ScreenManager::gravityFailureText() const {
  if (!gravity_.overPad()) return "MISSED THE PAD";
  if (!gravity_.uprightEnough()) return "CAME DOWN TILTED";
  if (!gravity_.slowEnoughDown()) return "CAME DOWN TOO FAST";
  return "STILL DRIFTING";
}

bool ScreenManager::tickGravity(uint32_t nowMs, bool visible) {
  if (!gravityWidgets_.craft) return false;
  if (tabs_.activeStack().current().kind != navigation::ScreenKind::Gravity) {
    return false;
  }
  if (!visible) return false;

  // Physics and sound every loop, drawing only every kGravityFrameMs --
  // the lesson Table Tennis cost real time to learn (ADR 0022): a sound
  // announced on the frame gate is a sound heard up to a frame late.
  gravity_.tick(nowMs);
  drainGravitySounds();

  if (nowMs - lastGravityDrawMs_ >= kGravityFrameMs) {
    lastGravityDrawMs_ = nowMs;
    applyGravityScene();
  }

  // A descent holds the display awake by itself: the knob and the screen
  // can both be untouched for the whole of one, and a dimmed panel stops
  // LVGL being pumped at all -- the flight would freeze, not dim.
  return gravity_.phase() == GravityGame::Phase::Flying;
}

void ScreenManager::drainGravitySounds() {
  if (!blips_) return;
  // The engine is a state, not an event (ADR 0023): start and stop its
  // noise from what the game *is* doing, rather than queueing on/off
  // pairs that would crowd the touchdown out of a six-slot queue.
  const bool thrusting = gravity_.thrusting();
  if (thrusting != gravityThrustSounding_) {
    gravityThrustSounding_ = thrusting;
    if (thrusting) {
      blips_->noise(games::kThrustNoise.frequencyHz, 0, games::kThrustNoise.level);
    } else {
      blips_->silence();
    }
  }

  for (GravityGame::Sound sound = gravity_.takeSound();
       sound != GravityGame::Sound::None; sound = gravity_.takeSound()) {
    const games::GravityBlip blip = games::blipFor(sound);
    if (blip.frequencyHz == 0) continue;
    gravityThrustSounding_ = false;
    if (blip.noise) {
      blips_->noise(blip.frequencyHz, blip.durationMs, blip.level);
    } else {
      blips_->blip(blip.frequencyHz, blip.durationMs);
    }
  }
}

}  // namespace knobify::ui
