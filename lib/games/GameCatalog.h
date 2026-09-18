#pragma once

#include "ScreenId.h"

namespace knobify::games {

// The Games list (ADR 0022): one row per game, in the order they are
// shown. Unlike kMenuEntries and CollectionId, nothing stores an index
// into this table, so rows may be reordered freely -- adding a second
// game is one row and nothing else.
struct GameEntry {
  const char *label;
  navigation::ScreenKind screen;
};

constexpr GameEntry kGames[] = {
    {"Table Tennis", navigation::ScreenKind::TableTennis},
};

constexpr int kGameCount = static_cast<int>(sizeof(kGames) / sizeof(kGames[0]));

}  // namespace knobify::games
