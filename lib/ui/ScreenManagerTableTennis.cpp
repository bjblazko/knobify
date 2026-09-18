// ScreenManager's Table Tennis screen (ADR 0022) -- one file per game,
// kept apart from the music screens the way ScreenManagerMenu.cpp keeps
// the menu ones: a court has nothing in common with a list, and
// ScreenManager.cpp is long enough already (docs/coding-guidelines.md).
//
// Its colours come from GameScreenStyle.h, which is where the games'
// exception to the palette is argued, not from lib/ui/Theme.h.

#include <lvgl.h>

#include "TableTennisGame.h"
#include "TableTennisSounds.h"
#include "GameScreenStyle.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"

namespace knobify::ui {

using games::TableTennisGame;

namespace {

using game_style::makeBlock;
using game_style::phosphor;
using game_style::vacuum;

// The court is centred in the framebuffer; its corners touch the bezel
// exactly, which is why nothing is ever drawn at one (the original has no
// side walls -- the ball leaving sideways is the point).
constexpr lv_coord_t kCourtLeft = (drivers::kLcdHorRes - TableTennisGame::kCourtWidth) / 2;
constexpr lv_coord_t kCourtTop =
    (drivers::kLcdVerRes - TableTennisGame::kCourtOuterHeight) / 2;
constexpr lv_coord_t kPlayTop = kCourtTop + TableTennisGame::kWallThickness;

constexpr lv_coord_t kNetDashWidth = 4;
constexpr lv_coord_t kNetDashHeight = 12;
constexpr lv_coord_t kNetDashGap = 8;

// Scores sit inside the court above the net, where the original puts
// them. The ball passes over them, as it does on the original.
constexpr lv_coord_t kScoreTop = kPlayTop + 20;
constexpr lv_coord_t kScoreInset = 72;

// The one hint the game draws itself, below the bottom wall, and only
// while the ball is parked. How to *leave* is a message instead (see
// renderTableTennis): it is needed once, on arriving, and a permanent line
// telling you how to stop playing is not part of a game.
constexpr lv_coord_t kHintTop = kCourtTop + TableTennisGame::kCourtOuterHeight + 8;
constexpr lv_coord_t kHintWidth = 220;

// Low in the court, clear of the parked ball above it and of the bezel
// below: the calmest wide spot on a screen that is otherwise all game.
constexpr ui_widgets::MessageAnchor kTableTennisMessageAnchor{drivers::kLcdHorRes / 2,
                                                       236};

}  // namespace

void ScreenManager::renderTableTennis() {
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
  lv_obj_add_event_cb(touchArea, onTableTennisTapped, LV_EVENT_CLICKED, this);

  // The two walls, on the inscribed rectangle's own top and bottom edges.
  makeBlock(screen_, kCourtLeft, kCourtTop, TableTennisGame::kCourtWidth,
            TableTennisGame::kWallThickness);
  makeBlock(screen_, kCourtLeft,
            kCourtTop + TableTennisGame::kCourtOuterHeight - TableTennisGame::kWallThickness,
            TableTennisGame::kCourtWidth, TableTennisGame::kWallThickness);

  // The dashed net.
  const lv_coord_t netX =
      kCourtLeft + (TableTennisGame::kCourtWidth - kNetDashWidth) / 2;
  for (lv_coord_t y = kPlayTop + 2;
       y + kNetDashHeight <= kPlayTop + TableTennisGame::kCourtHeight;
       y += kNetDashHeight + kNetDashGap) {
    makeBlock(screen_, netX, y, kNetDashWidth, kNetDashHeight);
  }

  tableTennisPlayerScore_.create(screen_,
                          kCourtLeft + kScoreInset -
                              ui_widgets::SegmentDigits::kWidth / 2,
                          kScoreTop, phosphor());
  tableTennisAiScore_.create(screen_,
                      kCourtLeft + TableTennisGame::kCourtWidth - kScoreInset -
                          ui_widgets::SegmentDigits::kWidth / 2,
                      kScoreTop, phosphor());

  tableTennisPlayerPaddle_ =
      makeBlock(screen_, kCourtLeft + TableTennisGame::kPlayerPaddleX, kPlayTop,
                TableTennisGame::kPaddleWidth, TableTennisGame::kPaddleHeight);
  tableTennisAiPaddle_ =
      makeBlock(screen_, kCourtLeft + TableTennisGame::kAiPaddleX, kPlayTop,
                TableTennisGame::kPaddleWidth, TableTennisGame::kPaddleHeight);
  tableTennisBall_ = makeBlock(screen_, kCourtLeft, kPlayTop, TableTennisGame::kBallSize,
                        TableTennisGame::kBallSize);

  tableTennisHint_ = game_style::makeLabel(
      screen_, (drivers::kLcdHorRes - kHintWidth) / 2, kHintTop, kHintWidth,
      &knobify_text_font_14);

  // This board has no button, the game has no back button, and a modal
  // screen whose only way out is an unannounced gesture is exactly the
  // mode you can get stuck in (ux-guidelines §7) -- the first person to
  // play it asked how to leave (2026-09-18). Said once, on arriving, in
  // the app's own voice rather than painted permanently on the court.
  messages_.show("Swipe right to leave", kTableTennisMessageAnchor, lv_tick_get());

  tableTennis_.start(lv_tick_get());
  shownTableTennisPhase_ = TableTennisGame::Phase::Rally;  // Force the first hint update.
  lastTableTennisDrawMs_ = lv_tick_get();
  applyTableTennisScene();
}

void ScreenManager::onTableTennisTapped(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->tableTennisBall_) return;
  self->tableTennis_.tap(lv_tick_get());
  self->applyTableTennisScene();
}

