// ScreenManager's Games screens (ADR 0022), kept apart from the music
// screens the way ScreenManagerMenu.cpp keeps the menu ones: the Pong
// court has nothing in common with a list, and ScreenManager.cpp is long
// enough already (docs/coding-guidelines.md).
//
// This is the one place in the app that does not use lib/ui/Theme.h's
// palette. Pong is white on black and a light-grey Pong is not Pong;
// ux-guidelines §3's "every screen is `surface`" rule is suspended here
// and only here, for a screen that is a cabinet rather than a control.
// See ADR 0022 for the argument.

#include <lvgl.h>

#include "PongGame.h"
#include "PongSounds.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"

namespace knobify::ui {

using games::PongGame;

namespace {

// White phosphor on a dark screen -- the only two colours in the game.
lv_color_t phosphor() { return lv_color_hex(0xFFFFFF); }
lv_color_t vacuum() { return lv_color_hex(0x000000); }

// The court is centred in the framebuffer; its corners touch the bezel
// exactly, which is why nothing is ever drawn at one (the original has no
// side walls -- the ball leaving sideways is the point).
constexpr lv_coord_t kCourtLeft = (drivers::kLcdHorRes - PongGame::kCourtWidth) / 2;
constexpr lv_coord_t kCourtTop =
    (drivers::kLcdVerRes - PongGame::kCourtOuterHeight) / 2;
constexpr lv_coord_t kPlayTop = kCourtTop + PongGame::kWallThickness;

constexpr lv_coord_t kNetDashWidth = 4;
constexpr lv_coord_t kNetDashHeight = 12;
constexpr lv_coord_t kNetDashGap = 8;

// Scores sit inside the court above the net, where the original puts
// them. The ball passes over them, as it does on the original.
constexpr lv_coord_t kScoreTop = kPlayTop + 20;
constexpr lv_coord_t kScoreInset = 72;

// The one hint the game draws itself, below the bottom wall, and only
// while the ball is parked. How to *leave* is a message instead (see
// renderPong): it is needed once, on arriving, and a permanent line
// telling you how to stop playing is not part of a game.
constexpr lv_coord_t kHintTop = kCourtTop + PongGame::kCourtOuterHeight + 8;
constexpr lv_coord_t kHintWidth = 220;

// Low in the court, clear of the parked ball above it and of the bezel
// below: the calmest wide spot on a screen that is otherwise all game.
constexpr ui_widgets::MessageAnchor kPongMessageAnchor{drivers::kLcdHorRes / 2,
                                                       236};

// A short line of white text, centred, for the hint the game draws.
lv_obj_t *makeHintLabel(lv_obj_t *parent, lv_coord_t top) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, kHintWidth);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, phosphor(), 0);
  lv_obj_set_style_text_font(label, &knobify_text_font_14, 0);
  lv_obj_set_pos(label, (drivers::kLcdHorRes - kHintWidth) / 2, top);
  return label;
}

// A plain filled rectangle: every moving part of this game is one.
lv_obj_t *makeBlock(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                    lv_coord_t h) {
  lv_obj_t *block = lv_obj_create(parent);
  lv_obj_set_size(block, w, h);
  lv_obj_set_pos(block, x, y);
  lv_obj_set_style_bg_color(block, phosphor(), 0);
  lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(block, 0, 0);
  lv_obj_set_style_radius(block, 0, 0);  // Nothing in Pong is rounded.
  lv_obj_set_style_pad_all(block, 0, 0);
  lv_obj_clear_flag(block, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
  return block;
}

}  // namespace

