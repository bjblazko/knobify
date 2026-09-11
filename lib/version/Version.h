#pragma once

// Pure, hardware-free module — deliberately the first thing in the tree,
// to prove the native test harness (docs/adr/0003-testing-strategy.md)
// works end to end before any real hardware-touching code exists.
namespace knobify {

inline constexpr const char *kVersion = "0.0.0-scaffold";

}  // namespace knobify
