#pragma once
// The game's own common dialogue (menu page 6; game_offsets.md, "Common dialogue"): a body of text and up to three
// buttons, drawn and driven by the game. The mod's in-game menus are made of it, so they look and handle like the game's.
// Game thread only. A dialogue that is open is closed by the player's answer; the mod does not close it.
#include "discovery.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace mod::dialog {
constexpr unsigned MaxRows=3;
constexpr unsigned NoAnswer=~0u,Dismissed=~0u-1;   // take(): nothing yet; backed out without a row
struct Request {std::u16string body;std::vector<std::u16string> rows;unsigned start=0;};   // rows: 1..MaxRows; start: the row first selected
struct Api {
    std::function<bool()> free;                     // nothing of the game's menus is up and no dialogue waits
    std::function<bool(const Request&)> open;       // false: not opened
    std::function<bool()> showing;                  // this game's dialogue is on the screen
    std::function<unsigned()> take;                 // the row answered since the last call, Dismissed, or NoAnswer
};
// The game's dialogue setting for a number of rows (0 OK, 3 Yes/No, 4 Yes/No/Cancel; 0xff: no such dialogue), and the row
// a button type stands for (the game's types: 1 OK, 2 Yes, 3 No, 4 Cancel). Our labels replace the game's on those rows.
uint8_t setting(size_t rows);
unsigned rowOf(size_t rows,uint8_t type);
Api nativeApi(discovery::Sampler&);
}
