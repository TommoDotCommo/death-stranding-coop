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

## Not done

- The partner's own body in the default build. The partner is drawn with the game's resident Sam double (below),
  which the game takes back for the private room. Live on 2026-11-07, an experiment build created a second Sam
  with the script export `EntityResource_ExportedCreateEntity` from the double's own resource and drove it as the
  partner with the double backend. The mod now has an `own` mode that makes, parks and drives that body (resident
  double handed back) and holds the partner's exact position. With the porter's art parts applied to it, the body
  is drawn and walks, stable over 90 s, but with stretched-polygon artifacts still to fix (`mod/game_offsets.md`,
  "Drawing the own body"). `peer` stays the default.
- BTs, vehicles, cutscenes, death and voidouts, saves of a shared world,
  the partner's gear and outfit, and the partner's own controller input. Cargo the porter himself picks up or puts down is detected by the logic
  below but has not yet been driven live (the run had no keyboard focus).
- A real two-PC session. The mod now accepts any unicast IPv4 peer (LAN or VPN) and binds only the address that
  routes to it (README, "Two PCs"); it has only run over loopback so far. No NAT traversal: across the internet use
  a VPN such as Tailscale.

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

Not done: the partner has no backpack or hood (the porter's gear is about 30 attached entities). A real
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
structures built or dismantled through the game's own menus (the run had no keyboard focus; the test used the
same exports).

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
