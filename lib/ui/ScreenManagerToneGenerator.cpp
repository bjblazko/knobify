// ScreenManager's tone generator screen (ADR 0024), beside the other
// one-file-per-screen parts of ScreenManager.
//
// Top to bottom, all centred -- a round screen has no usable corners
// (ux-guidelines §7): back and caption, the band (scope or spectrum, swiped
// between), what the band's scales are, which page it is on, the selected
// value, the chips, and Play/Stop as the one primary button.

#include <lvgl.h>

#include <algorithm>
#include <cstring>

#include "AudioOutputStage.h"
#include "GeneratorControl.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"
#include "ToneSettings.h"
#include "TriggeredScope.h"

#ifdef KNOBIFY_GENERATOR_DEBUG
#include <Arduino.h>
#include <esp_timer.h>
#endif

namespace knobify::ui {

using signal::ToneParam;
using signal::ToneSettings;
using signal::Waveform;

namespace {

constexpr lv_coord_t kBandWidth = 260;
constexpr lv_coord_t kBandHeight = 88;
constexpr lv_coord_t kBandY = 76;
constexpr lv_coord_t kScaleLabelY = kBandY + kBandHeight + 4;
constexpr lv_coord_t kDotsY = kScaleLabelY + 22;
constexpr lv_coord_t kDotSize = 6;
constexpr lv_coord_t kDotGap = 8;
constexpr lv_coord_t kValueY = 198;
constexpr lv_coord_t kChipY = 238;
constexpr lv_coord_t kChipWidth = 68;
constexpr lv_coord_t kChipHeight = 36;
constexpr lv_coord_t kChipGap = 6;
constexpr lv_coord_t kPlaySize = 56;
constexpr lv_coord_t kPlayY = 286;
// How far a finger must travel across the band to turn its page.
constexpr lv_coord_t kSwipeMinPx = 40;

// Noise has no pitch to lock on to; this picks the 20 ms timebase.
constexpr float kNoiseTimebaseHz = 100.0f;
// The spectrum's band: 0 dBFS at the top, -80 at the bottom, in the
// tenths of a dB it is drawn in.
constexpr int32_t kSpectrumTopDeci = 0;
constexpr int32_t kSpectrumBottomDeci = -800;

constexpr ToneParam kToneChipOrder[signal::kToneParamCount] = {
    ToneParam::Waveform, ToneParam::Frequency, ToneParam::Level, ToneParam::Shape};

// The frequency the scope's timebase is chosen for.
float timebaseHz(const signal::OscillatorParams &params) {
  return params.waveform == Waveform::Noise ? kNoiseTimebaseHz : params.frequencyHz;
}

}  // namespace

void ScreenManager::renderToneGenerator() {
  if (!toneSession_) return;
  toneScopeSamples_.resize(drivers::AudioOutputStage::kSampleRingSize);

  const lv_coord_t bandX = (drivers::kLcdHorRes - kBandWidth) / 2;
  toneScope_.create(screen_, bandX, kBandY, kBandWidth, kBandHeight, theme::ink(),
                    theme::surfaceAlt());

  // Over the trace, so it takes the finger; transparent, so it shows
  // nothing. Swiping it turns the band's page.
  toneBand_ = lv_obj_create(screen_);
  lv_obj_set_size(toneBand_, kBandWidth, kBandHeight);
  lv_obj_set_pos(toneBand_, bandX, kBandY);
  lv_obj_set_style_bg_opa(toneBand_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(toneBand_, 0, 0);
  lv_obj_clear_flag(toneBand_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(toneBand_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(toneBand_, onToneBandPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(toneBand_, onToneBandReleased, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(toneBand_, onToneBandReleased, LV_EVENT_PRESS_LOST, this);

  toneScaleLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(toneScaleLabel_, &knobify_text_font_14, 0);
  lv_obj_set_style_text_color(toneScaleLabel_, theme::structure(), 0);
  lv_obj_align(toneScaleLabel_, LV_ALIGN_TOP_MID, 0, kScaleLabelY);

  for (size_t i = 0; i < toneDots_.size(); ++i) {
    lv_obj_t *dot = lv_obj_create(screen_);
    lv_obj_set_size(dot, kDotSize, kDotSize);
    const lv_coord_t offset = (static_cast<lv_coord_t>(i) * 2 - 1) * (kDotSize + kDotGap) / 2;
    lv_obj_align(dot, LV_ALIGN_TOP_MID, offset, kDotsY);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    toneDots_[i] = dot;
  }

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
  applyToneView();
  updateToneGeneratorDisplay();
}

void ScreenManager::updateToneGeneratorDisplay() {
  if (!toneSession_ || !toneValueLabel_) return;
  const ToneSettings &settings = toneSession_->settings();
  char text[24];
  settings.valueText(settings.selected(), text, sizeof(text));
  lv_label_set_text(toneValueLabel_, text);
  layoutToneChips();
  applyToneLabel();
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

void ScreenManager::applyToneView() {
  const bool scope = toneView_ == ToneView::Scope;
  for (size_t i = 0; i < toneDots_.size(); ++i) {
    if (!toneDots_[i]) continue;
    const bool current = (i == 0) == scope;
    lv_obj_set_style_bg_color(toneDots_[i], current ? theme::ink() : theme::surfaceAlt(), 0);
  }
  toneScope_.setZeroLineVisible(scope);
  if (scope) {
    toneScope_.clear();
  } else {
    toneSpectrum_.reset();
    std::array<int16_t, ui_widgets::ScopeTrace::kPoints> floor;
    floor.fill(static_cast<int16_t>(kSpectrumBottomDeci));
    toneScope_.setPoints(floor.data(), floor.size(), kSpectrumBottomDeci, kSpectrumTopDeci);
  }
  applyToneLabel();
}

// The scope says what its scales are, the way a bench scope's time/div
// and volts/div do; the spectrum says what its axis spans.
void ScreenManager::applyToneLabel() {
  if (!toneScaleLabel_ || !toneSession_) return;
  char text[40];
  if (toneView_ == ToneView::Scope) {
    const ToneSettings &settings = toneSession_->settings();
    toneScale_.update(timebaseHz(settings.params()), settings.levelDb());
    toneScale_.label(text, sizeof(text));
  } else {
    snprintf(text, sizeof(text), "20 Hz \xE2\x80\x93 20 kHz \xC2\xB7 0 to -80 dB");
  }
  if (strcmp(lv_label_get_text(toneScaleLabel_), text) != 0) {
    lv_label_set_text(toneScaleLabel_, text);
  }
}

void ScreenManager::onToneBandPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  if (self) self->toneSwipeStartX_ = point.x;
}

// Right to left brings the spectrum in, left to right takes it back out,
// like turning a page; a tap does nothing.
void ScreenManager::onToneBandReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  const lv_coord_t dx = point.x - self->toneSwipeStartX_;
  ToneView next = self->toneView_;
  if (dx <= -kSwipeMinPx) next = ToneView::Spectrum;
  if (dx >= kSwipeMinPx) next = ToneView::Scope;
  if (next == self->toneView_) return;
  self->toneView_ = next;
  self->applyToneView();
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
  const uint32_t dtMs = nowMs - lastToneScopeMs_;
  if (dtMs < kToneScopeFrameMs) return;
  lastToneScopeMs_ = nowMs;

#ifdef KNOBIFY_GENERATOR_DEBUG
  const int64_t startUs = esp_timer_get_time();
#endif
  const size_t count =
      scopeSource_ ? scopeSource_->readRecent(toneScopeSamples_.data(),
                                              toneScopeSamples_.size())
                   : 0;
  if (toneView_ == ToneView::Scope) {
    drawToneScope(count);
  } else {
    drawToneSpectrum(count, std::min<uint32_t>(dtMs, 100));
  }
#ifdef KNOBIFY_GENERATOR_DEBUG
  static uint32_t frames = 0;
  static int64_t worstUs = 0;
  worstUs = std::max(worstUs, esp_timer_get_time() - startUs);
  if (++frames % 90 == 0) {
    Serial.printf("[tones] %s frame worst %lldus\n",
                  toneView_ == ToneView::Scope ? "scope" : "spectrum", worstUs);
    worstUs = 0;
  }
#endif
}

void ScreenManager::drawToneScope(size_t count) {
  if (count == 0) {
    // Nothing new reached the DAC: stopped (or never started).
    if (!toneSession_->running()) toneScope_.clear();
    return;
  }
  applyToneLabel();  // Also brings toneScale_ up to date.
  const float span = static_cast<float>(toneScale_.timebaseUs()) *
                     static_cast<float>(signal::kGeneratorSampleRate) / 1000000.0f;
  signal::TriggeredScope::traceWindow(toneScopeSamples_.data(), count, span,
                                      toneScopeTrace_.data(), toneScopeTrace_.size());
  // Held to the level range's top, not to the level itself: within a
  // range, +6 dB draws twice as tall.
  const auto fullScale = std::max<int32_t>(
      1, static_cast<int32_t>(ToneSettings::dbToLinear(toneScale_.topDb()) * 32767.0f));
  toneScope_.setSamples(toneScopeTrace_.data(), toneScopeTrace_.size(), fullScale);
}

void ScreenManager::drawToneSpectrum(size_t count, uint32_t dtMs) {
  toneSpectrum_.update(count ? toneScopeSamples_.data() : nullptr, count,
                       signal::kGeneratorSampleRate, dtMs);
  const auto &levels = toneSpectrum_.levels();
  for (size_t i = 0; i < toneScopeTrace_.size() && i < levels.size(); ++i) {
    toneScopeTrace_[i] = static_cast<int16_t>(
        std::clamp<float>(levels[i] * 10.0f, static_cast<float>(kSpectrumBottomDeci),
                          static_cast<float>(kSpectrumTopDeci)));
  }
  toneScope_.setPoints(toneScopeTrace_.data(), toneScopeTrace_.size(),
                       kSpectrumBottomDeci, kSpectrumTopDeci);
}

}  // namespace knobify::ui
