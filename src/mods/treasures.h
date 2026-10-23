#pragma once
#include <stdbool.h>

// How many subids treasure `id` has in the game's treasureObjectData: 1 for a treasure without
// subids, 0 for an id past the table or a null pointer entry.
int mods_treasure_subids(bool seasons, int id);
bool mods_treasure_valid(bool seasons, int id, int subid);
