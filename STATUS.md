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
| Modded copies stay off the publisher's service | The game's config and online-server connections are refused; it switches to its own offline mode. A private server on loopback or a listed partner address is reachable (`online_server`, below). |
| The partner's own body (`own` mode, 2026-11-08) | The mod makes a second Sam with the game's entity creator and dresses it with the game's own in-place art-parts swap (the call the porter's streaming control makes on the resident double), after waiting until the game has dressed the porter. Close-ups over 8 frames: a clean porter-suit Sam; the earlier apply-a-second-outfit dress showed two outfits overlapping. Two copies in own mode: each body within 5 mm of the other porter, visible 100 % of the time, no hang. `peer` (borrow the resident double) stays the default mode. |
| The partner shown without commands (2026-11-08) | `partner: "own"` starts own mode with no test command; the launcher now writes it with the game-thread hook and `partner_gear` (it wrote network-only `self_tick`, which drew nothing). Live, two copies configured that way: both made their body, drew the other porter, copied the gear and pinned the partner on the map (`partner_marker`); no fault recorded. A return to title and Continue made a new body with gear. |
| The partner's gear (opt-in, 2026-11-08) | `partner_gear` copies the porter's visible gear (backpack with its pouches, wrist cuff) onto the own body with the game's entity creator and attach call. Live, one copy and the stand-in: the backpack follows the body walking and running, hides with it, and a two-minute soak with park, return, removal and re-creation ran without a fault (crash recorder on). Two copies in `own` mode with gear on both passed `peer_free`. Each side sends what its porter shows (CARGO type 7) and the partner's body wears that set, and its outfit (art parts, CARGO type 8): live in two copies each body's parts then equalled the other porter's. Carried cargo is not drawn. |
| Story progress in the handshake | The episode is read from the game (read-only) and sent when the session starts (episode 1 in the test save); `progress_mismatch` warns (default) or refuses. |
| Join at host (opt-in) | `join_at_host` with `join_move: "native"` moves a guest that is over 300 m away next to the host once per session with the game's own player teleport (the world map's fast-travel path). Live, one copy and the stand-in host 600 m away: moved, read back 1.6 m from the target, the game set Sam on the terrain there and he stayed; no fault. |
| What the partner carries (opt-in) | `carry_visual` reads the porter's carried cargo read-only and sends a summary; the partner logs it. A cargo record names its slot, not its carrier, and the game uses records beyond the first 511: with that fixed (2026-11-09), the Episode 3 porter's stack read as 10 entries (backpack stacks and both waist slots) where it had read none. Nothing is drawn yet: the game draws each case as a mesh instance of a real cargo record (game_offsets.md). |
| The partner on the map, and pings (opt-in) | Live: a map pin follows the partner, a ping is sent and placed in both worlds, the pin is removed when the partner leaves. |
| Crash and hang recorder (opt-in) | `COOP_CRASH_RECORD=1`: module map at start, a record with module+offset stack for fatal-class exceptions, a minidump on an unhandled one. A research build's forced divide fault on a worker thread was recorded, dumped and recovered live. It found tonight's first bug: the game's crash dialog of 2026-11-07 was the proxy itself (two threads patching import tables at once), fixed with one lock (`redirect/iat_patch.h`). |
| Riding read from the game (2026-11-08) | Episode 3 save, a truck: a seated porter is a child entity of the vehicle; the reader checks the parent's class (bike, truck, speed car), its kind field and that the vehicle lists the porter. Presence carries what the porter rides (kind, offset, yaw/pitch/roll); two copies live: the other copy received the truck with its 15 degree pitch on a ramp. |
| The partner's vehicle (opt-in `partner_vehicle`, 2026-11-08) | While the partner rides, a vehicle of the same kind is drawn under him with the game's pooled vehicle creator (no nearby vehicle needed) and moved every tick by its own mover's transform playback; it stays parked where he gets off, is replaced when he rides another kind and removed when he leaves. Live, one copy: truck and bike drawn and following, removal clean; with the stand-in partner riding a bike, the bike appeared under it and stayed parked after it got off; no prompt to use it was offered; no fault. The partner's body sits on it in the game's riding pose: the body's animation controls are now bound against the network it actually runs, and the game's entry pulses (`Action_Rewind`, `Action_Rewind_NoInter`, `Is_Force_Upper_Reset`) are sent once on getting on; live with the stand-in on a bike it sat with hands on the handlebars and stood up again when it got off. The display vehicle has no game setup, so it gets an empty cargo carrier the mod owns and is marked occupied: without that, the porter's cargo and use checks near it faulted (three runs). An unattended one-copy regression (the stand-in rides a bike, gets off, leaves, then rides a truck beside the porter; classes, positions, the body's riding control and removal read from the process) passes with no fault record. Two real copies (2026-11-09): one player got into a truck in the garage and drove it up the exit ramp; the other copy drew the partner's truck where the real one stood and followed it up the ramp with its pitch, with the partner's body in the driver's seat (within a few cm of the seated porter) and the riding pose set; when he braked and got out, his truck stayed parked and he stood beside it. The rider's steering (`Turn_Heading`) travels too: steering right, the partner's body followed the driver's control from 0 to full lock (180). Its paint too: the partner's truck is drawn in the driver's colours (live: green, the same paint word). |
| Shared rain and timefall (opt-in `share_environment:"rain"`, or `"host"` with the hour too; 2026-11-09) | The host sends the game's rain condition where it stands with its time sample; a guest whose world disagrees there places the game's own timed local weather influence (rain or clear) around the host and renews it while the host stays wet or dry; it is owned by the mod, so the game does not save it. Live, one copy as guest and the stand-in host reporting heavy rain: the game showed its timefall tip, Sam put his hood up and the ground turned wet; when the host left, the rain lapsed by itself. The launcher uses `rain`, which never applies the host's hour (that setter can advance the day counter and shows no change of light); live, rain was shared and no hour applied. |
| The partner's carried cargo (opt-in `carry_visual`, 2026-11-09) | What the partner carries on the backpack arrives as the carry summary; the partner body now shows one case per carried item, packed in two columns by size on the copy of the backpack it wears, moving with its back. The cases are the game's own cargo case meshes: each item's case model and appearance as the game selects them for carried cargo (its definition looked up by the item id, the record's variant bit sent along), a plain case for an unknown item, and none of weapons, body bags, carts or machines drawn as cases; their collision is switched off; no cargo record, carrier or cargo table is made or touched, so nothing is saved or counted as anyone's cargo. Live, two copies on the Episode 3 save (nine cases on the backpack): each copy drew nine cases on the other's body, they followed it while it walked, and when one copy closed the other removed them; no fault. The game's own packing is not reproduced. The drawn cases first looked much lighter than the porter's own: tools and equipment travel in the game's packaged-weapon and construction-machine cases (dark), and the mod drew a plain delivery case for those types. It now uses the model the game selects for them too (2026-11-10; read live from the game's own case meshes: same footprint, at most four child parts); body bags and carts still fall back to a plain case. A carried record without an id of its own is named by its list item. |
| Driving at speed between two copies (2026-11-09) | One copy drove its truck 140 m out of the garage at up to 12 m/s (43 km/h); the other copy's partner truck followed it with a median gap of 1.35 m (about the 120 ms interpolation delay at that speed), at most 2.4 m, with no jump between samples, and came to rest within 1 cm of the real truck when it stopped. |
| The partner's name (opt-in `partner_tag`, on in the launcher, 2026-11-09) | The partner body shows the partner's nickname above its head in the game's own hologram-name label; the names travel between the mods on every connection. Live, two copies: each showed the other's name over the partner body and the label went when the other copy closed; no fault. |
| Likes for the partner (opt-in `partner_likes`, 2026-11-09) | Pressing L sends the partner a like; the partner's game gives its porter the like the game's own way (the like counter popup, saved like totals). Live, two copies: one press raised the other porter's likes by one with the stock popup. Off by default because the likes are real (saved, counted toward the porter's evaluation and trophies). |
| Session notices in the game's info log (opt-in `notices`, on in the launcher, 2026-11-10) | The game's own message feed tells the player what happens to the session: the partner joined, left, went into a cutscene or private room and came back, and, once, that nobody has arrived yet (with a hint to check the firewall). The mod adds a finished line to the info-log queue with the game's own add function, under the game's lock; no table item, nothing saved. A partner whose game is still loading is announced when he is in the world; with `partner_likes`, a like he sends is named with the game's like icon; a solo session started from the launcher shows none of this. Live, one copy and the stand-in: all five lines appeared in order and were read back from the queue in the game's memory; two copies: joined, left and joined again when the partner quit and restarted; no fault. |
| Return to the title screen and load again while connected (2026-11-10) | One copy opened the pause menu (scripted keys, no window focus), returned to the title screen and loaded its save again. The other copy hid the partner and said he was away, then back; the returning copy made its own body, gear and name label again in the new world; both showed the partner 3 to 5 mm from the other porter; no fault record in either process. |
| The real launcher's configuration (2026-11-10) | The launcher wrote each test copy's config (`--dev-direct --prepare-only`, host and client), and two copies started from exactly that (plus automatic loading of the save) reached the world, saw each other 3 to 5 mm apart and started every play feature by themselves: own body, gear, name, map pin, partner vehicle armed, carry summary, the joined notice; nothing refused, no fault. The Steam start and the lobby window are not part of this check. |
| The mod's cost per frame (2026-11-09) | A profiler logs, once a minute, every stage of the game-thread tick whose slowest run took over 1 ms. With two copies in the play setup the tick had cost about 105 ms (each memory read was two system calls, twice, and stages re-read the same chains); after caching region checks, reading aligned values once and reading each chain once per tick it costs about 5 ms on average in the same setup. The once-a-second read of the porter's gear (a few hundred scattered entities with an Episode 3 cargo stack) took 6-10 ms; with one copy per entity and a region cache that answers from any remembered region it takes about 2-3 ms. The signature checks that the cargo stack, likes and map markers run before their first native call (each reads the whole code section, about 200 ms) now run on a thread of their own instead of the game thread: their worst tick went from about 200 ms to under 4 ms. |
| Signed in to a server of our own (opt-in `online_server`, 2026-11-11) | The game never signs in by itself in a modded copy: its login task is only started by a title-menu message that does not arrive. With `online_server` the mod points the sign-in address at the players' own server (`world/`, `coop-world`), makes the gateway bodies plain JSON and asks the login task to sign in. Live, two copies with an Episode 3 save and the server on loopback: both signed in as two players and kept asking for the shared world (objects of the area, missions, highway resources, like history); no fault record. Served objects (2026-11-11): two cairns another player had built on the server were activated in the game's own list of online objects and stayed (read from the process; a cairn for another area never appeared). Uploads (2026-11-11): a ladder built with the game's upload flag was uploaded by the game, kept (the server's ids now start with the type letter, which the game requires), listed as activated and found by the other player on the server. Two copies (2026-11-12): a 30 m bridge one copy built with the upload flag was received by the other copy, which listed it as an activated online object and made a net bridge record at the builder's position; it stayed, and the builder was not handed its own bridge. Not working yet: the receiver's bridge is not drawn (its record stays in the pending construction state, which the game does not draw; the builder's now stands at once, 2026-11-12); a ladder between two copies (the crowded test save asks for none), a build through the game's own menus, and rain shelters (the receiving game makes them and takes them down again). The launcher has an opt-in "Shared world" setting: the host's launcher starts the server and both configs get its address; live, with the configs the real launcher wrote, two copies signed in as host and guest. |
| The partner is down or caught by BTs (with `notices`, 2026-11-12) | Each mod reads its porter's state read-only (the entity's dead bit and the animation controls `Is_Dead`, `Is_Drag`, `Int_Is_GazerCatch`, with the game's own predicate) and sends it in presence (flags Down and Caught, raised after 0.4 s of the state); the partner's info log says "... has been caught by BTs", "... got free", "... is down", "... is back on his feet". Live, one copy and the stand-in partner reporting those states: the four lines appeared in order in the game's message feed, and the copy's own reader followed the porter crouching and standing up (a state the test can cause) while reading alive and free. His body lies on its back where he fell while he is down and stands again afterwards (seen live with the stand-in; the body takes no death pose from the game: its `Is_Dead` control set by name, and the entity's own control request table, both left it standing, so the mod lays the body's transform down). Not shown: a real death or BT catch read from the game (no test reaches one yet); a caught partner's body does not change. |
| The partner calls out (with `notices`, 2026-11-12) | Holding the game's shout key shows in the porter's upper-body animation values (`Basic_Upper_Type` 10 with `Basic_Upper_Force_Type` 4); the mod reads them, raises the presence flag Call for 1.5 s per call, and the partner's info log says "... is calling out." (at most once every 8 s). Live: one copy read its own shout start and end, and the stand-in's call was told in order with the other notices. Nothing changes on the partner's body, and his voice is not heard. |
| A slow or lossy link (2026-11-12) | The delay at which the partner is shown follows the link: the mod measures how late the partner's samples arrive and shows him that much further behind (120 ms at the least, 500 ms at the most, changed at a tenth of real time so it does not show), and it keeps correcting its clock mapping from the quickest round trips. A stand-in link in the mod (`link`: `internet` = 2 % loss and 40..80 ms each way, `poor` = 12 % loss and 100..300 ms, on every packet) makes two copies on one PC behave like two homes. Live, both porters walking over `poor`: with the fixed delay the partner was up to 25 cm and 48 degrees off its own track; now at most 19 cm and 7 degrees (median 5 mm), shown 350 to 440 ms behind, and both still stand within 6 mm of each other at rest. Game-free: with latencies up to 150 ms and one packet in ten lost, the partner is drawn between two real samples in all but 0.4 % of frames. The cargo, name, like, body, gear and truck-drive regressions also pass over `poor` (two copies, 2026-11-12): the ordered streams resend what the link loses. With `notices`, each player is told once when the connection has been slow for five seconds (the partner shown 300 ms or more behind) and when it is steady again; live over `poor`, both copies showed it. |
| The launcher reads Windows Firewall (2026-11-12) | The lobby says in one sentence whether the game can receive the partner's data (allowed, blocked, no rule yet, off, unreadable), from Windows' own rule list, read-only; also in the diagnostics file and from `--firewall-status`. On the development PC it reported "no rule yet" for a copy with no inbound rule and "allowed" for a folder whose `ds.exe` has an allow rule. Rules that name no program are ignored: counting them had called every folder allowed. |
| Structures built with the game's own controls (2026-11-12) | One player picks the construction device on the game's tool wheel, aims and places a postbox; the partner's game shows it at once as a built Net postbox at the same place (two copies, Episode 3 save, read from both construction tables and seen on screen). Heavy structures the mod asks for (post, watchtower, generator, rain shelter, zipline, 30 m bridge) used to stay unbuilt until the next load: the game's request has a byte that makes it wait for the build effect its own menu plays, and the mod's request no longer sets it. Only what is built in the session is sent (decided by the build time the game stores in the record): before, every structure in a player's save was sent as new when the game brought it in after a load, 30 of them in the test save, and came back each time the player returned to it. Walking out of a structure's range and back is no longer taken for a dismantle and a new build (its record changes state, and is briefly taken away, while it stands). The safe house was refused by the game in this test; dismantling through the game's menu and upgrades are untested. |
| A MULE one player puts down is down for the partner (opt-in `share_enemies`, 2026-11-12) | Each game still simulates its own enemies; what is shared is who is down. Every MULE is a persistent actor placed by a locator whose UUID is the same in every player's game (live, two copies: the same 159 MULE and camp actors with the same UUIDs; positions up to 17 m apart). The mod watches the life state of its game's 150 MULEs (4 ms per look), sends each one that goes down with how (knocked, fainted, asleep, dead), and the partner's mod puts the same MULE down with the game's own defeat function. Live, two connected copies: one copy's nearest MULE was made to faint, the other copy's game put that MULE down in the same way within the next look and it lay on the ground there (screenshot); both games ran on. Camp alarms too: when a camp in one game comes to know someone is there (caution or alert), the same camp in the partner's game is told with the game's own function; live, eight camps one copy alarmed were on caution in the other within a second (the game makes it caution when nobody is near the camp, alert when someone is), and nothing was sent back. Not shared: where MULEs are, who they chase, a MULE that gets up again (each game wakes its own), cargo they carry, BTs. A real sighting was shared live (one copy's porter was spotted near a camp; the other copy's camp was put on alert). A MULE counts as down once two looks in a row show it (one was reported down for a moment right after connecting, untouched). Putting a MULE down from outside is not always taken by the game: live, an alerted MULE ignored "asleep" (also when the damage was attributed to the player) while "fainted" worked in both games; a MULE the partner reported down that still stands a second later is therefore made to faint. A real hit by the player is untested (the test command put the MULEs down). |
| BTs: kills, the boss fight and the cleared area are shared (with `share_enemies`, 2026-11-12) | Each game runs its own BTs. Live with two copies in one BT area: the same 74 BTs under the same identities in both games; the host's rain makes the area active for both; a BT destroyed in one game is destroyed in the other; a boss fight summoned in one game started in the other within seconds (tar, ruins, the partner standing in it), and when it ended in one with the area cleared, the other ended and showed the same cleared sphere. Not done: the fights are two separate bosses (no shared health), BT positions and who they notice are per game, a real grenade kill and a real capture were not played (the test destroys and summons through the mod's test commands). |
| A standard order one player takes on is the partner's too (opt-in `share_orders`, 2026-11-12) | The mod reads the game's order records, and when a standard order (the repeatable deliveries and collections) starts, is completed or is given up in one game it asks the partner's game for the same through the game's own mission requests. A partner who connects later is told what is under way. Main and sub orders are never shared: they move the story. Live, two connected copies (Episode 3 save): two orders started in one copy were under way in the other within a second and showed in its order display; one was then completed and one given up, and both games ended with the same states (success, failed). Each game makes its own cargo for the order, so the players do not yet carry one shared load: completing in one game completes in both, whoever delivers. Taking an order at a real terminal was then run live too (scripted keys through the game's own menus, two connected copies): one player took standard order no. 445 at the Lake Knot distribution centre, and when he headed out the partner's game had the same order under way; the partner saw "away" and "back" around the order cutscene. Handing in was run live as well: with the partner connected, the player carried the order's cargo to the Engineer's shelter, handed it in at the delivery terminal there (the game's own "Deliver Requested Cargo"), and the order was a success in both games. In that run the partner's game already had the order under way from its own earlier autosave, so what was shown is the completion reaching the partner; a mirrored order is saved by the partner's game like any other. Giving up was made with the mod's test command only. What each player is paid is unchecked, and each game still has its own cargo for the order. |
| A long jump with a partner connected no longer crashes (2026-11-12) | When the player was moved 1.4 km in one step with a partner connected, the game took the borrowed body back for an event of its own and the next animation value the mod set for it went to a control that no longer existed: the game read through a null pointer (fault at `ds.exe+0x1a52b0e`, called from the mod). The mod now sets a control only while its entry is in the body's running network. Live, the same jump with the partner connected: no fault, the game ran on. The game's own fast travel is expected to take the same path and is untested. |
| A split delivery (`share_order_units`, on in the launcher, 2026-11-12) | The game keeps a delivered state for each cargo unit of an order. With this setting, a unit that goes to delivered in one game is marked delivered in the partner's game and the partner's own copy of that cargo is removed, so that what each player hands in adds up and the last hand-in completes the order for both. Live, two connected copies with order no. 445 under way: one copy's unit 1 was marked delivered with the mod's test command; the other copy marked its unit 1 and removed one of its four cargo records; both games ran on. The game does count units marked this way (one copy, 2026-11-12): with two of the four units marked delivered and their cargo removed, its delivery screen showed those two as delivered and the two carried ones as deliverable, and handing the two in completed the order. A really delivered unit's row also holds a packed word and the cargo's two condition values; the mod now sends those with the unit so the partner's row is the same as after a real hand-in. A real partial hand-in between two connected copies followed (2026-11-12): one player, carrying two of the four units, chose the game's "Make Partial Delivery and Continue" at the Engineer's terminal; his order stayed open with those two units delivered, and the partner's game marked the same two units delivered with the same packed word and condition values, removed its own two copies of that cargo and kept the two still owed. The whole split ran between two connected copies the same day: one player took order no. 445 at the terminal and kept two of its units; the other picked the other two up from the floor by the terminal (the partner's game puts the order's cargo there), went to the Engineer and made a partial hand-in; the first player's delivery screen then showed those two units as delivered, with their weights, beside his own two as deliverable; he handed his two in and the order was a success in both games. The launcher's play setup turns this on. Not run: a unit both players carry at once (only cargo lying in the world is removed from the partner's game), and what each player is paid. |
| BT areas follow the shared rain (with `share_environment`, 2026-11-12) | BTs are tied to timefall: the game makes a BT area active while its weather map is wet there. The rain the mod already shares from the host is that same map, so a guest standing in a BT area gets the encounter when the host has timefall. Live, one copy as guest in a BT area in clear weather (its 74 BTs and the catcher territory loaded on approach, none active) and the stand-in host reporting heavy rain: within a minute the game's own "timefall here" switch was on, the area was in its list of active BT areas, ten BTs 20 to 45 m away had the active flag, and Sam's scanner came up (read from the process, and a screenshot). Each game still simulates its own BTs: where they are, who is noticed or caught, and a boss fight are not shared; a caught or downed partner is only told (the notices). |
| A guard for a game fault seen with a partner connected (2026-11-12, not confirmed live) | Twice in eight runs, with a partner connected and the player arriving at a delivery point with an order's cargo, the game hung: during its own arrival event its entity creation called a completion callback that had a target and no function (a worker thread jumped to address zero at the call in `ds.exe+0x22bdee0`, the main thread waited for it for ever). The same arrival without a partner was clean. The mod now answers exactly that fault (an execute fault at address zero returning to that one call site) by returning to the caller, and logs it. Why such a callback exists while the mod is in the game is not known, and the fault did not come back in the three runs made after the guard went in, so the guard has not been seen to act. |
| Release proxy name `avrt.dll` | The proxy also builds as `avrt.dll`, a DLL the game imports but does not ship, so an install never replaces a game file. Live in a test copy (game's own `d3dcompiler_47.dll` untouched): loaded at startup, the mod started, the game reached gameplay. Without the launcher's ticket or an explicit test environment the release proxy does nothing, so a game started from Steam runs untouched. |

## Not done

- Passengers; collision and wheel animation on the partner's vehicle (it is display-only: a porter walks through it;
  both depend on the vehicle physics the display vehicle skips); driving at speed between two copies (a drive up
  the garage ramp and a 140 m drive at up to 43 km/h ran live); the partner's carried cargo is drawn as cases (not
  the game's packing, or the cases on arms, waist and carriers), no cutscene sharing.
- BTs beyond the area becoming active with the shared rain (who is noticed or caught, the boss fight and its outcome); MULE positions, alarms and fights (who is down is shared, "Works"); a death pose for the partner (his body is laid down stiffly) and his capture shown on the body,
  voidouts, one shared load for an order (order states are shared, "Works"), saves of a shared world, the partner's own controller input.
- Structures: dismantling through the game's own menu, upgrades and materials, the safe house.
- A real two-PC session. The mod accepts any unicast IPv4 peer (LAN or VPN) and binds only the address that routes
  to it (README, "Two PCs"). On one PC (2026-11-12) two copies ran with the PC's own LAN address as each other's
  partner, the network policy enforcing with only that address allowed: both bound and addressed the LAN address and
  showed the partner within 5 mm, visible all the time. Two machines, a router and a firewall between them have not
  been tried. The launcher (`launcher/`, invite codes, LAN and
  Tailscale discovery, `deathstranding-coop://` links) is built and tested game-free only.

## QA level

Live regressions (2026-11-10), all driven without the window focus: both porters walking at the same time in two
copies (each copy's partner followed the other's own track with a median error of 5 mm and at most 9 cm over 15 to
18 m, and stood within 6 mm of the other porter at rest); the partner quitting and starting again (the body and name
went when he quit, both saw each other again 3 mm apart after the restart, no fault in any of the three processes).
`make test` runs its mutants side by side and takes under six minutes for everything.

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

Unattended testing: live tests drive a copy with scripted keyboard and mouse input that the proxy feeds to the
game as raw input on its own game thread (`COOP_VIRTUAL_PAD=1`, the mod's `virtual_pad`, a stamp-guarded
`pad.script`; `redirect/README.md`, "Scripted keyboard and mouse"), so the copy needs neither the window focus nor
the desktop's keyboard and mouse, and background copies are captured with PrintWindow. Live (2026-11-10): with
another window in the foreground the whole time, a scripted key walked the porter, a scripted mouse move turned the
camera, and he stood still when the script ended. The script's gamepad steps work on Windows without the game, but
the game never polls XInput: a connected controller holds pad slot 0 through Windows.Gaming.Input, and the game
only checks XInput for unassigned slots.

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
structures dismantled through the game's own menus. Building through them works since 2026-11-12 ("Works": heavy
structures stand at once, only the session's builds are sent).

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
a ping (B; V is the game's Melee) puts a pin at the porter's position in both worlds through a reliable CARGO event of type 5, shown once per
ping. The pins are the game's user markers, found statically (`mod/game_offsets.md`, "Map markers"); they are saved with
the game, so the mod uses two groups of its own, never touches the player's pins and removes its own leftovers after a
reload. Tested game-free with a fake marker table and four mutants, and live with one copy and the stand-in partner
(pin follows the partner, a ping reaches both worlds, the pin goes when the partner leaves). Player-placed pins were
not part of that run. The game deletes a user pin the player walks onto, so no partner pin is shown within 20 m of him.

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
time synchronization are implemented; default is off. Live 2026-11-09 (one copy as guest, the stand-in as host
sending 21.25 h): the hour was applied and read back (from 12), in Episode 1 and outdoors in Episode 3, with no visible
change in the light; the game reports no running cycle there, so sharing the hour has no visible effect yet. The guest uses the native
time export only on the verified game callback, with instruction fingerprints,
fresh host samples and a 30 s call limit.
The native setter can advance its day counter for backwards corrections; effects
on quests/saves are unknown. Rain is shared differently (see "Works", 2026-11-09): not by copying the forecast,
whose state is not deterministic from a seed, but by placing the game's own local rain or clear influence around the
host. See `mod/README.md` for the one-copy experiments.
