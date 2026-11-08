# Status

Target: DEATH STRANDING DIRECTOR'S CUT, Steam app 1850570, build 13300582, `ds.exe` SHA-256
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`. Other builds are refused.

## Works (tested live on that build)

| Step | Evidence |
|---|---|
| Disposable test copies with their own saves | Profile, settings and gameplay checkpoints written only inside each copy; real saves byte-identical after every run. |
| Two copies at once under one Steam account | The game's single-instance mutex is made per-copy; both reach gameplay and take input. |
| Read the local player's pose | Position (3 x float64) and rotation (3x3 float32) through the game's own local-player path; stable at rest, consistent while walking. |
| Exchange poses between two copies | Over loopback: median error 5 mm, all compared samples within 0.5 m and 10 degrees after excluding samples next to height-change snaps (`mod/analyse_presence.py --snap-window 2`). |
| Modded copies stay offline | The game's config and online-server connections are refused; it switches to its own offline mode. |
| The partner's own body (`own` mode, 2026-11-08) | The mod makes a second Sam with the game's entity creator and dresses it with the game's own in-place art-parts swap (the call the porter's streaming control makes on the resident double), after waiting until the game has dressed the porter. Close-ups over 8 frames: a clean porter-suit Sam; the earlier apply-a-second-outfit dress showed two outfits overlapping. Two copies in own mode: each body within 5 mm of the other porter, visible 100 % of the time, no hang. `peer` (borrow the resident double) stays the default mode. |
| The partner shown without commands (2026-11-08) | `partner: "own"` starts own mode with no test command; the launcher now writes it with the game-thread hook and `partner_gear` (it wrote network-only `self_tick`, which drew nothing). Live, two copies configured that way: both made their body, drew the other porter, copied the gear and pinned the partner on the map (`partner_marker`); no fault recorded. A return to title and Continue made a new body with gear. |
| The partner's gear (opt-in, 2026-11-08) | `partner_gear` copies the porter's visible gear (backpack with its pouches, wrist cuff) onto the own body with the game's entity creator and attach call. Live, one copy and the stand-in: the backpack follows the body walking and running, hides with it, and a two-minute soak with park, return, removal and re-creation ran without a fault (crash recorder on). Two copies in `own` mode with gear on both passed `peer_free`. Each side sends what its porter shows (CARGO type 7) and the partner's body wears that set, and its outfit (art parts, CARGO type 8): live in two copies each body's parts then equalled the other porter's. Carried cargo is not drawn. |
| Story progress in the handshake | The episode is read from the game (read-only) and sent when the session starts (episode 1 in the test save); `progress_mismatch` warns (default) or refuses. |
| Join at host (opt-in) | `join_at_host` with `join_move: "native"` moves a guest that is over 300 m away next to the host once per session with the game's own player teleport (the world map's fast-travel path). Live, one copy and the stand-in host 600 m away: moved, read back 1.6 m from the target, the game set Sam on the terrain there and he stayed; no fault. |
| What the partner carries (opt-in) | `carry_visual` reads the porter's carried cargo read-only and sends a summary; the partner logs it (live: "1 entry ... x2", cleared on disconnect). Nothing is drawn yet. |
| The partner on the map, and pings (opt-in) | Live: a map pin follows the partner, a ping is sent and placed in both worlds, the pin is removed when the partner leaves. |
| Crash and hang recorder (opt-in) | `COOP_CRASH_RECORD=1`: module map at start, a record with module+offset stack for fatal-class exceptions, a minidump on an unhandled one. A research build's forced divide fault on a worker thread was recorded, dumped and recovered live. It found tonight's first bug: the game's crash dialog of 2026-11-07 was the proxy itself (two threads patching import tables at once), fixed with one lock (`redirect/iat_patch.h`). |
| Release proxy name `avrt.dll` | The proxy also builds as `avrt.dll`, a DLL the game imports but does not ship, so an install never replaces a game file. Live in a test copy (game's own `d3dcompiler_47.dll` untouched): loaded at startup, the mod started, the game reached gameplay. Without the launcher's ticket or an explicit test environment the release proxy does nothing, so a game started from Steam runs untouched. |

## Not done

- Drawing the partner as anything other than a standing/walking/running Sam: no vehicles yet (a read-only bike/truck
  reader is built and treats "no ride component" as on foot, but the test save has no vehicle), no carried cargo drawn on
  the partner (the backpack and cuff are, and they follow what the partner shows), no cutscene sharing.
- BTs, MULEs, death and voidouts, shared orders and deliveries, saves of a shared world, shared weather (time of day
  sync is built, off by default, untested live; the time setter can advance the day counter), the partner's own
  controller input.
- Structures built from the game's own menus (the test save is in Episode 1, before the chiral network).
- A real two-PC session. The mod accepts any unicast IPv4 peer (LAN or VPN) and binds only the address that routes
  to it (README, "Two PCs"); it has only run over loopback so far. The launcher (`launcher/`, invite codes, LAN and
  Tailscale discovery, `deathstranding-coop://` links) is built and tested game-free only.