void ScreenManager::onTableTennisKnob(int16_t delta) {
  if (!tableTennisBall_) return;
  tableTennis_.movePlayerPaddle(delta);
  applyTableTennisScene();
}

// One blip per queued event, oldest first. Only the last one of a frame
// is actually heard -- the generator replaces rather than layers -- which
// is right: a ball that clips the wall and the paddle in the same 33 ms
// made one sound on the original too.
void ScreenManager::drainTableTennisSounds() {
  if (!blips_) return;
  for (TableTennisGame::Sound sound = tableTennis_.takeSound();
       sound != TableTennisGame::Sound::None; sound = tableTennis_.takeSound()) {
    const games::Blip blip = games::blipFor(sound);
    if (blip.frequencyHz != 0) blips_->blip(blip.frequencyHz, blip.durationMs);
  }
}

// Moves only what moved. LVGL invalidates just the areas involved, which
// is why this is six rectangles rather than a full-screen canvas: a
// 360x360 canvas would be 259 KB in PSRAM and redraw the whole court for
// an 8px ball. Heavy redraw activity is also what starves the audio
// decoder (AGENTS.md), so the frame does as little as it can.
void ScreenManager::applyTableTennisScene() {
  if (!tableTennisBall_) return;
  lv_obj_set_pos(tableTennisPlayerPaddle_, kCourtLeft + TableTennisGame::kPlayerPaddleX,
                 kPlayTop + tableTennis_.playerPaddleY());
  lv_obj_set_pos(tableTennisAiPaddle_, kCourtLeft + TableTennisGame::kAiPaddleX,
                 kPlayTop + tableTennis_.aiPaddleY());
  lv_obj_set_pos(tableTennisBall_, kCourtLeft + tableTennis_.ballX(),
                 kPlayTop + tableTennis_.ballY());
  tableTennisPlayerScore_.setValue(tableTennis_.playerScore());
  tableTennisAiScore_.setValue(tableTennis_.aiScore());

  const TableTennisGame::Phase phase = tableTennis_.phase();
  if (phase != shownTableTennisPhase_) {
    shownTableTennisPhase_ = phase;
    const bool parked =
        phase == TableTennisGame::Phase::Ready || phase == TableTennisGame::Phase::Over;
    if (parked) {
      lv_label_set_text(tableTennisHint_, phase == TableTennisGame::Phase::Ready
                                       ? "TAP TO SERVE"
                                       : "TAP TO PLAY AGAIN");
      lv_obj_clear_flag(tableTennisHint_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(tableTennisHint_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

bool ScreenManager::tickTableTennis(uint32_t nowMs, bool visible) {
  if (!tableTennisBall_) return false;
  if (tabs_.activeStack().current().kind != navigation::ScreenKind::TableTennis) {
    return false;
  }
  if (!visible) return false;

  // Physics and sound run every loop, drawing only every kTableTennisFrameMs.
  //
  // They used to share the frame gate, which made every blip up to a frame
  // late: a hit is detected inside one of tick()'s sub-steps, but nothing
  // was told about it until the next redraw 33ms later. Measured on the
  // device (2026-09-18), the audio path from trigger to the DAC is under
  // 2.5ms -- so the gate was most of the delay, and it was the half nobody
  // would look at, because both halves of a frame moved together and the
  // picture looked right.
  //
  // This costs nothing: tick() splits its elapsed time into fixed sub-steps
  // either way, so the same physics runs, just announced sooner. The
  // expensive half -- repositioning LVGL objects, which is what starves the
  // audio decoder if it runs too often (ADR 0006) -- keeps its own rate.
  tableTennis_.tick(nowMs);
  drainTableTennisSounds();

  if (nowMs - lastTableTennisDrawMs_ >= kTableTennisFrameMs) {
    lastTableTennisDrawMs_ = nowMs;
    applyTableTennisScene();
  }

  // A rally has to hold the display awake by itself: nothing here touches
  // the screen or the knob while the ball is in play, and once the idle
  // timeout dims the panel main.cpp stops pumping LVGL entirely -- the
  // game would freeze mid-point, not merely dim.
  return tableTennis_.phase() == TableTennisGame::Phase::Rally;
}

}  // namespace knobify::ui
