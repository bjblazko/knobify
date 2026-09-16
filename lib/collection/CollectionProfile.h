#pragma once

#include <cstddef>

#include "CollectionId.h"
#include "LibraryTypes.h"

namespace knobify::collection {

using library::SortOrder;

// Everything that makes one collection differ from another: where it
// lives, what it is called, and the handful of behaviour switches that
// keep spoken word from being treated as music. Pure data with no SD or
// LVGL types, so the table is host-testable.
//
// `icon` is deliberately not stored here -- it is an LVGL icon-font macro
// and belongs with the menu table in the UI layer, which keeps this
// header free of UI dependencies.
struct CollectionProfile {
  CollectionId id;
  // Shown as the main menu label and as the browse root's caption.
  const char *label;
  // Where this collection's audio files live on the SD card.
  const char *rootPath;
  // Its own index cache. Music keeps the historical path so existing
  // cards do not lose their index (and re-scan) on this update.
  const char *cachePath;
  // Artists/Albums/Tracks lists start with a Shuffle row (ADR 0011).
  // Shuffling an audiobook is never what anyone wants.
  bool hasShuffleRow;
  // Coming back to this collection returns to where the title was left,
  // not to its start -- what makes a long spoken-word title usable.
  bool resumesWithinTitle;
  SortOrder sort;
};

constexpr CollectionProfile kCollections[kCollectionCount] = {
    {CollectionId::Music, "Music", "/Music", "/knobify/library.idx",
     /*hasShuffleRow=*/true, /*resumesWithinTitle=*/false, SortOrder::ByTag},
    {CollectionId::Audiobooks, "Audiobooks", "/Audiobooks",
     "/knobify/audiobooks.idx", /*hasShuffleRow=*/false,
     /*resumesWithinTitle=*/true, SortOrder::ByName},
    {CollectionId::RadioPlays, "Radio Plays", "/RadioPlays",
     "/knobify/radioplays.idx", /*hasShuffleRow=*/false,
     /*resumesWithinTitle=*/true, SortOrder::ByName},
};

constexpr const CollectionProfile &profileOf(CollectionId id) {
  return kCollections[indexOf(id)];
}

}  // namespace knobify::collection