## QA level

Game-free: simulator, fuzzer and mutants for the core and every proxy protection (`make test`), plus seeded fuzzers
for the shared world: one runs the cargo and structure logic through random worlds and checks its invariants, one
runs two simulated worlds against each other and checks that they hold the same cargo after every quiet period.
They found two cargo races (fixed): a quick put-down and pick-up left a stray copy, and identical items close
together could be matched wrongly; the same race was then fixed for dismantled structures. With restarts added, the two-world fuzz
showed shared cargo falling out of step after either mod restarted (the shared ids live in memory); a pick-up now
also names the item and its place, so the partner finds its copy without the id. Dismantled structures got the same treatment: a dismantle
(its own event type) carries the structure's type and place, and structures built in an earlier session are
reported too. Live: two-copy runs
with state read from the processes, and an unattended one-copy regression with `coop-partner` that needs no input or
focus (2026-11-07, about 3 minutes): the copy continues its last save by itself, the double stands within 0.3 m of the
partner (read from the game's memory; 0.032 m measured), cargo goes both ways and each side's pick-up removes the
other's copy, a ladder is built and dismantled in both directions, and the real saves are unchanged. A partner built without the wire identity fails it.

### Guarded callback and failed Sam clone (2026-11-02)

The versioned game-thread callback ran live on the reviewed main-loop stack in
menu and loaded gameplay. The first supervised Sam-resource creator call then
crashed during component activation with a null DSPlayerController; the actor
never returned. No transform setter or removal ran. This establishes callback
delivery but leaves the world-mutation contract unverified. The native resource
is now quarantined before creation, with a tested refusal gate and no config
override. The next actor experiment needs a standalone resource or the supported
player-controller construction context. Full details are in `mod/game_offsets.md`.

The test process was stopped by identity, normal read-only configs restored, and
original saves remained unchanged immediately and after a 30-second settle check.

Static follow-up traced the missing setup: the context-driven creation path calls
the player owner's entity assignment before activation; that assignment constructs
and attaches DSPlayerController. It also changes player ownership, so it cannot
be used on the local player to make an independent puppet. Read-only resource
discovery now catalogs the separate HumanoidResource class and can search loaded
private memory for candidate instances. No new native creation attempt has run.

A later read-only live inventory found no loaded HumanoidResource in early
Episode 1: NPC humans (MULEs) use DSControlledEntityResource, whose component
list has no player-controller dependency but includes hostile AI. Details in
`mod/game_offsets.md`. No creation attempt has run since the quarantine.
Static follow-up: the crash came from DSPlayerState, a Humanoid sub-state. The
MULE resource creates a non-humanoid DSControlledEntity, so that path should not
run for it (untested). Component-removal, sleep, faction and invulnerability
exports are resolved as candidates for keeping such an NPC inert.

## The partner's body: the game's resident Sam double (2026-11-03)

The game keeps a second Sam in memory: `DSPlayerAKukanEntity`, the private-room body. It is parked asleep and
hidden at the world origin as a child of the porter's camera entity, and has no AI, controller or player state.
`mod/double.cpp` borrows it as the partner's body. It only takes the double while the game has it parked, and
it never creates or removes entities. All calls run on the game thread:
- the game's own SetSleeping, SetVisible and SetWorldTransform;
- its FixedPositionMover target (the double's only movement source while awake);
- the animation-variable exports;
- the manager's network-rebuild method.

Working live, unattended, in disposable copies with networking enforced:
- Two copies started from the same save each show the other player as their double. The two-copy regression
  measured the double within 1 cm of the real partner.
- The double walks, runs, crouches and jumps with the partner. It gets an instance of the porter's animation
  network while borrowed, because its own private-room network has almost no clips. Speed comes from the
  partner's movement and is smoothed. Sprint, crouch, jump, fall and landing travel in the presence flags and
  `action` bits and are set on the double's animation variables.
- Hand-back on release, on yield and when the game takes the double restores everything borrowed: the mover's
  resting target, the original animation network instance (the borrowed one is reused on the next borrow),
  the private-room floor effects (hidden and asleep while borrowed) and the parked flags and transform.
- A player in their private room (the game is using their own double) or in a game event or cutscene marks
  their presence accordingly, and the partner hides them. Locally, the double goes back to the game for the
  duration of an event.
- An unreadable pose for one tick never makes the mod forget a borrowed double; only a new or missing porter
  entity does.

The partner's backpack and cuff are copied onto the own body (opt-in, `partner_gear`); the hood is an art part and follows the dressing. A real
private-room entry is untested, because the test save carries a corpse bag and the game refuses entry with one.
Shared cargo works in a first version: cargo one porter places appears in the partner's world at the same spot. It
travels as reliable CARGO events, at most once even across reconnects; the core's fuzz tests run them through loss,
duplicates and an outage. The partner's mod creates the item with the game's own `CreateBaggage`.
Picking up (2026-11-07): a shared item has one id in both worlds. When a porter picks it up (the cargo record's
location byte goes from world to porter) or it vanishes near him, his mod sends "taken" and the partner's mod deletes
its copy with the game's `DeleteBaggage` (which takes the record's index). Putting it down again is a new placement.
Live with `coop-partner`: the partner picking up the game's crate removed it from the game, and the partner's own
crate, created in the game, was removed when the partner picked it up (`mod/game_offsets.md`, "Cargo").

Unattended testing: live tests drive the focused copy with synthetic keyboard input and capture background
copies with PrintWindow. The proxy's opt-in virtual gamepad (`COOP_VIRTUAL_PAD=1`, the mod's `virtual_pad`,
a stamp-guarded `pad.script`) works on Windows without the game, but the game never polls it. A connected
controller holds pad slot 0 through Windows.Gaming.Input, and the game only checks XInput for unassigned slots.

