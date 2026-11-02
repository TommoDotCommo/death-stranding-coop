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

- Drawing the other player in the world (puppet). Static leads: `EntityResource_ExportedCreateEntity` is a real
  creation function; the SpawnSetup and buddy spawn exports are empty stubs in this build. See
  `mod/game_offsets.md`. The message pump and application callback chain are mapped statically; a safe call point
  on the game's thread remains unverified.
- Height-change snaps in the presence core (25-30 % of samples are near one; a puppet would jump on slopes).
- Shared cargo (picking up the same package works in both worlds today: no sync), structures, BTs, vehicles,
  cutscenes, death and voidouts, saves of a shared world.
- Networking beyond one PC (partners' addresses can already be allowed in the network policy; the mod itself still
  only talks to 127.0.0.1).
- A license. Until one is chosen, all rights are reserved by the author.

## QA level

Game-free: simulator, fuzzer and mutants for the core and every proxy protection (`make test`). Live: supervised
two-copy runs with state read from the processes; no automated in-game bot yet.