void ScreenManager::renderPong() {
  // Modal, like Touch calibration and USB drive: no back button, no
  // caption, no mini-bar. A left-to-right swipe still leaves, which is
  // the only way out -- this board has no button.
  lv_obj_set_style_bg_color(screen_, vacuum(), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

  // A tap serves, and starts a new match once one is over. The whole
  // screen takes it: a target to hit would be one more thing on a screen
  // whose whole point is that there is nothing on it but the game.
  lv_obj_t *touchArea = lv_obj_create(screen_);
  lv_obj_set_size(touchArea, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_set_pos(touchArea, 0, 0);
  lv_obj_set_style_bg_opa(touchArea, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(touchArea, 0, 0);
  lv_obj_clear_flag(touchArea, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(touchArea, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(touchArea, onPongTapped, LV_EVENT_CLICKED, this);

  // The two walls, on the inscribed rectangle's own top and bottom edges.
  makeBlock(screen_, kCourtLeft, kCourtTop, PongGame::kCourtWidth,
            PongGame::kWallThickness);
  makeBlock(screen_, kCourtLeft,
            kCourtTop + PongGame::kCourtOuterHeight - PongGame::kWallThickness,
            PongGame::kCourtWidth, PongGame::kWallThickness);

  // The dashed net.
  const lv_coord_t netX =
      kCourtLeft + (PongGame::kCourtWidth - kNetDashWidth) / 2;
  for (lv_coord_t y = kPlayTop + 2;
       y + kNetDashHeight <= kPlayTop + PongGame::kCourtHeight;
       y += kNetDashHeight + kNetDashGap) {
    makeBlock(screen_, netX, y, kNetDashWidth, kNetDashHeight);
  }

  pongPlayerScore_.create(screen_,
                          kCourtLeft + kScoreInset -
                              ui_widgets::SegmentDigits::kWidth / 2,
                          kScoreTop, phosphor());
  pongAiScore_.create(screen_,
                      kCourtLeft + PongGame::kCourtWidth - kScoreInset -
                          ui_widgets::SegmentDigits::kWidth / 2,
                      kScoreTop, phosphor());

  pongPlayerPaddle_ =
      makeBlock(screen_, kCourtLeft + PongGame::kPlayerPaddleX, kPlayTop,
                PongGame::kPaddleWidth, PongGame::kPaddleHeight);
  pongAiPaddle_ =
      makeBlock(screen_, kCourtLeft + PongGame::kAiPaddleX, kPlayTop,
                PongGame::kPaddleWidth, PongGame::kPaddleHeight);
  pongBall_ = makeBlock(screen_, kCourtLeft, kPlayTop, PongGame::kBallSize,
                        PongGame::kBallSize);

  pongHint_ = makeHintLabel(screen_, kHintTop);

  // This board has no button, the game has no back button, and a modal
  // screen whose only way out is an unannounced gesture is exactly the
  // mode you can get stuck in (ux-guidelines §7) -- the first person to
  // play it asked how to leave (2026-09-18). Said once, on arriving, in
  // the app's own voice rather than painted permanently on the court.
  messages_.show("Swipe right to leave", kPongMessageAnchor, lv_tick_get());

  pong_.start(lv_tick_get());
  shownPongPhase_ = PongGame::Phase::Rally;  // Force the first hint update.
  lastPongTickMs_ = lv_tick_get();
  applyPongScene();
}

void ScreenManager::onPongTapped(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->pongBall_) return;
  self->pong_.tap(lv_tick_get());
  self->applyPongScene();
}

void ScreenManager::onPaddleMove(int16_t delta) {
  if (!pongBall_) return;
  pong_.movePlayerPaddle(delta);
  applyPongScene();
}

// One blip per queued event, oldest first. Only the last one of a frame
// is actually heard -- the generator replaces rather than layers -- which
// is right: a ball that clips the wall and the paddle in the same 33 ms
// made one sound on the original too.
void ScreenManager::drainPongSounds() {
  if (!blips_) return;
  for (PongGame::Sound sound = pong_.takeSound();
       sound != PongGame::Sound::None; sound = pong_.takeSound()) {
    const games::Blip blip = games::blipFor(sound);
    if (blip.frequencyHz != 0) blips_->blip(blip.frequencyHz, blip.durationMs);
  }
}

// Moves only what moved. LVGL invalidates just the areas involved, which
// is why this is six rectangles rather than a full-screen canvas: a
// 360x360 canvas would be 259 KB in PSRAM and redraw the whole court for
// an 8px ball. Heavy redraw activity is also what starves the audio
// decoder (AGENTS.md), so the frame does as little as it can.
void ScreenManager::applyPongScene() {
  if (!pongBall_) return;
  lv_obj_set_pos(pongPlayerPaddle_, kCourtLeft + PongGame::kPlayerPaddleX,
                 kPlayTop + pong_.playerPaddleY());
  lv_obj_set_pos(pongAiPaddle_, kCourtLeft + PongGame::kAiPaddleX,
                 kPlayTop + pong_.aiPaddleY());
  lv_obj_set_pos(pongBall_, kCourtLeft + pong_.ballX(),
                 kPlayTop + pong_.ballY());
  pongPlayerScore_.setValue(pong_.playerScore());
  pongAiScore_.setValue(pong_.aiScore());

  const PongGame::Phase phase = pong_.phase();
  if (phase != shownPongPhase_) {
    shownPongPhase_ = phase;
    const bool parked =
        phase == PongGame::Phase::Ready || phase == PongGame::Phase::Over;
    if (parked) {
      lv_label_set_text(pongHint_, phase == PongGame::Phase::Ready
                                       ? "TAP TO SERVE"
                                       : "TAP TO PLAY AGAIN");
      lv_obj_clear_flag(pongHint_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pongHint_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

bool ScreenManager::tickPong(uint32_t nowMs, bool visible) {
  if (!pongBall_) return false;
  if (tabs_.activeStack().current().kind != navigation::ScreenKind::Pong) {
    return false;
  }
  if (!visible) {
    lastPongTickMs_ = nowMs;
    return false;
  }
  if (nowMs - lastPongTickMs_ < kPongFrameMs) return false;
  lastPongTickMs_ = nowMs;

  pong_.tick(nowMs);
  drainPongSounds();
  applyPongScene();

  // A rally has to hold the display awake by itself: nothing here touches
  // the screen or the knob while the ball is in play, and once the idle
  // timeout dims the panel main.cpp stops pumping LVGL entirely -- the
  // game would freeze mid-point, not merely dim.
  return pong_.phase() == PongGame::Phase::Rally;
}

}  // namespace knobify::ui
