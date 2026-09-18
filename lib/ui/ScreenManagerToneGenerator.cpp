// ScreenManager's tone generator screen (ADR 0024), beside the other
// one-file-per-screen parts of ScreenManager.
//
// Top to bottom, all centred -- a round screen has no usable corners
// (ux-guidelines §7): back and caption, the scope, the selected value,
// the chips, and Play/Stop as the one primary button.

#include <lvgl.h>

#include <algorithm>

#include "AudioOutputStage.h"
#include "GeneratorControl.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"
#include "ToneSettings.h"
#include "TriggeredScope.h"

namespace knobify::ui {

using signal::ToneParam;
using signal::ToneSettings;
using signal::Waveform;

namespace {

constexpr lv_coord_t kScopeWidth = 260;
constexpr lv_coord_t kScopeHeight = 100;
constexpr lv_coord_t kScopeY = 84;
constexpr lv_coord_t kValueY = 192;
constexpr lv_coord_t kChipY = 238;
constexpr lv_coord_t kChipWidth = 68;
constexpr lv_coord_t kChipHeight = 36;
constexpr lv_coord_t kChipGap = 6;
constexpr lv_coord_t kPlaySize = 56;
constexpr lv_coord_t kPlayY = 286;

// Noise has no pitch to lock on to; 20 ms of it reads as noise.
constexpr float kNoiseTimebaseHz = 100.0f;

constexpr ToneParam kToneChipOrder[signal::kToneParamCount] = {
    ToneParam::Waveform, ToneParam::Frequency, ToneParam::Level, ToneParam::Shape};

}  // namespace

void ScreenManager::renderToneGenerator() {
  if (!toneSession_) return;
  toneScopeSamples_.resize(drivers::AudioOutputStage::kSampleRingSize);

  toneScope_.create(screen_, (drivers::kLcdHorRes - kScopeWidth) / 2, kScopeY,
                    kScopeWidth, kScopeHeight, theme::ink(), theme::surfaceAlt());

  toneValueLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(toneValueLabel_, &knobify_text_font_28, 0);
  lv_obj_set_style_text_color(toneValueLabel_, theme::ink(), 0);
  lv_obj_align(toneValueLabel_, LV_ALIGN_TOP_MID, 0, kValueY);

  for (int i = 0; i < signal::kToneParamCount; ++i) {
    toneChipContexts_[i] = ToneChipContext{this, kToneChipOrder[i]};
    lv_obj_t *chip = lv_btn_create(screen_);
    lv_obj_set_size(chip, kChipWidth, kChipHeight);
    lv_obj_set_ext_click_area(chip, kChipGap / 2);
    theme::styleSecondaryButton(chip);
    lv_obj_t *label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, &knobify_text_font_16, 0);
    lv_obj_center(label);
    // On the press, not the click: holding a chip and turning the knob
    // with the other hand is this device's two-handed gesture (§7).
    lv_obj_add_event_cb(chip, onToneChipPressed, LV_EVENT_PRESSED,
                        &toneChipContexts_[i]);
    toneChips_[i] = chip;
  }

  tonePlayButton_ = makeIconButton(screen_, LV_SYMBOL_PLAY, kPlaySize, kPlaySize,
                                   LV_ALIGN_TOP_MID, 0, kPlayY, onTonePlayClicked,
                                   this, ButtonRole::Primary);

  lastToneScopeMs_ = lv_tick_get();
  applyTonePlayButton();
  updateToneGeneratorDisplay();
}

void ScreenManager::updateToneGeneratorDisplay() {
  if (!toneSession_ || !toneValueLabel_) return;
  const ToneSettings &settings = toneSession_->settings();
  char text[24];
  settings.valueText(settings.selected(), text, sizeof(text));
  lv_label_set_text(toneValueLabel_, text);
  layoutToneChips();
}

// Shows the chips this waveform has, centred as a row, the selected one
// filled in ink like a selected list row.
void ScreenManager::layoutToneChips() {
  const ToneSettings &settings = toneSession_->settings();
  const Waveform wave = settings.waveform();
  int shown = 0;
  for (ToneParam param : kToneChipOrder) shown += ToneSettings::visible(param, wave);
  const lv_coord_t rowWidth = shown * kChipWidth + (shown - 1) * kChipGap;
  lv_coord_t x = (drivers::kLcdHorRes - rowWidth) / 2;

  for (int i = 0; i < signal::kToneParamCount; ++i) {
    lv_obj_t *chip = toneChips_[i];
    if (!chip) continue;
    const ToneParam param = kToneChipOrder[i];
    if (!ToneSettings::visible(param, wave)) {
      lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(chip, x, kChipY);
    x += kChipWidth + kChipGap;
    const bool selected = settings.selected() == param;
    lv_obj_set_style_bg_color(chip, selected ? theme::ink() : theme::surfaceAlt(), 0);
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    lv_obj_set_style_text_color(label, selected ? theme::surface() : theme::ink(), 0);
    lv_label_set_text(label, ToneSettings::chipLabel(param, wave));
  }
}

void ScreenManager::applyTonePlayButton() {
  if (!tonePlayButton_ || !toneSession_) return;
  shownToneRunning_ = toneSession_->running();
  lv_label_set_text(lv_obj_get_child(tonePlayButton_, 0),
                    shownToneRunning_ ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
}

void ScreenManager::onToneChipPressed(lv_event_t *e) {
  auto *context = static_cast<ToneChipContext *>(lv_event_get_user_data(e));
  if (!context || !context->self || !context->self->toneSession_) return;
  context->self->toneSession_->select(context->param);
  context->self->updateToneGeneratorDisplay();
}

void ScreenManager::onTonePlayClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->toneSession_) return;
  signal::ToneSession &session = *self->toneSession_;
  if (session.running()) {
    session.stop();
  } else {
    // A tone is for measuring, so it plays alone (ADR 0024). Paused, not
    // stopped: the track is still there to resume afterwards.
    if (self->playback_.state() == playback::PlaybackState::Playing) {
      self->playback_.togglePlayPause(lv_tick_get());
    }
    session.start();
  }
  self->applyTonePlayButton();
}

void ScreenManager::tickToneGenerator(uint32_t nowMs, bool visible) {
  if (!toneSession_ || !toneValueLabel_) return;
  if (shownToneRunning_ != toneSession_->running()) applyTonePlayButton();
  if (!visible) {
    lastToneScopeMs_ = nowMs;
    return;
  }
  if (nowMs - lastToneScopeMs_ < kToneScopeFrameMs) return;
  lastToneScopeMs_ = nowMs;

  const size_t count =
      scopeSource_ ? scopeSource_->readRecent(toneScopeSamples_.data(),
                                              toneScopeSamples_.size())
                   : 0;
  if (count == 0) {
    // Nothing new reached the DAC: stopped (or never started).
    if (!toneSession_->running()) toneScope_.clear();
    return;
  }
  const signal::OscillatorParams params = toneSession_->settings().params();
  const float hint =
      params.waveform == Waveform::Noise ? kNoiseTimebaseHz : params.frequencyHz;
  signal::TriggeredScope::trace(toneScopeSamples_.data(), count,
                                signal::kGeneratorSampleRate, hint,
                                toneScopeTrace_.data(), toneScopeTrace_.size());
  // Scaled to the set level, so the shape fills the band at any level --
  // the number below says how loud it is.
  const auto fullScale = std::max<int32_t>(
      1, static_cast<int32_t>(params.amplitude * 32767.0f));
  toneScope_.setSamples(toneScopeTrace_.data(), toneScopeTrace_.size(), fullScale);
}

}  // namespace knobify::ui
