#pragma once

#include <cstddef>
#include <cstdint>

namespace knobify::collection {

// The audio collections the device browses. knobify used to be a music
// player with one library rooted at "/Music"; a collection is that same
// player parameterised by a root folder and a small behaviour profile, so
// spoken-word material lives beside music instead of inside it -- see
// docs/adr/0018-collections-and-menu-visibility.md.
//
// Stored values: the id is persisted in resume records and in the main
// menu's visibility bitmask, so these are append-only.
enum class CollectionId : uint8_t {
  Music = 0,
  Audiobooks = 1,
  RadioPlays = 2,
};

constexpr std::size_t kCollectionCount = 3;

constexpr std::size_t indexOf(CollectionId id) {
  return static_cast<std::size_t>(id);
}

// False for a byte that names no collection -- decoding a persisted value.
constexpr bool isValidCollection(uint8_t value) {
  return value < kCollectionCount;
}

}  // namespace knobify::collection