## Shared structures (2026-11-07)

A structure one porter builds appears in the partner's world as the game's own "Net" structure, the variant the
game uses for other players' structures. The mod reads the construction manager's table (one record per structure:
id, type, state, world transform; `mod/game_offsets.md`, "Structures"), sends each structure its porter builds, and
the partner's mod builds the Net variant at the same transform with the game's `CreateConstruction`. Net structures
have their own id range, so they are never sent back. Live with `coop-partner`, early Episode 1: a ladder built in
the game reached the partner, and the partner's ladder was built in the game as a Net ladder (read back from the
table, state "built"). Posts, shelters, watchtowers, bridges and ziplines requested the same way stayed pending
during that run and were standing after the next load; syncing them is untested. Dismantling is shared too: when a porter's structure disappears, the partner's mod removes its Net copy with the
game's `RemoveConstructionsInArea`, limited to Net structures in a 3 m box; both directions ran live. A partner's
structure that already stands here (saved from an earlier session) is not built twice. Not done: upgrades, and
structures built or dismantled through the game's own menus (the test save is early in Episode 1, before the
construction device; the test used the same exports).

Cargo driven live with keyboard input (2026-11-07, one copy and `coop-partner`): the porter carried the partner's
crate on his back (E), grabbed it by hand, put it down from the cargo menu and from his hand; each pick-up removed the
partner's copy and each put-down reached the partner at the porter's feet. Two bugs found and fixed that way: an
item put down from the menu keeps a carry-frame position for over a second (it was announced there), and story cargo
(the president's corpse, after a reload) was shared; only ordinary cargo is shared now. A return to the title screen
and continue while connected: the crate put down after the last save vanished with the reload and the partner was
told to remove it. A third: the cargo menu stages the items next to the porter exactly like a pick-up, so opening it
next to shared cargo made the partner's copies vanish and come back; a pick-up is now reported once the porter walks
on, and opening and closing the menu sends nothing (live). Then two real copies with the mod in both (no stand-in): the
porter of one carried a crate off, put it down from the menu (the other game created it at the same place to the
centimetre) and picked it up again (the other game removed it by its shared id). The other copy's idle porter sat
down and put the corpse down meanwhile; it stayed local. While a porter's cargo menu is open the game uses the
double for its preview, so the partner is hidden there and comes back when the menu closes.

Shared cargo that moves on its own (slides down a slope, floats down a river) is followed: once it settles more than
1 m from where the partner has it, the partner's copy goes and comes back at the new place. Walking into a crate
moved it about 1 cm live, so this is for physics, not for porters. Found by the world fuzz once items could slide: a
pick-up named the item's current place instead of the one the partner knows, and the search by item and place (used
after a restart, when ids are unknown) could take another shared item lying at the same spot. A pick-up without an id
now carries the sender's run tag, so the receiver prefers items from before that run. Known limit: identical items
stacked on one spot around a restart can still be matched the wrong way round (6 in 2000 fuzzed sessions with
restarts and sliding; none without restarts).

