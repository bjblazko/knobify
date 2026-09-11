// Entry point placeholder. Real wiring (constructing drivers, injecting
// them into logic classes per docs/coding-guidelines.md) starts once
// v1 features are designed — see the plan's "explicitly not in this
// session" note.
#include <Arduino.h>

#include "Version.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("knobify %s starting\n", knobify::kVersion);
}

void loop() {
  // Intentionally empty until playback/UI logic exists.
}
