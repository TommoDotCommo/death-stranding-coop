# Collections: shareable groups of structures

A **collection** is a group of structures a player built, written as one line of text (a **DSK string**,
`DSK1-...`) or a `.dsk` file holding it. Someone else pastes it into his launcher, and the server he plays on makes
those structures for him, here where he stands or at their original place; his game then gets them the way it gets
every other player's structures (`build_received` builds them). The owner asked for it on 2026-11-18: "some sort of
way to select items in your game and form them into a group for sharing, maybe somehow make it import/export string".

Built and tested without a game (2026-11-18). No game has received an imported collection yet; what a live run must
check is at the end.

## What a player does

1. **Make one.** In the launcher, Play, **My strand on this server**: select rows (Ctrl or Shift for several), then
   **Make a collection of selected**; or **Collect near me** with a radius in metres (everything he built on that server
   within that distance of where his game last said he stood). The launcher copies the DSK string to the clipboard and
   writes it to a `.dsk` file in `%LOCALAPPDATA%\DeathStrandingCoop\collections\`, and says both.
2. **Share it**: paste the text anywhere (chat, a forum), or send the file.
3. **Import one.** **Import a collection** in the same window: paste the text (or **Load .dsk file...**), choose
   **Here** (around where his game last said he stands, turned to the way he faces) or **Original place**, then
   **Import**. The launcher says how many structures the server made and which it left out, and why.

Collections need a group server: making and importing are calls of the server (its records of the player's objects,
and of where his game is). Without a server, in solo play, there is nothing to make or import (not built: see the end).

## The DSK string (format 1)

`DSK1-` and the data in Crockford base32 (`0-9A-Z` without `I L O U`), in groups of 5 separated by `-`, like the
invites (`launcher/vendor/coop_invite.h`). Reading it: the prefix in any case; `-`, spaces, tabs and line breaks in the
body are ignored (a pasted string may be wrapped); lower case, `I`/`L` as 1 and `O` as 0 are taken. At most 16384
characters in all. No deflate: the repository vendors no compressor, so the data is a compact encoding of its own
(variable-length integers), about 15 to 40 bytes a structure.

The data, every integer a LEB128 varint (signed ones zigzag), and a CRC-32 (IEEE) of everything before it, 4 bytes
little-endian, at the end:

| Field | Values |
|---|---|
| version | 1 (any other: refused) |
| flags | 1 the heading is known, 2 a name follows; any other bit: refused |
| count | 1..200 structures |
| anchor area | the game's area number `q` (0..1000000) |
| anchor x, y, z | centimetres in the game's axes (x, y on the ground, z up), each within 100 km |
| heading | with flag 1: the porter's facing in hundredths of a degree (-18000..18000; 0 is the game's +y, 90 its +x) |
| name | with flag 2: 1..32 bytes of UTF-8, no control characters, no space at either end |

Then each structure:

| Field | Values |
|---|---|
| kind | the type letter the strand server stores (`t`: `l` ladder, `e` cairn, `b` bridge, `n` rain shelter, ...), `a`..`z` |
| fields | which of the optional parts below follow (bits); any other bit: refused |
| dx, dy, dz | its place, centimetres from the anchor, each within 2 km |
| rz | its turn about the vertical, the game's `rz` (degrees x 10^5) |
| 1: tilt | `rx`, `ry` (degrees x 10^5), at least one not 0 |
| 2: level | `st`, 1..32 letters, digits, `_`, `.`, `-` (a ladder's level "0", a bridge's "30", a sign's type) |
| 4: area | its own area when it is not the anchor's |
| 8: cell | the game's cell `x`, `y` and map `m` |
| 16: p | the game's `p` is 1 |
| 32: materials | the six material counts its builder delivered (the game's `mt`, summed), each up to 1000000 |
| 64: comment | a sign's text: up to 16 phrase numbers (0..65535) and up to 120 bytes of text |
| 128: blocks | the game's info blocks of integers (`ri` rope, `si` stone, `bi` bridge, `pi`, `vi`, `ei`, `ci`): up to 7, each up to 8 fields named by 1..8 lower-case letters |

Rules the reader keeps (`world/collection.h`, `parse`): every field in its range; blocks and their fields in their
order, once each; the CRC right; no byte left over; and the text exactly the one this data writes (the canonical
form: no varint longer than it needs, no stray bits in the last character). Anything else is refused with the reason,
and nothing is made. What is not carried: the builder's name and id, likes, the original service's ids and accounts,
the game's `tags`.

## The server's calls

Two calls of the launcher's API (`world/README.md`, "The launcher's API"), under the prefix, `POST`, signed under
identity keys like every change there:

| Call | Answer |
|---|---|
| `POST …/coop/collection` `{"player":KEY,"objects":[ID,...]}` or `{"player":KEY,"near":METRES}`, optional `"name":TEXT`, `"relay_id":"N"` | `collection` (the DSK string), `count`, `left_out` (`id`, `why`), `anchor` |
| `POST …/coop/import` `{"player":KEY,"collection":"DSK1-...","place":"here"\|"original"}`, optional `"relay_id":"N"` | `imported`, `ids`, `shared`, `held`, `kept`, `waiting`, `left_out` (`index`, `why`), `anchor` (`area`, `x`, `y`, `z`, `turned`), `materials` |

**Making one.** Only the player's own objects built here (in any state but a remnant, collections he imported
included), never another player's, an import from a linked server, or an object without a position. `objects` names
1..200 of them; `near` takes every one within 1..2000 m of his **spot**: where the server's relay last had his game
(below), not older than 10 minutes. The anchor is the spot with its heading (`near`), or the middle of the named
objects with the area most of them have and no heading (`objects`). A structure more than 2 km from the anchor is
left out. A string over 16384 characters is refused: choose fewer.

**Importing one**, in this order:

1. The policy's `import` decides who may: `everyone`, `members` (the owner's members), `owner` (the owner's key only),
   `nobody`. Refused (403) otherwise.
2. The string is read as above; a bad one is refused (400) with the reason.
3. **Here**: his spot (409 when the server has none from the last 10 minutes: "start the game and move a little").
   The structures are turned about the anchor by his heading minus the collection's (when both are known; else not
   turned), moved to his spot, and given the area of the nearest object with a place on the server within 1.5 km
   (any player's: the game asks objects by area) and its cell and map; with none there, 409 (use Original place).
   **Original place**: the anchor and every structure's own area, cell and map as they were.
4. The hard cap: the whole import is refused (403) when it would take him over 4000 objects.
5. A structure closer than the policy's `spread` to one of its kind in the same area (one this import made, one every
   player is handed, or one of his own) is left out (`too-close`): the game would fail it. So is one whose place the
   game cannot hold (`outside`: beyond about 21 km on an axis).
6. Each structure that is left becomes an object of his, as his game's upload would: shared, held (the `build`
   setting or the per-player `limit`), kept or waiting, as his choices and the policy say. It carries the collection's
   name (or `DSK1-` and its CRC) as its source (`collection` in the world file and in `mine`).

His own game is then handed them like other players' structures: an object he imported from a collection is the one
kind of his own objects that `findQpidObjects` gives back to him (his save does not have it), whatever its state;
the others are handed them as any shared object. Withdrawal, review, offer, the owner's removal and linked servers
treat them as his objects.

`import_cost`: `free` (every preset) or `materials`. With `materials` the server records what the structures carried
(the six counts, summed) on each object and in the answer; nothing is taken from anyone yet.

**The spot.** The relay keeps, for each player of this server whose game says where it is, the last place and (for a
broadcast player, from his presence) the facing, and gives them to the world about once a second (`RelayConfig::spotted`;
the relay's axes x, up, y become the game's x, y, z). Under identity keys the spot is the relay id made from his key;
under identity none the call names it (`relay_id`, the launcher's player number), as anyone can there. A spot is not
saved with the world. The facing comes only with a presence update, and a game sends those only while another
broadcast player is near it: a player alone has a place and no facing, so a collection he makes has no heading and an
import Here is not turned (each structure keeps its own `rz`).

| Rule | Why |
|---|---|
| A collection holds only the caller's own objects built here | nobody packs and carries off another's work |
| The string is checked whole before anything is made: ranges, order, CRC, nothing left over, the canonical text | a pasted string that was cut or changed makes nothing |
| 200 structures and 16384 characters at most | one paste cannot fill a world or a server's memory |
| `import` decides who imports; the per-player limit holds the rest; the hard cap refuses | an import goes through the same rules as building |
| A structure the game would fail (too close, outside) is left out and said so | nothing is stored that no game can place |
| Here needs a fresh spot; the area comes from objects with a place near it | a structure is never put at a guessed place |

## Not built, and unknown

- **Solo** (no server): export and import without a server. To do: the mod would read the porter's structures from
  the game (`mod/own_strand.h` has a reader) and build imported ones itself.
- **In the game**: no key or menu in the game makes or imports one; the launcher does.
- **The area of a place.** The game says which area its porter is in only by what it asks; the server takes the area
  of the nearest object it knows. Wrong near an area's border is possible. Whether the game places a structure whose
  `q`, cell and map are those of a neighbour is unknown.
- **The importer's own game** is handed objects whose builder is its own player. Whether the game takes those as its
  own (and so, perhaps, deletes or uploads them) is unknown: the first live check.
- **Turning.** `rz` follows the game's angle reading (`mod/game_offsets.md`, "What the body holds"): turning a
  structure by a heading changes `rz` by minus that heading. The blocks are carried unchanged: a climbing rope's
  heading (`ri.h`) or a bridge's angle (`bi.a`) may be in the world's axes and then point the old way after a turn.
- What a received structure skips (the game's spacing and area checks, `mod/README.md`, "Received structures built")
  applies to imported ones too.

Live checks: (1) a collection of a ladder and a cairn imported at the original place by a second game: built there;
(2) the same imported Here: built at the porter, turned; (3) the importer's own game: does it build them, and does it
send `deleteObject` or `createObject` for them; (4) a bridge and a rope turned by 90 degrees.

## Tests

All game-free, in `make test`:

- `world/collection_test.cpp`: the DSK string written and read back (every optional part, lower case, wrapped, without
  dashes, I/L/O), found in a chat line, described; refused for a wrong prefix, version or flags, a check sum, 201
  structures, an offset past 2 km, blocks out of order, a byte left over, a number written long, a character too many,
  stray bits, a text over 16384 characters; the turn. Then a fuzzer (3000 rounds in `make test`, built with
  AddressSanitizer and UndefinedBehaviorSanitizer when the compiler has them): random collections round trip, every
  changed character is refused, several random edits are never read as another collection, random bytes never crash.
  Mutants `COLLECTION_CRC`, `_VERSION`, `_COUNT`, `_RANGE`, `_ORDER`, `_TRAILING`, `_SIZE`, `_CANONICAL`, `_TURN_SIGN`.
- `world/collections_test.cpp`: the two calls through the HTTP surface: own objects only; at the original place on the
  same server nothing is made (the originals stand there), on another all of it, with angles, levels, blocks and
  materials; the importer's game is handed his imports and a third player's too; withdrawal; the world file; near the
  spot, a stale spot, here (moved, turned by 90 degrees, in the area of the nearest object, 409 where there is none);
  `import` nobody, members, owner; the limit; `import_cost`; the spread; the hard cap; bodies and strings refused;
  identity keys (unsigned 401, another's key 403, the key's own relay id). Mutants `COLLECTION_NOT_SELF`, `_OTHERS`,
  `_IMPORT_ANYONE`, `_NO_TURN`, `_CAP`, `_SPREAD`, `_SAVE_LOST`, `_SPOT_AXES`.
- `world/relay_test.cpp` and `world/broadcast_test.cpp`: the relay reports a player's place about once a second, never
  one without a place, and a broadcast player's facing once another player is near (mutants `RELAY_SPOT_NO_POSITION`,
  `RELAY_SPOT_YAW`).
- `world/test_collections.py`: the built server over sockets, the stand-in games signing in and asking
  `findQpidObjects` as the game does: one game's ladder, cairn and bridge collected, imported at the original place on
  a second server and here on the first (the importer's place from a HELLO to the relay), and handed to the importer's
  game (what `build_received` builds) and to the others. A server built with `MUTANT_COLLECTION_NOT_SELF` must fail it.
- `launcher/core_test.cpp` and `launcher/test_collections.py`: a pasted text and a `.dsk` file found, checked and
  described; the file's name and its save (never over another); the launcher's calls on the built server, from rows
  and near the game, imported from a file and from a chat line; a changed string refused before it is sent (mutant
  `LAUNCHER_COLLECTION_UNCHECKED`, in both).

Not checked: the launcher's window (built for Windows, not seen on a screen), and anything in a game.