## The partner on the map, and pings (built and run live 2026-11-08)

Off by default (`mod/marker.h`, armed with the `puppet.marker` command). The partner is shown as one of the game's own
map pins, moved at most once a second and removed when he disconnects or either porter is in an event or private room;
a ping (V) puts a pin at the porter's position in both worlds through a reliable CARGO event of type 5, shown once per
ping. The pins are the game's user markers, found statically (`mod/game_offsets.md`, "Map markers"); they are saved with
the game, so the mod uses two groups of its own, never touches the player's pins and removes its own leftovers after a
reload. Tested game-free with a fake marker table and four mutants, and live with one copy and the stand-in partner
(pin follows the partner, a ping reaches both worlds, the pin goes when the partner leaves). Player-placed pins were
not part of that run. Default key bindings are not in the executable, so
whether V collides with a game key is unknown.

## Testing aids (built 2026-11-03, first run live 2026-11-07)

Live 2026-11-07, one copy, unattended: `auto_continue` reached gameplay from the title with no input (62 s cold,
8 s warm); `coop-partner` connected once it spoke the mod's wire identity, and the game drew it with the double.
Not yet measured live: the background frame cap with two copies (it held a lone background copy at its 2 fps, as
designed, so one-copy runs now turn it off). A driver installed since 2026-11-03 loads its main DLL through a
`\\?\` path; the proxy now checks such a request in its plain form.


- `COOP_BACKGROUND_FPS` (proxy): with two copies on one GPU, the copy that is not in front is held to a few frames
  per second so the one being played keeps its full frame rate. Measured before the cap existed: suspending the
  second copy roughly doubled the first copy's frame rate.
- The partner's double now stays borrowed, hidden and asleep through short gaps in the partner's packets instead of
  being handed back and taken again each time (`Hidden` stage in `mod/double.cpp`).
- `coop-partner`: a stand-in partner process that speaks the real protocol, follows the porter or replays a
  recorded walk, and can place cargo, so one copy can be tested at full speed. Tested game-free on Linux, including
  two partners against each other.
- `auto_continue` (mod): from the title screen, asks the game to continue the last save, with no menu input.

## Shared environment (static/game-free only)

The pinned build's time-of-day getter/setter and weather map reader have been
traced statically. A read-only provider and opt-in `share_environment:"host"`
time synchronization are implemented; default is off. The guest uses the native
time export only on the verified game callback, with instruction fingerprints,
fresh host samples and a 30 s call limit. No live environment check has run.
The native setter can advance its day counter for backwards corrections; effects
on quests/saves are unknown. Shared rain/timefall is not implemented: weather has
independent forecast/map state and its deterministic schedule and safe complete
setter contract remain unknown. See `mod/README.md` for the one-copy experiment.
