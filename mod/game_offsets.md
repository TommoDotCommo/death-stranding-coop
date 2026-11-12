# Game structure discovery: static evidence for build 13300582

Only the private copy of the Director's Cut executable was inspected, read-only.
Its size is 86,479,872 bytes and its SHA-256 is
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
No game or install program was run, no live memory was read, and no game file was
modified. Everything below is static evidence, not live compatibility. All object
semantics remain **hypotheses** until the stated live observation passes.

RVA means relative virtual address from image base `0x140000000`. RVAs identify
this pinned build's evidence; they are not a patch-independent runtime API. A
future adapter must resolve reviewed signatures, validate every pointer and object
lifetime on a proven game thread, and fail closed on a changed hash, absent or
ambiguous signature, invalid target or implausible value.

## PE and name-reference method

Relevant PE fields are:

| Section | RVA | Virtual size | Raw file offset | Raw size |
|---|---:|---:|---:|---:|
| `.text` | `0x1000` | `0x3b389ac` | `0x400` | `0x3b38a00` |
| `.rdata` | `0x3b3a000` | `0xa252b0` | `0x3b38e00` | `0xa25400` |
| `.data` | `0x4560000` | `0x393d5e0` | `0x455e200` | `0xa05000` |
| `.pdata` | `0x7e9e000` | `0x1c88b8` | `0x4f63200` | `0x1c8a00` |

The exception directory contains 155,834 12-byte function entries. They were used
to attribute code references to function bounds, not to assume that every function
has a symbol. A separate string scan found 191 Microsoft RTTI descriptors, mostly
runtime/Havok types; the useful game names below are Decima reflection/export
strings in `.rdata`, not Microsoft C++ RTTI proof.

For the inventory, a direct cross-reference is an `objdump`-decoded `mov` or `lea`
instruction with a RIP-relative disp32 operand resolving exactly to the selected
standalone NUL-terminated string. Metadata-indirect uses are not included. Thus
zero means **no direct code reference found**, not that the type is unused. These
counts are not total call counts or proof of object layout.

### Relevant Decima names

Every entry below is in `.rdata`; notation is `name @ string RVA / direct refs`.

- Local player/human: `Player @ 0x3bb9c60 / 2`; `PlayerCharacter @ 0x3bbb700 / 1`;
  `PlayerGame @ 0x3d5bb58 / 1`; `DSPlayerEntity @ 0x3d760b0 / 1`;
  `DSPlayerSystem @ 0x3d6ca10 / 1`; `ControlledEntity @ 0x3ee8f58 / 1`;
  `HumanoidController @ 0x3eeb3a0 / 1`; `HumanoidMover @ 0x3f13438 / 1`;
  `NetPlayerManager @ 0x3bd05b8 / 1`.
- Character/skeleton/animation: `AnimationManager @ 0x3d08608 / 1`;
  `AnimationData @ 0x3baca30 / 1`; `SkeletonAnimationResource @ 0x3c116f8 / 1`;
  `RagdollComponent @ 0x3cff950 / 1`; `HumanoidInAirState @ 0x3eedf40 / 0`;
  `HumanoidMountState @ 0x3eef248 / 0`; `HumanoidSwimState @ 0x3eee920 / 0`.
- Transform/position/orientation/velocity: `Entity @ 0x3d033e8 / 49`;
  `WorldNode @ 0x3c23400 / 1`; `WorldPosition @ 0x3ba0d30 / 1`;
  `WorldTransform @ 0x3b8ef30 / 2`; `Position @ 0x3b8ee28 / 10`;
  `Velocity @ 0x3bb1f78 / 1`.
- World/region/streaming: `WorldData @ 0x3b98950 / 1`; `Scene @ 0x3d0787c / 2`;
  `SceneComponent @ 0x3d15bc8 / 1`; `SceneInstance @ 0x3d15280 / 1`;
  `StreamingTileResource @ 0x3c1ce90 / 1`;
  `StreamingTileStateResource @ 0x3c1dd68 / 1`.
- Cutscene/loading/menu: `Cutscene @ 0x3dfe4c0 / 0`;
  `InGameCutscene @ 0x3dfe558 / 0`; `HeavyCutscene @ 0x3dfe870 / 0`;
  `LoadingScreenMenu @ 0x3e62060 / 1`; `MovieMenu @ 0x3e614a8 / 1`;
  `MenuSaveDataDialog @ 0x3f192f8 / 1`; `PrivateRoom @ 0x3d67848 / 0`;
  `GameModule @ 0x3d5c730 / 1`.
- Cargo/vehicle/BB: `Vehicle @ 0x3bdd910 / 1`; `VehicleEntity @ 0x3e22e60 / 1`;
  `DSCargoStrandComponent @ 0x3dd3170 / 0`;
  `CargoStrandEntityResource @ 0x3dd74f0 / 0`; `BBPod @ 0x3d669d4 / 0`;
  `PlayerBBConfig @ 0x3d74958 / 0`; `DSDBBAliveHumanComponent @ 0x3de5cc8 / 0`;
  `DSBaggage2Manager @ 0x3dd2720 / 2`.

## Patch-oriented signature tool

Run from the repository root; this is the one-command verifier for the pinned build:

```sh
python3 -B mod/find_signatures.py "<game dir>/ds.exe"
```

The tool uses only the Python standard library. It reads and hashes the file,
refuses any other SHA-256, parses PE32+ sections with checked arithmetic, scans
only executable sections, and requires exactly one match per candidate. Patterns
are at most 32 bytes. Every relocatable branch/RIP displacement used by a pattern
is wildcarded. Each declared RIP reference must have ModRM mod=00, r/m=101 and a
fully masked disp32; its resolved target must lie in a non-executable PE section.
Zero matches, multiple matches, bad masks, invalid references and bad targets all
produce a nonzero exit and an explicit error. JSON reports the match count, match
RVA and resolved targets. No historical absolute address is a fallback.

Current results, all with `match_count: 1`:

| Candidate | Match RVA | Resolved RIP targets |
|---|---:|---|
| `DSPlayerEntity_sExportedGetLocalDSPlayerEntity` | `0x2757a30` | manager `0x7bc7568` (`.data`), type metadata `0x4b1acb8` (`.data`) |
| `Player_sExportedGetLocalPlayer_impl` | `0x23ca2fc` | manager `0x7bc7568` (`.data`), selection state `0x4b1acbc` (`.data`) |
| `WorldNode_orientation_matrix_body` | `0x1e04e80` | matrix constant `0x3b87f94` (`.rdata`) |
| `MenuSaveDataDialog_sExportedIsSaveDataLoadingOrRequested` | `0x3634fe0` | manager `0x7be6318` (`.data`) |
| `DSPlayerEntity_sExportedHasBaggage_body` | `0x26e8652` | related global `0x7be79b0` (`.data`) |
| `DSSoundNodeGraphBindings_sGetCutsceneSoundPlaying` | `0x2dd37e0` | sound global `0x7ae8700` (`.data`) |

The independent local-player paths resolving the same manager is useful static
corroboration, but not proof that either path is safe to call from a mod.

Ambiguity was measured, not post-filtered by a desired name. The first entity
position lock/copy window had 31 matches; a tighter window containing both
position loads still had 12. The shared WorldNode default-transform block had 3,
and the vehicle type-check sequence had 37. Those patterns were rejected. The
common reflection-registration prologue is also shared and is not used. Therefore
there is deliberately no accepted entity-position or vehicle-driving signature
in this milestone; their pinned RVAs below are analysis leads only.

## Object-layout hypotheses and exact live validation

“High” confidence describes the stated machine-level loads/copies in this build.
It does not imply high confidence in the semantic interpretation. No direct game
call or dereference is authorized by this table. Live work must first establish
the isolated runtime, safe game-thread hook, type/lifetime checks and read-only
observation.

| ID | Static observation and semantic hypothesis | Confidence | Exact live observation that confirms or refutes it |
|---|---|---|---|
| H1 | Both local-player implementations use manager global `0x7bc7568`; `DSPlayerEntity` performs a type lookup. **Hypothesis:** this is the local-player manager path. | High for the shared path; medium for lifetime/local semantics. | On the proven game thread, resolve both signatures independently and compare the manager in menu, gameplay, death and reload. Confirm only if both agree, gameplay yields a valid typed local object, and stale objects are rejected after transitions. Refute if they diverge, gameplay returns null/stale data, or the selected object follows a remote/NPC. |
| H2 | `Player_ExportedGetEntity` at `0x2361b80` returns pointer `Player+0x70`, or null for null Player; the DS wrapper also returns object `+0x70`. **Hypothesis:** this is the entity used for presence. | High for offset; medium for entity meaning. | Follow the validated local Player and compare `+0x70` with the DS getter result. While using only local input, evaluate the position lead on that entity. Confirm if both pointers agree and only the controlled character moves; refute on disagreement, unrelated motion or stale reuse after reload. |
| H3 | `Player_ExportedIsLocalHuman` requires byte `Player+0x78 != 0` and `+0x79 == 0`; the manager filter reads the same bytes. **Hypothesis:** these are local/human selection flags. | High for predicate; medium for flag meanings. | Record both bytes for the controlled human, another human/NPC and a remote player when available, including vehicle mounting. Confirm if the predicate selects exactly the local human in every case; refute if local becomes false or any non-local passes. Do not infer bit meanings beyond the observed predicate. |
| H4 | `Entity_ExportedGetPosition` at `0x2362af0` locks through entity `+0x288`, then copies 16 bytes from `+0xc8` and 8 from `+0xd8` (24 total). **Hypothesis:** position storage; scalar layout, units and axes are unknown. | High for widths/offsets; low for numeric interpretation. | With a validated entity, sample all 24 bytes while stationary and after a measured 10 m walk, a jump and a teleport. Confirm a position interpretation only if values stay finite, are stable while stationary, change with the controlled entity, and one consistent scalar decoding gives the measured displacement/scale. Test float64 and packed alternatives rather than assuming three doubles. Refute on NaN/Inf during valid play, unrelated motion or inconsistent scale. |
| H5 | `Entity_ExportedGetWorldTransform` wrapper `0x2362c30` reaches helper `0x234c210`, which returns 60 bytes from `+0xc8..+0xdf`, `+0xe0..+0xff`, and dword `+0x100`. **Hypothesis:** position plus orientation and a final region/flags word. | High for returned layout; low for final-word meaning. | Compare all 60 bytes with H4 and H6 during walking, rotation, teleport and a region boundary. Confirm transform semantics if position agrees, the middle values form a consistent orientation, and the final word changes only with a reproducible state/region transition. Otherwise reject the region/flags interpretation. |
| H6 | WorldNode position getter `0x1e0d260` copies 24 bytes at `+0x20`; transform getter `0x1e0d2c0` copies `+0x20..+0x5b`. Orientation helper `0x1e04e30` reads nine float32 values at `+0x38,+0x3c,+0x40,+0x44,+0x48,+0x4c,+0x50,+0x54,+0x58` and expands them into a 64-byte 4x4 matrix. **Hypothesis:** 3x3 orientation plus position. | High for widths/offsets; medium for matrix orientation. | On a validated WorldNode, yaw exactly 90 degrees, then pitch/roll and translate without rotating. Confirm if the nine values remain finite and orthonormal within measured tolerance, rotation changes only the 3x3 portion, and translation agrees with H4/H5. Determine row/column order and axes from the 90-degree cases; refute orthonormal rotation if those invariants fail. |
| H7 | Entity velocity implementation `0x234c180` returns 16 bytes through object `+0xb0`, virtual slot `+0xc8`; fallback uses `+0xb8` and virtual slot `+0x130`. Angular wrapper `0x23628b0` uses `+0xb0`, slot `+0xd8`, also returning 16 bytes. **Hypothesis:** linear/angular velocity vectors, possibly with padding. | High for call paths; medium for vector semantics. | After H4 establishes units and axes, sample position and both 16-byte results at a fixed game-thread cadence while stationary, walking and driving. Confirm linear velocity only if it is near zero at rest and matches finite differences of position within measured timing error; confirm angular velocity by a controlled 90-degree turn. Identify any padding by invariance. Refute on inconsistent scale, axes or unrelated changes. |
| H8 | `HumanoidController_ExportedGetMove` at `0x350fd60` reads float32 `+0x14c` and `+0x150`. **Hypothesis:** two local move/input components, not world velocity. | High for float loads; low-medium for normalization/meaning. | Record both values with neutral input, full deflection in four directions, walking and sprinting. Confirm a normalized move vector if neutral is zero, full deflection stays within [-1,1], directions are repeatable and magnitude responds to input rather than world speed. Any sustained out-of-range or speed-proportional result refutes normalization; H7, not this field, must establish world velocity. |
| H9 | `AnimationManager_ExportedGetFloatAnimationVariable` at `0x2396fe0` uses manager pointer `+0x58`, record stride 24, and float32 record `+0x20`. No bounds check is visible for caller index EDX. **Hypothesis:** animation-variable records. | High for addressing; low for safe indices/lifetime. | First recover the valid manager and index through the export's own caller/metadata. Then read a known variable through the normal path during idle, walk and ragdoll. Confirm only if the value stays finite and follows that named variable; refute if it is unrelated or index lifetime changes. Never probe arbitrary indices: a crash or out-of-bounds read is not validation. |
| H10 | `VehicleEntity_sExportedIsDriving` implementation `0x2fa7450` uses dword `+0x4cc`, dword `+0x63c`, pointer `+0x528`, type checks and a mode argument. **Hypothesis:** driving is a composite state, not one boolean. | High for composite predicate; medium for field meanings. | Capture all three fields and both mode results on foot, entering, seated stationary, moving and exiting. Confirm the export if its output matches actual driving in every state and mode; use the export rather than guessing a flag. Any mismatch refutes the field interpretation. A single field changing is insufficient confirmation. |
| H11 | Baggage body `0x26e8652` reads global `0x7be79b0`, gates on global-object dword `+0x14 <= 2`, then follows player(?) `+0x54d0`, pointer `+0x50`; later records compare dword `+0xc8` with a requested ID. **Hypothesis:** cargo/baggage lookup state. | High for accessed offsets; low-medium for owner and ID semantics. | With validated objects, observe adding, removing and rearranging one known cargo item. Confirm only if the relevant record appears/disappears with that item and `+0xc8` consistently equals its independently observed ID; refute on unrelated records or unstable IDs. Establish all pointer lifetimes before dereferencing. |
| H12 | BB affection path reaches `0x27a2f10`: return dword `+0x7844`, except byte `+0x7851 != 0` forces zero. **Hypothesis:** BB affection value plus suppression/reset state. | High for returned predicate; medium for affection semantics. | During validated BB gameplay, record both fields through soothing, crying and any affection gain/loss. Confirm if the dword tracks the independently displayed affection and the byte explains forced-zero intervals; refute if displayed affection moves without this value or suppression disagrees. Range and maximum remain unknown until measured. |
| H13 | Save-dialog getter `0x3634fe0` reads manager `+0x328` count and `+0x330` pointer array; an entry with dword `+0xc` in 6..15 yields true. **Hypothesis:** save-data loading/request state only. | High for predicate and narrow scope. | Trigger menu save, save completion, load and an unrelated level/streaming transition. Confirm the narrow predicate if it becomes true only for matching save-dialog operations and entry states match 6..15. If ordinary world loading never changes it, reject it as a general loading gate rather than treating that as failure of the narrow predicate. |
| H14 | Cutscene-sound getter `0x2dd37e0` reads global `0x7ae8700` and calls another function when non-null. **Hypothesis:** cutscene audio state, not visual cutscene state. | High for audio-specific call path; low for visual gating. | Compare it during gameplay, an audible cutscene, a muted cutscene and cutscene exit. Confirm only that it tracks the observed audio condition. If muted/visual states diverge, refute visual-cutscene use. Presence suppression needs a separate visual/loading observation. |

Rejected or insufficient leads:

- `GameModule_sExportedIsGameLoading` resolves to `0x1920dc0`, which is just
  `xor al, al; ret` in this build. It provides no working loading-state evidence.
- `LoadingScreenMenu`, `MovieMenu`, `PrivateRoom`, streaming resources and BB/cargo
  type names are inventory leads only; no safe state field was established.
- Entity position and vehicle driving need new, distinctive signatures before
  runtime use. Their layout observations above are not accepted scanner results.

## Live evidence (2026-11-01, launch 9, read-only discovery, one walk and one quarter turn)

The derived H1 fast path (`player = *(manager + 0x48 + 8*index)`, selection record at the `Player_sExportedGetLocalPlayer_impl`
selection target minus 4: index, mode 1, kind 2, null pointer) yielded a coherent player for 657 samples after the owner
pressed Continue; the manager global is null at the main menu. With the entity at `player + 0x70`:

| Field | Decoding | Evidence |
|---|---|---|
| H4 position, `entity + 0xc8` | 3 x float64 (x, y, z), z up | stationary to the millimetre for 30 s; a straight walk of about 38 units at about 4 units/s; stationary again. The float32 alternative is nonsense. Units: consistent with metres (Decima convention, walking pace); not yet measured against a known distance. |
| H5 rotation, `entity + 0xe0 .. + 0x103` | 3 x 3 float32, row-major; the 60-byte transform's final word is m22 (1.0 on flat ground) | unchanged while walking straight (about 172 degrees), a smooth rotation of about 81 degrees during the owner's quarter turn with the position unchanged; rows stay unit length and orthogonal to 0.001. |
| Forward vector | row 2: (m10, m11, m12) | walking heading from the position track is -97.3 degrees; row 2 points at -97.5 degrees. Row 1 is the right vector. |
| H3 local-human flags, `player + 0x78/+0x79` | `01 00` for the whole run | matches the game's own predicate for the local human. |

Still unconfirmed: lifetime across reload and death, behaviour in vehicles, the slow selection path, and a world/region id
(the transform has no extra word for it). H6 (WorldNode) is not needed for presence while H4/H5 hold.

## Unknowns and required next evidence

- Player/entity/manager type identity, ownership, generation, destruction and
  reload behavior; a safe game-thread callback and supported calling convention.
- Position scalar types, metres/other units, axes, origin, region/cell meaning,
  cross-region conversion and explicit teleport detection.
- Concrete velocity virtual targets, vector padding, angular representation and
  timing relative to the sampled transform.
- Skeleton/pose representation, safe animation IDs/index bounds, action mapping,
  ragdoll handling and remote actor/puppet creation. No animation write is safe yet.
- A general loading/streaming/menu/private-room/visual-cutscene gate. Save loading
  and cutscene sound are not substitutes.
- Vehicle/cargo/BB field meanings and lifetimes, and any presence flag mapping.
- Signature survival on another patch. Uniqueness and RIP validation are proven
  only for the pinned bytes; no other build may be scanned as if compatible.
- All live behavior. `GameProvider` must remain unavailable until these gates pass.

## Commands and timings

Commands were run from the repository root unless noted. Working disassembly and
JSON dumps are confined to ignored `mod/build/`; they are not repository evidence
files. Focused disassembly used `objdump -d -M intel --start-address=<VA>
--stop-address=<VA> "<game dir>/ds.exe"` over the functions
above (0.72-0.95 s per small range).

| Command | Result | Wall time |
|---|---|---:|
| `sha256sum "<game dir>/ds.exe"` | matched profile hash | not measured |
| `objdump -h "<game dir>/ds.exe"` | section table above | 0.07 s |
| `python3 mod/build/analyze_rtti.py "<game dir>/ds.exe" mod/build/rtti` | 191 Microsoft RTTI descriptors | 11.17 s |
| `python3 mod/build/analyze_decima.py "<game dir>/ds.exe" mod/build/rtti` | keyword/name-pointer leads; later exact pass superseded noisy counts | 9.30 s |
| `python3 mod/build/exact_name_refs.py "<game dir>/ds.exe" mod/build/rtti/exact_name_refs.json` | exact standalone-name/direct-RIP inventory | 14.34 s |
| `python3 -B mod/find_signatures.py "<game dir>/ds.exe"` | six unique candidates | 13.62 s first pass; 13.34 s final pass |
| `python3 -B -m unittest ./tests/test_find_signatures.py -v` | 9 synthetic host tests passed | 0.06 s |
| `python3 -B -m unittest discover -s ./tests -v` | 71 game-free tests passed | 104.80 s |
| `make -C ./mod test all` | adapter tests, safety mutants and Windows cross-build passed | 30.90 s |
| `make -C ./core test` | core fuzz and all 28 mutants passed; includes mod tests | 23.80 s |

The scanner's rejected runs failed loudly as intended: 31 and then 12 entity-position
matches, 3 transform-default matches and 37 vehicle type-check matches. They were
analysis evidence, not successful verifier runs.

## Puppet leads: script-binding exports resolved statically (2026-11-02, nothing called)

Method: each exported name has one RIP-relative `lea r8,[name]` in a registration function; 0x17 bytes earlier
`lea rax,[impl]` loads the implementation, and `lea rdx,[desc]` 0xa bytes later loads the short name. Calibrated on two
addresses known from earlier analysis (`Entity_ExportedGetPosition` 0x2362af0, `Player_ExportedGetEntity` 0x2361b80):
both match. Tool: a 40-line scanner over the pinned exe (kept in session scratch, not committed yet). RVAs are for build
13300582 only and are leads, not accepted signatures (`find_signatures.py` has not been extended).

| Export | Impl RVA | Body (first instructions) | Reading |
|---|---:|---|---|
| `SpawnSetup_ExportedCreateEntityFromSpawnSetup` | 0x18f5a20 | `xor eax,eax; ret` | **stub in retail**: always null. Dead end. |
| `BuddyManager_sExportedRequestSpawnBuddy` | 0x18e9b10 | `ret` | stub. |
| `EntityResource_ExportedCreateEntity` | 0x23806a0 | 4 register args + byte + 2 pointers + pointer to a 16-byte tagged value, forwards to 0x2374400 | **real**. Puppet candidate: create an entity from a character resource. Argument meanings UNKNOWN. |
| `EntityResource_ExportedCreateEntityWithParent` | 0x2380630 | real body | candidate. |
| `EntityResource_ExportedCreateDamageEntity` | 0x23805b0 | real body | not for puppets. |
| `Entity_ExportedSetWorldTransform` | 0x2361ea0 | null check on rcx; calls 0x22dff80 (rcx=entity, rdx passed through) then 0x235bf60(entity, 0) | drive the puppet from the received pose. Transform layout presumably the 60-byte H5 block; UNCONFIRMED. |
| `Entity_ExportedPlaceOnWorldTransform` | 0x2361c00 | real | alternative placement. |
| `Entity_ExportedSetVisible` | 0x2361e90 | null check, tail jump 0x235f640 | hide while stale. |
| `Entity_ExportedRemove` | 0x2361c20 | `jmp 0x234caa0` | despawn on disconnect. |
| `Entity_ExportedSetSleeping` / `SetInvulnerable` | 0x2361e20 / 0x2361d90 | real | keep the puppet out of physics/damage. |
| `DsNpcGeneralCommand_sExportedTeleportNpc` | 0x2bf7930 | real | NPC teleport. |
| `Spawnpoint_ExportedSpawn` / `MultiSpawnpoint_ExportedSpawn` | 0x23da6c0 / 0x23da650 | real, need a placed spawnpoint object | possible but needs a spawnpoint instance. |

Open before any call: which EntityResource to use (the local player's entity probably references its own resource;
cloning Sam may bring player-controller components; an NPC porter resource would be safer), the eight CreateEntity
arguments (read the game's own callers of 0x2374400), entity lifetime/ownership, and a game-thread hook to call from.

### The creator behind `EntityResource_ExportedCreateEntity` (0x2374400), read statically

Hypothesis from the body: `CreateEntity(EntityResource* res /*rcx; null -> null*/, const double pos[3] /*rdx*/,
const float rot[3][4] /*r8: 3 rows, 16-byte stride, xyz used*/, name_or_id /*r9: converted, stored at entity+0x50*/,
bool place /*if set: 0x235a010(entity,&transform,1,0)*/, instigator /*optional: 0x235df30(entity,a)*/,
attack_event /*optional: 0x235d5f0(entity,b)*/, tagged_value /*16 bytes by pointer; 0x23c8130(value,entity)*/)`.
It builds a 60-byte transform (3 x f64 position + 3x3 f32 rotation, the live H5 layout), calls `0x2378cf0` on it,
creates through `res->vtable[0x80/8](res,&transform)`, then adds the entity to the world through the global at
0x7bc4e70 (`vtable[3](mgr, entity, 0, 0, 0)`). The game's own code does not call 0x2374400 except through the export.

`Entity_ExportedGetResource` (0x2362ba0): null check, then `0x1dde990(entity+0x58)`: the entity's resource reference
lives at +0x58. Candidate puppet chain, all through the game's own functions: local entity (live, `player+0x70`) ->
GetResource -> CreateEntity(resource, pose) -> SetWorldTransform per received pose -> SetVisible/Remove.
Risks: Sam's resource may carry player-only components (camera, input, BB, cargo); an NPC porter resource avoids that
but must be found. `PlayerGame_ExportedSwitchPlayerCharacter` (0x2642cf0) suggests character resources are swappable.


### Message pump and creator arguments: next static pass (2026-11-02)

`python3 -B mod/find_message_calls.py "<game dir>/ds.exe"` is a reproducible,
read-only inventory for the same pinned SHA-256. It parses the ordinary USER32
imports and AMD64 exception directory, scans executable sections for RIP-relative
indirect call/jump candidates, follows direct rel32 callers of jump stubs, and
reports LEA references to containing function starts. It reports **byte candidates**,
not decoded instructions; register-indirect uses and delay imports are outside its
inventory. Function bounds are exception-directory ranges, not complete semantic
functions. Synthetic tests cover bounds, signed displacements, data decoys,
import thunks, malformed images, missing imports and the hash refusal.

Focused `objdump` disassembly from the exception-directory start independently
confirmed the following instruction boundaries and targets on the pinned file:

| API | IAT RVA | Call RVA | Containing range (end exclusive) |
|---|---:|---:|---|
| PeekMessageA | 0x3b3a7b8 | 0x1a19381 | 0x1a19350–0x1a193df |
| GetMessageA | 0x3b3a7c0 | 0x1a19398 | 0x1a19350–0x1a193df |

The pump peeks without removing (fifth argument 0), then gets, translates and
dispatches at most 100 messages per invocation. A zero GetMessage result returns
false; a negative result is not specially handled by this game function. No
thread identity or world-mutation phase follows from this code alone.

There are no E8/E9 byte candidates targeting the pump start. Instead, LEA at
0x1a16cae in constructor range 0x1a16c20–0x1a17050 resolves to the pump and the
following store puts it at application object +0xc0. The object's vtable is
0x3ba6488. The routine at 0x1a17a90 reads +0xc0 and invokes it through RAX at
0x1a17aea. Its pointer occurs at 0x3ba6660 (vtable offset +0x1d8); a global
counter at 0x577d68c guards reentrancy around this invocation. These are static
links, **not proof of a safe game-thread callback**. That counter also means a
hook must not assume message dispatch is a unique frame boundary. Thread ID,
outer caller stack, cadence in gameplay/loading, and overlap with world jobs
remain unknown. Do not drive an entity creator from either message API yet.

The creator's arguments 6 and 7 now have stronger static names:

| Argument | Evidence | Interpretation |
|---|---|---|
| 6 | Creator passes it to 0x235df30. Registered `Entity_ExportedSetInstigator` resolves to 0x2361d30 and tail-jumps to the same routine after checking existing fields. | Instigator, not an arbitrary attachment pointer. Exact pointee layout/lifetime unknown. |
| 7 | Creator passes it to 0x235d5f0. Registered `Entity_ExportedSetAttackEvent` resolves to 0x2361c70 and tail-jumps there. | Attack event, not a parent pointer. Exact pointee layout/lifetime unknown. |
| 8 | Export copies 16 bytes; helper 0x23c8130 reads signed tag byte +0, switches on tags 1/2/3, and loads a pointer at +8 in tags 1/2. Other tags return without those actions. | A tagged value; **the earlier UUID hypothesis is rejected**. Zero tag performs no branch action in this helper, but a safe complete spawn contract is not proven. |

The helper unconditionally reads the tag through argument 8: null is not an
acceptable default pointer. Its type name and tags' complete semantics remain
unknown. A raw zero-initialized 16-byte value would exercise only the observed
no-action branch; that does not establish a valid resource/lifetime contract.
No creator or transform setter was called in this pass. Whether Sam's resource
creates camera, input, BB or cargo components is still unknown.

Verification for this pass: pinned-file scanner and focused disassembly ran;
10 synthetic scanner tests and the full repository `make test` passed (including
28 core and 33 pose mutants plus the proxy protection oracles). No game launched,
no live thread identity observed, and no actor created.


### Message-thread observation: menu, loading, movement and pause (2026-11-02)

The opt-in proxy observer (`redirect/thread_trace.h`, `COOP_THREAD_TRACE=1`)
was first exercised by a Windows fixture probe: two threads, nested dispatch,
message/flag and LastError preservation, GetMessage positive/zero/negative
results, disabled mode, invalid configuration refusal, enforced-network
requirement and process-creation-time refusal. Four recorder mutants and six
reader tests passed their oracles. The repository's full `make test` and Windows
cross-build passed before the game observation.

A supervised single disposable instance of the pinned build then produced 906
published records (entry/return pairs), zero dropped pairs, and counters of
45,326 PeekMessageA and 9,947 GetMessageA calls. The only observed thread ID was
9316. Independent pose sampling produced 844 samples, 422 with a local player;
position spans were about 42.95/15.40/11.23 metres in adapter axes, peak sampled
speed 5.32 m/s, and the final paused sample had speed zero. The owner identified
the menu and completed the requested load/walk/pause sequence. A valid pose alone
does not establish a pause or visual-cutscene gate.

The common captured return-address chain was:

`0x1a19387` (Peek) or `0x1a1939e` (Get) -> `0x1a17aec` ->
`0x263d2ae` -> `0x263a869` -> `0x263753d` -> `0x18e9e88` ->
`0x18ea224` -> `0x18ea0a6` -> `0x3b0405e`.

Focused disassembly confirms the caller at 0x263d2a8 dispatches application
vtable slot +0x1d8 through the application global at 0x577d3f0. Routine
0x2637510 loops over calls to 0x263a5d0 and 0x263b860. The former calls
0x263cfe0 at 0x263a864 (return 0x263a869); after that return it iterates
subsystems via vtable +0x8 at 0x263a8a8, calls 0x263ec30, then iterates via
vtable +0x10 at 0x263a8f6. Those subsystems' types and their job synchronization
contracts remain unknown. Do not infer a completed world-job barrier here.

Earlier startup/loading samples also had paths through 0x263094f or
0x1a9862e/0x2630985, then 0x262c353/0x18e9e6c. Thus the message APIs are not
exclusive to the steady main-loop phase. Every sampled live depth was 1, while
the synthetic Windows probe demonstrated depth >1: absence in this sample is
not proof that the game cannot reenter dispatch.

**What this establishes:** a concrete, live-observed game-thread candidate and
its common main-loop caller. It does not establish entity-creation safety,
resource ownership, a visual cutscene gate, or a world-mutation phase. No entity
creator or transform setter was called. A future experiment should restrict
itself to the reviewed caller path and game-thread identity, reject nested or
unavailable-world states, and validate the complete creation/lifetime contract.

Additional static caution: the resource helper at 0x1dde990 (used by
`Entity_ExportedGetResource`) follows a descriptor and can execute reference-count
operations (`lock xadd` at 0x1dde9cf) on one branch. Treat it as an engine call
with side effects, not as a substitute for a guarded read from a worker thread.


The run was stopped by recorded process identity. Both copies passed post-run
source/copy verification; original saves were unchanged immediately and after a
30-second settle check. Both copies received matching source-built proxy and mod
DLLs and fresh isolation/module/network probes before the run. The observer is
opt-in and changes no mod wire identity. Windows scope and enforced-network gate
mutants were also rejected by the fixture probe. These checks do not turn the
candidate into a verified entity-mutation API.


### First guarded creator experiment: missing player controller (2026-11-02)

An opt-in dispatcher successfully entered the mod on the observed startup/main
thread, after an empty PeekMessage return, with the independently checked return
chain `1a19387 -> 1a17aec -> 263d2ae -> 263a869 -> 263753d`.
This verifies callback delivery, not a world-job barrier or mutation contract.

One supervised native creator invocation using the loaded local Sam's resource
crashed before returning an actor. No transform setter or removal call ran.
The owner-provided crash stack contains `23806eb -> 237451c -> 23500e4 ->
237c5c3 -> 271fa8e -> 373a94d -> 351b5c5 -> 27e6510 -> 26b8b00`.
The faulting instruction writes through RCX, which was zero.

Static inspection of the pinned executable explains that null: component init
at 27e63c0 looks up metadata 4bb2770 through helper 22b2a60 at 27e6476.
That metadata names DSPlayerController. The returned component is retained in
R14 and at component+418; at 27e650b it is passed as RCX to 26b8b00 without
a null check. That callee starts by writing RCX+5370. This matches the observed
stack and null register. The generic resource path did not supply this required
controller. Whether other player-only parts (camera/input/BB/cargo) can be
safely cloned remains unknown.

Creator helper 2374400 calls activation 234f3e0 before return site 237451c;
its placement-flag check follows that activation. Changing placement or the
optional tagged value does not address this failure. Do not bypass the null
write or skip the initializer: subsequent calls need the same controller.
The native local-Sam backend now refuses resource approval before calling the
creator, emits `resource_rejected`, and has no config override. A missing
approval callback also refuses. A mutant proves that removing this gate lets
an unapproved fake resource reach creation. Next research needs a standalone
NPC resource or the supported player-controller construction context.

Additional ABI verification: Remove at 2361c20 tail-jumps to 234caa0, whose
second argument (DL) selects immediate versus queued removal. Pass false for
the reference-owning queued task; typed host and Windows stubs test this.
The creator's fourth argument is a non-null C string, not a string object.
Its rotation occupies three 16-byte rows; the transform setter reads 60 bytes.

The failed test copy was stopped by process identity. Original saves were
unchanged immediately and after a 30-second settle check; both copies passed
post-run source verification (archive contents were not rehashed). These checks
do not make the failed creator call safe.


### Player context supplies the missing controller before activation

Static follow-up on the same pinned executable (no new engine calls):

| RVA | Observed behavior |
|---|---|
| 2638880 | Player-specific entity assignment. When player+78 is true and +79 false, constructs DSPlayerController then attaches it to the target entity. |
| 26388e2 / 26388e9 | Loads DSPlayerController metadata 4bb2770, calls generic component construction 22b4fc0 with R8=0 and a generated 16-byte identifier in R9. RDX comes from global 4e14178; its exact type is unknown. |
| 26388ee–26388f4 | Passes the returned controller as RDX, target entity as RCX, to attachment thunk 2348210 -> 2348220. |
| 26388ff | Calls base player assignment 23c7e50, which replaces player+70 at 23c7f21. Calling this with the local player would change its controlled entity. |
| 37447e0 | Player/context creation helper: calls 2380920 to allocate/initialize a 0x1f0-byte creation context, writes player pointer at context+f8, related entity pointer at +98, and creation mode at +94. Calls 237dc10, then 2375ba0 to obtain the result. |
| 2373720 | Context-driven creation: resource virtual+80 call at 23737aa, owner callback virtual+80 at 2373877 using context+f8, then component activation 234f3e0 at 23738b8, then world-manager virtual+18 at 2373905. |

This independently explains the ordering absent from generic export 2374400:
the owner callback can attach the required controller **before** activation.
`PlayerGame_ExportedSwitchPlayerCharacter` 2642cf0 jumps to 262a030, which queues
a task carrying callback 26398c0. That callback invokes 37447e0 on its player,
then performs another player assignment. These are player replacement paths,
not a proven independent-puppet API. Do not call them with the live local player.
An isolated player context would need construction/registration, input/camera
exclusion and cleanup contracts; those remain unknown.

The metadata catalog (`find_actor_types.py`) finds these concrete resource leads:

| Type | Metadata RVA | Getter RVA | Vtable candidate RVA | Factory slot +80 RVA |
|---|---:|---:|---:|---:|
| HumanoidResource | 4ec16b0 | 3737880 | 3f3bb98 | 2366570 |
| DSPlayerEntityResource | 4bc92e0 | 2711b90 | 3d7d010 | 2366570 |

DSPlayerEntityResource derives from HumanoidResource, which derives from
ControlledEntityResource. Both factory slots jump to 2373940, which asks virtual
+68 for the entity type, allocates through 1a8eee0 and copies the transform.
Virtual +68 on DSPlayerEntityResource resolves through getter 270fd70 and
pointer slot 4bb59c8 to DSPlayerEntity metadata 4bc6e00. On HumanoidResource,
getter 3737030 reads slot 4ebf408, whose static value is Humanoid metadata
4ec1530. Thus the shared factory can allocate different entity classes.
These are static initialized pointers; the read-only catalog labels the result
`static_entity_type` rather than claiming a runtime class/lifetime observation.
Sharing a factory does not mean sharing a spawn contract. HumanoidResource is
a possible NPC lead, not an approved standalone resource; no live instance has
yet been identified by the new collector. Its bounded read-only heap inventory
is available as `read_actor_resources.py`; no production DLL or build identity
changed in this follow-up.

### Live resource inventory: NPCs are not HumanoidResources (2026-11-02, read-only)

One supervised run of one copy, early Episode 1, with only query/VM_READ handles:
no injection, no engine call, no creation. The whole private address space
(about 10.4 GB) was scanned to exhaustion more than once, including while the
player carried the episode's corpse.

- `read_actor_resources.py` found only the player's resource
  (`ds/entities/player/ds_player_character`, DSPlayerEntityResource). No plain
  HumanoidResource was loaded at any point.
- A wider scan over every vtable whose slot 0 is a reviewed type getter with
  ControlledEntity or ControlledEntityResource ancestry found no Humanoid entity
  other than DSPlayerEntity. The loaded NPC resources are
  **DSControlledEntityResource** (`ds/entities/npc/mule/pe_mule`,
  `.../pe_demens`, catchers, gazers, animals) and DSDeliveryBotEntityResource,
  plus many cinematics resources.
- Resource layout matches the player's: +30 path string, +48
  SphereBasedActivationPolicy (NPC), +98 Decima array (count/capacity, pointer)
  of 40 component resources, +140 DSNpcImpactEffectResource, +148
  DSGroundMoveEntityUpdaterResource. The MULE component list has no
  DSPlayerPadOperator, Camera, DSPlayerInventory or DSPlayerLife component, which
  the player's list has. It does have AI, combat, patrol, weapon and Odradek
  components (DSAIIndividual, AIHelper, DSPatrol, DSNpcCombatSettings,
  DSMuleWeapon, DSMuleOdradek); demens adds DSDemensComponentResource.
- A bounded (4 GiB, incomplete) reverse-pointer scan attributed no live
  DSControlledEntity to the MULE resources: loaded, apparently not instanced.

So the HumanoidResource lead does not reach NPCs in this build. A MULE-family
resource is a loaded standalone NPC candidate without the player-controller
dependency, but it carries hostile AI. Whether its activation needs other
context, and how to keep its AI inert, are unknown. The creation quarantine is
unchanged and nothing here approves a spawn.

### MULE resource creation path, read statically (2026-11-02, nothing called)

All five ControlledEntityResource classes share factory slot +80 (2366570 ->
2373940). They differ in the entity-type slot +68, which reads a static class
pointer:

| Resource | +68 getter | Entity class created | +70 / +78 |
|---|---:|---|---|
| DSControlledEntityResource (MULE, demens, cinematics) | 2da4880 -> 4c5d438 | DSControlledEntity | DSControlledEntityRep / NetControlledEntity |
| HumanoidResource | 3737030 -> 4ebf408 | Humanoid | HumanoidRep / NetHumanoid |
| DSPlayerEntityResource | 270fd70 -> 4bb59c8 | DSPlayerEntity | |

The failed Sam creation faulted in function 27e63c0, which is DSPlayerState
vtable 3d82348 slot +2c8. DSPlayerState (metadata 4bcf7d0) is a
**HumanoidSubState**, built from the player resource's +2e0 array of
DSPlayerStateResource. Its `FindComponentByType` lookup (22b2a60) for
DSPlayerController (a HumanoidController component, metadata 4bb2770) returned
null. The MULE resource object ends before +2e0 and creates a DSControlledEntity,
which is not a Humanoid. So this crash path has no static reason to run for a
MULE resource. This is untested, and other activation requirements are unknown.

Exports for keeping a created NPC quiet, resolved with the method above
(calibrated on GetPosition 2362af0, Player GetEntity 2361b80):

| Export | Impl RVA | Reading |
|---|---:|---|
| `Entity_ExportedFindComponentByType(entity, rtti)` | 2361990 | null checks; `22b2a60(entity+90, rtti)` |
| `Entity_ExportedFindComponentsByType` | 23619b0 | |
| `EntityComponent_ExportedRemove(component)` | 23618c0 | `235b930(component+48 /*owner*/, component)` |
| `Entity_ExportedSetSleeping(entity, bool, byte)` | 2361e20 | sleeping flag is bit 8 of entity+88; vtable +a0 sleep / +a8 wake |
| `Entity_ExportedSetFaction` / `SetDefaultFaction` | 2361d00 / 2361c90 | |
| `Entity_ExportedSetInvulnerable` / `SetVisible` | 2361d90 / 2361e90 | |
| `AIIndividualComponent_ExportedGetAIIndividual` | 241dd20 | returns component+50 |
| `AIIndividual_ExportedIncrementIgnoreOrderCount` | 2562e10 | |
| `DSNpcCombatSettings_ExportedSetCombatSettings` | 2b9e550 | |

Component class metadata for the MULE's AI-related parts: DSAIIndividualComponent
4c289a0, AIHelperComponent 4af5990, DSPatrolComponent 4c1d300, DSMuleComponent
4c116f0, DSMuleWeaponComponent 4c12540, DSNpcCombatSettingsComponent 4c1c6b0,
DSMuleOdradekComponent 4c15b60, DSNoticeAndKnowledgeReceiveComponent 4c23180,
DSGroundMovementComponent 4c60400, DSDemensComponent 4c1abb0.

Candidate plan for a later supervised experiment (not authorized; the creation
quarantine still refuses every native resource): create from a loaded pe_mule
resource far from the player, then immediately make it invulnerable, neutral and
quiet (find and remove the AI, patrol, weapon and Odradek components, or sleep
it), and drive it with SetWorldTransform. Unknowns: whether removing components
after activation is safe, whether sleeping stops animation as well as AI, what
the NPC looks like as a stand-in for the partner, and the entity lifetime under
world streaming.

## The resident Sam double: DSPlayerAKukanEntity (2026-11-02, read-only)

The pinned build ships two Sam doubles that are plain `Entity` subclasses (not
`DSControlledEntity` or `HumanoidEntity`), built by the shared entity factory
2366570:

| Class | Resource vtable | Entity vtable | Notes |
|---|---:|---:|---|
| `DSPlayerAKukanEntity` | 3d7dcf0 | 3d7de48 (type getter 2711a80) | resource `ds/entities/player/akukan/ds_player_akukan_entity`; depends on the Sam model, Sam's morpheme network, facial rig, voice and the Odradek |
| `DSPlayerMotionEntity` | 3d7d490 | 3d7d5e8 (ctor 26f4f00) | used by one mission scene only; not loaded in the runs below |

"AKukan" is read here as 亜空間 (subspace), so the double is probably Sam's
private-room body. That is a guess and unverified. Game code at 2922cc0 treats `DSPlayerEntity` and
`DSPlayerAKukanEntity` alike when fetching `DSBaggage2CarrierComponent`.

Live, read-only, with the porter in open world in Episode 1 (two runs):

- One live `DSPlayerAKukanEntity` exists. It has 15 components: AKukanConfig, AKukanInventory,
  Baggage2Carrier, Voice, CustomSound, MorphemeAnimationManager, DSTalk,
  FacialRigManager, FacialRig, DSCoverAndAnimModel, DSCoverModel, ArtPartsData,
  NodeGraph, SkinnedModel and FixedPositionMover. It has no AI, controller, pad
  input or player state (which is what crashed the Sam clone above).
- It is parked at (0,0,0) with identity rotation, asleep and hidden. Its flags at
  entity+88 read 0x232850551; the porter's read 0x232854453.
- Flags at entity+88: bit 1 = visible (SetVisible 2361e90 → 235f640 sets or clears it),
  bit 8 = sleeping (SetSleeping 2361e20 calls vtable +a0 to sleep and +a8 to wake;
  the third argument is passed through to +a0 and its meaning is unknown).
- Entity tree, as walked by the engine's own transform propagation
  (235bf60): +70 parent, +78 first child, +80 next sibling. A dynamic child array
  also sits at +240 (count, capacity) / +248 (pointer).
- Pointer path, checked by type and by the child's parent slot at every step:
  porter `DSPlayerEntity` → child `CameraEntity` (vtable 3d198e8, getter
  242daf0) → child `DSPlayerAKukanEntity`. The double's parent chain is exactly
  `CameraEntity` → `DSPlayerEntity`. A complete reverse-pointer scan of the
  process found it referenced only by its own components and children, AI notice
  publishers, render effects, a shared gimmick and the position telemetry. No
  global singleton holds it.
- SetWorldTransform (2361ea0 → 22dff80) copies the 60-byte block to entity+c8
  under a lock at entity+288, sets flag bit 0, then 235bf60 sets bit 4 on the entity
  and its subtree. The double sat at the world origin while its parent camera
  moved with the porter, so placement is not simply relative to the parent.
  A woken double does NOT keep a position set this way. Its `FixedPositionMover`
  (vtable 3d0d270, at double+b0, owner at mover+48) stores its own 60-byte
  transform at mover+58 (position) / +70 (rotation). Its constructor 23b1eb0 sets that
  to origin/identity, and its update (vtable slot 38, 23b6dc0) copies it back into the
  entity through 22dff80 every frame unless the entity sleeps. No setter was found, so
  the backend writes mover+58 as well, and restores it on release.

Live results, 2026-11-02, unattended disposable copies with networking enforced:
- one copy, `echo`: the double appeared beside the porter and followed him;
  release restored the resting flags exactly (0x232850551) at the origin.
- two copies from the same save, `peer`: each copy showed the other porter
  through its own double. The double's position in one copy matched the other
  copy's porter exactly. Saves were unchanged and both copies verified after every run.
- the double has no locomotion animation yet. It runs a different animation network from
  the porter (`sam/references/akukan`; its variable ids differ). That network does contain
  `Speed`, `DSMove`, `Move_Float` and `Move_Float_Interp`, and the backend sets them through
  `AnimationManager_Exported{Init,Set}{Bool,Float}AnimationVariable`
  (2397050/23970b0/23971e0/2397220; `Init(manager, name)` returns an index). The ids are valid
  and the manager updates (`Distance_To_Camera` changes), but the double holds a near-bind pose
  while moving. The prefetch index explains why: `sam/references/akukan` is 1.0 MB against
  121 MB for the porter's `sam/sam`, so the private-room network carries almost no clips.
  A walking double needs the `sam/sam` network.
- Walking works (2026-11-03, live). The manager (vtable 3d09910) holds its
  `MorphemeAnimationManagerResource` at +30 and its network instance at +a0. That instance's
  `MorphemeNetworkDefResource` is at instance+38 (the double's def has 0x1d3 entries, the
  porter's 0x2da3). The manager method 23a2fc0(manager) builds a new instance from
  resource+d8 into +a0. It neither frees nor unregisters the old one, so it is not torn down.
  The backend points the double's resource+d8 at the porter's definition only for the call, then
  restores it. It keeps the old instance pointer and puts it back on release (the borrowed
  instance stays allocated). With the porter's network, the same
  `Speed`/`DSMove`/`Move_Float`/`Move_Float_Interp` settings give a normal walk.
  The borrowed instance is kept after release and swapped back in on the next borrow of the
  same manager, verified live over three cycles. A world reload builds new managers, so a new one
  is made.
- Animation variable slots: manager+50 is the u32 count and +58 points to 48-byte slots
  (+0 a pointer to the C-string name, +8 the network control id or -1, +20 the value as an f32
  or a byte). Measured on the porter: walking sets `Speed`, `DSMove`, `Move_Float(_Interp)`,
  `RealMoveSpeedInterp`, `Move_Speed_Config` and `RealSpeedFrame`. Sprinting sets `Sprint` and
  `RunToDash_Rate(_Interp)`. Crouching sets `Is_Crouch`, `Crouch_Rate` = 1 and
  `Stance_Change_Level`. The mod reads `Sprint` and `Is_Crouch` read-only into the presence
  flags (`ds::Sprint`, `ds::Crouch`), and the receiver sets `Sprint`, `Is_Crouch` and
  `Crouch_Rate` on the double. Jumping sets `Is_Jump`, `Action_Move` and `Is_Actions`, and clears
  `Is_BasicAction`. These, with `Is_Fall` and `Is_Landed`, travel as bits in the wire `action` field,
  and the receiver sets the same bools on the double (`Is_BasicAction` = not `Is_Actions`), which
  makes it jump.
- The double's three `DSPlayerAKukanEffFloorEntity` children (vtable 3d7db80) draw the
  private room's yellow floor ring under it once it is shown. Parked, they are hidden and asleep.
  Waking the double wakes them, and an awake floor effect shows itself again every frame, so
  SetVisible alone does not hold. While the double is borrowed, the backend records each one's
  visible and sleeping state and hides and sleeps them on every tick. On release or yield it
  restores the recorded state.
- Hand-back order, the same on release and on yield: the mover's resting target first (the game never sets it, so a
  stale target would pull its own double to the partner's last position every frame), then the animation network,
  then the floor effects.
- Game events: `DSGameState_sExportedIsStoppingGameEvent` (2dd3760) takes no arguments and returns a bool. It
  checks game-state flags (global 7bf6e88, +8/+10 against a mask) or 26303b0. Live, it is true from the title menu
  through loading and the start-up notices, and false in gameplay. During a local event the mod hands the double
  back and marks its presence like the private room, so the partner hides the player instead of showing a porter
  frozen in a cutscene pose.
- Private room signal: the game wakes and uses a porter's own double for the private room. When that double has been
  yielded to the game, or is awake or shown while the mod is not driving it, the mod sets `ds::PrivateRoom` in its
  presence, and the partner's core then hides that porter (the partner's double is released). On two copies there
  were no false signals before, during or after borrowing. A live private-room entry is untested: the test save
  carries a corpse bag, and the game refuses the private room with one ("Cannot enter to the privateroom :
  CorpseBag").
- the exported SetWorldTransform also runs 235bf60, which marks the entity and its subtree
  as transform-changed. Calling it every tick froze the double mid-stride, so driving now
  only updates the mover target and lets the game move the entity.

`mod/double.cpp` borrows this entity as the partner's body. It only takes the
double when the game has it asleep and hidden. It stops calling as soon as the
double's flags or position differ from what it set (the game is using it), and it
puts it back to its saved resting transform, hidden and asleep, when there is
nothing to show. It never creates or removes anything; its only direct write is
the mover transform above. Unknowns: how to drive its locomotion animation, and whether the private room
still works normally after the double has been borrowed and released.

## Cargo (research for shared-world cargo, 2026-11-03, static)

Script exports of `DSBaggage2Manager`, resolved with the `lea r8,[name]` / `lea rax,[impl]` method:

| Export | Thunk | Implementation | Reading |
|---|---:|---:|---|
| `CreateBaggage` | 2cd1450 | 2cd6b20 | ecx = cargo type id, rdx = 60-byte world transform; fills a request (mask 0x1fffff, kind 4) and queues it with 2c2a9c0 on the manager (global at 7bf4968) + 46d9f0. Creation is asynchronous and returns nothing. |
| `CreateAndAddBaggageToPlayer` | 2cd1440 | | |
| `DeleteBaggage` | 2cd1460 | | |
| `ExtractBaggageInfo` | 2cd14d0 | (inline) | |
| `GetAllBaggages` | 2cd15a0 | 2cd74f0 | filter arguments (ecx id/mask 0x1fe000, r8b, r9b); builds a large list on the stack |
| `GetBaggagesInRange` | 2cd15d0 | 2cd7ae0 | |

Live and read-only, early Episode 1: there are 264 `DSBaggage2Entity` objects, and 263 of them are a hidden,
sleeping pool (flags 0x30040540). The one active entity is the cargo the porter carries. A cargo entity has the
standard entity layout: components at +98 (interaction, anti-gravity, stencil, sound...), `PhysicsMover`
at +b0, `SkinnedModel` at +b8, `AIFaction` at +178 and `ArtPartsData` at +180. The cargo data itself (type,
owner, state) is in the single `DSBaggage2Manager`'s `DSBaggage2` records:
- The manager holds 511 records inline at +20, with a stride of 0x240.
- Record fields: +18 slot, +20 a state bitfield (not a type id: the same corpse record read 0x205 and later 0x105,
  perhaps the holder in bits 8-9), +28 a `DSGameBaggageListItem*`, and +c8 the item hash.
- List items (stride 0x70): +20 is the name text resource (a string at its +20, for example "Resins ({0})"),
  +28 the description, +30 the hash and +50 small floats (weight?).
- Early Episode 1 has 35 live records: metals, resins, boots and the corpse the porter carries.
- A cargo entity points to its record at entity+330.
- Record +38 is the world position as 3 x f64; it is zero for equipped items, and distant world cargo is listed too.
- Record +30 holds two floats, probably durability current and max (900/900, 1500/0).
- Record +1c reads 0xfffffe00 for loose or equipped items and 0 for the carried cargo, so it may be the holder. Unconfirmed.

`CreateBaggage` and `CreateAndAddBaggageToPlayer` work live (2026-11-03). Their id is the cargo **item hash**
(`DSGameBaggageListItem`+30, also record +c8; for example resins = 0x0782598d). Called on the game thread,
`CreateBaggage(hash, transform)` put a real "Resins (40)" crate on the road at the requested point: a new world
record (state 0x107) whose +38 position is the spawn point. `CreateAndAddBaggageToPlayer(hash, false)` adds a record
too. Earlier attempts with other numbers were silently ignored, and the "+4c" exit in 2c470e0 belongs to a different
branch, not to this path.

Shared cargo, first version (`mod/cargo.{h,cpp}`, core CARGO events):
- **Read:** the mod reads the record table through the manager global (image 7bf4968, vtable 3dd9338) four times a
  second, read-only.
- **Announce:** a record counts as placed by this porter when it is new within 15 m of him, or when he held it
  (within 2.5 m) and it now stays put while he is at least 3 m away. It must also have settled for 1 s. Each
  placement is announced once, as {item hash, position}.
- **Recreate:** the partner calls `CreateBaggage(hash, transform)` on its game thread. It first registers the
  expected item and point, so the record that appears for it is marked as a mirror and never announced back.
- **Live, two copies:** a crate created on one copy appeared in the other at the same coordinates, with no echo.
- **Not yet (then):** picking cargo up and ownership, a real put-down by the porter, and cargo far from the partner.
  The first two are done and were driven live on 2026-11-07 (below).

### Cargo, live 2026-11-07: where an item is, and deleting one

- Record byte `+21` (bits 8..15 of the `+20` word) is where the item is: **1 in the world, 2 carried by the porter**.
  The carried corpse read `0x205`; when Sam sat down to rest and put it down, the same record read `0x105` and its
  position dropped to the ground. Equipped items read `0x200`.
- Record `+84` is a persistent cargo id for game-placed cargo (`0x2003`, `0x202e`, `0x2061`...; repeated at `+f8`).
  Cargo made by `CreateBaggage` has `0x1fffff` there (no id).
- `DeleteBaggage` (2cd1460 -> 2cd6d40) queues a kind-2 request with its argument at request `+8`. The queue loop
  (2c95540, branch at 2c95b2b, taken when request `+4` is -1) masks the argument with `0x1fff`, uses it as the
  **index of the record in the manager's array** (`manager+20+index*0x240`) and goes on only when record byte `+21`
  is 1, 2 or 8. Live: `delete 7` removed the crate in record index 7 (36 -> 35 records); its `+20` value (263),
  its `+18` slot (65) and a `+84` id (8238) did nothing.

### Cargo, live 2026-11-07 with keyboard input: the porter's own pick-ups and put-downs

- **Carry frame.** While an item is on the porter's back, its record `+38` position is not a world position: it holds
  values near (100, 100, 20) that jitter with his walk. Put down from the cargo menu (Offload, then Confirm), the record
  reads `0x107` at once but keeps the carry-frame position for over a second; then the real place is written (here
  1692.4, 1056.5, 135.7, at his feet). The first build announced the carry-frame place to the partner. An item in his
  hand (grabbed with a mouse button) holds world coordinates. The partner's double was parked at (100, 100, 20) too
  while the game was on the title screen.
- **Kind.** Record `+20` bits 0..7: 7 on every ordinary item seen (crates, materials, copies made by `CreateBaggage`), 5
  on the president's corpse (story cargo; holder `+1c` 0 where ordinary items read `0xfffffe00`), 0 on equipped gear.
  After a reload with Sam resting, the corpse lay on the ground and was announced as a put-down; only kind 7 is shared
  now.
- **The cargo menu stages.** With the cargo menu open, items on the ground within reach read exactly like carried
  cargo (`0x207`, carry frame) although the menu lists them as "Not carried"; closing it puts them back (location 1
  at their old place, after the same carry-frame delay). The mod therefore reports a pick-up only once the porter has
  walked 0.5 m (he cannot move while the menu is open), or the item lands elsewhere or is used up; an item back at
  its place was never taken. Live: opening and closing the menu next to a crate sent nothing; carrying it (E) was
  reported once he walked.
- **Controls (PC defaults, from the game's Key Assignments, K):** E carries an item on the back; holding the right or
  left mouse button grabs it into that hand, and a left click puts it down; I opens the cargo menu (Enter on an item
  opens its commands; Confirm is a button at the bottom left that takes a mouse click; right click goes back); Tab
  opens the cuff links (map); F performs actions and opens terminals; C crouches; Q scans. Taps under about 300 ms
  are sometimes lost at 20 fps.

### Carried cargo: what a porter carries and how the game draws it (static, 2026-11-08, nothing called)

Enumerations from RTTI (enum descriptor: `-1`, kind 3, size byte, u16 value count, values at `+18`, 0x28 bytes each:
u32 value, name pointer at `+8`):
- `EDSBaggage2SlotType`: 0 Default, 1 BackPack, 2 LeftArm, 3 RightArm, 4 LeftWaist, 5 RightWaist, 6 LeftHand, 7 RightHand,
  8 LeftCarrier, 9 RightCarrier, 10 MainWeapon, 11 SubWeapon, 12 SubWeaponPouch, 13 Item, 14 Shoes, 15 SpareShoes,
  16 SuitParts, 17 Glove, 18 Mask, 19.. storage, terminal shelves and vehicle slots.
- `EDSBaggage2CarrierType`: 0 None, 1 Player, 2 Cart, 3 Truck, 4 Bike, 6 Post, 7 Terminal, 8 Mule, 12 Ground,
  13 AutoDeliveryCarrier, 14 TerminalShelf, 17 PrivateRoomStorage, 20 PlayerBackup, ...
- Also `EDSBaggage2Type`, `EDSBaggage2Size`, `EDSBaggage2Category` (7 = DynamicOffline, 5 = FriendlyMule, 0 = Private)
  and `EDSBaggage2Model`. Whether record `+20` (the "kind" 7/5/0 above) is the category is unconfirmed.

The porter's carried cargo, as the game reads it in `DSPlayerSystem_sExportedDoesHaveBaggage` (thunk 26e67a0 ->
26e8610, then 2c532f0 -> 2c53340 -> 2c53640):

| Object | Field | Meaning |
|---|---|---|
| `DSPlayerEntity` | +54d0 | its `DSBaggage2CarrierComponent` (cached by 271fa60 with the component lookup 22b2a60; zeroed by the constructor 26f3f40) |
| `DSBaggage2CarrierComponent` (vtable 3dd9750, type 4c39790, 0xb0 bytes) | +50 | the carrier |
| carrier | +10 / +18 | slot count (i32) / slot pointer array; 2c52010 finds a slot by type |
| carrier | +38, +88, +a0 | carrier id (the manager lists every carrier at +46d280 count / +46d288 array and finds one by +38, 2c52040), flags, lock |
| slot | +1 | slot type (`EDSBaggage2SlotType`) |
| slot | +4 | item count (i32; the holster code below tests the BackPack slot's) |
| slot | +18 / +20 | stack count / stack pointer array (a stack is a column of cargo) |
| stack | +10 / +18 | record count / pointer array to `DSBaggage2` records (bottom first is assumed, not confirmed) |
| record | +80, +90 | holder carrier type (`EDSBaggage2CarrierType`) and holder carrier |

With its second argument set, `DoesHaveBaggage` also walks every carrier in the manager (2c5e920), so the game's own
"has the porter got this item" checks count records on any carrier.

How carried cargo is drawn: the cargo entity update 2cc33b0 (a `DSBaggage2Entity`, its record at entity+330) runs when
record +21 is 2 (carried). It finds the holder entity through the record's carrier (+90, then +28, then +20, a handle
to the holder's component): for a `DSPlayerEntity` the parent is the entity at +4cb8 of its `DSPlayerInventoryComponent`;
for a `DSPlayerAKukanEntity` (the resident double, and the body the mod makes) the entity at +f0 of its
`DSPlayerAKukanInventoryComponent`; for a `DSVehicleMotorbike` the entity at +d60. It then picks a helper name (body bags:
`HLP_DeadBody`, `_2nd`, `_3rd`, `_4th`; vehicle carriers: the slot's helper from 2c54810) and, when the cargo's mover
(entity+b0, an `AttachToHelperMover` with its helper name at +b0) names another helper, re-parents the cargo (235e990)
and sets the helper (23a8bb0). Weapons choose `CarrierAttachHelperName` or `EmptyCarrierAttachHelperName`
(`DSWeaponParameter` +100/+108) by whether the owner's BackPack slot holds anything; the same code accepts the double as
owner (2922cc0, 291156a). So the game can draw cargo on a double's back by itself.

Why the partner's cargo is not made as real records held by the partner body: such a record is the game's real cargo.
It lives in the manager's save-backed array, the checks above would count it as this porter's, and no export puts a
record on any carrier but the local porter's (`AddBaggageToPlayer` 2cd1370, `MoveBaggageToPlayer` 2cd1680 and
`CreateAndAddBaggageToPlayer` 2cd1440 all target the local player). Weight and UI effects are unknown.

Purely visual route (untried): `Entity_ExportedSetParentAndAttachToHelper` (thunk 2361dc0 -> 234ceb0: entity, parent,
helper name as a C string, bool; the bool sets entity+88 bit 9) sets the parent (235e990), builds an `AttachToHelperMover`
(23aa8d0; type 4b140a0, constructor 2398c30, 0x110 bytes) with the helper name at +b0, installs it and sends a
transform message. `AttachToHelperMover_sExportedCreate` exists too. What is missing is an entity that looks like the
cargo without being a record: a `DSBaggage2Entity` expects its record at +330, and making one outside the manager is
untried (it could fault like the Sam clone). The helper names the backpack uses for ordinary cargo were not found as
literals (likely from 2c54810 or the backpack resource).

`mod/carry_visual.{h,cpp}` (config `carry_visual`, off by default) reads the chain above read-only, checks the
component's vtable, the counts, that each record is one of the manager's and names this carrier as its holder, and
sends a capped summary (item, slot, stack, level, count) as CARGO events of type 6. The partner's mod keeps the latest
summary and only logs it (`CARRY partner shows ...`); it never makes or moves a record. Unconfirmed live: the whole
chain, the stack order, and the slot types seen for a back carry and a hand carry.

## Outfit (2026-11-03, read-only)

The double has no backpack or hood (the backpack is now copied onto the own body: "Gear on the partner body"). The
porter's backpack, pouches and other gear are about 30 attached child entities (plain `Entity` and groups of `DSAttachOrPhysicsEntity`), not body art parts, so outfit parity means
attaching gear entities to the double. Body art parts are switched by `ArtPartsData_sExportedEnableModelPartFromHash`
(22b4f80 -> 22b5090: entity, part name, on). The name is hashed with CRC32C (190fe50) and looked up in the 52-entry
table of the entity's `ArtPartsData` (entity+180, +100 count, +108 array). The porter and the double share that
resource. The archive lists 133 Sam art parts (`part_sam_part_hoodotter`, caps, boots, suits, ...).

Outfit route, static and untried: `ArtPartsVariation_sExportedApplyArtPartsVariation` (22bc100) takes an entity and a
variation resource. It adds a component built from that resource (234ae90, refused when entity+88 bit 31 is set),
then applies it (22bae20). Applying the porter's variation to the double would need the added component removed on
release (`EntityComponent_ExportedRemove`, 23618c0) so the private room is unchanged. It would cover the suit and
hood, but not the backpack. Live, though, the porter has no variation component: 68 components, including `ArtPartsData`,
`ArtPartsShaderVariableComponent` and `BoostMeshStreamingForArtPartsDataComponent`. His look is the art-part states
plus attached gear, so this route does not reproduce it.

## Gear on the partner body (2026-11-08, live)

The porter's gear, read live (`ds.exe` build 13300582, Episode 1 test save; Decima type names from each vtable's slot-0
type getter and the descriptor name at +40): about 60 child entities. Direct children of the porter are plain `Entity`
pieces (armour plates, power gloves, cuffs, ammo pouch, harmonica, head pieces, boot, BB cord), each moved by an
`AttachToHelperMover` (entity +b0, vtable 3d0b140) whose helper name is a C string at mover +b0 (`HLP_Hcuf`,
`HLP_Waist_l_armor`, `HLP_Head`, ...); a `DSBackpackEntity` (vtable 3dcb9e0, 0x5600 bytes) at `HLP_Backpack` with a
`CustomizedAttachToHelperMover` (3ded730); the `DSOdradekArmEntity` (3da2ee8); and `DSAttachOrPhysicsEntity` groups
(3df0420) with no helper. Gear the porter does not show is there too, hidden (entity flag bit 1 clear, usually asleep
too): in the test save only the cuff, the backpack and the Odradek are visible. The backpack's own children are its
pouches, rain cover, cylinder and item pieces and the carried cargo (`DSBaggage2Entity`, the corpse at
`HLP_DeadBody_3rd`). `DSBackpackEntityResource` names its parts: `DreamCatcherEntity` +100, `ChiralCylinderEntity`
+108, `BBPodBagEntity` +110, `RainCoverEntity` +118, `GPCaseEntity` +120, `PouchSetEntity` +128, `BackpackBeltEntity`
+130, `ArtPartsVariations` +138. A backpack made from that resource builds them itself (0x2bdf942 stores the rain
cover it creates at backpack +3a0), so a copy comes with its pouches in their default state.

The resident double has only a harmonica, an Odradek and three private-room floor effects. While the porter's cargo
menu is open the game does not make a second backpack: it re-parents the porter's own gear, backpack and cargo to
the double (moved to the preview place, 100, 100, 20) and back afterwards. A partner seen at the same time as the
porter cannot use that.

Copying a piece: `EntityResource_ExportedCreateEntity` (23806a0) with the piece's resource (`Entity_ExportedGetResource`,
2362ba0), then `Entity_ExportedSetParentAndAttachToHelper` (234ceb0: entity, parent, helper C string, bool; the bool
sets entity +88 bit 9; it re-parents with 235e990, then installs a new `AttachToHelperMover` and sends a transform
message). `EntityResource_ExportedCreateEntityWithParent` (2380630) takes the same arguments plus the parent (ninth);
its creator (23745a0) sets the parent and bit 9 before the entity joins the world. Live, a plain `Entity` (the cuff)
copied either way works.

A copied backpack hung the game a frame after it was made, with or without a parent, attached or not. The main thread
waited for a job counter; the crash recorder (`COOP_CRASH_RECORD=2`) showed the job had faulted reading address 0x18 at
2c52010, called from the backpack's update at 2be3a84. The backpack's `DSBaggage2CarrierComponent` (vtable 3dd9750)
keeps its carrier at component +50; the cargo manager fills it only for the porter's backpack, so a copy's is null.
2c52010(carrier, slot type) returns the carrier's slot of that type (count +10, array +18, type at slot +1) or null,
and the update writes slot +70 without a null check. A sleeping copy (`Entity_ExportedSetSleeping`) never runs that
update and did not hang, but it stays where it was made instead of following the body. Given an empty carrier the mod
owns (one slot for each of 32 slot types, no stacks or items), the next fault was a write to 0x24 inside
`EnterCriticalSection` from 2c5ce00: the carrier's +a0 and each slot's +78 are Windows `CRITICAL_SECTION`s. With both
initialised, a copied backpack follows the body while it walks and runs, and a two-minute soak, parking, hiding and
removal ran without a fault (`mod/gear.h`).

## The partner's own body: the engine's spawn path, read from RTTI (static, 2026-11-03, nothing called)

The double is the game's own private-room Sam, so the game takes it back for the private room and cutscenes. Co-op mods
elsewhere (Skyrim Together's proxy actors, the Horizon Zero Dawn Remastered entity spawner) give each remote player a
character of its own, created through the engine. Decima keeps a type descriptor for every class in `.data`, and that
descriptor names the constructor and the fields. Compound descriptor: `+0` id (`-1`), `+4` kind (4 = compound),
`+6` base count, `+7` field count, `+14` size, `+18` alignment, `+20` constructor, `+28` destructor, `+40` name,
`+60` bases (16 bytes each: type, offset), `+68` fields (0x38 bytes each: `+0` type (null = category heading),
`+8` u16 offset, `+a` u16 flags, `+10` name, `+18` getter, `+20` setter). Found by the name string's address appearing
at descriptor+0x40 after an `ffffffff 04` head.

| Type | Descriptor | Size | Constructor / destructor | Fields used |
|---|---:|---:|---|---|
| `Spawnpoint` : `SpawnpointBase` : `ParentWorldNode` : `WorldNode` | `0x4b1dbc0` | 0x1b0 | `0x23cd210` / `0x23cd230` | `ObjectUUID` +8 (GGUUID), `Orientation` (WorldTransform, accessor: getter `0x1dfe580`, setter `0x1dfe5c0`), `SpawnSetup` +88, `EntityResource` +90, `FactsArePersistent` +b0, `AutoSpawn` +b1 |
| `Entity` | `0x4afcdb0` | 0x330 | `0x2330720` / `0x2330780` | `Resource` +58 (the entity's EntityResource), `Model` +b8 |
| `DSPlayerAKukanEntity` : `Entity` | `0x4bca020` | 0x7a0 | `0x26eeb20` / `0x26eeb40` | none of its own |
| `EntityResource` | `0x4b02810` | | | |
| `SpawnSetup` | `0x4ed4830` | | | |

The spawner's recipe, read-only reference (Nukem9/hzdr-gameplay-tweaks, `EntitySpawnerWindow.cpp`): build a
`Spawnpoint` through its RTTI constructor, set `ObjectUUID` (fresh), `Orientation`, `SpawnSetup`, `FactsLifetime` 0
and `AutoSpawn` false, call the spawnpoint's Spawn on a job, then GetEntity. In ds.exe these are
`Spawnpoint_ExportedSpawn` (`0x23da6c0`), `Spawnpoint_ExportedGetEntity` and `Spawnpoint_ExportedDespawn`.

Plan for the partner: the double's own resource (`double+58`) is a Sam body that is already loaded and made to be
puppeted (no player controller). A spawnpoint with `EntityResource` set to it (or `EntityResource_ExportedCreateEntity`
`0x23806a0` directly) should give a second Sam that belongs to the mod alone. It would be driven exactly like the double
(mover target, the porter's animation network) and despawned on disconnect. It has not been called yet. Open: whether
the spawnpoint must be registered in a world-node tree, the memory owner for a refcounted `RTTIRefObject` built
outside the engine allocator, and whether the spawned Sam has a FixedPositionMover like the double.

### The engine's common entity creator (static, 2026-11-03, nothing called)

`Spawnpoint_ExportedSpawn` (`0x23da6c0`) only proceeds when spawnpoint `+188` and `+189` are 0 and `+138` is non-zero
(probably "active in a loaded world", set by the world-node tree), then calls vtable slot 28 (`0x23d8580` ->
`0x23d8b10(spawnpoint, false)`). A spawnpoint built by the mod outside a world tree would therefore be refused
silently. The spawn path ends in `0x2380920`, which 33 game sites call (Spawnpoint, EntityResource exports, gimmicks):

`0x2380920(Ref* out /*rcx, zeroed first*/, const WorldTransform* at /*rdx, 0x3c bytes: 3 f64 position + 9 f32
rotation*/, Resource* resource /*r8*/, void* unknown /*r9*/, const GGUUID* id /*stack +0x20*/)`

It allocates a 0x1f0-byte object, initialised by `0x236cfc0` (vtable `0x3d07fa8`, refcount at `+18`, the transform
copied to `+50`, the id to `+38`), and stores it in `*out` with a reference taken. It also asks the global at
`0x7ae90e0` (vtable slot 3, mode byte 5) for the resource. **Reading:** the result is an asynchronous creation
request, not the entity. The entity appears once the resource is ready, which for an already loaded resource (the
double's own, `double+58`) should be within a frame or two. Caller at `0x2373d6b`: transform = object+20, resource =
object+60, unknown = object+80, id derived from object+8. First live step (one copy, owner away): call it once on the
game thread with the double's resource, a transform 3 m beside the porter and a fresh id; log the request's fields
for a few frames; look for a new DSPlayerAKukanEntity in the world. Remove it with `Entity_ExportedRemove`.

### The common entity creator with the double's resource, live (2026-11-07)

One call of `0x2380920` on the game thread with the double's own resource (`double+58`), a transform 3 m beside the
porter and a fresh id never returned to the mod: no log line after the call. The game kept running, but the mod's
runtime lock was still held, so the mod stopped ticking (commands not read, the partner disconnected). A thread
snapshot found no thread inside the creator, and the mod's loop thread was waiting on that lock. Reading: the call
faulted and the game's own structured exception handling resumed the main loop above the mod's frames. As with the
local Sam's resource (2026-11-02), calling the creator directly does not give a second Sam. The remaining engine
route is a registered `Spawnpoint`; until then the partner stays on the resident double.


## Structures (2026-11-07, static and live)

`DSConstructionPointManager_sExportedCreateConstruction` (thunk 2cd1870 -> 2cd6c00) takes `(u8 type, const
WorldTransform*, bool)`. It returns at once when the manager global (image+7bf4960) is null, otherwise it fills a
0x6c8-byte request (type at +0, the transform at +8, the bool at +640, +665 = 1) and queues it on the manager
(+56a278, under the lock at +56a5b8). The type is `EDSConstructionPointType` (enum descriptor in RTTI, 0x31 values),
in Stage / Player / Net triples: 2-4 safe house, 5-7 post, 8-10 watchtower, 14-16 charger, 17-19 rain shelter,
21-23 zipline, 24-26 ladder, 27-29 climbing rope, 30-32 bridge 30 m, 33-35 bridge 45 m; 0 delivery base,
1 preppers' shelter, 20 MULE post, 36 road rebuilder. "Net" is the variant the game uses for structures that came
from other players.

The manager keeps one 0x710-byte record per structure, in a table that ends at manager+0x5699c8 and grows
downwards (a new record took the slot below the lowest one):

| Offset | Field |
|---|---|
| +0, +10 | a pointer to the record class in the image, and the same pointer + 0x10 (+8 is zero) |
| +18 | id (u32): below 10000 the game's own, 10001.. built by this porter, 100001.. Net |
| +1c, +1d | type, category |
| +1e | state: 2 built, 1 the game's existing structure, 0 refused or pending |
| +28 | world position (3 x f64); the game snaps the height of what it builds |
| +40 | rotation (9 x f32, rows: right, forward, up) |

Live, early Episode 1 (Capital Knot City, before the chiral network): `CreateConstruction(26)` (Net ladder) and
`(25)` (Player ladder) built a ladder lying across the road at once (state 2; ids 100001 and 10002). Post, rain
shelter, watchtower, bridge and zipline requests stayed in state 0 with nothing visible during that run, and were
standing, in state 2, after the next load (the game saved them pending). 155 records were in the table, mostly the game's delivery bases and shelters.

Removal: `DSConstructionPointManager_sExportedRemoveConstructionsInArea` (thunk 2cd1bf0 -> 2cd9760) takes `(const
float4* box min, const float4* box max, const WorldTransform* box placement, bool, bool, bool)`. It collects the
structures in the box (2ce0260 on manager+56a258) and marks for removal (`+aa |= 0x40`, `+ab |= 1`) those whose owner
byte `+b2` is selected: the first bool selects owner 1, the second owner 0, the third owner 2. Live: the third bool
removed a Net ladder and the first removed a ladder built by this porter, so **owner 1 = built by this porter,
2 = Net, 0 = the game's own**. A ladder saved from an earlier session came back after loading (its record
reached state 2 only after the mod's first look), so a receiver checks for an existing built copy before building.

### What completes a structure (2026-11-12, live with a write watch on the state byte, and static)

The record is the construction point object from its `+0x18` on (object = record - 0x18; the flag bytes at object
`+0xa8..+0xb1` are record `+0x90..`). States (record `+0x1e`): 0 a request the game has not built, 1 built and out of
the player's range, 4 being made, 2 built and in range. A structure the player walks away from goes 2 -> 1 and comes
back to 2; for a moment on the way back its record is not in the table at all. The game's own build (the
construction device's menu) takes a record 0 -> 1 -> 4 -> 2 within a frame or two: created by the queue processor
(`0x2c85ce0`, which the save loader `0x2c9b780` also calls right after its own pushes), then `0x2c87f20` walks the
pending list (manager `+0x56a0d0`, count `+0x56a0c8`) and, for each object that passes `0x2c35fb0`, calls
`0x2c6acf0` (state 1); `0x2c7fd90` sets 4 and `0x2c9d76a` sets 2.

`0x2c35fb0` refuses an object whose flag `+0xab & 0x40` is set. That bit is copied from request `+0x645` when the
record is made (`0x2c32d70`; categories 10 and 11, ladder and rope, skip it), and the game clears it (`0x2cb4ee0`)
when the build effect object at `+0x360` has come within reach of the structure (`0x2cb9cd6`). The menu makes that
effect; `CreateConstruction` sets `+0x645` and makes none, which is why its posts, towers, shelters, ziplines and
bridges stayed in state 0 until a load. **A request with `+0x645` clear is built at once**: live, Player post and Net
post, watchtower, generator, rain shelter, zipline and 30 m bridge all reached state 2 within seconds and were drawn;
a Net safe house got no record. The other request bytes are defaults of the request constructor (`0x277fde0`); the
game's own build request differs from the export's only in `+0x623`, `+0x50` (1), a float at `+0x80` and a second
copy of the placement at `+0x680`, none of which matters for this (each tried).

Record `+0x388` is the build time, microseconds since year 1 UTC (request `+0x608`); the save keeps it, so the
structures of a 2022 save carry 2022 times. After a load the game's own records (147 in the Episode 3 test save)
are in the table about 2.5 s before the save's (73, already in state 1 or 2).

The tool wheel reads the pointer (`GetCursorPos` through the window's method at `0x1a17f70`), not raw mouse motion;
releasing the wheel key equips the highlighted item.

## Enemies: the game actor manager (2026-11-12, static, the reads and the defeat call confirmed live)

Enemies are game actors, like vehicles. Manager `M = *(image+0x7bbb8c0)`: `M+0x440` (i32) units, `M+0x448` the array
of unit pointers. A unit: `+0x30` first index, `+0x40` count, `+0x48` array of actor pointers; its vtable tells the
kind: MULE 0x3db71b8, MULE camp 0x3dbe8d0, Gazer 0x3da58a8, Catcher 0x3daf530, Catcher jellyfish 0x3da81d8. An actor:
`+0x18` its locator (the locator's `+0x8` is a 16-byte UUID), `+0x20` live byte, `+0x24` LOD (2 = has an entity),
`+0x28` world transform (3 doubles, 9 floats), `+0x68` weak entity pointer. Actor id = (unit index << 13) | index.
`0x22fa980(M, id)` gives the actor, `0x22faa50(M, &id, &uuid)` the id of a locator UUID.

MULE actor: life state `+0xea0` (copy `+0x11c`): 1 and 2 standing, 4 down and present, 3 dead body, 5 removed; flags
`+0xea4` (copy `+0x120`): when down, bit 19 knocked, 20 fainted (timer `+0x338` from the config), 21 asleep (timer
`+0x33c`). The game's defeat function `0x2aa8000(actor, kind)`: kind 0 knocked, 1 fainted, 2 asleep, 3 dead; with an
entity it goes through the damage component (`+0xe90`, `0x2af6930(component, kind+1, 0)`), without one it sets the
state and the flag bit. Setter of the life state: `0x2acedb0(actor, state)`. MULE camp actor: phase `+0x214` (0
normal, 1 return, 2 pre-caution, 3 caution, 4 evasion, 5 alert), members `+0x4c0` (count) / `+0x4c8`. MULE manager
`*(image+0x7bf09d8)`: camp actor ids at `+0xd8` (count) / `+0xe0`. Script functions by camp locator UUID:
`ForceSetCPPhase` 0x2b649a0 (uuid*, phase), `DisableCPAndAllsMembers` 0x2b64880 (uuid*, bool), `IsExtinctionCP`
0x2b64f50. `ForceSetCPPhase(uuid, 5)` live, with the porter far from every camp: eight of nine camps went from phase
0 to 3 (caution), not 5; the ninth stayed 0. The other two are not called yet.

Live, Episode 3 save, two copies: 9 camps and 150 MULEs in units 0 to 4, the same ids and locator UUIDs in both, all
with a locator; 16 to 18 MULEs within range had an entity; all standing in state 1; the camp next to the porters went
to phase 5 in both copies by itself. `0x2aa8000(actor, 1)` on a MULE with an entity: state 4, flag bit 20, and it lay
on the ground; called in the partner's game for the same UUID it did the same there. No Gazer or Catcher actor was
live at that place (units of 256 and 1, none live): BTs are not mapped beyond the unit kinds.

## Orders: the mission system (2026-11-12, static, the reads and the three requests confirmed live)

`DSMissionSystem* S = *(image+0x7c0aff8)`. Static orders: `*(S+0x108)` -> {count (i32), records pointer at +8}, one
0xa0-byte record per order: `+0x00` its resource, `+0x08` a runtime instance while it is under way, `+0x2a` result
(1 not started, 2 under way or failed, 4 completed), `+0x2c` state (u16: 1 available, 4 in progress, 8 failed, 16
success, 64 not available), `+0x38` the mission id. Resource: `+0x40` the order number shown to the player, `+0x46`
category detail (0 main, 1 sub, 2 standard delivery, 3 standard collection, 8 race, 9 firing range). `+0x110` holds
dynamic orders (lost cargo; ids with bit 62) and `+0x118` online ones. A cargo record's `+0xb0` is the id of its
order.

Cargo units: `T = *(S+0x18)`, rows at `T+8`, capacity (u32) at `T+0x14`, 0x38-byte rows: a hash (0 = free) at
`+0`, the key at `+8` = mission id | unit << 48, the state (u16) at `+0x18`: 1 not delivered, 2 delivered. Live: four
rows, units 0 to 3, state 1 after order 0x372d7 started. A cargo record's unit is its byte `+0xdc` (also the high
word of `+0x1c`); its `+0xb0` holds the mission id without the unit.

Requests are queued under the system's lock and run by its own update: start `0x33b80a0(S, id, 0, 0)` (returns 1
when accepted; the order must be available), success `0x33b82d0(S, id, 0, 0)`, fail `0x33b75e0(S, record)` (reads
only the record's `+0x38`).

Live, Episode 3 save: 1140 records, 723 with an id (485 standard deliveries, 66 collections, 96 main and sub), 53
completed, none under way. Start of order 0x36330 (no. 414): state 4, an instance, result 2, and the game made two
cargo records with `+0xb0` = the id. Success: state 16, result 4, no instance. Fail of another: state 8, result 2.
The same three requests in the partner's game gave the same states there.

## Save sections: the game's own serialization over a memory buffer (2026-11-12, static; one section run live)

The save is about 45 sections written and read in a fixed order by two straight-line functions (master save
`0x2e1e190`, master load `0x2e11820`), each section by its subsystem's own pair of functions over an abstract byte
stream. Each section opens its own tagged scope and writes its version first, so a section can be called alone.

- Memory stream over a plain buffer: `0x18fc750(obj[0x30, zeroed], u8 mode, buf, i32 size)` (mode 1 to write, 0 to
  read); `+0x18` begin, `+0x20` cursor, `+0x28` end.
- Writer: `0x1d3d420(writer[reserve 0x1000], stream, 0x40, 0x2000)`, finish `0x1d3e8d0(writer)`; bytes
  `0x1d42820`, variable-length integer `0x1d42760`.
- Reader (`LoadState`): `0x1d3d270(reader[0xa8, zeroed], u16 0, stream, u32 size)`; bytes `0x1d41230`, integer
  `0x1d40c00`, scope `0x26215a0`.
- Section pairs (object global: load, save): weather forecast `0x7bf83b8`: `0x2e12520`, `0x2e1e990`; constructions
  `0x7bf4960`: `0x2c9b780`, `0x2ccec10` (per record: write `0x2ca7970(manager, writer, request)`, read
  `0x2c99c50(manager, reader, request, version)`); cargo `0x7bf4968`: `0x2c9b1d0`, `0x2cce980` (its loader empties
  the lists first); mission system `0x7c0aff8`: `0x33b4f80`, `0x33c66a0`; MULE manager `0x7bf09d8`: `0x2b11db0`,
  `0x2b2ba70`; gazer manager `0x7bebec0`: `0x299fc90`, `0x29a6800`; user map markers (getter `0x2f40090`):
  `0x2f35a70`, `0x2f3eda0`; roads `0x7c173b0`: `0x3472eb0`, `0x3477da0`; vehicles (in `0x7bfecc0`): `0x2f79f10`,
  `0x2fa6450`. Most loaders assume a world that is being built (they append or clear); only sections that are
  overwritten in place are candidates for a running game.

Live, the weather forecast (mod research command `stream`): written on the game thread of a running game it is
21334 bytes; the section of a copy on the Episode 3 save was read into a running copy on the Episode 1 save, both
games ran on, and the receiver's section written out again differed from the source in 291 bytes (one block at
5323..6635 and one bit) where it had differed in 4938 before. What the game shows after that (sky, rain) was not
looked at.

### BTs: what destroys a gazer, and the catcher command live (2026-11-12)

- The gazer's state block is at actor `+0xe8` (the game copies it to `+0x218` at the end of each update: the fields
  noted below as `+0x2e4` / `+0x2b0` are that copy). Live life is the float at `+0x180`, live flags `+0x1b4` (bit 3
  dead, bit 20 a gazer made for the moment, removed by the game when the rain stops), detection byte `+0x1a8`.
- There is no kill function: every kill is `life = 0`, and the actor update (`0x2995210`, run for every actor at any
  LOD) does the rest: flag, the saved record of dead gazers (`0x29a31a0`), the body's reaction. The mod writes the
  life (bts.cpp). Live: a gazer at LOD 0 written to zero in one game was reported, and destroyed in the partner's.
- Two copies of one save in the same BT area hold the same 74 gazers with the same locator UUIDs and positions;
  the actor ids differ. The area becomes active in both through the host's rain.
- Catcher command `0x2a928c0(u8)` live: 0 with no fight running and outside every cleared sphere starts the fight
  around the player within seconds (record at manager `+0x88`, tar and ruins); 1 ends it and leaves a cleared sphere
  (`+0xa8`, at the territory, radius about 280, shrinking with time; saved). Inside a cleared sphere 0 does nothing
  and the weather map there reads clear, so rain placed there brings no BTs.

### Catcher life (2026-11-13, static; read and write confirmed live)

- Each catcher of a fight has a record (manager `+0x88` count / `+0x90`, stride 0x110: `+0x0` weak entity (entity =
  value - 0x20), `+0x20` actor id, `+0x28` type (0 octopus, 1 quad, 2 whale, 3 titan, 4 ghost), `+0xf0` 0 = active,
  `+0xf4` life in percent, written by the component every tick).
- The life is in the entity's catcher base component (vtable `image+0x3daf340`; found among the entity's components,
  count `entity+0x90`, pointers `entity+0x98`; `+0x48` entity, `+0x58` actor id): `+0x138` full life (100),
  `+0x13c` life, `+0x140` the life the bar shows (follows at 10 per second). Hits are converted to percent by the
  catcher's full life and the difficulty, so percent compares between two games.
- A written life is honoured by the bar, the record and later hits. Death is not polled: it happens only inside the
  hit that takes the life to zero, and that handler leaves at once when the life is already zero. So zero must never
  be written (the catcher would be unkillable); the mod leaves 0.05.
- A cleared sphere is {x, y, encounter radius, countdown from 300}; it goes when the countdown ends.

### BTs: the gazer and catcher managers (2026-11-12, static; the reads confirmed live)

Gazer manager `G = *(image+0x7bebec0)`: `+0x238` byte "timefall here" (the weather map at the sampled position
`+0x118`: on below -0.5, off above -0.3); registered BT areas `+0x320` (count) / `+0x328` (pointers), active ones
`+0x330` / `+0x338`, rebuilt every frame; live gazer actor ids `+0x78` / `+0x80`. An area object: position `+0x20`
(3 doubles), radius `+0x22c`, a byte `+0x29b`, its UUID at `+0x98`. A gazer actor (in the unit with vtable
0x3da58a8, index = id & 0x1fff minus the unit's first): locator `+0x18`, LOD `+0x24`, transform `+0x28`, flags
`+0x2e4` (bit 0 has an entity, 3 dead, 9 active), life `+0x2b0` (float). Catcher manager `C = *(image+0x7bee060)`:
streamed territories `+0x308` / `+0x310` (`DSCatcherTerritoryLocator`: UUID `+0x8`, encounter radius `+0x80`,
annihilation radius `+0x84`), catcher records `+0x88` / `+0x90` (stride 0x110, `+0xf0` byte 0 = active), cleared
spheres `+0xa8` / `+0xb0` (4 floats each). Hunter manager (inferred) `*(image+0x7becbd0)`, `+0x140` non-zero while
a hunter crowd is up. `*(image+0x7becbc0)` is the catcher jellyfish manager.

Live, Episode 3 save: 25 areas registered around Lake Knot City, none active in clear weather. In the area at
668,252 (radius 250): on arrival 74 gazer actors were live (each with a locator UUID) and one territory streamed
(encounter radius 230, annihilation 75); the byte `+0x29b` of that area was 1, of far ones 0. When the mod's shared
rain made the map wet there, `G+0x238` went to 1, the area was in the active list, and the ten gazers with an
entity (20 to 45 m away, life 300 to 1500) had flags 0x90a01 or 0xa0a01; the others 0x90200 and 0x10200 without an
entity, and 35 with bit 2 (0x10004, 0x1010004) not decoded. No catcher record, no cleared sphere, no hunter crowd.

### Creation callback with no function (2026-11-12, from two crash dumps)

`0x22bdee0(delegate)`: if `delegate+0x10` (a weak target, object = value - 8) is set it calls the function pointer at
`delegate+0x28` (this-adjust at `+0x30`) with the target, unchecked: `call rax` at `0x22bdf18`. It is reached through
`call [rax+8]` at `0x237c5c0`, in the completion of an entity creation (`0x237c5xx` <- `0x237e2b0`), on a worker
thread while the main thread waits (`0x1a0413b` <- `0x2400eb4` <- `0x23f5f98` <- `0x23ff9c5` <- `0x237e4a3` <-
`0x358b1f5`). Twice, with a partner connected and the porter arriving at a prepper's shelter with an order's cargo,
the function pointer was null. Which creation and whose callback is unknown.

### The partner's own body, live (2026-11-07)

`EntityResource_ExportedCreateEntity` (0x23806a0; the existing puppet probe's signature-checked call) **does** create
a second Sam when given the resident double's own resource (`double+58`, read with 0x2362ba0) instead of the
porter's: a new `DSPlayerAKukanEntity` (vtable 3d7de48) at the requested transform (readback exact), which also moved
exactly and was removed on request, with the porter unchanged and the game running. Unlike the parked double it
starts visible and awake (flags `0x232854453`). After parking it (SetVisible false, SetSleeping true) the double
backend took it like the resident double: shown, woken, its mover target set, the porter's animation network
borrowed, driven to the partner (memory readback equal to the partner's position), released and taken again. So
the partner can have a body that belongs to the mod, and the game's own double (private room, cutscenes) can be left
alone. Built only into experiment DLLs so far (`MOD_PUPPET_DOUBLE_RESOURCE`); the default build still borrows the
resident double. Open: one run showed only a small signal icon where the new body stood without being driven
(unexplained), switching bodies while the resident double is borrowed left the resident double standing (it must
be handed back first), and the body's lifetime across loads is untested.

Own-body mode (`puppet.double` command `own`, default build, 2026-11-07): the mod makes the body once per world,
parks it, and drives it with a second Double backend; the resident double is handed back (parked flags, resting
transform) and left to the game. Live: the body was made, taken and held exactly at the partner's position
(memory readback), with the resident double parked. **But only the body's Odradek renders, not Sam.** The made body
has 12 components where the resident double has 15: it lacks `DSFacialRigComponent` (vtable 3cf6050),
`DSCoverAndAnimModelComponent` (3cf5e80) and `DSCoverModelComponent` (3cf55d0), the visible model, and its art-parts
table is empty. On the resident double those three components have no resource pointer (+30 is 0), so the game
builds them in code, probably while it sets the double up for the private room. Finding that step (or adding the
three components another way) is what remains before the partner can use its own body. `peer` (the resident
double) stays the default.

Drawing the own body (2026-11-07, live, one copy, 90 s hang watch per variant):
- The body's `ArtPartsData` (entity+180, vtable 3cf2b18) has get-or-create accessors for the three model components:
  22b2010 (cover, cached at +60), 22b1f80 (cover+animation, +68) and 22b20a0 (facial rig, +70); each builds the
  component with the generic constructor (22b4fc0) and attaches it (2348210). Built this way they are empty (no
  model or resource pointers, unlike the double's). Cover, or cover and cover+animation: not drawn, stable. Any mask
  with the facial rig: drawn, then the game hangs within about 20 s (main thread waiting in ds.exe+1a0873b).
- The real difference: the double's `ArtPartsData` uses the porter's art-parts resource (+30 = the 52-part
  resource); a made body keeps the one-part resource of its entity resource. `ApplyArtPartsVariation` (22bc100) with
  the double's art-parts resource adds a second `ArtPartsData` from it, which builds all three model components
  properly: **the body is drawn and walks, with no hang over 90 s.** It shows stretched-polygon artifacts (long
  lines and a large flat triangle reaching away from the body) at a distance and while walking, with or without
  the borrowed animation network; standing 4 m from the camera it looked clean over 18 s (gear on its back, no
  stretched polygons).
  The per-part state bytes (+2c) equal the double's. Removing the body's original `ArtPartsData` afterwards
  (EntityComponent_ExportedRemove, 23618c0) hangs the game. `own` now uses the apply step.

The stretched polygons (2026-11-07, one run, parts switched one at a time with the research command
`puppet.cargo "part <index*2+on>"`, which calls the toggle worker 22b8fe0 only when part +2c bit 0, the enabled
request, differs): they appear while the body walks (also with no teleport, and not at distance as such) and come
from **part 20 of the porter's 52-part set**, a cable-like part whose far end is anchored to something the made body
lacks. Switching it off once after the body was shown removed the line for that run. Switching it off at
creation did not last (it was on again a few seconds after the body was shown), and enforcing it once a second while
the body was shown made the drawing much worse (very large flat polygons across the view, distant buildings gone).
Correction from two more runs: switched off once, 3 s after the body was shown, part 20 stayed off for 40 s and two
screenshots were clean; but with part 20 confirmed off from memory (+2c 0x0c) a later run still showed the long
line. The line is intermittent, so a three-screenshot search cannot pin it to one part; part 20 is at most part of
it. Not fixed. Next: detect the line from the game's state rather than screenshots (the model's bone or cable
positions), or count dark thin lines in many frames, before blaming a part.
A measured scan (fixed camera, changed pixels against a reference frame outside the porter, HUD and the partner's
path; clean frames score about 500 to 1000, frames with the line 4000 to 16000) failed for a different reason:
after switching parts 0-25 off and 0-19 back on with the toggle worker, the body drew as huge flat polygons across
the view. Switching parts back on this way corrupts the drawing, so part toggling cannot locate the line, and the
earlier one-part conclusions are void. The research command stays for reading states, not for fixes.
The artifact also appears while the body stands still (2026-11-07, n52: a large dark flat polygon from the hand
of the standing body). `Application_sExportedContinueLastSave` called during gameplay does nothing (it works from
the title screen), so a world-change test of the own body needs the pause menu. With `own`, the one-copy regression
passes (the body driven is the made one, 0.032 m from the partner, cargo and structures both ways).

Static lead (2026-11-07): `AddComponentFromResource` (234ae90; callers: ApplyArtPartsVariation 22bc111 and 234b025)
refuses when entity+88 bit 31 is set; with its third argument true it first removes the entity's components of the
resource's component type that were built from the same resource (resource+30 equal), then builds and attaches the
new one (22b5970, 2348220). It never removes a component built from another resource, which is why a made body ends
up with two `ArtPartsData` (its own one-part one and the porter's). The game's double, made from the same entity
resource, has only the porter's, so the game removes the original somewhere else; removing it right after the
apply froze the game, and so did removing it after the body was parked and asleep (before it was shown; live,
hang within 20 s). Next: find that removal (callers of 235b930 near the double's setup: the double's class is
referenced at 26f329e, 26fb1d4 (vtable) and 2711a80, 291160c, 2922d5b, 2cc37b3 (type)), or avoid the second
component altogether (replace the original's resource before the body is activated).
Switching the original one-part `ArtPartsData`'s part off once after the body was shown made no measurable
difference (15 scored frames before and after). Frame scoring with a fixed camera is confounded after about a minute:
an idle porter sits down to rest and the camera moves. Clean measurements need the porter kept standing (input).
The double's class code does not set its model: 26f3270 is its constructor, 26fb1c0 its destructor, and the other
type references (291160c, 2922d5b, 2cc37b3) are is-this-the-double checks followed by component lookups. The
porter's art parts reach the double from outside, most likely from the private-room or outfit system that copies
the porter's look; that system is the next place to look (callers of ApplyArtPartsVariation's registration
22ba011 from scripts, and the outfit/ArtPartsVariation resources it uses).
The double's 15 components by RTTI name: ArtPartsData, DSCoverModelComponent, DSCoverAndAnimModelComponent,
DSFacialRigComponent, DSFacialRigManagerComponent, DSTalkComponent, VoiceComponent, MorphemeAnimationManager,
SkinnedModel, FixedPositionMover, NodeGraphComponent, DSPlayerAKukanConfigComponent,
DSPlayerAKukanInventoryComponent, DSBaggage2CarrierComponent, DSCustomSoundComponent. No outfit component; the
outfit system's exports (`PlayerOutfitComponent_sExportedApplyPlayerOutfit`, `...SetPlayerOutfit`,
`...ApplyPlayerOutfitPart`) and the double's own `NodeGraphComponent` script are the candidates for how its look
is set at load. A made body has the same graph component but is not dressed.
`PlayerOutfitComponent_sExportedApplyPlayerOutfit` (34ef380 -> 34ef670) and `...SetPlayerOutfit` (34ef400 -> 34efaf0)
look up a `PlayerOutfitComponent` (type 4e07de0) on the given entity and do nothing without one; the double and a
made body have none, so these cannot dress them. Remaining candidate: the double's `NodeGraphComponent` script.


## Story progress and join-at-host research (2026-11-07, static only)

Pinned build 13300582, hash as above; opened `<game dir>/ds.exe` read-only. This
pass searched `.rdata` for Episode, GameProgress, StoryProgress, ChapterManager,
MainOrder, teleport and respawn names, followed name pointers and RIP-relative
references, then disassembled the relevant `.pdata` ranges. No game ran.

Observed static chain (all addresses are RVAs):

- `CurrentEpisodeHashFact` string `0x3eb7380` has a reflection field descriptor at
  `0x4dc3cf0`, encoding offset `+0x68`. `CurrentEpisodeIndexFact` at `0x3eb7478`
  has a descriptor at `0x4dc3e78`, encoding `+0xa0`. These are **fact resource
  references**, not direct integer values.
- Registration at `0x332935c` associates `DSMissionGoalsResource_sExportedSetEpisode`
  (`0x3eb7728`) with thunk `0x335e500`, which jumps to `0x335f3a0`.
  The latter obtains `goals = *(*( *(base+0x7c0b000) + 0x28) + 0x1f0)`.
  It searches the goals' episode pointer array (signed count `+0x40`, array
  `+0x48`) for a resource UUID at resource `+8/+0x10`, computes the two x86 CRC32
  instructions' CRC32C with seed zero and no final XOR, and writes that hash
  through the fact resource at goals `+0x68`, database `*(base+0x5794720)`,
  scope UUID at `base+0x4dd9600`, using `0x1afce50`. The optional index fact at
  goals `+0xa0` is cleared to -2; reading its resource bytes as an episode number
  would be wrong. ClearEpisode (`0x335f0f0..0x335f16b`) writes hash zero and
  clears the optional index similarly.
- The integer getter `0x1afc710..0x1afc7e8` looks up the scope in database `+0x20`
  with `0x1b038f0` (32-byte rows, nonzero hash at +0, UUID +8, values pointer +24).
  It looks up a fact resource's UUID at resource `+8` in values `+0x50` with
  `0x1b037e0` (24-byte rows, nonzero hash +0, UUID +4, integer +20). Table headers
  hold row pointer +0 and capacity +12. Hashes use CRC32C(UUID) OR `0x80000000`.
  Native lookup compares the full UUID as well as the hash. Its missing-value
  fallback is resource `+0x30`; the discovery reader deliberately returns unknown
  instead of treating that resource default as current save progress.

`join::ProgressReader` follows that read path without calling the setters or
getter. It uses the hash-validated discovery Sampler's guarded copies; validates
code prefixes, module data spans, pointers, power-of-two table capacities (max
4096), unique scope/fact rows, and an episode count 1..64; then rechecks every
copied span. It matches the current hash against the episode resource array and
reports the matching array index. A missing, duplicate, unreadable, torn, changed,
zero-cleared or unmatched value returns unknown. No story memory is written.
Fake-memory tests exercise the layout and refusal paths, with failing mutants.
Repeated guarded reads cannot establish allocation lifetime or atomicity.

**Unknown:** correspondence of this resource index to the episode number shown
in the UI, live table sizes/read availability, episode transitions and a main-order
or chapter ID. The protocol progress ID therefore stays unknown. An authorized
read-only live comparison against the save's displayed episode is required before
claiming numeric episode semantics. The hash is only a discovery diagnostic, not
a main-order ID and not replicated world state.

Teleport leads: `SequenceNetworkTeleportPlayerNode` at `0x3d406c0` references
metadata `0x4b79d20`; direct references include type checks at `0x24e182b` and a
metadata getter at `0x24ef570`. `GameMovie_ExportedPlayMovieAndTeleport` at
`0x3f176b0` registers thunk `0x3634f60`, which jumps to
`0x362b550..0x362b5d0`. That path checks a global and object state, copies a
60-byte transform into a queued movie operation, sets state +0x18 and +0x60,
and calls `0x324b5a0`/`0x325bf20`. It is not established as a standalone porter
teleport. The generic Entity SetWorldTransform path described above updates the
entity and marks its subtree; it does not prove that Sam's mover/controller,
physics, streaming or respawn state is reset. NPC teleport is also not evidence
for the local player's contract. **No safe local-porter relocation path was
established.** Join-at-host therefore produces a game-thread decision and a
`JOIN would move ...; native teleport unavailable; no move performed` log only.

(Superseded for the move itself by "Moving the porter" below.)

### Moving the porter: the game's own player teleport (static, 2026-11-08, nothing called)

Pinned build 13300582, `<game dir>/ds.exe` read-only; `.rdata` names, data pointers to them, `lea r8,[name]`
script-export registrations, `.pdata` bounds and direct call/RIP references, as above. No game ran.

Routes looked at, and why they were not chosen:

| Route | Where | Reading |
|---|---|---|
| Fast travel (script) | `DSMissionLogicCommand_sFastTravel` -> 0x33c9a10 -> 0x341f760 | Takes one integer. Queues a request (type 8, the integer at +4) in a 32-entry queue of the game-flow manager at global 0x7c0d6c0. A destination id, not a position; ids for a host's arbitrary spot do not exist. Loading screen and facts (`Fact_FastTravelDisabled`, `IsUsingFastTravelFact`) belong to this path. |
| Area change | `DSMissionLogicCommand_sChangeArea` -> 0x33cb320 | Same family; an area, not a position. |
| Player PlaceOn (script) | `Player_ExportedPlaceOn` -> 0x18e9b10 | `ret`: a stub in retail. |
| Spawn locator (debug) | `DSMissionLogicCommand_sDebugSetPlayerSpawnLocatorUUID` -> 0x18e9b10 | Stub. |
| Sequence restore | `DSPlayerSystem_sExportedPop/PushWorldTransform` -> 0x26e9940 / 0x26e9a60 | Push saves the porter's transform into the player system (global 0x7be7980, +0x202c0, flag +0x206d0 bit 0); Pop needs a pushed transform and porter byte +0x678 == 0, then writes it raw (0x22dff80) and sends `MsgEntityTeleported` itself. Only returns to a saved spot. |
| Movie and teleport | `GameMovie_ExportedPlayMovieAndTeleport` -> 0x362b550 | Queues a 60-byte transform behind a movie; needs a movie resource. |
| FloatAndMovePlayer | `DSPlayerSystem_sExportedFloatAndMovePlayer` -> 0x26e8cd0 | Posts a message (vtable 0x3d6f5c8) with a 16-byte float vector and four floats to the dispatcher at 0x7bbb8c0; meaning unknown. |
| Voidout return | `DSPlayerSystem_sExportedSetHighPrioReturnTransformAfterAnnihilationAttack` -> 0x26e7360 | Sets where a voidout returns the porter; needs a voidout. |
| Raw transform | `Entity_ExportedSetWorldTransform` (0x2361ea0 -> 0x22dff80) | Writes the transform only; no teleport message, so no physics or mover reset. |

**Chosen: PlayerGame's teleport virtual.** The world map's developer callbacks are a table at 0x4e7f0e0 (name,
signature, function): `FastTravelToCrosshair` -> 0x3663ea0 -> 0x3668a10, `FastTravelToMarker` -> 0x3668b20.
0x3668a10 returns false unless global byte 0x7c26e3c is set (a developer switch; off in retail), gets the local
Player through the manager at 0x7bc7568 (type 0x4b1acd0), copies its entity's 60-byte transform (entity+c8..+103),
replaces the position with the map cursor's three floats (cursor object +0x100, +0x80/+0x84/+0x88, widened to
double), and calls `player->vtable[+0xa8](player, &transform, 2, 0)`. No fade, no loading screen, no other check.

- Player class vtables (slot 0 is the type getter): `Player` 0x3d0f1c0 (getter 0x23c6120), `PlayerGame`
  0x3d5c860 (getter 0x262b920). Slot +0xa8: Player 0x23c6940 (entity teleport, mode != 0 -> placement flag),
  PlayerGame **0x2634f00**.
- 0x2634f00 (player, const WorldTransform*, int mode): returns if `player+0x70` (the entity) is null; sends
  `MsgDismount` (message vtable 0x3d03e48, type 0x4b07fe0) to the entity through 0x235c990; mode < 0 does
  nothing more; mode 0 or 1 calls the entity teleport with placement = (mode == 1) and sets dword player+0x16c = 0;
  mode 2 calls it with placement on and sets player+0x16c = 1. What reads player+0x16c is unknown.
- Entity teleport **0x235a010** (entity, const WorldTransform*, bool place, bool): with `place`, the transform goes
  through entity vtable slot +0x20 first. Then the rotation is re-orthonormalised (0x19c38d0), the transform written
  to entity+c8 under the entity lock at +0x288, flag bit 0 set at +0x88, and `MsgEntityTeleported` (message vtable
  0x3d03c68, type 0x4b03960) delivered to the entity's physics representation (entity+0x60, via 0x2376490 and the
  physics world at 0x7bc7df8) and to its message handlers (entity+0x2b0, 0x237c430). `Entity_ExportedPlaceOnWorldTransform`
  (0x2361c00) is the same call with the last argument zero. The Entity handler 0x2357520 (which sends
  `MsgEntityRepTeleported` and marks the subtree changed through 0x235bf60) is the kind of reset this message
  triggers: previous transform saved at +0x108, children walked, physics bodies moved.
- `DSPlayerEntity` vtable 0x3d7d0d8 (getter 0x2711b40), slot +0x20 **0x3737930**: ground placement. It casts a
  ray from 2 m above the target to 2 m below (constants 0x3bb8360 / 0x3d19e20, z up) in the physics world at
  0x578c140, up to 5 hits, ignoring the porter's own entity tree (it walks to the root through +0x70), and puts
  the porter on the hit. With no hit the target is used as given (branch at 0x3737ad2).
- Thread: the map callback runs in the game's menu update on the main thread; the mod calls from its verified
  game-thread callback. The entity write takes the entity lock; nothing here waits.
- Streaming: nothing in this path loads the world. Decima streams around the camera/porter on later frames, so at
  600 m the ground's collision may not be loaded yet when the porter arrives; whether he then falls, is held, or is
  caught by the game's fall recovery is **unknown**. The ground probe only reaches 2 m.
- Cargo and orders: the move carries the entity tree (carried cargo is attached to the porter); fast-travel facts
  are not consulted, because this is not fast travel. Whether a story order that forbids fast travel misbehaves
  after a move is unknown; the live check uses an ordinary early save.

`join::Mover` (join_move `native`) calls 0x2634f00 with mode 2 and the porter's own rotation, as the map callback
does, once per session, only in normal gameplay, on foot (`vehicle::Reader`), with a target inside a sanity box,
after checking the three code prefixes (40 bytes each, unique in the code), the call from 0x2634f7f to 0x235a010,
both vtables' type getters and slots, and the live objects' vtables. It reads the porter back (same player and
entity, within 5 m of the target) and checks again 3 s later that he has not dropped more than 5 m. It never retries.

### Dressing the own body: the engine's model swap (static, 2026-11-07, nothing called)

Read from `<game dir>/ds.exe` with the PE and name-reference method (objdump, `.pdata` bounds, rel32 call and
RIP-relative scans, Decima type descriptors). Answers to the four questions about the own body's drawing, in order.

**Where a made body's art parts come from.** The entity initialiser 234f3e0 builds one component per component
resource in the entity resource (generic constructor 22b4fc0, attach 2348220) and caches the first `Model` at
entity+b8 and the `ArtPartsData` (type 4adfe00) at entity+180 (234fb34; a default one at 234fe24 when there is none).
So a body made from `ds_player_akukan_entity` wears that resource's own one-part `ArtPartsDataResource`. The double
wears the 52-part one, so the game changes the double after it is made.

**The engine's model swap, SetModel 235e010** `(Entity*, ModelResource*, float blend)`, not virtual, 13 game callers.
`ModelResource` (type 4b13080, size 0x58) has `ArtPartsDataResource` at +30, `ModelPartResources` +20, `Helpers` +40.
Every component keeps its resource at +30 and its owner at +48. The sequence:
1. build the new model component from the resource (22b4fc0, with the resource's virtual +20/+28 type getters);
2. take the entity lock (entity+288; 3b3a578 try, then 3b3a588);
3. if entity+180 is set: calls that read as taking the entity out of its world lists (2376490 on entity+60's +208
   and 23d4660 on the global 7bc7df8; 237c430 on entity+2b0; names unknown), then remove the old `ArtPartsData`
   (235b930) and clear +180;
4. if the entity is active (flags +88 bit 14) call its virtual +f0 with 0; unregister the old model (237bdf0 on +2b0);
5. store and attach the new model (+b8, 2348220); if `ModelResource+30` is set, build an `ArtPartsData` from it
   (22b5970), attach it, cache it at +180 and send two messages (235c990, message types 3d03bc0 and 3d03f00);
6. 2360400(entity); with blend > 0 the new model takes the old pose (model virtual +c8); model virtual +60;
7. virtual +f0 with 1 when it was active; register again (236e260, 2376490, 237c430); 235bf60 (transform changed);
8. release the lock, then remove the old model component (235b930).

Game code calls it the way the mod would: 2cfb570 checks a resource is a `ModelResource` (type 4b13080), compares it
with the owner's model resource (owner+b8, +30) and, if they differ, calls `235e010(owner, resource, 0.0)`.

**Why removing the one-part `ArtPartsData` hangs.** `EntityComponent_ExportedRemove` (23618c0) is a jump to
235b930(owner, component), the same removal SetModel uses, but without steps 2-4 and 7: the entity stays in its world
lists with the removed component still cached at +180. The hang site 1a0873b is in 1a086a0, the job system's counter
wait: it spins (pause, then a yield 1a04200 after 50 rounds) running other jobs (1a07fa0) until a job counter reaches
zero. The other addresses on the hung main thread fit that: 363ef34 is the return from 1cd4890 (global 578c148) in the
large per-frame function 363eb40, and 3649124 follows a release of a job handle (refcount +28, then 1a08060) in
3649060. Reading, unverified: a frame job still uses the removed component (or the stale +180) and never finishes, so
the main thread waits forever. The engine's own removal of a primary `ArtPartsData` is SetModel.
`AddComponentFromResource` (234ae90) in replace mode also uses the plain 235b930. That removal is used only for
same-resource variation components, which are not at +180.

**Who dresses the resident double.** Not found in this pass; found on 2026-11-08 (next section: the porter's
`DSPlayerStreamingControlComponent` with the in-place replacement 235d320). `ApplyArtPartsVariation` (22bc100) has no direct callers (script
only); the art-parts apply (22bae20) is called from 22bb6c0, 32e1730 and the porter's update 2cc2ea0 (a component of
type 4ae3af0 on the porter). None of the six code references to the double's type (2711a83, 29115fc/291160f,
2922d49/2922d5e, 2cc37a3/2cc37b6) dresses it. Of SetModel's 13 callers, 2cb7f10 is the cargo manager (entities with a
pending-model byte at +33c), 2bd54d0 sets a model right after initialising an entity (234f3e0), and 37b7490, 2cfb570 and
2e38130 set the model of a component's owner (component+48). The rest were not read. The porter's
`DSPlayerStreamingControlComponent` (vtable 3d815d8, resource type 4bd0560: `DefaultBodyModelResource` +20,
`DefaultShoesModelResource` +28, `DefaultArtPartsVariations` +30, `Variants` +48 of `DSPlayerBodyVariant` (type
4bd03e0: `SkinnedModelResource` +38, `BootsSkinnedModelResource` +40, `ArtPartsVariations` +48, `ShoesArtPartsVariation`
+58)) is the porter's body-model switch and the best candidate for also dressing the double. Whether it does is
unknown. The mod can replay the result without finding the caller. The double's own model component (double+b8)
holds the `ModelResource` it was given, so SetModel with that resource should rebuild the made body like the double.

**Part 20.** Part names are not in the executable. The art-parts resource's name map (`ModelNameHandleMap`, resource
+e0, 12-byte entries {used, CRC32C name hash, part index}, capacity at +ec) maps names to indices. The reverse lookup
is 22b6a00(resource, index) -> hash, and 22b69b0 is the forward lookup used by `EnableModelPartFromHash` (22b5090).
Part 20's name, and what it is anchored to, are unknown. Mask 0x40 logs its hash; match it against CRC32C of the
archive's part names.

**A body built dressed (question 4).** SetModel is that path: called right after `EntityResource_ExportedCreateEntity`
with the double's `ModelResource`, the body would get the double's model and one `ArtPartsData` built from its 52-part
resource, with the one-part one removed the engine's way. A different entity resource is not needed.

Research mask bits (`puppet.double` command `own<hex> <stamp>`, one or two lowercase hex digits; default `own` = 8
is unchanged). The body is made once per world, so send the command before the first `own` of a game process.
Host-tested in `double_test.cpp` (`modelSwap`, `bodyReport`, `ownCommand`). Mutants that must fail:
`MUTANT_OWNBODY_{OWN_PARSE,MODEL_OWNER,MODEL_EXACT,DRESSED_COUNT,APPLY_ALWAYS}`.
- `0x10`, ranked first: SetModel(body, double's `ModelResource`, 0) before parking, only when the double's model is
  its own and its `ModelResource+30` is exactly the double's art-parts resource. Success leaves one `ArtPartsData` of
  the body's own at +180, built from the 52-part resource (checked by reading).
- `0x20`: the same swap without the exact condition (the model must still build some art parts), then the variation
  apply only if the body is still not dressed like the double. The apply is never repeated with the same resource:
  its replace mode would remove the swap's component with the plain removal.
- `0x40`, read-only: after dressing, one log row per component of the made body and the resident double (vtable as an
  image offset, owner, resource), the primary `ArtPartsData`, each `ArtPartsData`'s part count and part 20's state
  byte and name hash. Combine it with the other bits (`50`, `60`, `48`).

### Who dresses the resident double: the porter's streaming control (static, 2026-11-08, not called yet)

Live facts this starts from (2026-11-08): the resident double has exactly one `ArtPartsData` (vtable 3cf2b18), whose
resource has 52 parts (part 20 state 0xf, name hash 0x5acbbf14), but its own model's `ModelResource` (component vtable
3d0adb8) builds a different, one-part art-parts resource. So the game replaces the double's primary `ArtPartsData` in
place after the double is made. With mask 0x60, SetModel ran safely but also left two `ArtPartsData` (the model builds
the one-part one, then the apply adds the 52-part one).

Read from `<game dir>/ds.exe` with the PE and name-reference method (full objdump, `.pdata` bounds, rel32 call index,
Decima type descriptors and their names at descriptor+0x40).

**The only code that rebuilds an entity's primary `ArtPartsData` from a resource in place** is 235d320
`(Entity*, ArtPartsDataResource*, bool force)`. The builder of an `ArtPartsData` from a resource, 22b5970, has six
callers: the entity initialiser 234f3e0, SetModel 235e010, `AddComponentFromResource` 234ae90, a variation-group
builder 2353a33 (not +180), a message handler 2352700 (attach only, not +180) and 235d320. 235d320, in order:
1. returns at once when entity+180 is set, its resource (+30) is the given one and `force` is false;
2. announces `MsgArtPartsWillChange` (message vtable 3d03f18, type 4b0ae60) to the scene (2376490 on entity+60 +208,
   then 23d4660 on global 7bc7df8) and to the entity's own listeners (237c430 on entity+2b0), as SetModel step 3 does;
3. under the entity lock (entity+288; skipped while the byte at 7bc4e7c is set, which 36448b0 clears after the
   frame's scene jobs): **22b4310(old ArtPartsData)**, then the plain removal 235b930, then clears +180;
4. builds the new one (22b5970), attaches it (2348220), caches it at +180;
5. announces `MsgModelPartsChanged` (3d03bc0, type 4b03f60) and `MsgArtPartsChanged` (3d03f00, type 4b0ada0) with the
   new component, the same two messages SetModel sends (its 235c990 calls).
It does not deactivate the entity, unregister or replace the model, or mark the transform changed (SetModel's steps 4,
6 and 7). 22b4310 is the step a plain `EntityComponent_ExportedRemove` lacks: for the entity's model (owner+b8) it opens
an update (2391620(model, 1)), takes every part of the old `ArtPartsData` off the model (each part's mesh from part+20
+38 +48, or the resource's +20 +48 for an empty slot: 23948f0(model, mesh), or 23948a0 when there is none; parts whose
virtual +60 is non-zero are skipped) and closes the update (23966a0). Reading, unverified: the hang after removing the
one-part `ArtPartsData` came from the model still holding parts of a component that no longer exists.

**The caller.** 235d320 has exactly three call sites, all in 27caae0 (chunk 27cae90), the update of
`DSPlayerStreamingControlComponent` (type 4bcf890, size 0xb0; the porter keeps it at porter+54f8, filled at 271fc90 by
the component lookup 22b2a60, and the porter's update calls it at 26d64aa). It is a small state machine on
component+70:
- state 0: when the wanted body variant (component+76; 8 means "ask" 298d7e0) differs from the current one (+74), it
  picks `Variants[n]` of its resource (component+30, `DSPlayerStreamingControlComponentResource` 4bd0560, array
  data at resource+50) into the reference at +80 and asks the streaming manager (global 7ae90e0, virtuals +28 and +38)
  to load it; state 1;
- state 1: waits for the loaded flag (+a1 bit 0), then state 2;
- state 2 (27cae74): with the loaded `DSPlayerBodyVariant` (type 4bd03e0: `SkinnedModelResource` +38,
  `BootsSkinnedModelResource` +40, `ShoesArtPartsVariation` +58, `ReflectShoesArtPartsVariation` +60):
  `235d320(porter, variant+38 -> +30, true)`, then **`235d320(resident double, variant+38 -> +30, true)`**, then
  `235d320(owner of the component at (porter+54c8)+4d88, variant+40 -> +30, true)` (the boots) and stores +58/+60
  in that component; it tells a manager at global 7bf6e78 (name unknown; 2dcdae0, 2dcd360, 2dcd770 read entity+180's
  resource) about the old and new resources of the porter only, releases the previous variant and keeps the new one
  at +78; state 3 with a 1.0 timer at +98;
- state 3: counts the timer down and returns to 0.
The second entity is `269c560(global 7be79b0)`: the global's reference at +30, minus 0x20 (a reference points at
entity+20). Read statically as the resident double, three ways: 273bb10, which 2757d40 calls with it, uses its
resource's array at +118/+120, where `DSPlayerAKukanEntityResource` (4bca0e0) keeps `SentenceResourceContainers`;
273bb10 locks entity+790, inside the 0x7a0-byte `DSPlayerAKukanEntity`; and 26c7760 skips its work while that
entity's flags (reference+68 = entity+88) have bit 8, sleeping, set, as the parked double does. The global is the
object `DSPlayerEntity_sExportedHasBaggage` also reads (above). No writer of the global's +30 reference was found by the
direct-access scan. The 0x40 report now logs `BODY report streaming double <address> (the double)` when this entity is
the double the mod found, which settles it live.
So the game dresses its double exactly like the porter, whenever the porter's body variant streams in (at load and
at every outfit variant change), and a body the mod makes is not on that list.

**Replaying it: research mask bit 0x80** (`puppet.double` command `own80 <stamp>`, or `ownc0 <stamp>` with the
report). After the body is made and before it is parked, the mod reads the resident double's primary `ArtPartsData`
resource and calls `235d320(body, that resource, true)` once, the game's own sequence. Refused, with nothing called,
unless the body has exactly one `ArtPartsData` of its own and it is the primary one, the double's primary one is its
own, and the body does not wear that resource already; refused too when the function's first 48 bytes differ. Success
is checked by reading: the body then has one `ArtPartsData`, cached at +180, built from the double's resource. 0x80
runs alone: with it, bits 1, 2, 8, 0x10 and 0x20 are ignored (0x40 still reports). Host test `inPlaceSwap` in
`double_test.cpp`; mutants that must fail: `MUTANT_OWNBODY_INPLACE_{SINGLE,RESOURCE,SAME}` (accept a body with a second
`ArtPartsData`, take the art parts the double's model builds instead of those it wears, repeat on a dressed body) and
`MUTANT_OWNBODY_STREAMING_REF` (the report's reading of the global without the 0x20 adjustment).
Not replayed: the boots entity and the 7bf6e78 manager calls, which the game makes for the porter only. Unknown: whether
the made body's cover, cover+animation and facial-rig components appear after this swap (the apply built them; the
attach here is the same 2348220), and whether a later outfit change should be replayed on the body too.

**What the hang at 1a0873b waits for.** 1a086a0 is the job system's wait for a counter to reach zero (it runs other
jobs, 1a07fa0, while it waits). On the hung main thread it is called from 1cd4890, whose profiling label is the string
`RenderSystem::FinishDraw` (3c02398): it waits on the counter at (render system, global 578c148)+e0 -> +5ba280, with
the job system at 577b780. That counter is raised when 36448b0 submits the frame's jobs (1a06860 with the counter) and
lowered by them (lock xadd -1). So the main thread waits for the frame's draw jobs, and one of them never finished.
Which job, and why, is unknown; a draw job touching the parts of the removed `ArtPartsData` fits the 22b4310 reading
above.

## Vehicles

### Riding, live (2026-11-08, Episode 3 save, a truck in the Lake Knot garage)

The static ride-component path below is **refuted** for driving a truck: the seated porter has no
`DSRideVehicleMovementComponent` at all. What changes instead (read from the process, on foot vs seated):

| Field | On foot | Seated in the truck |
|---|---|---|
| porter `+0x70` (parent entity) | null | the `DSVehicleTruck` |
| vehicle `+0x78` (first child; siblings at `+0x80`) | not the porter | the porter |
| `DSPlayerMover` (porter `+0xb0`) `+0x880` / `+0x898` | not the truck | truck `+0x20` |
| `DSPlayerPadOperatorComponent` `+0x58` / `+0x60` | not the truck | the truck |
| `DSPlayerParamComponent` `+0xad8` | not the truck | truck `+0x20` |

Vehicle `+0x4c4` read 2 on all three trucks and 3 on the motorbike, as the constructors write. The seated porter's
world position follows the truck (it is a child); porter minus truck was (-0.80, -0.44, +1.95), z up. `vehicle.cpp`
now reads riding as: the porter's parent is a type-checked bike, truck or speed car, its kind field agrees, and its
child list contains the porter. Presence carries the vehicle block (kind, offset from the rider, yaw/pitch/roll);
two copies live: the receiver logged kind 2, a 2.1 m offset and the truck's 15 degree pitch on the exit ramp.

### The partner's vehicle, live (2026-11-08)

`ride.cpp` makes a display vehicle with the pooled creator wrapper `0x2f532c0(pool, &word, &WorldTransform, float)`
(word: unit type 2 truck / 3 motorbike in the low four bits, variant 0; float 0), from a mod-owned pool laid out like
the game's (`0x2f5af30`: entities `+0`, sources `+0x20`, four `0x50` records at `+0x40` built with `0x2f51e90`), and
removes it with `0x2b7b9e0(pool, entity)`. The resource comes from the vehicle unit table (below), so no vehicle needs
to be nearby. Each tick it calls `0x2f801b0(VehicleMover, &WorldTransform)` (mode 1) on the vehicle's own
`VehicleMover` (found among its components). Live, one copy: a truck and the reverse trike were drawn and followed
an 8 m lead target; changing kind removed one and made the other; removal left nothing; no fault recorded. With the
stand-in partner riding a bike, the bike appeared under it, stayed parked when it got off, and Sam standing beside it
was offered no prompt (no "Use Vehicle"); the vehicle was removed when the partner left. The created entity's
`+0x560` read `0x08114002` (the creator's `0x08004000` bits set).

Resolved live (read-only): `*(0x7bfecc0)` -> `+0x38` `DSVehicleTruckGameActorUnit`, `+0x40`
`DSVehicleMotorbikeGameActorUnit`; unit `+0x80` -> `...GameActorUnitResource`; its `+0x38` reference -> `*ref` ->
`+0x20` = `GameActorSpecificData`; its `+0x38` = `DSVehicleTruckResource` / `DSVehicleMotorbikeResource`. The
reference's loaded flag is bit 59 of the referenced object's `+8` (resolver `0x1dde990`).

The partner's body on the vehicle: setting `Is_Vehicle`, `Vehicle_Type`, `Is_Actions` and `Is_Landed` on the body's
manager left it standing (live, twice). Two changes from "Vehicle animation entry" below made it sit (live, the stand-in
on a bike: legs bent, hands on the handlebars; standing again after getting off): the handles are resolved while the
manager resource's `+0xd8` points at the definition of the instance the body runs (`*(manager+0xa0)+0x38`, restored at
once), and on getting on `Action_Rewind`, `Action_Rewind_NoInter` and `Is_Force_Upper_Reset` are pulsed once for 0.1 s
with `AnimationManager_ExportedTriggerBoolAnimationVariable` (`0x2397310`), with `Vehicle_Seat` 0.

### Static ride-component path (2026-11-07; refuted live for the truck, kept for reference)

Scope: the pinned build/hash at the top of this file, opened read-only. The old
37-match vehicle type-check window remains rejected: it is common Decima ancestry
code, not a unique riding signature. This pass instead followed the exact export
name references, `.pdata` ranges, compound descriptors and slot-zero type getters,
then independently disassembled the relevant functions. Reproduce focused reads with
`x86_64-w64-mingw32-objdump -d -M intel --start-address=<base+RVA> --stop-address=<base+end RVA> "<game dir>/ds.exe"`.
Addresses below are RVAs, with image base `0x140000000`. No bytes, resources or
other game content are distributed here.

### Entity and resource identities

The compound-descriptor method is the same as for the own body above: standalone
name pointer at descriptor `+0x40`, kind 4 header, base descriptors at `+0x60`.
Sizes and inheritance are metadata evidence, not a live allocation inventory.

| Class | Descriptor | Size | Base | Primary table / slot-zero type getter |
|---|---:|---:|---|---|
| `VehicleEntity` | `0x4ca59a0` | `0xbe0` | `Entity` | `0x3e25580` / `0x2f6bda0` |
| `DSVehicleMotorbike` | `0x4ca65a0` | `0xdb0` | `VehicleEntity` | `0x3e25db8` / `0x2f6bb60` |
| `DSVehicleTruck` | `0x4ca6d60` | `0x1120` | `VehicleEntity` | `0x3e26790` / `0x2f6bcb0` |
| `DSVehicleSpeedCar` | `0x4ca7660` | `0xd20` | `VehicleEntity` | `0x3e274d8` / `0x2f6bc10` |
| `DSRideVehicleMovementComponent` | `0x4c6b160` | `0x330` | `DSMoverSupportComponent` | `0x3dfe930` / `0x2dbbe70` |
| `VehicleMover` | `0x4ca7ed0` | `0x170` | `PhysicsMover` | `0x3e28010` / `0x2f6be60` |

The getters are `lea rax,[rip+descriptor]; ret`, not engine calls made by the
provider. `DSPlayerEntity` is checked too: table `0x3d7d0d8`, getter `0x2711b40`,
descriptor `0x4bc6e00`. Constructors independently select the bike/truck tables
at `0x2f61b9c` / `0x2f62470` and write vehicle `+0x4c4 = 3` / `2` at
`0x2f61cfc` / `0x2f62571`. The speed car constructor writes 1 at `0x2f62201`.
The provider supports bike/truck only; base vehicles, speed cars and subclasses
with other tables are **unsupported**, never silently treated as a bike.

| Resource | Descriptor / size | Base | Primary table / getter | Entity selector (slot `+0x68`) |
|---|---|---|---|---|
| `VehicleEntityResource` | `0x4ca5ca0` / `0x280` | `EntityResource` | not needed by the provider | not used |
| `DSVehicleMotorbikeResource` | `0x4ca6420` / `0x330` | `VehicleEntityResource` | `0x3e246b8` / `0x2f5dba0` | `0x2f5dbb0` loads `0x4ca3508`, whose file pointer is the motorbike descriptor |
| `DSVehicleTruckResource` | `0x4ca6be0` / `0x310` | `VehicleEntityResource` | `0x3e266b8` / `0x2f6bd30` | `0x2f6b050` loads `0x4ca1568`, whose file pointer is the truck descriptor |
| `VehicleMoverResource` | `0x4ca7e10` / `0x58` | `PhysicsMoverResource` | not needed by the provider | not used |
| `DSRideVehicleMovementComponentResource` | `0x4c6ac00` / `0x58` | `DSMoverSupportComponentResource` | not needed by the provider | not used |

`VehicleEntityResource` has static `TopSpeed` at resource `+0x11c`, battery at
`+0x130`, and inherited `EntityComponentResources` at `+0x90`; these are resource
settings, **not instantaneous vehicle speed**. The motorbike resource names
front-left, front-right and back wheels at `+0x2e8/+0x2ec/+0x2f0`. This is a lead
for the reverse trike, not proof of a particular loaded variant. No separate
compound descriptor exactly named `Vehicle`, `Bike`, `Motorbike`, `Truck`,
`Trike` or `Ride` was found by this method; do not infer extra classes from the
earlier `Vehicle` string alone. Resource instances/variants loaded by a later
save remain unknown.

### Porter -> ride component -> vehicle and seat

The local root stays the reviewed manager -> selected Player -> `Player+0x70`
entity chain. Entity components have count at `+0x90` and pointer array at
`+0x98`. Static callers at `0x2af2b44..0x2af2b64` and
`0x2b5f48f..0x2b5f4ab` ask the component lookup `0x22b2a60` for the
`DSRideVehicleMovementComponent` descriptor. `0x22b2a60` looks up the associated
sorted type IDs and returns the component pointer; the provider instead walks
the bounded pointer array and checks the exact table/getter pair.

The following are observed machine operations; riding semantics still require
the live action sequence below:

| Location | Observation and inference |
|---|---|
| Component `+0x48` | Owner entity; `0x2dbbedc` uses it to read the porter's transform. Require it to equal the local porter. |
| Component byte `+0x60`, bit 0 | Active predicate `0x29aa8c0`; `0x2dbbe90` compares that predicate with requested activation. `0x2dae6b0` checks/changes the owner's active mover at entity `+0xb0`, then updates the bit. Require active bit and `entity+0xb0 == component` to agree. |
| Component `+0x318` | Pointer to seat/ride state. Activation `0x2dc2696` reads it and `state+0xa68`. Exact state class and how this pointer is assigned remain unknown. |
| State `+0xa68` | Vehicle reference, stored as vehicle primary address **plus `0x20`**. `0x2dc26a3..0x2dc26e4` converts it to/from the primary address and stores the reference in component `+0xe0`. The provider requires both references to agree and subtracts `0x20` once. |
| State dword `+0` | Helper/seat selector: `0x2dc2706`, dispatch `0x2dc2761..0x2dc279b`; also `0x2dc27b7` and `0x2dc27da`. Selector 0 chooses `HLP_Driver`; 1..4 choose `HLP_Enemy_3`, `HLP_Enemy`, `HLP_Enemy_4`, `HLP_Enemy_2` respectively (`0x2dc8520` and its string initialization). Reading: driver vs numbered passenger helpers; physical seat positions and local passenger support unknown. Other values stay unknown. |
| Vehicle `+0xc8` / `+0xe0` | Inherited Entity world position (3 f64) / rotation (9 f32). The common entity factory itself copies the 60-byte world transform to these offsets at `0x23739b6..0x23739eb`. This is the vehicle transform, not the seated porter's transform or a bone. |

The generic `Entity_ExportedIsMounted` is a different predicate: registration
at `0x2332e31..0x2332e59` points at `0x2362cf0`, which calls `0x2381780`.
That function checks for `MounterComponent` (`0x4b40da0`) through the entity's
component list. Its `+0x50` references `MountableComponent` (`0x4b409f0`), whose
owner is `+0x48`; reflection also names its `Mounter` at `+0x58`. This generic
mount path has not been established as the DS bike path, so the provider does
not substitute it when the DS ride component is absent.

`VehicleEntity_sExportedIsDriving` registration `0x2f5e337..0x2f5e35f` selects
wrapper `0x2fa68c0` -> implementation `0x2fa7450..0x2fa7514`:
mode true returns `vehicle+0x4cc` not in {0,5,6}; mode false requires
`+0x63c != 0`, `+0x528 != null`, and `+0x4cc == 2`. This says nothing about
**which porter** occupies the vehicle. In particular, `+0x528` is not a proven
driver entity pointer: `0x2f66450` reads its `+8` as float control input. The
provider neither calls this export nor equates its result with local riding.

### Speed, creation and parking/driving proposal

The existing Entity velocity path (`0x234c180`, mover `+0xb0`, virtual `+0xc8`,
fallback model `+0xb8` / virtual `+0x130`) still requires engine calls; a stable
vehicle-physics velocity storage offset was **not established**. The read-only
provider therefore reports a separately marked finite-difference speed from
successive vehicle positions and monotonic sample times, including vertical
motion. First sample, identity/seat change, unavailable reads, 10 ms minimum /
500 ms maximum interval, clock reversal, and discontinuities above 200 world
units/s invalidate the estimate. It is never clamped to the 20 m/s walking
provider limit or substituted with resource `TopSpeed`.

Static creation route: both concrete vehicle-resource tables have factory slot
`+0x80 = 0x2366570`, which jumps to `0x2373940`. At `0x2373973` that factory
calls the resource's entity selector (`+0x68`), allocates through `0x1a8eee0`,
sets the transform, and initializes using the supplied resource. The existing
script creator `EntityResource_ExportedCreateEntity` (`0x23806a0`) forwards to
`0x2374400`, which calls precisely this resource factory. Thus a **correctly
loaded and retained concrete vehicle resource reaches the matching vehicle
allocation path through the same creator as the own body**. This is static
dispatch evidence, not a successful or approved spawn: activation dependencies,
physics/world registration, save/game-actor ownership, resource lifetime and
cleanup remain unknown. Nothing was created or removed in this pass.

Vehicles have a `VehicleMover : PhysicsMover`, not the double's
`FixedPositionMover`. As corroboration, the game's vehicle-creation path
`0x2f534e0..0x2f5360e` looks up `VehicleMover` on a created vehicle at
`0x2f53570`, sets mover dword `+0x128 = 2` at `0x2f53598`, then adds it to the
world. The mover constructor `0x2f5da60..0x2f5da99` starts this mode at 0.
A further static trace identifies its transform-playback path:

| Mover operation | Evidence |
|---|---|
| Set a target transform | `0x2f801b0..0x2f801f5`: takes mover in RCX and a 60-byte world transform in RDX, sets dword `+0x128 = 1`, copies position to `+0x130..+0x147` and rotation to `+0x148..+0x16b`. |
| Update transform | Primary mover table slot `+0x130` points at `0x2f899d0`. Mode 0 tail-calls normal PhysicsMover path `0x23b7060`; mode 1 tail-calls `0x22e97f0(mover, mover+0x130)`; other modes, including 2, return without this update. |
| Apply target to owner | `0x22e97f0..0x22e98ef`: compares the full pose with owner `*(mover+0x48)`, locks owner `+0x288`, writes position at `+0xc8` and rotation at `+0xe0`, marks owner flag bit 0, and unlocks. |
| Existing native caller | `0x2fa5a1f..0x2fa5a2d` loads `vehicle+0xb0`, passes `vehicle+0x8f8` as the transform, and calls `0x2f801b0`. This independently connects the setter to the vehicle's active mover, not an assumed component offset. |

**Inference:** mode 1 supplies a candidate for parking at a fixed world pose
or playing back successive partner transforms, analogous to the own body's
mover target, with a different layout and setter. Mode 2 suppresses this
transform callback only; its complete purpose and effect on simulation are
unknown, so it is not a proven parking flag. `VehicleMover` velocity slot
`+0xc8` is `0x23b3c90`, which obtains a physics-related object through the
owner and calls its virtual `+0x130`; it does not expose a direct velocity
storage field for read-only copying.

A future partner experiment would first inventory the occupied vehicle's
resource and mover read-only; then, separately authorized, create from a retained
vehicle resource, prove ownership/removal, and test a stationary mode-1 target
with independent entity **and physics-body** readbacks before attempting moving
playback. This is still not a complete park/drive contract. Neither writing the
double's mover layout into a VehicleMover nor driving via guessed input offsets
is justified. Passenger attachment/animation, wheel animation, collision,
steering, and preventing the vehicle's own simulation from fighting playback
remain open. No vehicle write API or config option is added.

### Provider and live evidence still needed

`vehicle.cpp` uses the hash/signature-validated `discovery::Sampler`, exact
loaded table/getter bytes, local-human/owner checks, a maximum of 256 components,
unique ride-component membership, matching activation and vehicle references,
finite proper world rotation, and post-copy dependency/selection rechecks. A
missing/unsupported component or vehicle yields unavailable, never a fabricated
on-foot result. Inactive components do not dereference cached vehicle/seat
references. This is bounded guarded copying; stable freed memory or ABA reuse
can still evade it. All interpretations remain provisional, even on available
rows. It does not alter the presence wire or partner body.

Host fake-memory oracles and independent mutants are in `vehicle_test.cpp` and
`test_vehicle_mutants.py` (`make -C mod vehicle-test`, also in `make test`).
They establish guard behavior only, not that this save contains a bike, that
mounting uses this component in the pinned runtime, or that passenger selectors
are available to the player.

The stamp-guarded `puppet.vehicle` commands and exact 30-second stand/mount/ride/
brake/dismount sequence are documented in `mod/README.md`, "Live test commands".
One copy is sufficient. Record the process's `VEHICLE` rows (`t_us`, riding,
kind, seat, vehicle address, full transform, speed and refusal reason) and
independent read-only memory/action evidence. Expected: on foot -> bike driver
at rest -> moving bike with positive derived speed -> on foot. Transitions may
briefly be unavailable; sustained refusal or a mismatch with the observed
vehicle refutes the candidate. Repeat for a truck and passenger seat; resource
variants, reload/death, despawn/address reuse and scene changes need separate
validation. No such live experiment ran for this provider.


## Map markers (static, 2026-11-08, nothing called)

Method as above: name strings in `.rdata`, their absolute pointers in reflection tables, RIP-relative references, `.pdata`
bounds, direct `call` cross-references. Throwaway scripts only.

- The names a search would try first are dead ends. `SetPlayerPlacedMarker` and `ClearPlayerPlacedMarker` are script
  bindings of a map class (`FastTravelToCrosshair`, `SwitchToLowerFloor`...) whose implementation is `jmp 0x18e9b10`, the
  retail `ret` stub. `AddUserMarker`, `RemoveUserMarker`, `RemoveAllUserMarker` and `AddUserMarkerLine` are fields
  (+0x28..+0x40) of a map-menu command-text resource, not functions. `DSUINodeGraphBindings_sDisableMarkerLocator` is a
  `ret` stub; `EnableMarkerLocator` (0x3054910) needs a marker locator placed in the level data.
- `EDSMarkerType` (enum table 0x3e1c100, 35 values): None 0, Player 1, Baggage 2, Structure 3, Mule 4, Gimmic 5,
  **UserMarker 6**, MuleLastKnown 7, MarkerLocator 8, Destination 9, Rope 10, Signboard 11, ...
- The marker manager: global `ds.exe+0x7bfd940` (read by the `EnableMarkerLocator` export), built by 0x2f1e7d0. It holds
  marker pointers at +0 (count +0x5dc0), per-type first index at +0x5dc4+4*type and count at +0x5e4c+4*type (0x2f28e80
  searches one type within a radius, at most 25), a dirty mask at +0x5f08 and the id counter at +0x5f00. The constructor
  gives type 6 a fixed pool of **50 records** (`+0x5e64 = 0x32`), allocated as one block at **+0x5ef8**, stride 0x50.
- User-marker record (0x50 bytes): +0x00 vtable, +0x10 position 3 x f32 (game axes, z up) + pad, +0x20 id (u64, the
  counter at manager+0x5f00, new on every set), +0x28 flags, +0x2c bit 0 in use, +0x2d type (6), +0x30/+0x38 type and id
  of another marker it is attached to (the per-frame update removes it when that one goes), +0x40 reached, +0x41/+0x42
  snap to ground (a `DSUserMarkerCollisionJob` ray, manager+0x5fe0, then corrects z), +0x43 flag, +0x44 a timer,
  **+0x4c group (0..4), +0x4d point in the group (0..9)**. Five groups of ten: a group is one pin or a route (lines
  between its points; `AddUserMarkerLine`).
- Set: **0x2f22d00**(manager, `const float pos[4]*`, group, point, snap, flag) replaces that point (the old record is
  switched off through its vtable+8), refuses when another point of the same group lies at the same x and y, then
  writes the record. The map menu calls the wrapper **0x2f22c90** with the same arguments, which adds a u16 from
  0x262a860(*0x7be6318) (a floor or layer value; meaning unknown). Map-menu flow (0x31c30a0..0x31c3300): group =
  0x2f29320(manager) (the first empty group, else it clears the oldest single-pin group: a sixth pin replaces the
  oldest), point = 0x2f29430(manager, group) (next free point), then 0x2f22c90(manager, cursor, group, point, 1, 0).
- Remove one: **0x2f378b0**(manager, record*) (switches it off, then closes the gap in a route). Remove a group: tail of
  0x2f29320 (loop at 0x2f293e0). Remove all: 0x2f37770(manager) (map menu "remove all", hold time from
  `UserMarkerAllRemoveHoldStartSec`/`FinishSec`); 0x2f38270 also clears manager flag bit 7.
- **Saved:** the load path (0x2f35a9e..0x2f35bc9) removes all user markers, checks a version (1) and reads 50 records of
  12-byte position + flags (bit 0 used, bit 1 snap, bit 2 flag) + group + point + u16, calling 0x2f22d00 for each.
  So a marker the mod sets is in the next save. The save writer and the moment of saving were not traced (unknown);
  no "do not save" flag was found. The mod therefore uses its own two groups, removes its markers whenever they should
  not show, and fingerprints their positions so a saved one is removed the next time the feature is armed
  (`mod/marker.h`).
- Reaching a user marker: the per-frame update (from 0x2f3dd02) compares each marker with the porter's position and
  sets +0x40/+0x43; whether a reached pin is removed was not established (unknown). The mod re-places the partner
  marker within a second if it disappears.
- Not script exports: the mod calls these internal functions by RVA after checking unique code signatures
  (0x2f22c90: 28 bytes, 0x2f378b0: 16 bytes), on the game thread only.

### Keys (static, 2026-11-08)

The co-op standard uses V / middle mouse for a ping, hold Tab for the player list and F1 for the guide. What the
executable shows: the game's actions are the `EDSFunction` enum (table 0x3df10f0), including `kDSFunctionCommon_Marker`,
`MarkerPrev`, `MarkerNext`, `Sensor` (the Odradek scan) and `Shout`; key assignments are `DSFunctionKeyBindDefinition`
resources and the key-assign menu (`DSUIKeyAssignMenuKeyBindResource`). The **default keyboard and mouse bindings are not
in `ds.exe`** (they are data resources in the packed archives), and the player's own bindings are in the profile's
settings. The profile's `bindings.cfg` (read 2026-11-09 in a test copy with default bindings: lines
`bind "<Context>_kDSFunction<Action>" "<Key>" "<0|1>"`) binds **V to Melee** and Tab to the main menu; keys 1-5, A, C,
D, E, F, F8, G, I, Q, R, S, T, W, Z, Space, Left Alt/Ctrl/Shift and the mouse buttons are taken. Free: B, H, J, K, L,
M, N, O, P, U, X, Y. The mod's ping key is therefore **B** (changeable at run time with the `key <vk>` command), read
only while the game window has the focus. Middle mouse has a menu meaning (`EMenuInputFunction_FUNCTION_MOUSE_MIDDLE`,
map menu `OnMouseMiddle...` handlers).

## Time of day and weather (static only, build 13300582)

Observed by hashing the pinned executable and following Decima registration
strings, RIP-relative references and focused disassembly. No game was launched,
no process memory was read and no native setter was called for this work. Runtime
semantics, visible sky/rain changes and object lifetime remain **unknown**.

Time exports are registered in `ds.exe+0x3656a30..0x36586ef`:

| Export string RVA | Export | Implementation RVA |
|---|---|---:|
| `0x3f1f978` | `NodeGraphBindingsGame::sSetTimeOfDay` / `Game_SetTimeOfDay` | `0x365d960` |
| `0x3f1f9b8` | `NodeGraphBindingsGame::sGetTimeOfDay` / `Game_GetTimeOfDay` | `0x365d470` |
| `0x3f1f9f8` | `NodeGraphBindingsGame::sEnableDayNightCycle` | `0x365ca10` |
| `0x3f1fa48` | `NodeGraphBindingsGame::sIsDayNightCycleEnabled` | `0x365d4a0` |

The getter loads application `A = *(ds.exe+0x7be6318)`, then time object
`T = *(A+0x58)`, then float `T+0x20`. When A is null the native getter returns
12; the mod refuses a null chain instead of announcing that fallback as a real
observation. The cycle predicate is byte `T+0x2c != 0 && T+0x2d == 0`.
The public `GameWorldTimeState` name is at `0x3f179c8`; that name alone does not
establish the runtime class of T.

The setter at `0x365d960` takes two floats in XMM0/XMM1, fetches the same T,
shuffles them into a member call and tail-jumps to `0x3631f90`. The first float is
target hours; the second is transition duration. The helper multiplies hour deltas
by 3600 (`0x3b88e94`) and wraps at 24 (`0x3bb5488`, with -24 at `0x3bb7584`).
A positive duration sets a remaining delta/rate at `T+0x30/+0x34`. A nonpositive
duration changes `T+0x20` immediately, clears the remaining delta and increments
integer `T+0x28` when wrapping forward across midnight. **A backwards clock
correction can therefore advance the native day counter.** The opt-in mod calls
this export with zero duration, skips circular drift within 0.01 hour (36 s),
and never writes fields directly or calls the cycle-enable export. Effects on
quests, saves, lighting overrides and elapsed game time are unknown. Host cycle
enablement/rate and day count are not synchronized.

`environment.cpp` verifies full instruction-span fingerprints at the pinned RVAs
inside executable image sections, after the ordinary SHA-256/signature sampler
gate. The wrapper's entire 30 bytes and the helper's entire 252 bytes are checked
again before every attempted call. These spans contain no absolute relocation
operands. The getter (31 bytes) and cycle predicate (34 bytes) are checked before
reading. Fingerprints use FNV-1a-64 to compare reviewed loaded instructions; the
executable SHA-256 pin remains the identity protection. These are pinned-build
checks, not signatures for unknown patches. The small leaf functions lack
`.pdata` entries; their explicit return/padding boundaries were disassembled.

Weather is a separate subsystem:

- `DSWeatherSystem` name `0x3e06b28`; exports are registered in
  `0x2e00750..0x2e00be5`. Global weather instance `W = *(ds.exe+0x7bf83b8)`.
- `weather::sGetRainyConditionFromMap` (`0x3e06ae0`) registers `0x2e1f6e0`,
  which passes W and a 3-double game position to `0x2e0c3f0`.
- `0x2e0c3f0` projects world x/y (z is up) using origin doubles `W+8/+0x10`,
  float4 scale `W+0x70` and offset `W+0x80`, clamps to dimensions
  `int32 W+0x38/+0x3c`, and calls `0x2e0b490`. That helper scales coordinates
  by the cached float4 at `ds.exe+0x7bf8a10` (initialized from `int32 W+0x6564`),
  subtracts 0.5, clamps again, and bilinearly reads four float4 cells through
  `*(W+0x64f0)`. The export returns `4*interpolated.z - 2`.
- The read-only provider reproduces that calculation, bounds dimensions/factor
  and reads only four cell channels. It refuses an uninitialized/different scale
  cache, bad pointers, nonfinite/range-invalid cells, or changed read dependencies.
  `rain_from_map` is this **signed native map condition**, not precipitation
  intensity or proof of timefall. Its visual/gameplay classification is unknown.
- `weather::sGetRainyCondition` (`0x3e06aa8`) registers `0x2e1f670`, which further
  clamps the map result using `W+0x69e0`. The mod does not apply that clamp or
  advertise its result as a percentage.
- `weather::sResetForecastTime` (`0x3e06a10`, implementation `0x2e1fc80`)
  reads float `W+0x64a8` and changes forecast timing/control fields, independently
  of the time-of-day object. The provider exposes `forecast_time` with unknown
  units. The weather map is derived state; it is never written by the mod.
- `SetForecastTypeOfRegionDirect` at `0x2e1fdd0` selects a table using
  `int32 W+0x64a0`, a byte region index and byte forecast type, then writes a
  regional override and timing fields. `DirectEx` at `0x2e1fe20` adds a current
  world/area check. Their table/region lifetime, duration/reset ownership,
  forecast-to-render propagation and safe complete state-transfer contract are
  unresolved. No weather setter is approved or called.

Whether weather is a deterministic schedule given additional seed/clock/region
state is **unknown**. These independent clocks and mutable regional overrides do
not establish that syncing time of day will sync weather. No shared-rain claim
is made; time-only sync is an explicit partial milestone.

Reproduce the focused disassembly with the method above, for example (absolute
VA = image base `0x140000000` + RVA):

```sh
x86_64-w64-mingw32-objdump -d -M intel --start-address=0x14365d470 --stop-address=0x14365d4c2 "<game dir>/ds.exe"
x86_64-w64-mingw32-objdump -d -M intel --start-address=0x14365d960 --stop-address=0x14365d97e "<game dir>/ds.exe"
x86_64-w64-mingw32-objdump -d -M intel --start-address=0x143631f90 --stop-address=0x14363208c "<game dir>/ds.exe"
x86_64-w64-mingw32-objdump -d -M intel --start-address=0x142e0c3f0 --stop-address=0x142e0c4c3 "<game dir>/ds.exe"
x86_64-w64-mingw32-objdump -d -M intel --start-address=0x142e0b490 --stop-address=0x142e0b6be "<game dir>/ds.exe"
```

Next evidence needed: a loaded-world read of the time path against a visible sky,
then an authorized one-copy time setter run with a stand-in, independent process
state observations and 90 s responsiveness. For weather, observe region/map and
forecast state through a rain transition, then trace its producer/reset/seed and
regional override lifetime before considering any weather-setting implementation.

## Vehicles: creation, resources and transform playback (2026-11-08, static only)

Inspected build 13300582, SHA-256
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
All addresses below are RVAs relative to `0x140000000`. No game ran or native function was called. Names assigned to unnamed arguments are **inferences**.

### Type identification

Vtable slot 0 points to `lea rax,[rip+descriptor]; ret`; descriptor `+0x40` points to the class name.

| Class | Descriptor | Vtable | Slot-0 getter |
|---|---:|---:|---:|
| DSVehicleMotorbikeResource | `0x4ca6420` | `0x3e246b8` | `0x2f5dba0` |
| DSVehicleTruckResource | `0x4ca6be0` | `0x3e266b8` | `0x2f6bd30` |
| DSVehicleMotorbike | `0x4ca65a0` | `0x3e25db8` | `0x2f6bb60` |
| DSVehicleTruck | `0x4ca6d60` | `0x3e26790` | `0x2f6bcb0` |
| DSVehicleMotorbikeGameActorUnit | `0x4ca6790` | `0x3e261c0` | `0x2f6bbb0` |
| DSVehicleTruckGameActorUnit | `0x4ca6f70` | `0x3e26b98` | `0x2f6bd00` |
| VehicleMover | `0x4ca7ed0` | `0x3e28010` | `0x2f6be60` |
| DSNetVehicleInfo | `0x4dfbf70` | `0x3ee0010` | `0x34b55c0` |

For example, `0x2f5dba0` returns descriptor `0x4ca6420`, and `0x2f6bd30` returns `0x4ca6be0`.

### Resource acquisition without finding a nearby vehicle

There is a global table of vehicle GameActorUnits:

```text
manager = *[base + 0x7bfecc0]
unit    = *[manager + 0x28 + 8*vehicleType]
```

`0x2f6c840(manager, vehicleType)` reads that table under its lock:
`0x2f6c86f: mov rdi,[rdi+rsi*8+0x28]`.

The confirmed types are:

- **Truck = 2.** Constructor `0x2f626f0` installs vtable `0x3e26b98`; `0x2f62749` stores the unit into `manager+0x38`.
- **Motorbike = 3.** Constructor `0x2f61e00` installs vtable `0x3e261c0`; `0x2f61e59` stores the unit into `manager+0x40`.

Both constructors fill an empty slot; they do not replace an existing unit.

The caller at `0x2f532c0` demonstrates the resource chain:

```text
unitResource = *[unit + 0x80]
specificData = ResolveReference(unitResource + 0x38)
entityResource = *[specificData + 0x38]
```

Evidence:

| RVA | Instruction/action |
|---|---|
| `0x2f532e9` | Calls `0x2f6c840` using the input word’s low four bits as vehicle type. |
| `0x2f532f3` | `mov rdi,[rax+0x80]` |
| `0x2f53319–0x2f5332a` | Checks reference readiness with `0x1de12e0`, then resolves `rdi+0x38` through `0x1dde990`. |
| `0x2f5332f` | `mov rdx,[rax+0x38]` |
| `0x2f53356` | Passes that resource to `0x2f534e0`. |

Reflection corroborates the chain: `GameActorUnitResource`, descriptor `0x4af0e20`, names `+0x38` **DefaultSpecificData**; `GameActorSpecificData`, descriptor `0x4af0ee0`, names `+0x38` **EntityResource**.

There is a separate branch when `(inputWord & 0xfc0) == 0x1c0`: it uses `*[unitResource+0x58]`, then that object’s `+0x38` (`0x2f5330a–0x2f53317`). The branch’s complete semantic meaning is **unknown**.

**Inference:** registered units provide a resource source independent of nearby entity searches. This is not proof that both units and their dependencies are always resident. If the unit is null or DefaultSpecificData is unresolved, a reliable force-load contract is **unknown**. This path does not demonstrate a direct GGUUID-to-vehicle-resource lookup.

### The supplied creator: a separate four-slot entity path

`0x2f534e0` has this observed Win64 argument layout:

| Argument | Location | Observed use |
|---|---|---|
| Pool pointer | RCX | Four entity pointers at `+0`; four source pointers at `+0x20`; four `0x50`-byte records at `+0x40`. |
| EntityResource pointer | RDX | Calls resource virtual `+0x88`. |
| Integer | R8D | Written to vehicle `+0x4c8`; wrappers extract six bits. **Inference:** variant/customization selector. |
| Data pointer | R9 | Passed to vehicle virtual `+0x228`. Reviewed implementations consume a word. **Inference:** appearance/customization data. |
| WorldTransform pointer | Entry RSP `+0x28` | Passed to `0x22dff80`; 60-byte position/rotation layout. |
| Float | Entry RSP `+0x30` | Passed to `0x2f7f190(vehicle+0x670, value, vehicle)`. Meaning **unknown**. |

Return value: signed pool index. Wrappers `0x2f532c0` and `0x2f533a0` return the entity from that slot.

The creation sequence is:

| RVA | Evidence |
|---|---|
| `0x2f53522` | Calls resource virtual `+0x88`, returning an entity. |
| `0x2f53531` | Calls `0x22dff80(entity, transform)`. |
| `0x2f53550` | Calls `0x235d1d0` with an activation-policy pointer obtained through `0x2617d60`. |
| `0x2f5355d` | Stores entity in the pool. |
| `0x2f53561` | `or dword [entity+0x560],0x08004000` |
| `0x2f5356b` | Calls activation helper `0x234f3e0(entity,0)`. |
| `0x2f53570–0x2f53598` | Finds VehicleMover by descriptor `0x4ca7ed0`; writes mover `+0x128 = 2`. |
| `0x2f535ac` | Calls world-manager virtual `+0x18` with `(entity,0,1,0)`. |
| `0x2f535c1` | Stores argument 3 at entity `+0x4c8`. |
| `0x2f535c7` | Stores `pool+0x40+index*0x50` at entity `+0x898`. |
| `0x2f535d4` | Calls vehicle virtual `+0x228` with argument 4. |

`Entity` reflection names `+0x260` **ActivationPolicy**. Helper `0x235d1d0` refcounts and replaces that field (`0x235d1fc–0x235d217`); it is not evidence of assigning player ownership.

Both concrete vehicle resource vtables use:

- virtual `+0x80` → `0x2366570`;
- virtual `+0x88` → `0x22b74b0`.

`0x22b74b0` builds a default transform and invokes resource virtual `+0x80` at `0x22b74f5`. The pooled path therefore creates synchronously through the resource factory; it does not call asynchronous request creator `0x2380920`.

**Capacity hazard:** `0x2f53517` sets the index to `-1` when all four slots are occupied, but execution continues into creation and indexed stores. A caller must establish capacity before calling. Neither null-resource nor null-result handling is demonstrated here.

The game initializes its pool at global `0x7bfe668` in `0x2f5af30`: allocates `0x180` bytes, constructs four records with `0x2f51e90`, and clears both pointer arrays. **Inference:** a mod-owned pool could avoid competing with the game’s four slots; its allocation and lifetime contract still requires validation.

### Matching pooled removal and registration

`0x2b7b9e0(pool, entity)` is the matching removal routine. Caller `0x268d4a4–0x268d4ab` supplies the game’s pool at `0x7bfe668`.

It finds the entity among four entries, then:

```text
0x2b7ba29  call [worldManagerVtable+0x20]  ; entity, 0, 0
0x2b7ba2c  mov [pool+index*8],0
0x2b7ba34  mov [pool+index*8+0x20],0
```

World registration is real even for pooled entities. Initialization at `0x262d7f4–0x262d81e` installs world-manager vtable `0x3d5d140`; its add/remove slots resolve to `0x357dfb0` / `0x3587110`.

The add path calls `0x2370840`, which hashes the 16-byte key at entity `+8`, accesses the manager’s `+0x2c0` table, sets entity registered bit `0x20000000` at `0x2370a2f`, and inserts into another collection at manager `+0x130` through `0x2370bb8–0x2370bc2`. **Inference:** the key is the entity UUID. These operations do not establish save exclusion.

### Actual parking take-out and online shared vehicles

The named parking export is a different path:

```text
SpawnVehicleSelectedInParking
  registration 0x2d2088e–0x2d208b6
  thunk 0x2d6dc60 → 0x2d6ece0
  → 0x2d5cb20
  → call 0x2f830a0 at 0x2d5cddb
  → call 0x2f654e0 at 0x2f831f9
```

At the parking call, RCX is the vehicle manager; RDX is selected-row `+8`; R8 points to the calculated transform; R9B is 1; stack argument 5 points to parking-controller `+8`; stack argument 6 is 0 (`0x2d5cdaf–0x2d5cddb`).

`0x2f830a0` selects `manager+0x28+8*(recordWord&0xf)`, builds a `0x68`-byte request, and calls `0x2f654e0`. The latter queues up to four requests at unit `+0x240`, with count at `+0x3f8` (`0x2f656ad–0x2f656e1`). Unit update `0x2f98520` invokes queue processing `0x2f98f40`, then base processing `0x22f9300`.

The request flag derived from R9B becomes bit `0x4` (`0x2f831c5–0x2f831cc`). Queue processing tests that bit at `0x2f99609`; when set, it calls `0x2f5ab00` with the manager’s `+0x58` collection and sets actor `+0x19d` bit `0x40`. **Inference:** this is the parking/player vehicle-list registration control. It is not a proven no-save switch.

The online path also reaches this queue:

- `0x34b4ebd–0x34b4ec0` initializes a `DSNetVehicleInfo`.
- `0x34b4e79` gets the vehicle unit using `0x2f6c840`.
- `0x34b4fbd` ORs request flags with `0xc0a`.
- `0x34b4fcd` calls the same `0x2f654e0`.
- Another online caller occurs at `0x34b4557`.

**Inference:** online shared vehicles use a separate preparation path feeding ordinary vehicle GameActor creation, rather than the four-slot pooled creator. The flags’ complete ownership semantics, player-name assignment, and exact fabrication/terminal “Use vehicle” branches remain **unknown**.

Ordinary entity construction also links the GameActor: `0x2f6905a` writes it to entity `+0x8a8`. The reviewed pooled creator does not perform that assignment. Its resource factory and activation still have additional behavior; this is not an exhaustive proof of isolation from persistence.

### Flags relevant to an inert partner vehicle

The pooled creator’s `0x08004000` mask has concrete effects:

| Flag | Observed effect |
|---|---|
| `0x08000000` | `0x2f731c1–0x2f731ce` skips vehicle setup ending at `0x2f7359d`. That setup includes body/model access and vehicle construction calls. **Inference:** suppresses vehicle physics setup. |
| `0x08000000` | `0x2fa4b90–0x2fa4bab` skips code that automatically selects mover modes 0/1 and overwrites the target transform. |
| `0x4000` | `0x2f750dd–0x2f750e7` skips automatic construction/attachment of `QueryTriggersComponent`, descriptor `0x4af99d0`, at `0x2f75121–0x2f7512c`. |
| `0x4000` | `0x2f68a9a–0x2f68aa7` skips another creation path using resource `+0x1e0`, reflected as WheelHologram. |

**Inference:** preserving these flags, avoiding ordinary GameActor/list registration, and using paired pooled removal is the strongest identified candidate for a display-only partner vehicle.

Whether this fully prevents local entry, theft, collisions or save persistence is **unknown**. In particular, skipping automatic QueryTriggersComponent construction does not prove that the selected resource cannot supply interaction components itself. No verified “locked and never saved” flag was established.

### VehicleMover mode 1 and transform playback

`0x2f801b0(mover, WorldTransform*)` writes mode 1 and copies the complete transform into mover `+0x130`:

```text
0x2f801b0  mov dword [rcx+0x128],1
0x2f801bA  vmovups xmm0,[rdx]
0x2f801be  vmovups [rcx+0x130],xmm0
```

The remainder copies the third position double and nine rotation floats through `0x2f801ee`.

VehicleMover vtable `+0x130` resolves to `0x2f899d0`:

```text
mode 0 → 0x23b7060
mode 1 → 0x22e97f0(mover, mover+0x130)
other  → return
```

At `0x22e97fa`, the helper gets the entity from mover `+0x48`. When the transform differs, `0x22e988b–0x22e98bf` copies it into entity `+0xc8/+0xe0` under the entity lock, then sets transform-dirty bit 1 at `0x22e98c2`.

A second callback, `0x2f75df0`, returns immediately for nonzero modes (`0x2f75dfa`). **Inference:** mode 1 bypasses both reviewed normal mover branches and is suitable for testing transform playback, especially with the pooled creation flags. Freedom from all physics correction remains **unknown**.

The generic exported alternative is `Entity_ExportedSetWorldTransform`, `0x2361ea0`: calls `0x22dff80`, then `0x235bf60(entity,0)` at `0x2361ead–0x2361eb7`. No separate vehicle-specific exported transform setter was established.

Validation: standard-library PE parsing and **32 descriptor/instruction assertions passed**; focused MinGW objdump disassembly corroborated the sites above. The executable hash was unchanged afterward.

## Vehicle attachment and riding animation — static follow-up (2026-11-08)

Inspected build 13300582 read-only. SHA-256 matched `9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`; image base is `0x140000000`. All addresses below are RVAs. No game ran and no live observation was reproduced.

### Animation controls

The player’s animation-variable registration tables identify these names:

| Name | Player’s internal enum | Static evidence |
|---|---:|---|
| `Is_Vehicle` | bool `0x19` | Registration at `0x1883f62`; enabled at `0x2772ee9` and `0x27749be`; cleared at `0x27e96fb`. |
| `Vehicle_Type` | float `0x1b` | Registration at `0x1884d49`; riding setup writes it at `0x2774469`, `0x277448c` or `0x27744b5`. |
| `Vehicle_Seat` | float `0x1c` | Registration at `0x1884d66`; setup writes it at `0x2774681`; bike update changes it at `0x277645a`. |
| `Turn_Heading` | float `0x04` | Riding setup clears it at `0x2774693`; updates write it at `0x277666a` and `0x27766c0`. |
| `Temporary_Float8` | float `0x18` | Registration table `0x1884a40–0x18859ec`; mount setup writes it at `0x2773333`. Its complete riding-state value map is **unknown**. |

These enums are **not AnimationManager slot indices**. The player-side float setter `0x2741df0` addresses `entity + 0x470c + 12*enum`; bool setter `0x273f7f0` changes the corresponding packed record at `entity + 0x4dc8 + 12*enum`.

`Vehicle_Type` is distinct from the vehicle entity’s kind:

| Vehicle | Entity kind, `u32(vehicle+0x4c4)` | Animation `Vehicle_Type` |
|---|---:|---:|
| `DSVehicleMotorbike` | `3` | `0.0` |
| `DSVehicleTruck`, either door index nonnegative | `2` | `1.0` |
| `DSVehicleTruck`, both door indices negative | `2` | `2.0` |
| `DSVehicleSpeedCar` | `1` | `3.0` |

Evidence:

- Constructors write the entity kinds at `0x2f61cfc` (motorbike), `0x2f62571` (truck) and `0x2f62201` (speed car).
- Riding setup reads kind into its state byte at `0x277437f–0x2774391`.
- Kind `3` takes the zero-valued animation-type branch at `0x2774418–0x2774469`.
- Otherwise `0x2774474` calls vehicle vtable `+0x120`. True selects `1.0`; false selects `3.0` for kind `1`, otherwise `2.0`, at `0x2774483–0x27744b5`.
- Truck’s `+0x120` implementation, `0x2f6cc70`, tests resource `+0x300/+0x304`. The `DSVehicleTruckResource` descriptor `0x4ca6be0` names these `DoorLeftIndex` and `DoorRightIndex`. Motorbike and speed-car implementations return false (`0x1920dc0`).

`Vehicle_Seat` must not be interpreted as driver versus passenger. During bike mounting, `0x27744ba–0x2774681` derives its initial value from relative position/orientation. Bike update `0x27763df–0x277645a` changes it using direction tests and state `+0x3f0`. **Inference:** it selects mounting/foot-down side or a related pose variant. Its complete meaning is **unknown**.

For truck/speed-car steering, `0x2776680–0x27766c0` reads current vehicle `+0x58c`, multiplies by `180.0`, smooths toward that value, and writes `Turn_Heading`. Bike handling uses a longer calculation ending at `0x277666a`. **Inference:** replaying the named variable preserves the game’s steering/lean input more faithfully than deriving it from world yaw. Exact clip response and limits are **unknown**.

The animation manager also resolves these names through `0x238ffe0` during `0x2759e40`:

| Name | Resolution call | Stored result |
|---|---:|---:|
| `Vehicle_ToOnLoad` | `0x275b548` | output `+0x244` |
| `Vehicle_ToOffLoad` | `0x275b573` | output `+0x248` |
| `Vehicle_Driving` | `0x275b59e` | output `+0x24c` |
| `Vehicle_FootDown` | `0x275b5c9` | output `+0x250` |
| `Vehicle_ToFootDown` | `0x275b5f4` | output `+0x254` |
| `Vehicle_FootDownMove` | `0x275b61f` | output `+0x258` |

These are resolved animation names; this does **not** establish that they are writable bool variables. Fixed network node IDs and name hashes are **unknown**.

### Applying the variables to another Sam

The exported path targets the supplied manager:

- `InitBoolAnimationVariable`: `0x2397050`; `InitFloatAnimationVariable`: `0x23970b0`.
- `SetBoolAnimationVariable`: `0x23971e0`; `SetFloatAnimationVariable`: `0x2397220`.
- Setters calculate a 48-byte slot from `manager+0x58`, update slot `+0x20`, read its network control ID at `+8`, and dispatch only when that ID is not `-1`.
- Morpheme bool dispatch `0x239a1d0` loads **that manager’s** network instance from `+0xa0` and jumps to `0x1a52b30`.
- Float dispatch `0x239a1f0` similarly uses `manager+0xa0` and jumps to `0x1a52af0`.

There is no local-player lookup in these setter paths. **Inference:** another Sam with its own instance of the porter’s network definition can receive these controls through the existing exports. Whether this produces a complete seated pose without additional controls/IK remains **unknown**.

The committed `mod/double.cpp` creates a separate network instance from the porter’s definition; it does not share the porter’s running instance pointer. Resolve handles on the partner’s manager, and verify each slot’s control ID is not `-1`. A nonnegative `Init` result alone does not prove the network contains that control.

**Candidate experiment, untested:** set `Is_Vehicle`, the appropriate `Vehicle_Type`, and replay `Vehicle_Seat`, `Turn_Heading` and relevant temporary controls from a seated porter. A sufficient minimal set for bike foot-down/driving transitions is **unknown**.

### Mounting and helper placement

Player riding function `0x2772c10` contains a concrete parent assignment:

```text
0x2773040  rcx = [state+0x30]       ; porter
0x2773044  r8d = 0
0x2773047  rdx = r14               ; resolved VehicleEntity
0x2773051  call 0x235e990
```

It then records the vehicle through `0x2741370`, called at `0x277305d`. That routine stores a tracked reference to `vehicle+0x20` at player-param component `+0xad8` (`0x2741386–0x27413ba`), matching the supplied live observation.

The parent implementation is concrete:

- `0x235e990` calls `0x235ebc0`.
- `0x235ec51–0x235ec88` unlinks the child from the former parent’s `+0x78/+0x80` list.
- `0x235ed05–0x235ed09` clears child parent/sibling pointers.
- `0x235ed6c–0x235ed7b` stores the new parent at child `+0x70` and inserts the child at parent `+0x78`.

The helper and transform are selected later in the same riding function:

- Kind `2` (truck) selects `HLP_Driver` at `0x27733f1`.
- Kind `1` (speed car) selects `HLP_Driver` at `0x277345c`.
- Kind `3` (motorbike) selects `HLP_C_Driver` at `0x27734a0`.
- `0x277356c–0x2773595` constructs the transform from quaternion `(0,0,0,1)` and zero translation.
- `0x277359a–0x27735fe` passes the helper, tracked vehicle reference, transform and zero blend value through vtable `+0x338` of `state+0x58`.

Thus the supplied helper-local offset is **identity**, rather than a hard-coded truck-seat displacement. The concrete implementation behind that last virtual call was **not established**.

The supplied live world-position difference `(-0.80,-0.44,+1.95)` is not proof of a fixed local offset: helper placement, vehicle rotation and animation can contribute.

### Dismount

The riding state’s cleanup method is vtable entry `+0x30` (`0x27e9680`) in the table installed by constructor `0x2784c40`:

- `0x27e96e0` calls `state+0x58` vtable `+0x10` with `EDX=1`.
- `0x27e96ea` calls its vtable `+0x238`.
- `0x27e96fb` clears `Is_Vehicle`.
- `0x27e989a` calls vehicle vtable `+0x110` with the porter actor id and `R8B=0`, reversing the reservation made during mounting.
- `0x27e9b86` calls `state+0x58` vtable `+0x380` with zero.
- `0x27e9b92` calls `0x2741370(param,null)`, clearing the tracked vehicle reference.

The exact player-dismount instruction that ultimately clears `porter+0x70`, and the final exit transform calculation, remain **unknown**. The cleanup calls above must not be presented as a fully resolved unlink implementation.

### Read-only vehicle and driver detection

Vehicle class identity can be recovered through slot-zero compound descriptors:

| Entity vtable | Type getter | Descriptor | Class |
|---|---:|---:|---|
| `0x3e25db8` | `0x2f6bb60` | `0x4ca65a0` | `DSVehicleMotorbike` |
| `0x3e26790` | `0x2f6bcb0` | `0x4ca6d60` | `DSVehicleTruck` |
| `0x3e274d8` | `0x2f6bc10` | `0x4ca7660` | `DSVehicleSpeedCar` |

Each getter is `lea rax,[rip+descriptor]; ret`; descriptor `+0x40` points to the listed name.

A stronger driver test than parenthood alone is available:

1. Read and type-check `vehicle = *(porter+0x70)`.
2. Read porter actor id at `porter+0x314`.
3. Compare it with `u32(vehicle+0x8b0)`, also checking occupancy `u32(vehicle+0x63c)`.

Evidence: mount reads porter `+0x314` at `0x2772f7b` and supplies it to vehicle vtable `+0x110` at `0x2772fcb`. Truck implementation `0x2f7c310` delegates to common routine `0x2f7c4c0`, which sets occupancy to `1` and stores that id at vehicle `+0x8b0` (`0x2f7c537–0x2f7c547`). Release compares the supplied id at `0x2f7c7df–0x2f7c7e8`, then clears occupancy and sets the id to `0x1fffff` at `0x2f7c84d/0x2f7c85f`.

**Inference:** matching parent, occupancy and actor id identifies the porter as the registered driver. A parented entity whose id does not match is **not thereby proven to be a passenger**. Use guarded reads and recheck the chain during transitions.

Player-usable truck passenger seats are **unknown**. The executable contains passenger mechanisms, including five helper selections in `0x2dc8520` and `Passenger_Mounting`/`Passenger_Mounting_Direction` registration at `0x2b8746f–0x2b874df`. Their existence does not establish a player passenger action. The mapped player mount path selects driver helpers. This pass neither confirms passenger support nor proves its universal absence.

### Attaching the owned non-player Sam

The generic native exports are resolved:

| Export | RVA | Behavior |
|---|---:|---|
| `Entity_ExportedSetParent` | `0x2361dd0` | Calls `0x235e990(child,parent,0)`; optional flag sets entity flag `0x200`. |
| `Entity_ExportedClearParent` | `0x2361900` | Clears flag `0x200`, then calls `0x235e990(child,null,0)`. |
| `Entity_ExportedSetParentAndAttachToHelper` | `0x2361dc0` | Jumps to `0x234ceb0(child,parent,helperCString,flag)`. |

`0x234ceb0` parents the entity, then calls `0x23aa8d0` for a nonempty helper and nonnull parent. That routine obtains/configures an `AttachToHelperMover` and installs it through entity vtable `+0x40` at `0x23aa97d`.

For `DSPlayerAKukanEntity`, that slot is `0x235e3f0`: it replaces entity `+0xb0` at `0x235e53d`, registers the new mover, and removes the old one through engine paths.

**Inference:** this is an engine-supported candidate for attaching the owned plain-Entity Sam to a vehicle helper. It replaces the own body’s `FixedPositionMover`; parenthood alone does not establish helper-following movement. Retaining/restoring the original mover and stopping the existing mover-target writes would therefore be required. Exact mover lifetime ownership and successful attachment/restoration remain **unknown**.

The node-graph wrapper explicitly rejects Humanoids (`0x178602–0x178618`, diagnostic at `0x3f5c78d`). The owned AKukan body is a plain Entity subclass, as documented above, so **inference:** that particular restriction does not exclude it. This is not evidence that the player’s Humanoid mounting machinery can safely be reused on it.

Visual attachment would not itself register a second driver or establish a player passenger seat.

## Vehicle animation entry: native pulses, tags and Morpheme APIs

Static analysis of build 13300582; executable SHA-256
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
All addresses below are RVAs; image base `0x140000000`.

### Native riding entry includes animation-variable pulses

The riding code does more than latch `Is_Vehicle` and `Vehicle_Type`.

At `0x2772edd..0x2772f05`:

```text
2772edd  mov rcx,[rsi+30]
2772ee1  mov r8b,1
2772ee4  mov edx,19
2772ee9  call 273f7f0       ; Is_Vehicle = true

2772eee  mov rcx,[rsi+30]
2772ef2  mov edx,44
2772ef7  call 2741c80       ; pulse Action_Rewind_NoInter

2772efc  mov rcx,[rsi+30]
2772f00  mov edx,7d
2772f05  call 2741c80       ; pulse Is_Force_Upper_Reset
```

A second riding-entry branch repeats `Is_Vehicle=true` at `0x27749be`
and pulses enum `0x44` at `0x27749cc`.

Names are established by the bool-name initialization routine
`0x1883ae0..0x1884a35`:

| Player bool enum | Name |
|---:|---|
| `0x19` | `Is_Vehicle` |
| `0x29` | `Action_Rewind` |
| `0x32` | `Is_BasicAction` |
| `0x44` | `Action_Rewind_NoInter` |
| `0x46` | `Is_Actions` |
| `0x7d` | `Is_Force_Upper_Reset` |

`0x2741c80` is a pulse setter, not a Morpheme message broadcaster:

```text
2741c80  mov eax,edx
2741c82  lea rdx,[rax+rax*2]
2741c86  movzx eax,byte ptr [rcx+rdx*4+4dc8]
2741c8e  and al,fb
2741c90  or al,2
2741c92  mov [rcx+rdx*4+4dc8],al
```

The flush at `0x278b4d1..0x278b520` recognizes bit 1, writes true to the
manager's variable record and invokes manager virtual `+0x148`. It then
marks the player-side state and requests false through `0x273f7f0`.
This supplies a true pulse followed by reset through normal updates.

### Is_Actions is an aggregate, not an established action-ID channel

The inspected selector at `0x2803e1a..0x2803fb2` reads player bools with
`0x2717c40`, including `Is_Vehicle` at `0x2803f03..0x2803f0b`, and builds
an action bitset.

When the new bitset is nonzero and differs from `DSPlayerState+0x474`,
it pulses enum `0x29`:

```text
2803f91  cmp [rsi+474],ebx
2803f97  je 2803fa6
2803f99  mov rcx,[r14]
2803f9c  mov edx,29
2803fa1  call 2741c80       ; Action_Rewind

2803fa6  mov rcx,[r14]
2803fa9  movzx r8d,dil
2803fad  mov edx,46
2803fb2  call 273f7f0       ; Is_Actions
```

The bitset is subsequently stored at state `+0x474`. This inspected path
does not export it as a numeric animation “action ID”.

`Is_BasicAction` is computed separately at `0x27fff90..0x280001a` from
controller flags. It is not established here as the native logical
inverse of `Is_Actions`.

**Inference:** replaying only the steady bool/float values can miss the
transition/reset pulse used to enter the riding branch. A snapshot of
nonzero variables can also miss a pulse that has already expired.

### Existing export for pulses

`AnimationManager_ExportedTriggerBoolAnimationVariable` is `0x2397310`:

```cpp
void TriggerBool(AnimationManager* manager, int handle, float duration);
```

Evidence:

```text
2397319  vmaxss xmm3,xmm2,[7bc6810]
2397321  lea r8,[rsp+20]
2397326  mov byte ptr [rsp+20],1
239732b  call 238a2f0
```

The helper writes the bool through virtual `+0x148`, stores a positive
expiry in variable-record `+0x10`, and registers the handle in manager
`+0x60/+0x68`. The update at `0x2393510..0x239353d` decrements the expiry;
`0x2393631..0x2393653` resets an expired bool to false.

The runtime value of duration floor `0x7bc6810` is unknown from this
static pass. Zero-duration behavior should not be assumed.

### Smallest justified next experiment

This is a candidate, not a demonstrated sufficient or minimal sequence:

1. Validate the controls against the partner's active porter definition.
2. Set the desired steady controls on the partner's own manager:
   `Vehicle_Type=1`, `Vehicle_Seat=0`, `Is_Vehicle=true`,
   `Is_Actions=true`, plus the existing stationary/landed controls.
3. Once on entry, pulse `Action_Rewind` and
   `Action_Rewind_NoInter` using `0x2397310`.
4. To reproduce the observed riding branch completely, also pulse
   `Is_Force_Upper_Reset`. Its necessity for the seated lower-body pose
   is unknown.
5. Keep the steady controls applied while allowing the pulses to expire.
   Ensure another mod update does not overwrite them before evaluation.

A short positive trigger duration, such as 0.1 seconds, is an experimental
choice, not a recovered native duration. Do not retrigger every tick.

Candidate return to locomotion: clear `Is_Vehicle`, clear `Is_Actions`
when no other action remains, and resume the ordinary locomotion controls.
The inspected aggregate selector does not pulse `Action_Rewind` when its
new bitset is zero. A sufficient exit sequence on the partner remains
unknown.

### Important binding caveat after borrowing the porter definition

A valid handle/node number does not establish the intended control name.

`0x2390d80` searches manager `+0x58` records by name. New records are
resolved in `0x2390e70`; at `0x2390f61..0x2390f7a`, bool/float lookup
dispatches through manager `+0x30` — the manager resource.

`MorphemeAnimationManagerResource` lookup `0x2399070` loads resource
`+0xd8` and calls `0x1a4aff0`. Existing matching records with a non-`-1`
binding can return without re-resolution (`0x2390f0a..0x2390f13`).

The current borrowing implementation restores the double resource's
`+0xd8` after rebuilding its instance. Consequently, controls initialized
later can resolve against that restored definition. Whether this explains
the supplied failed experiment is unknown.

Compare each handle's binding at:

```text
manager->records + 0x30 * handle + 0x08
```

with a lookup against:

```text
activeDefinition = *(manager->instance + 0x38)
```

Useful native lookup: `0x1a4bcf0(definition, const char* fullNodeName)`,
used by `0x1a4aff0` after constructing `ControlParameters|<name>`.
Failure is `0xffff`. Do not confuse the player enums above with
definition-specific control node IDs.

The setter disassembly establishes a **48-byte (`0x30`) record stride**.
The supplied IDs 72/73 being valid does not independently prove their
name/type correspondence in the active porter definition.

### The six Vehicle_* names are animation tags

`0x238ffe0` dispatches through resource virtual `+0xd8`:

```text
238ffe0  mov rcx,[rcx+30]
238ffe4  mov rax,[rcx]
238ffe7  jmp [rax+d8]
```

For resource vtable `0x3d0a7e8`, this is `0x2399210`, which loads resource
`+0xd8` and jumps to `0x1a4acc0`.

This is also the path used by
`AnimationManager_ExportedGetAnimationTagID` at `0x2397430`.
Resource metadata identifies the map at definition `+0x80` as
`AnimationTags`; `Messages` is a separate field at `+0x60`.

| Name | Output offset | Player tag enum |
|---|---:|---:|
| `Vehicle_ToOnLoad` | `+0x244` | `0x91` |
| `Vehicle_ToOffLoad` | `+0x248` | `0x92` |
| `Vehicle_Driving` | `+0x24c` | `0x93` |
| `Vehicle_FootDown` | `+0x250` | `0x94` |
| `Vehicle_ToFootDown` | `+0x254` | `0x95` |
| `Vehicle_FootDownMove` | `+0x258` | `0x96` |

The initializer stores these at `0x275b551..0x275b628`.
Its caller, `0x27498b4`, supplies entity `+0x6a8` as the output table.

Entity virtual `+0x140`, implemented by `0x270e330`, retrieves
`[entity + 0x6a8 + 4*enum]`. Query helper `0x2717100` passes the resulting
tag ID to the animation manager.

Confirmed consumers include:

- `0x278d484`: query enum `0x91`.
- `0x278d49b`: query enum `0x92`.
- `0x278d4c5`: query enum `0x94`.
- `0x278d50e`: query enum `0x95`.

These are observations of animation activity, not evidence that sending
those names starts the corresponding animation. Their tag IDs must not
be passed as message IDs or forced-state node IDs.

### Native message and force-state interfaces

The following methods exist independently of the bool-variable pulses:

| Purpose | RVA | Established behavior |
|---|---:|---|
| Resolve message name | `0x1a4bab0` | Definition plus engine String reference; calls `0x2202a50`; failure `0xffffffff`. |
| Broadcast request | `0x2399cb0` | Manager virtual `+0xd0`; `RCX=manager`, `EDX=message ID`, `R8B=queue`. |
| Select message preset | `0x2399cf0` | Manager virtual `+0xd8`; takes message ID, preset index and queue flag. |
| Force state path | `0x2399c10` | Manager virtual `+0xc8`; `EDX=node ID`, `R8=optional data`, `R9B=flag`. |

For broadcast, queue=false reaches `0x1a488d0`. It loads instance
`+0x40`, constructs a message with ID at `+0xc`, type `0xa` and status
true, then calls `0x21fd490`. That routine iterates the definition's
registered recipients. Queue=true appends `{message ID, 0xffff}` through
`0x1a50220`.

`0x2399cf0` reaches `0x1a48950` for immediate processing. It selects a
preset message from the distributor's `+0x20` array through
`0x21fcb50`, then uses the same broadcaster. Its preset argument is not
established as a target state/node.

For forcing, `0x2399c10` operates on `*(manager+0xa0)` and calls
`0x1a4a7c0`. That routine searches enclosing state-machine structure
through `0x1a4bf70`, recursively handles ancestors, and calls
`0x22009c0` to select the matching child state.

An existing native caller is `0x239f7b0`; at `0x239f8aa..0x239f8c2` it
passes a 16-bit node ID and calls manager virtual `+0xc8` with `R9D=0`.
The precise meaning of the optional data/flag is not fully established.

Vehicle-specific message names, message IDs and forceable node IDs:
**unknown**. No riding-to-broadcast or riding-to-force-state call chain
was established. The recovered riding entry uses bool pulses.

### Export-name search

The inspected `AnimationManager_Exported*` names include variable
initializers/setters/getters, the bool trigger, and tag/event queries.
No matching named Request, Message or State setter export was found.

Relevant query exports:

| Export | RVA |
|---|---:|
| `GetAnimationEventID` | `0x23973d0` |
| `GetAnimationTagID` | `0x2397430` |
| `IsEventActive` | `0x2397490` |
| `IsTagActive` | `0x23974b0` |

Static evidence establishes the entry pulses and native APIs. Whether
those pulses seat the partner, which subset is sufficient, and reliable
entry/exit behavior remain untested.

## Weather replication follow-up — build 13300582, static only

Executable SHA-256 verified:
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
PE headers, section mappings, exception records, metadata and imports were parsed directly; code was inspected with `x86_64-w64-mingw32-objdump`. All addresses below are **RVAs**, relative to image base `0x140000000`.

No game was launched, no process memory was inspected and no native function was called. Visible results, runtime object lifetimes and a safe complete state-transfer contract remain **unknown**.

The reported Episode 1/3 observation—hour 12, cycle disabled and no lighting change after setting the hour—is supplied live evidence, not independently verified by this static analysis.

### Two weather objects

Distinguish these objects:

| Object | Address | Evidence |
|---|---:|---|
| Forecast/map state, called **W** below | `*(image+0x7bf83b8)` | Allocated as `0x6a40` bytes at `0x2e0dcab`; constructor `0x2e04fd0` |
| Engine `DSWeatherSystem`, called **S** below | `*(image+0x578c150)` | Consumers check descriptor `0x4c77980`; descriptor `+0x40` names `DSWeatherSystem` |
| `DSWeatherSystem` vtable | `0x3e09690` | Slot 0 → `0x2e0c310`: `lea rax,[descriptor]; ret` |

**Correction to the earlier notes:** `0x7bf83b8` holds forecast/map state, rather than the typed `DSWeatherSystem` object. W’s constructor installs primary vtable `0x3e09410`, whose slot-0 getter identifies `StateObject`.

### Forecast generation and spatial rain

W contains four banks of 64 regional records:

```text
R(bank, region) = W + 0x8a0 + (bank*64 + region)*0x5c
bank           = int32(W+0x64a0)
active regions = uint64(W+0x48)
```

The constructor initializes 256 records at `0x2e05033..0x2e05050`. Record layout established by scheduler, query and serialization code:

| Record offset | Meaning |
|---|---|
| `+0x00` | Forecast-type integer |
| `+0x04`, `+0x08` | Schedule window boundaries |
| `+0x0c` | 32 generated forecast bytes |
| `+0x2c` | 32 effective forecast bytes, after additional processing |
| `+0x4c` | Additional control value |
| `+0x50`, `+0x54` | Evolving control/blend values |
| `+0x58` | Flags |

Important W fields:

| Offset | Established use |
|---|---|
| `+0x64a4` | Fractional clock accumulator |
| `+0x64a8` | Forecast clock |
| `+0x64ac`, `+0x64b0` | Forecast rebuild boundaries |
| `+0x64b4` | Forecast interval; constructor default `600.0` |
| `+0x64b8` | Countdown affecting clock advancement |
| `+0x6514`, `+0x6515` | Reset/rebuild controls |
| `+0x64d0` | Coarse scalar weather grid |
| `+0x64e0` | Spatially/temporally filtered scalar grid |
| `+0x64f0` | Final float4 map sampled by the existing reader |
| `+0x6a00` | Byte region-index map |
| `+0x90`, `+0x98` | Count/pointer for local weather influence records |

The scheduler/map producer is `0x2e183f0`. At `0x2e18431..0x2e1849f`, it adds elapsed time to the accumulator, calls `floorf` through `0x3b24d08`, conditionally advances the forecast clock, and retains the fractional remainder. Seconds are a reasonable inference from the elapsed-time path and interval, but the public reader’s units have not been independently verified live.

The normal forecast generation at `0x2e1875b..0x2e18856` computes a fixed arithmetic hash from interval-aligned forecast time and region index, then selects a forecast byte using the probability table at `0x4c77ed0`. **No mutable seed is read in this particular calculation.**

Forced forecast types have degenerate probability rows:

| Type | Name | Generated byte |
|---:|---|---:|
| 3 | RainyOnly | 2 |
| 4 | CloudyOnly | 1 |
| 5 | HeavyRainOnly | 3 |
| 6 | SunnyOnly | 0 |

Enum metadata is at `0x3e03fc0`; `14` means `Invalid(NotChange)`.

However, the complete pipeline is not determined by this hash:

- `0x2e1861c` and `0x2e1864c` call shared RNG `0x19051c0` in additional regional-control branches.
- That RNG reads/writes global `0x4f6bb40`, using `state = state*0x19660d + 0x3c6ef35f`.
- Those branches also consult manager `*(image+0x7bf57a0)`.
- `0x2e082a0` refreshes regional controls from additional world state.
- Effective forecasts and rain maps retain evolving blend/history values.

The byte region map determines which regional forecast applies at a location. `0x2e0c6b0` projects game x/y into W’s grid and returns `*(W+0x6a00)[cell]`. `0x2e0e430` constructs this map from a loaded resource or fallback data. `DSWeatherRegionResource` metadata identifies `MapWidth`, `MapHeight`, `OriginOffsetX/Y`, `MapScaleX/Y` and `MapData`.

The producer approaches regional target values in the coarse grid at `0x2e19340..0x2e1942d`, applies spatial filtering and temporal blending at `0x2e19461..0x2e196bb`, then resamples and writes float4 cells at `0x2e19790..0x2e19b45`.

Consequently, identical forecast type/time can still produce different **current** rain maps.

### Signed condition and rendering

The existing map getter remains:

```text
condition = 4 * bilinear_sample(final_map).z - 2
```

Lower values mean wetter conditions in the native classifier. `0x2e0c7f0` applies the `W+0x69e0` adjustment and an optional float bias, then returns:

| Adjusted condition | Return |
|---|---:|
| `> 0.5` | 0 |
| `[-0.5, 0.5]` | 1 |
| `[-1.5, -0.5)` | 2 |
| `< -1.5` | 3 |

The forced forecast rows and map producer support interpreting these as sunny/cloudy/rain/heavy rain. Their precise visible thresholds remain untested.

`0x2e1d600` samples the local player’s position and selects S’s weather state at `S+0x5b4`, incorporating encounter/world controls as well as the rain map. `EDSWeatherStateType` metadata at `0x3e04d58` separately names `Sunny`, `Cloudy`, `Rainny`, `RainnyBt`, `Knot`, `Subspace`, `HeavyRain`, `DarkCloudy` and `RainyBtTar`. These metadata values are not interchangeable with the zero-based internal state field.

`0x2e1c460` consumes the final float4 map for the rendering path. At `0x2e1c530`, it loads `W+0x64f0` and passes it onward with the rendering resource. Copying only `.z` would reproduce one scalar query, not all weather rendering inputs.

### Native setters and force candidates

These are Decima script registrations, not Windows PE exports.

| Export/candidate | RVA | Observed contract |
|---|---:|---|
| `weather::sSetWeatherOfRegion` | `0x18e9b10` | **No-op:** registered target is `ret 0` in this build |
| `weather::sSetForecastTypeOfRegion` | `0x2e1fce0` | RCX region-name string; EDX forecast type. Searches region names and changes one active-bank record |
| `weather::sSetForecastTypeOfRegionDirect` | `0x2e1fdd0` | CL region index; DL forecast type |
| `weather::sSetForecastTypeOfRegionDirectEx` | `0x2e1fe20` | CX area ID; DL region; R8B type. Applies only when the current area matches |
| `weather::sResetForecastTime` | `0x2e1fc80` | CL control byte; invalidates rebuild boundaries, sets rebuild controls |
| `weather::sSetStartControlFlags` | `0x2e1fed0` | Nonzero CL sets control bit 0 and countdown `0.2`; this is not a general rain-start function |
| `WeatherSystem_ExportedSetWeatherOverride` | `0x1acaf20` | RCX `WeatherSystem*`; RDX `const WeatherSetup*`; XMM2 float |
| `WeatherSystem_ExportedResetWeather` | `0x1acaf10` | RCX `WeatherSystem*`; sets byte `this+0x3a0` |

Weather registrations are at `0x2e00750..0x2e00be5`. Engine override/reset registrations are at `0x1a9aff1..0x1a9b046`.

For the direct forecast setter:

```text
0x2e1fde5: load bank from W+0x64a0
0x2e1fdfb: add bank*64, region and record-base adjustment
0x2e1fdfe: multiply by 0x5c
0x2e1fe02: record.type = requested type
0x2e1fe06: load record+4
0x2e1fe0b: record+8 = record+4
```

It rejects type 14, but does **not** establish bounds checking for arbitrary supplied region/type bytes. It invalidates a regional window; it does not immediately replace the final rain map.

`SetWeatherOverride` delegates to `0x1ac10a0` with an initial blend value of zero and the supplied float forwarded as another blend parameter. Metadata confirms `WeatherSystem*`, `const WeatherSetup*`, and `float`. The helper manages setup references and override records under a lock. A transition-duration interpretation of the float is an inference. Null setup disables existing entries in the selected override list. Ownership, restoration and interaction with DS-specific state selection remain unresolved.

Two additional native spatial-weather candidates were found:

```text
0x2e14f60(W, double_position*, float radius,
           float lifetime, owner_pointer, bool detach_owner)
0x2e14fa0(W, double_position*, float radius,
           float lifetime, owner_pointer, bool detach_owner)
```

Both call `0x2e15df0`:

- `0x2e14f60` supplies target `-1.0`, type 2: **rain candidate**.
- `0x2e14fa0` supplies target `+1.0`, type 0: **clear-weather candidate**.
- The helper projects the double position, scales radius, and inserts/updates a `0x30`-byte record in W’s influence list.
- Record `+0x18` is decremented and expired records are removed at `0x2e18bd0..0x2e18c39`, supporting the lifetime interpretation.
- `owner_pointer` is used as an identity key; the boolean can clear it.
- The map builder applies horizontal distance/radius falloff at `0x2e19960..0x2e199e5`.

These are stronger candidates for forcing local rain/clear weather than the no-op named setter. They are **not verified callable APIs**.

No separately named `StartRain`, `StopRain`, `StartTimefall` or `StopTimefall` export was found in the inspected ASCII registration-name search. This does not prove that no other forcing path exists.

### Thread and update-phase constraints

The forecast producer runs through a queued job:

```text
0x2e045a0: load W+0x6990; jump to 0x2e183f0
0x2e17edc..0x2e17eea: submit W+0x6998 job
```

The spatial influence helper and scheduler share a lock at `W+0x69a8`. Direct forecast setters contain no equivalent locking in the inspected bodies.

**Exact required thread/update phase is unknown.** A game-thread callback alone does not establish that the weather job is quiescent. Any implementation needs the native scheduling/lifetime contract; network-worker calls or unsynchronized field copies are unsupported.

### Save, story and BT coupling

Forecast state participates in serialization:

- Writer `0x2e1e990` writes forecast clock `W+0x64a8`, then all four banks of 64 records.
- `0x2e1ea45..0x2e1eafb` writes record type, boundaries, both 32-byte arrays and control fields.
- Reader `0x2e12520` restores clock and regional state, then requests rebuilding.
- The writer also serializes selected influence-list entries whose owner pointer is null, at `0x2e1eb74..0x2e1eca8`.

Thus forecast forcing and ownerless local-weather influences can enter save data. Restoring only the current map would not undo changed forecast records.

DS weather serialization also writes `S+0x274/+0x278`. The inspected version-5 read path consumes two floats without a visible corresponding store, so persistence of those two rendering fields across reload is **unknown**.

BT-related weather is not represented by ordinary precipitation alone:

- Catcher-side code at `0x2a57206` checks a resource byte at `+0xb7` before calling rain candidate `0x2e14f60`.
- Reflected metadata identifies resource offset `0xb7` as `IsDisableWeatherControl`.
- Other encounter paths call clear candidate `0x2e14fa0`.
- The DS weather selector consults encounter/world state and chooses separate BT/dark-rain states.
- `DSWeatherDarkRainEventInstance` methods at `0x32ced60` and `0x32cee20` update `S+0x278` from event curves.
- Weather-effect code emits the named runtime signal `DSDarkRainActive` at `0x3352951`.

This establishes **encounters controlling weather** and separate BT weather presentation. It does **not** establish the reverse implication. Whether forcing a rainy map or preset spawns/enables BTs remains **unknown**. No quest-advance call was identified in the short forecast setters, but indirect story/mission effects have not been excluded.

### Replication conclusion

**Seed plus forecast time is not an established synchronization solution.** Normal schedule generation has a deterministic time/region calculation, but the full weather pipeline also depends on mutable regional controls, shared RNG branches, local influence records, filtering history and encounter state.

Two candidate approaches remain:

1. **Replicate forecast inputs:** active bank/regions, forecast clock and timing controls, regional records and local influences. This still needs a complete reconstruction/reset contract for filtered maps and DS rendering controls.
2. **Replicate host-derived weather output:** final float4 rain map plus matching grid mapping, followed by the native rendering upload/refresh path. This avoids relying on forecast determinism, but job synchronization, renderer transitions, overrides and encounter-state precedence remain unresolved.

For matching only the existing read-only scalar query at one position, four `.z` samples and matching mapping dependencies suffice; `W+0x69e0` is additionally needed for the adjusted getter. **That is not sufficient evidence of matching visible rain/timefall.**

The next static/runtime work should establish the weather-job completion boundary and observe S, map channels, local influences and forecast records through an actual rain transition. No weather-writing implementation is justified by this analysis alone.

### Shared rain, live (2026-11-09)

Implemented with the third approach the static pass named only as a candidate: the game's own local weather
influence. The host sends its map condition where it stands (`ds::Environment::rain`); a guest whose own map
disagrees at the host's position calls `0x2e14f60(W, &position, 150.0, 45.0, owner, false)` (rain; `0x2e14fa0` for
clear) on the game thread with a non-null owner (the save writer keeps only ownerless influences) and renews it every
15 s while the host stays wet (`mod/weather.cpp`). Live, one copy as guest outdoors in Episode 3 and the stand-in
host reporting -1.8: the guest's map at the porter went from 0 to -1.2, the game showed its "Timefall Storms" tip,
Sam pulled his hood up and the ground turned wet. With the host gone the influence lapsed and the map read 0 again
(about 100 s later). No fault record; real saves unchanged. Whether the forced rain can bring BTs is unknown (none
appeared in that run).

## Partner vehicle appearance, collision and wheels — static follow-up

Inspected build 13300582, SHA-256
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
All addresses are RVAs relative to image base `0x140000000`. Static analysis only; no native calls or live validation.

### Variant and appearance are separate inputs

For both `DSVehicleTruck` and `DSVehicleMotorbike`:

| Location | Established meaning |
|---|---|
| Entity `+0x4c8`, u32 | Six-bit variant/configuration selector. |
| Entity `+0x664`, u16 | Retained appearance/customization word. |
| Entity `+0x8a8`, pointer | Linked vehicle GameActor, when present. |
| GameActor `+0x198`, u32, bits 22–27 | Actor’s variant selector. |
| GameActor `+0x1a2`, u16 | Actor’s appearance/customization word. |

Creator evidence:

```asm
0x2f5333c  lea   r9,[rsi+2]             ; appearance-word pointer
0x2f53340  shr   r8d,6
0x2f53347  and   r8d,0x3f               ; variant from descriptor word
0x2f535c1  mov   [rdi+0x4c8],ebp
0x2f535d4  call  [r8+0x228]
```

Concrete appearance implementations:

| Class | Entity vtable | Virtual `+0x228` |
|---|---:|---:|
| DSVehicleMotorbike | `0x3e25db8` | `0x2f80e00` |
| DSVehicleTruck | `0x3e26790` | `0x2f81400` |

The motorbike stores the supplied word at entity `+0x664` (`0x2f80e53`) and, when its GameActor exists, actor `+0x1a2` (`0x2f80e63`). The ordinary truck path reaches common routine `0x2f81790`, which performs the corresponding stores at `0x2f81a7a` and `0x2f81a8a`.

The ordinary initialization path independently establishes the actor-to-entity relationship:

```asm
0x2f77010  mov   rdx,[rsi+0x8a8]
0x2f77023  mov   eax,[rdx+0x198]
0x2f77029  shr   eax,22
0x2f7702c  and   eax,0x3f
0x2f77037  mov   [rsi+0x4c8],eax
...
0x2f77087  add   rdx,0x1a2
0x2f77091  call  [rax+0x228]
```

There is also a native copy precedent: wrapper `0x2f533a0` extracts `(actor+0x198 >> 22) & 0x3f`, resolves the source entity’s resource, and passes `actor+0x1a2` to the same pooled creator `0x2f534e0` (`0x2f533c2–0x2f53407`).

Thus the candidate read-only sampling and descriptor construction are:

```cpp
variant = read_u32(sourceVehicle + 0x4c8);
appearance = read_u16(sourceVehicle + 0x664);

// Require the pinned truck/bike class and variant < 64.
uint16_t descriptor[2] = {
    uint16_t(unitType | (variant << 6)), // truck 2; motorbike 3
    appearance
};
```

**Inference:** this reproduces ordinary player-vehicle customization more faithfully than `{unitType, 0}`. It has not been tested live. Matching appearance words do not establish matching entity resources for enemy or special configurations. The native copy wrapper uses the source resource; `0x2f532c0` instead selects from the unit table.

### What the appearance word selects

Common routine `0x2f81790` calls entity virtuals `+0x158` and `+0x160` before retaining the complete word:

```asm
0x2f81a5a  call  [rax+0x158]
0x2f81a69  call  [rax+0x160]
```

| Class | Word bits 0–5, virtual `+0x158` | Word bits 6–11, virtual `+0x160` |
|---|---:|---:|
| Motorbike | `0x2f82690` | `0x2f827f0` |
| Truck | `0x2f82750` | `0x2f828d0` |

These routines extract six-bit indices and call `0x2b96220(entity, &materialSelectorHash, index)`. That helper finds a model material selector, converts the integer index to float, and updates its material parameter.

For the non-broken ordinary truck:

- Low six bits reach selectors `0x4ab08872`, `0x3bd8a365`, and `0x182eff2d`.
- Bits 6–11 reach selector `0x6fd0472a`.
- The motorbike uses `0x4ab08872`/`0x3bd8a365` for the low channel and `0x0bf2faea`/`0x2ac3b3f5` for the next channel.

**Inference:** these are colour channels, likely primary/secondary. Their exact primary/secondary ordering and colour-index mapping remain **unknown**.

Additional established branches:

- Low-channel values `30` and `31` select resource fields `GoldApv` and `SilverApv` (`resource+0x1f8/+0x200`) in `0x2f81790`; broken variants use `BrokenGoldApv`/`BrokenSilverApv` (`+0x210/+0x218`).
- The truck’s enemy branch is selected through resource virtual `+0xb8`, getter `0x2f6f7a0`, which reads reflected field `IsEnemy` at resource `+0x30c`. This branch accepts low-channel indices below 10 and follows a different material path.
- Preserve the complete u16 appearance word. The reviewed channel consumers do not establish the meaning of its upper four bits.

The reflected `VehicleEntityResource.PaintResource` field at **resource `+0x1d8` is weathering configuration**. Its target `DSVehiclePaintResource`, descriptor `0x4ca5a60`, contains `MudHeight`, `MudFade`, `MudSpeed`, `TarSpeed`, `SnowSpeed`, and related fields. It is not the established source of the selected green colour.

UI reflection corroborates indexed customization:

- `DSUIVehicleCustomizeColor`, descriptor `0x4cd4bb0`, contains `ShaderColorIndex` at `+0`, `PaletteColor` at `+4`, and default/primary-only flags.
- `DSUIVehicleCustomizeResource`, descriptor `0x4cd1870`, contains named colour records, including `CustomizeColor_GRASS_GREEN` at `+0xf0`.

Those are resource-layout offsets, not green’s shader index. The actual index for the supplied live green truck is **unknown**; copying its retained appearance word avoids guessing it.

Variant is broader than colour: truck variant 6 selects reflected `HalflifeApv` at resource `+0x2b0`; bike variant 3 selects `RideTypeApv`/`BrokenRideTypeApv`; bike variant 8 can create `HitchCarrier`. Variant 7 also selects the wrapper’s alternate unit-resource path. A complete variant enumeration remains **unknown**.

### Collision setup and mode-1 playback

The skipped function starts at `0x2f73170`. Its early checks skip setup when flag `0x08000000` is set or entity `+0x330` is already non-null.

The reviewed block does not simply create one collision component:

| RVA | Established action |
|---|---|
| `0x2f731e4–0x2f731f4` | Obtains an existing model collision instance through `0x234dcd0`; caches it at entity `+0x340/+0x358`. |
| `0x2f73280` | Calls vehicle virtual `+0x250` to obtain pose data. |
| `0x2f73294` | Calls vehicle virtual `+0x150`, constructing vehicle-physics state at entity `+0x330`. |
| `0x2f732ff` | Calls `0x2f688e0` with motion type 3; retains an auxiliary collision instance at entity `+0x348`. |
| `0x2f73580–0x2f73596` | Passes the primary body to `0x23d3a00(..., true)` and sets entity `+0x564` bit 3. |

`0x234dcd0` forwards to model helper `0x2390260`, which searches model entries for an existing collision instance. `PhysicsCollisionInstance` is identified by descriptor `0x48c7d50`, getter `0x1c3ee90`, vtable `0x3bdde88`.

Concrete virtual `+0x150` implementations:

- Truck: `0x2f6e0e0`; allocates a `0x170`-byte object, constructs it with `0x1c145e0`, stores it at `+0x330`, initializes it with the primary body through `0x1c20970`, and registers it through `0x1c174c0`. It also creates an auxiliary keyframed collision instance retained at truck `+0xcd0`.
- Motorbike: `0x2f6dc30`; creates an auxiliary keyframed instance retained at `+0xd00`, constructs a `0x1f0`-byte vehicle-physics object through `0x1c146f0`, and initializes it with `0x1c20a60`.

The exact class names of those vehicle-physics objects remain **unknown**.

`EPhysicsMotionType`, descriptor `0x48c6578`, establishes:

```text
1 = Dynamic
2 = Keyframed
3 = Static
```

For `PhysicsCollisionInstance`, virtual `+0xe8` resolves to `0x1be4fd0`:

```asm
0x1be4fd0  mov [rcx+0x5c],edx
0x1be4fd3  jmp 0x1c3b590
```

The latter applies the selected motion type to the physics body.

Crucially, an existing native caller updates the collision body separately from the mode-1 mover:

```asm
0x2fa5a02  mov  rcx,[rdi+0x340]
0x2fa5a09  lea  rdx,[rdi+0x8f8]       ; WorldTransform
0x2fa5a10  xor  r8d,r8d
0x2fa5a19  call [rax+0xb0]            ; collision transform
0x2fa5a1f  mov  rcx,[rdi+0xb0]
0x2fa5a26  lea  rdx,[rdi+0x8f8]
0x2fa5a2d  call 0x2f801b0             ; mover target
```

Collision virtual `+0xb0` resolves to `0x1c46ee0`. Mode 1 itself reaches `0x22e97f0`, which updates the entity transform and dirty flag.

**Candidate, untested:** retain the pooled suppression flags, acquire the model’s primary collision instance, select Keyframed motion, establish its activation/filtering, and update its collision transform alongside each mover target. This could avoid constructing the ordinary driving simulation.

**A sufficient minimal recipe is unknown.** The pooled instance’s body availability, activation, collision filtering against the porter, teardown ownership, and persistence behavior were not established. Calling the entire skipped setup would construct normal vehicle-physics state and auxiliary bodies; it is not a proven static display-body solution. No claim of infeasibility is established either.

### Wheel rotation and steering

The reviewed native wheel-pose paths consume vehicle-physics state:

- Bike update `0x2fa3370`, called at `0x2f9d1fd`, reads entity `+0x330`.
- At `0x2fa39ed`, it calls `0x1c1e6a0` to obtain per-wheel orientations. That helper reads `*(physics+0x28)+0x88`, uses wheel-record stride `0xc0`, and derives orientations from record `+0x70/+0xa8`.
- At `0x2fa3a2b`, it calls `0x1c1dd10` for per-wheel positions/contact state from the physics structures.
- Bike resource fields `FrontWheelLeftIndex`, `FrontWheelRightIndex`, `BackWheelIndex`, and `HandleIndex` occupy `+0x2e8..+0x2f4`; they identify bones, not current wheel angles.
- Entity `+0x58c` feeds the seated porter’s `Turn_Heading` animation calculation. That does not establish it as a sufficient wheel-visual setter.

**Conclusion:** the reviewed wheel visuals depend on physics-derived pose data. A supported scalar wheel-spin/steer field or physics-free setter for pooled display vehicles is **unknown**. Direct bone-pose playback is a possible separate approach, but its update phase and overwrite behavior are unverified.

### The partner's vehicle paint, live (2026-11-09)

The reader takes the riding vehicle's variant (`+0x4c8`) and paint word (`+0x664`); the vehicle block carries them
(byte 97, bytes 112-113) and the partner's world passes them to the creator as `type | variant << 6` and the data
word, as the game's copy wrapper `0x2f533a0` does. Only ordinary looks are copied (variant 0 for a truck, 0 or 1 for
the bike, both colour channels below 30): anything else is drawn in the default finish. Live, two copies: the driver's
green truck had paint word `0x04d3` (both channels 19); the partner's truck in the other world got the same word and
drove up the ramp green instead of unpainted grey.

## Carried cargo mesh realization and native slot insertion — static follow-up

Pinned executable SHA-256:
`9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`.
PE headers and section mappings parsed independently; image base `0x140000000`.
All addresses below are **RVAs**. Evidence comes from
`x86_64-w64-mingw32-objdump -d -M intel`; nothing was called.

### Corrections to the previous layout

**Record `+0x90` points to its slot, not directly to its carrier.**
The slot points back to the carrier at `+0x28`.

The native stack append routine `0x2c2b530` establishes this chain:

```asm
; r14 = stack, rsi = cargo record
2c2b60e  mov r15,[r14+0xc0]       ; stack's slot
2c2b615  mov rax,[r15+0x28]       ; slot's carrier
2c2b619  movzx r13d,byte [r15]    ; carrier type
2c2b61d  mov edi,[rax+0x38]       ; carrier ID
2c2b63c  mov [rsi+0x80],r13b
2c2b643  mov [rsi+0x84],edi
2c2b649  mov [rsi+0x90],r15
```

Consequences:

- `carry_visual.cpp::readCarried` currently compares record `+0x90` with the
  carrier. Normally inserted records would fail this check. Validate against the
  current slot, then validate slot `+0x28` against the carrier.
- Record `+0x84` is written with the current carrier ID on this path. It must not
  be treated as an independently stable cargo identity.
- Slot `+0x04` is accumulated cargo **volume units**, rather than a literal case
  count. Insertion adds the volume result there; slot `+0x0c` increments once per
  appended record.

The executable also scans **`0x1e98` records (7,832)**, exceeding the reader’s
511-entry bound. For example, `0x2c55180`:

```asm
2c55183  lea rax,[rcx+0x38]       ; first record +0x18
2c551a1  add rax,0x240
2c551a7  cmp r8,0x1e98
...
2c551b6  lea rcx,[rax+rax*8]
2c551ba  shl rcx,6               ; index * 0x240
2c551be  lea rax,[r10+0x20]
```

This establishes the native scan extent, not how many records are occupied.

### What draws ordinary cases

The cargo code realizes a **`DSAnimatingSkinnedMeshInstance`** and retains its
pointer at **record `+0x148`**. An entity is not required by this branch.

Type evidence: getter `0x2c5d2a0` resolves descriptor `0x4c36e70`, whose name at
descriptor `+0x40` is `DSAnimatingSkinnedMeshInstance`. Its constructor
`0x2c1fd40` installs the RTTI-bearing interface vtable `0x3dd92d0` at object
`+0x168`; the full allocation size in the descriptor is `0x1e0`.

The relevant realization path is:

| RVA | Observed behavior |
|---|---|
| `0x2c47b50` area | Enumerates a carrier’s records through `0x2c53340`; calls `0x2c488c0` for eligible records at `0x2c47c4e`. |
| `0x2c488c0` | Returns an existing record `+0x148` instance; refuses category byte `+0x20 == 5`; selects resources and builds a mesh instance. |
| `0x2c5acc0` | Selects the model resource from the item definition and variant flag. |
| `0x2cd4660` | Allocates and initializes `DSAnimatingSkinnedMeshInstance` from a visual parameter structure. |
| `0x2c49328` | Stores the resulting instance at record `+0x148`. |
| `0x2c4e6b0` | Releases the instance through virtual `+0x80`, clears record `+0x148`, and handles additional visual objects. |

Concrete realization evidence:

```asm
2c48931  mov rax,[rdi+0x148]      ; existing instance
...
2c48978  mov rdx,[rdi+0x28]       ; DSGameBaggageListItem
2c4897f  shr r8d,5
2c4898b  and r8b,1               ; record +0x204 bit 5 variant
2c489a0  call 2c5acc0
...
2c489e5  mov rax,[rsi+0x30]      ; selected resource's loaded object
2c48a6e  mov [r14],rax           ; visual parameter +0
...
2c48cb5  mov rcx,r14
2c48cb8  call 2cd4660
...
2c49328  mov [rdi+0x148],rbx
```

**Inference:** this explains ordinary cases being visible without corresponding
cargo entities. Whether the renderer ultimately batches them into GPU instanced
draw calls is **unknown**; the established object is a mesh instance per realized
record.

There is also a separate queued visual path:

- `0x2c49b90` maintains record-keyed entries of stride `0x28` at manager
  `+0x46da10/+0x46da18`, each containing visual descriptions of stride `0x150`.
- `0x2c4a1e0` fills a description, including record pointer at description
  `+0x110`, then submits it through `0x32f7980` using global `0x7c09dd8`.
- Submission writes a handle at description `+0x148`.
- `0x32f4fc4` also calls `0x2cd4660`.

The queued path’s exact role in ordinary rendering versus visual transitions or
effects is **unknown**. Do not equate it with the permanent record `+0x148`
instance path.

### Where model and size come from

`DSBaggage2Config` RTTI establishes:

- `ModelResources`: config `+0x198` array, pointer at `+0x1a0`.
- `DeadBodyBagModelResources`: config `+0x250`.
- Manager `+0x08` points to the config used by model selection.

`DSGameBaggageListItem` RTTI establishes:

| Offset | Field |
|---:|---|
| `+0x3b` | `TypeOfCase` |
| `+0x3c` | `TypeOfContentsDamage` |
| `+0x3e` | `TypeOfVolume` |
| `+0x50` | `Weight` |

Selector `0x2c5acc0(manager,item,variant)` first searches the manager’s additional
asset list at `+0x46dfb0/+0x46dfb8`, matching item definition `+0x34`. A matching
asset can supply its model at `+0x38`. Otherwise it calls
`0x2c5ad70(config,item,variant)`.

The fallback selects model entries using case type, volume type, damage type,
specific definition hashes and the variant flag. In its generic volume branch,
volume values 0/1/2 select array byte offsets `0x08/0x10/0x18`, or
`0x20/0x28/0x30` for the variant; volume 3 selects `+0x68`. Special-case branches
override these choices.

`0x2c563a0(record)` converts ordinary volume values 0/1/2/3/4 to
**1/2/4/6/8 volume units**, with a case-type exception returning 8.

Resource lifetime matters: `0x2c6b8d0` walks the 31 model entries, takes each
resource’s `+0x30`, and registers it through `0x2dcd770` with argument 5.
**Inference:** the normal path expects resource/streaming setup before mesh
construction. Safe reproduction of that lifetime contract is **unknown**;
a model pointer alone is insufficient evidence of readiness.

### Native insertion calls

Two slot-level wrappers are established:

```cpp
// Inferred names; Microsoft x64 ABI.
bool insertInlineBatch(Slot* slot, InlineBatch* batch,
                       int placementMode, bool realize); // RVA 0x2c7dd50

bool insertPointerArray(Slot* slot, PointerArray* records,
                        int placementMode, bool realize); // RVA 0x2c7dde0
```

Observed contracts:

- Inline batch: signed count at `+0x00`, inline record pointers beginning
  `+0x10`.
- Pointer array: signed count at `+0x00`, array pointer at `+0x08`.
- Both wrappers enter slot `+0x78` and return the worker’s boolean.
- `0x2c7dd50` calls worker `0x2c7e280`.
- `0x2c7dde0` calls `0x2c7e9b0`, which converts the pointer array into an inline
  batch and calls `0x2c7e280`.
- Calls at `0x2cffd6a`, `0x2cffd7d` and `0x2cffdb6` demonstrate native use.
- Exact placement-mode meanings remain **unknown**. Native callers use 0 and 2.
- The final boolean is combined with slot eligibility at
  `0x2c7e2be–0x2c7e2ca`, then reaches the append routine’s mesh-realization gate.
  “Realize” is an inferred name.

Worker `0x2c7e280` checks compatibility through `0x2cd22c0`, checks record
location byte `+0x21` for 1 or 8, chooses or allocates stacks, and calls
`0x2c2b530(stack,record,realize,-1)`.

The append routine:

- inserts into stack `+0x10/+0x18`;
- updates stack volume `+0x34` and weight `+0x38`;
- updates slot volume `+0x04`, weight `+0x14`, and count `+0x0c`;
- updates carrier volume `+0x04` and weight `+0x0c`;
- writes record carrier type `+0x80`, carrier ID `+0x84`, and slot `+0x90`;
- changes applicable record locations to 2;
- can call `0x2c488c0` to realize the mesh;
- subsequently calls global cargo bookkeeping.

These are gameplay mutations. Slot locking does not establish arbitrary-thread
safety. The exact permitted thread/job phase is **unknown**; retain the game-thread
mutation boundary and do not invoke them from the networking worker.

### Requirements and remaining risks for a display carrier

Established layout requirements beyond the existing empty carrier include:

| Object | Fields consumed by the native path |
|---|---|
| Slot | carrier type `+0x00`, slot type `+0x01`, counters above, stack array `+0x18/+0x20`, carrier backpointer `+0x28`, initialized lock `+0x78` |
| Stack | shape/capacity pointer `+0x00`, record array `+0x10/+0x18`, volume `+0x34`, weight `+0x38`, slot backpointer `+0xc0`, flags near `+0xf0` |
| Record | valid item definition `+0x28`, state/location, initialized lock `+0x210`, holder fields and visual lifetime state |

`0x2c4bd90` creates a slot-appropriate stack and writes stack `+0xc0` at
`0x2c4bf1b`. A zeroed stack is therefore not an established substitute.

**Minimal sufficient initialization is unknown.** Merely inserting N pointers
into the copied backpack’s slot arrays is not proven to trigger realization or
maintain moving case transforms. The complete stack-to-world transform update
chain remains unresolved.

**Mod-owned records:** the mesh factory itself accepts visual parameters rather
than requiring a manager-table address. That is a candidate route for independent
visuals. However, native carrier insertion reaches global cargo systems, carrier
IDs, counters and record bookkeeping. Safe use with mod-owned records is
**unknown**.

**Allocation/free:** creation requests reach `0x2c470e0` through queue processing
(`0x2c957da`); this stages work and returns no demonstrated synchronous
allocate-and-attach contract. A safe record allocator/free pair for an arbitrary
display carrier was **not established**. The existing `DeleteBaggage` queue path
addresses the manager’s records by index and is not a destructor for external
records.

**Persistence and delivery risks:** native insertion changes actual carried
state and weight. Record reset `0x2ca4710` also constructs an actor ID from
record `+0xd4` and calls actor-registry removal. Consequently, bypassing manager
ownership can leave stale visual/actor references; using real manager records
can affect inventory queries and saved cargo. Exact save exclusion and
delivery/order side effects are **unknown**.

The most promising visual-only follow-up is the
`0x2cd4660` mesh-instance construction path with correctly retained resources and
explicit transform/lifetime management. It remains an untested candidate,
not a ready-to-call implementation.

## Visual-only cargo stack on the partner body — static recipe

Pinned executable SHA-256: `9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`. All addresses below are **RVAs**, image base `0x140000000`.

**Established** means supported by the cited disassembly or executable data. **Inferred** identifies the proposed record-free use. Nothing described here was called in the game.

### Creation and draw registration

**Established:** `0x2cd4660` allocates and fills the mesh instance; it does **not** complete scene registration. The native caller subsequently sends `MsgInit`:

```asm
2c48cb5  mov rcx,r14
2c48cb8  call 2cd4660
...
2c48cc8  lea rax,[3ba7b28]       ; MsgInit vtable
...
2c48cf8  call 19f4ae0           ; reflected message dispatch
```

The instance descriptor’s message table at `0x4c36f30` maps `MsgInit`, descriptor `0x486b1c0`, to thunk `0x2cdedf0`, which jumps to **`0x2c6f3b0(instance, message)`**.

**Established:** this initializer creates a `DSMovableSkinnedMeshInstance`, stores it at instance `+0x48`, initializes its transform, and registers its scene interface:

```asm
2c6f439  lea rcx,[4bab4f0]      ; DSMovableSkinnedMeshInstance
2c6f440  call 19f12f0
...
2c6f476  mov [rsi+48],rbx
...
2c6f65d  lea rdx,[rsi+b0]       ; initial transform
2c6f675  call 1e0b020
...
2c6f90f  mov rcx,[578c130]      ; scene interface
2c6f91b  mov rax,[rsi+48]
2c6f925  lea rdx,[rax+60]
2c6f92d  call [r8+28]           ; scene add
```

**Established:** `MsgInit` is `0x10` bytes, alignment 8. Native construction sets its vtable to `base+0x3ba7b28` and byte `+8` to zero; the remaining bytes can be zero-initialized.

**Inferred minimal creation sequence:**

1. Select and validate a loaded ordinary case model as described below.
2. Fill the visual parameter block below, using a valid initial world transform.
3. Call `0x2cd4660(&parameters)`.
4. Send `MsgInit`, either through the native reflected dispatch or its established handler `0x2c6f3b0(instance,&message)`.
5. Apply the initial transform through virtual `+0x78`.
6. Match the carried mesh’s finishing calls: virtual `+0x88(instance,4)`, `+0xa0(instance,9)`, then `+0x90(instance,true)`.

The finishing calls are established at `0x2c4978a–0x2c497c8`; their complete rendering semantics remain unknown.

### Visual parameter block

**Established:** the ordinary local block is **`0xa8` bytes**, initially zeroed at `0x2c489a5–0x2c489b8`. Its base is `rsp+0x2c0`. The following table covers its initialization and changes before the factory call. Unnamed fields retain offset names deliberately.

| Offset | Representation | Native initialization/source |
|---:|---|---|
| `+00` | pointer | Selected `ModelResource`’s loaded object at resource `+30`; `0x2c489e5`, `0x2c48a6e`. |
| `+08` | pointer | Initially null. Usually result of `0x2c541d0(manager,item,variant)`; case type `0xb` on carrier type `0xc` instead uses the pointer reached through config `+2b8`, then `+0`. `0x2c48b30–0x2c48b7a`. |
| `+10` | pointer | Null; optional override for instance `+30`, consumed only when nonnull. |
| `+18` | pointer | Null; optional override for instance `+40`, consumed only when nonnull. |
| `+20` | three doubles | Initial position, zero locally. |
| `+38` | nine floats | Initial basis, identity copied from `0x4591d70/0x4591d90`. |
| `+5c` | four bytes | Zero padding after the basis. |
| `+60` | 16-byte array header | Initially empty; count/capacity and pointer to 32-bit values. Conditional additions described below. |
| `+70` | 16-byte array header | Empty locally. |
| `+80` | `u32` | Initially `7`; becomes `2` for carrier type 1. |
| `+84` | `u32` | `0xffffffff`, loaded from `0x4563070`. |
| `+88` | byte | Initially 0; becomes 1 for carrier type 1. |
| `+89` | byte | Initially 0. Set to 1 unless the checks at `0x2c48c50–0x2c48c81` preserve zero; a further global predicate can set it at `0x2c48ca0`. |
| `+8a..+8c` | bytes | Zero locally. |
| `+8d` | byte | Set to 1 at `0x2c48a71`. |
| `+8e..+90` | bytes | Zero locally. |
| `+91..+97` | padding | Zero locally. |
| `+98` | pointer | Normally null. Special case type `0xa`, player hand slots, can select config `+230 -> +28 -> +18/+20`; `0x2c48ad1–0x2c48b22`. |
| `+a0` | byte | 1. |
| `+a1..+a7` | padding | Zero locally. |

**Established:** when record `+34 <= 0`, native code appends to `+60`:

- One of `0x4f2c5f8b`, `0x72d62591`, or `0x05064ba2`, selected by damage/case/volume fields.
- `0x2b3df951` for damage type 3, otherwise the value at `0x4c35d5c`.
- If predicate `0x2c63710(record)` succeeds, also `0x039e5aaa` and `0x616756be`.

Evidence: `0x2c48b7e–0x2c48c4c`; append helper `0x26867c0` copies one 32-bit value. The meanings of these identifiers are unknown.

**Established:** a caller-supplied parameter pointer replaces `r14` at `0x2c489e9`. Consequently, the local initialization does not establish defaults for that external block.

**Established factory mapping:** `0x2cd4660` copies position/basis to instance `+b0/+c8`, the arrays to `+90/+a0`, and:

```text
parameter +80 → instance +158
          +84 →          +13c
          +88 →          +15c
          +89 →          +15f
          +8a →          +161
          +8b →          +162
          +8c →          +163
          +8e →          +160
          +8f →          +166
          +90 →          +167
          +98 →          +188
          +a0 →          +1da
```

Evidence: `0x2cd46e4–0x2cd47d7`.

### Keeping the instance independent of cargo and physics

**Established:** none of this block’s ordinary fields receives a record, carrier or slot pointer. The factory consumes model/resource data and visual parameters.

**Established:** the cargo wrapper introduces a record association **after** initialization:

```asm
2c48d0a  mov rcx,[rbx+50]
...
2c48d16  lea rax,[rdi+8]        ; record interface
2c48d1e  mov [rcx+60],rax
```

Instance `+50` is a **`PhysicsCollisionInstance`**, established by descriptor `0x48c7d50` used in `0x2c47560`.

**Established:** parameter `+89=1` sets instance `+15f`, suppressing that collision-instance creation:

```asm
2cd46ed  mov byte [rbx+15f],1
...
2c6ed06  cmp qword [rcx+40],0
2c6ed0e  je ...
2c6ed10  cmp byte [rcx+15f],0
2c6ed17  jne ...
2c6ed19  call 2c47560
```

**Inferred minimal settings:** zero the block, set the model, transform, `+80=2`, `+84=-1`, `+88=1`, **`+89=1`**, `+8d=1`, and `+a0=1`. Leave `+08`, overrides, arrays, `+98` and animation-related bytes zero.

**Inferred restriction:** initially accept only models whose loaded object has an empty child-model list at `+d0`. `MsgInit` traverses this list through `0x2c70900`; child creation does not visibly copy the parent’s collision-suppression byte in the inspected assignment sequence. Root suppression alone therefore does not establish that an arbitrary compound resource is entirely collision-free.

**Established, bounded conclusion:** the inspected factory, transform setter and mesh update handler do not require a cargo record/carrier argument. The native collision owner rewrite above supplies the demonstrated record reference. Exhaustive behavior of arbitrary resource children and deeper renderer callbacks remains unknown.

### Per-frame transform and update

**Established:** the transform passed to the instance is:

```cpp
struct Transform60 {
    double position[3];     // +00
    float basis[9];         // +18, tightly packed
};                          // 0x3c bytes
```

Virtual **`+0x70`**, implementation **`0x2cb2a70(instance,&world)`**, writes the current transform:

```asm
2cb2aab  vmovups xmm0,[rdx]
2cb2aaf  vmovups [rcx+f0],xmm0
2cb2ab7  vmovsd xmm1,[rdx+10]
2cb2abc  vmovsd [rcx+100],xmm1
...
2cb2ade  vmovups ymm0,[rdx+18]
2cb2ae2  vmovups [rcx+108],ymm0
2cb2ae6  mov eax,[rdx+38]
2cb2ae9  mov [rcx+128],eax
...
2cb2af8  call 1e0b020          ; update underlying render instance
...
2cb2bd0  call 1c82920          ; bounds/spatial update
```

**Established:** on its first placement, `+0x70` dispatches to virtual **`+0x78`**, implementation `0x2cb32a0`. That implementation performs unconditional placement and marks instance `+15e=1`. Child meshes receive composed transforms through `0x2c56ba0`.

**Established native carried path:** slot update **`0x2ccb240`**, called by carrier update `0x2ccdbd0`, walks each stack and record. It collects record `+148`, constructs the case world transform, calls virtual `+70`, and stores the same transform back into record `+38`:

```asm
2ccc526  mov rdx,[r14+148]      ; mesh
...
2ccd262  lea r8,[rsp+920]       ; case-local placement
2ccd272  lea rcx,[rsp+970]      ; stack/model transform
2ccd27d  call 1ae4bd0           ; compose → rsp+c90
...
2ccd2c0  mov rcx,[rbx]
2ccd2c3  lea rdx,[rsp+c90]
2ccd2ce  call [rax+70]
...
2ccd2fb  vmovups [r14+38],xmm0
2ccd301  vmovsd [r14+48],xmm1
```

**Established inputs to native placement:**

- Stack `+3c`: helper identifier.
- Stack `+40`: local stack transform.
- Stack `+80`: cached/current placement transform.
- Stack `+c8`: optional preceding/supporting stack.
- Stack flags `+f0/+f1`: choose direct placement, helper variants and other paths.
- Holder’s `SkinnedModel` component: lookup with descriptor `0x4b132c0`, then component `+40`.
- Helper query **`0x23a80f0(model,&helperId,&matrix64,0)`**.
- Matrix conversion **`0x19c7000(&transform60,&matrix64)`**.
- Transform composition **`0x1ae4bd0(&parent,&output,&local)`**.

Evidence: `0x2ccb543–0x2ccb7dd`, `0x2cccd42–0x2ccd14c`. The helper query returns a boolean; failed lookup must be handled.

**Established:** the default player BackPack helper identifier is **`0x024334b7`**, stored at RVA `0x4c3a0bc`. The player-slot constructor selects it for slot type 1 at `0x2c4c8a1–0x2c4c95b`, `0x2c4d26b–0x2c4d27d`. Its string name was not established.

**Established:** native carried visuals also receive an update through **`0x2c75980(instance,&MsgUpdate)`**, called at `0x2ccdb10`. This updates the render instance, recursively updates children and optionally performs animation work.

`MsgUpdate` is `0x28` bytes, alignment 8. Native construction at `0x2ccd5ff–0x2ccd64a` supplies:

```text
+00 vtable = base+0x3badf50
+08 byte = 0
+10 u32 = 0
+18 u64 = 0xffefffffffffffff
+20 u32 = 0
+24 float = frame delta
```

**Inferred frame sequence:** resolve the partner’s current helper matrix after its pose is available; compose partner world transform, helper transform, mod-owned stack placement and case placement; call mesh virtual `+70`; then call `0x2c75980` with the update message. All placement state can live in mod-owned memory.

### Stack spacing

**Established:** native placement uses **accumulated volume below the case**, not its ordinal index. The slot updater starts the accumulator at zero, passes it to `0x2c5b010`, and adds each case’s volume afterward:

```asm
2ccc4b4  xor ebx,ebx
...
2ccd0e9  movzx r9d,byte [rsp+44] ; accumulated volume
2ccd0f7  movzx r8d,byte [rsp+28] ; this case's volume
2ccd0fd  mov rcx,[r12]           ; placement shape
2ccd104  call 2c5b010
...
2ccdb7e  add ebx,[rsp+28]
```

**Established:** `0x2c5b010(shape,out,caseVolume,volumeBelow)` scans four entries at shape `+28`, stride `0x30`. Each entry contains:

| Offset | Meaning |
|---:|---|
| `+00` | Case-volume key, byte |
| `+01` | Volume-below key, byte |
| `+10/+14/+18` | Three float translation components |
| `+20/+24/+28` | Three float rotation inputs |

It converts these into `Transform60`; no matching entry returns identity. Evidence: `0x2c5b02b–0x2c5b1ae`. These are packed arrangements, so total stack `+34` volume is **not a height in metres**.

**Established:** ordinary case dimension constants, initialized by `0x1891fb0`, `0x1892170`, `0x1892190`, `0x18921b0`, are:

| Volume units | Model-space dimensions |
|---:|---|
| 1 | `(0.382, 0.132, 0.274)` |
| 2 | `(0.382, 0.132, 0.548)` |
| 4 | `(0.382, 0.264, 0.548)` |
| 6 | `(0.382, 0.396, 0.548)` |

**Established:** native backpack placement separates neighboring stacks using half of each stack’s dimension in the **second local axis**, then adds the resulting negative-axis translation. Evidence: `0x2c4cf22–0x2c4cff8`, `0x2c4d154–0x2c4d258`. Native packing also rotates cases and changes depth offsets.

**Inferred simplest N-case display:** use one case per layer, retain one chosen case orientation, and separate layer centers by half the preceding height plus half the next height:

```text
center[0] = chosen anchor
center[k+1] = center[k] - localAxisY * (height[k] + height[k+1]) / 2
```

For identical ordinary medium cases, the separation is `0.132` model-space units. Compose this local axis through the helper/body transform; do not apply it directly to world Y.

This supplies a simple visual stack without reproducing native packed layouts. Exact model pivot alignment and the desired anchor offset remain visually unverified. Special cases, body bags and volume-8 objects need separate treatment.

### Model selection and retention

**Established callable contract, Microsoft x64 ABI:**

```cpp
ModelResource* selectCaseModel(
    Manager* manager, ItemDefinition* item, bool variant);
// RVA 0x2c5acc0; RCX, RDX, R8B; result RAX.
```

It searches additional assets by item `+34`, returns a matching nonnull asset `+38`, or calls `0x2c5ad70(manager->config,item,variant)`. The inspected selector only reads these objects; it performs no allocation, loading, cargo insertion or reference acquisition. Evidence: `0x2c5accf–0x2c5ad5a`.

**Established:** the return is the **resource wrapper**, not its loaded object. Creation uses wrapper `+30`. The fallback reads item `+34`, `+3b`, `+3c`, `+3e` and the variant flag. Its complete implementation is `0x2c5ad70–0x2c5afbb`; ordinary generic choices are config model-array indices 1/2/3, or 4/5/6 for the variant, with volume 3 choosing index 13. Special case types and definition hashes override these choices.

**Established:** the 31 default model resources are **not permanently retained merely because config was once loaded**:

- Registration skips null wrapper `+30` and calls `0x2dcd770(streamingManager,loadedObject,5)`: `0x2c6b8d0–0x2c6b905`.
- A matching removal path walks the same 31 entries, calls `0x2dcdae0(...,5)`, and clears manager config `+8`: **`0x2c6d9b0–0x2c6da35`**.
- Registration maintains entries keyed by object identifiers `+8/+10`, increments entry `+18`, and issues requests through `0x22b6e50`: `0x2dcd904–0x2dcd9b5`.
- Removal decrements `+18` and releases request data when it reaches zero: `0x2dcdb54–0x2dcdc20`.

**Established:** the mesh’s own retention settings are balanced. With parameter `+88=1` and `+80=2`, initialization registers its model with request category 2 and sets instance `+15d`; the destructor unregisters it. Evidence: `0x2c6ef4b–0x2c6ef7c`, `0x2c2501f–0x2c25060`.

**Inferred requirement:** validate the selected wrapper, loaded object and factory-required nested model data before creation; keep ownership valid throughout the instance lifetime. A nonnull config array entry is insufficient. These routines do not establish synchronous GPU readiness or a complete lifetime guarantee for a cached raw loaded-object pointer.

### Removal

**Established:** release the owned mesh once through **virtual `+0x80`**. It reaches `0x2c4dee0`, the deleting-destructor path through the RTTI interface at `+168`, and destructor `0x2c253e0`.

The destructor removes the render instance from the scene:

```asm
2c25412  mov rdx,[rcx+48]
...
2c25425  mov rcx,[578c130]
...
2c25434  add rdx,60
2c25438  call [rax+30]          ; scene remove
```

It then releases renderer/animation allocations. The base destructor balances residency, unregisters collision if present and releases child meshes (`0x2c25010–0x2c2518e`).

**Established:** `0x2c4e6b0` additionally disposes record-owned visual-object arrays at record `+158/+160` and `+168/+170`. Those arrays are separate from a standalone mesh. **Inferred:** a mod owning only the mesh uses its virtual release and clears its own pointer; it must not call the record cleanup routine.

### Threading, remaining unknowns and fault risks

**Established:** native placement runs through carrier/slot update code, reads animation helper buffers, and mutates renderer and spatial state. The inspected functions contain no general-purpose dispatch to a safe engine phase.

**Inferred requirement:** create, initialize, place, update and release on the existing game-thread mutation boundary, with the partner’s pose available and before dependent scene teardown. This is a conservative boundary; the exact required phase is **unknown**.

| Step | Remaining unknown / risk |
|---|---|
| Model acquisition | **High:** loading completion and raw-pointer lifetime across resource-set unload are not established; stale nested pointers can fault. |
| Factory | **High:** allocation failure is not safely handled internally: after a null-allocation branch it still writes through `rbx` at `0x2cd468a`. |
| `MsgInit` | **High:** requires valid renderer/resource infrastructure and phase. Compound models may create children with their own behavior. |
| Collision suppression | **Established for the root; unknown for arbitrary children.** Restrict the first implementation to an inspected child-free case resource. |
| Helper lookup | **Medium:** helper presence and bone-buffer readiness on the partner body remain unverified. Refuse placement when lookup fails. |
| Placement | **Low visual risk:** anchor, pivot and axis errors can cause clipping or misplaced cases; malformed pointers or transforms can still fault renderer code. |
| Update | **Medium:** the exact supported update phase and behavior of unusual model resources remain unknown. |
| Release | **High during teardown:** release once while scene/streaming infrastructure remains valid; double release or delayed release can fault. |

**Inferred conclusion:** the smallest justified implementation owns mesh pointers and transforms only, uses the factory plus `MsgInit`, suppresses root collision, updates through virtual `+70` and `MsgUpdate`, and releases through virtual `+80`. It needs no cargo-record allocation or mutation, carrier/slot insertion, or cargo-manager table writes. It remains an untested native-call recipe.

## Display vehicle wheels: spin and steering — static recipe

**Established — scope:** Static inspection of build 13300582, executable SHA-256 `9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`. Addresses below are RVAs from image base `0x140000000`. No game ran and no native game function was called.

**Established — result:** The inspected wheel paths produce joint transforms directly. They expose substantially more than the previously identified physics dependency, but do **not** establish a safe scalar spin/steering setter for a pooled display vehicle.

### Wheel state and its writers

**Established — shared physics layout:** For both concrete vehicles, let:

```text
V = vehicle entity
P = *(V + 0x330)             // game vehicle-physics wrapper
H = *(P + 0x28)             // hknpVehicleInstance
W = *(H + 0x88)             // WheelInfo array
wheel[i] = W + 0xc0*i
```

**Established — identification evidence:** Wrapper initialization `0x1c20970` calls `0x215a120`, storing its result at wrapper `+0x28` at `0x1c20a01`; bike initialization `0x1c20a60` does likewise at `0x1c20aee`. The Havok instance vtable is `0x3cc75b8`. Reflection identifies `hknpVehicleInstance` at descriptor `0x4ad1ba0`, and `hknpVehicleInstance::WheelInfo` at `0x4ad1a10`, with size `0xc0`.

**Established — fields:** The member tables at `0x3cb7e80` and `0x3cb9010` explicitly name these fields:

| Object | Offset | Reflected name | Representation |
|---|---:|---|---|
| `H` | `+0x108` | `mainSteeringAngle` | float |
| `H` | `+0x110` | `wheelsSteeringAngle` | float-array data pointer; count `+0x118` |
| `wheel[i]` | `+0x50` | `currentSuspensionLength` | float |
| `wheel[i]` | `+0x70` | `spinAxisChassisSpace` | float4 |
| `wheel[i]` | `+0x90` | `steeringOrientationChassisSpace` | quaternion float4 |
| `wheel[i]` | `+0xa0` | `spinVelocity` | float |
| `wheel[i]` | `+0xa4` | `noSlipIdealSpinVelocity` | float |
| `wheel[i]` | `+0xa8` | `spinAngle` | float |

**Established — frame writers:**

| Function RVA | Observed writes |
|---|---|
| `0x215ad70` | Computes wheel angular velocity, advances `spinAngle`, and stores `spinVelocity`. |
| `0x215db00` | Dispatches steering-component virtual `+0x18`, then stores main and per-wheel steering angles. |
| `0x215dd70` | Copies wheel-collision results, including suspension length, and constructs steering quaternions and spin axes. |

**Established — spin integration evidence:**

```asm
215b046  vdivss xmm3,xmm1,DWORD PTR [rcx+rbp]
215b04b  vmulss xmm0,xmm3,xmm9
215b050  vminss xmm1,xmm0,xmm14
215b055  vaddss xmm2,xmm1,DWORD PTR [rbx+rdi+0xa8]
215b05e  vmovss DWORD PTR [rbx+rdi+0xa8],xmm2
...
215b07d  vmovss DWORD PTR [rbx+rdi+0xa0],xmm3
215b090  add    rbx,0xc0
```

**Established — suspension and steering evidence:**

```asm
215de17  mov    eax,DWORD PTR [r15+0x1c]
215de1b  mov    DWORD PTR [rbx+rbp+0x50],eax

215de7a  mov    rax,QWORD PTR [rdi+0x110]
215de81  vmulss xmm1,xmm8,DWORD PTR [rax+r13]
...
215dee8  vmovups XMMWORD PTR [rdx],xmm4  ; wheel +0x90
215def5  vmovups xmm0,XMMWORD PTR [rax+0x60]
215defa  vmovups XMMWORD PTR [r8],xmm0   ; wheel +0x70
```

**Inferred — units:** `spinAngle` is radians and `spinVelocity` is radians/second: the angle feeds a half-angle sine/cosine quaternion constructor, while the integrator multiplies angular velocity by its timestep. Per-wheel steering angles use radians; their conversion constructs a normalized quaternion from half-angle times suspension direction and `w=1`. Suspension length uses physics/world distance units, rather than degrees or a normalized animation control.

### From physics state to mesh joints

**Established — common wheel-pose reader:** `0x2f6c980` calls `0x1c1f730`, which dispatches `H` virtual `+0x50`, resolving to `0x215b870`.

**Established — its inputs and calculation:**

```asm
215b8c0  shl    rbx,0x6
215b8c4  add    rbx,QWORD PTR [rcx+0x88] ; WheelInfo, stride 0xc0
215b8d8  vmovss xmm0,DWORD PTR [rbx+0xa8]
215b8e0  vxorps xmm2,xmm0,[signMask]
215b8e8  lea    rdx,[rbx+0x70]
215b8ec  call   0x1e77c60               ; quaternion(axis, -spinAngle)
215b8f1  vmovups xmm8,XMMWORD PTR [rbx+0x90]
...
215b965  vmovups XMMWORD PTR [rsi],xmm2
215b96d  vmaxss xmm2,xmm4,DWORD PTR [rbx+0x50]
215b977  vmulps xmm0,xmm1,XMMWORD PTR [rdi+0x10]
215b97c  vaddps xmm2,xmm0,XMMWORD PTR [rdi]
```

**Established:** This combines steering and spin quaternions, and obtains the wheel center from the suspension hardpoint plus direction times nonnegative suspension length. It subsequently transforms those results through the supplied chassis transform.

**Established — entity-side arrays:** Vehicle `+0x4c0` is the wheel count; `+0x4d8` points to joint indices. `+0x4e0/+0x4e8` point to current/previous wheel-center float4 arrays, **not scalar spin-angle arrays**. The copy loop at `0x2fa5e60` moves current centers to previous centers, then copies newly calculated centers into current centers.

| Vehicle | Count | Joint-index storage | Center storage |
|---|---:|---|---|
| Truck | 4 | entity `+0xc60..+0xc6c` | current `+0xbe0`, previous `+0xc20` |
| Motorbike | 3 | entity `+0xcb0..+0xcb8` | current `+0xbe0`, previous `+0xc10` |

**Established — array evidence:** Constructors `0x2f62450` and `0x2f61b80` establish these pointers and counts at `0x2f62563..0x2f625b0` and `0x2f61ce0..0x2f61d33`. Truck initialization `0x2f74e90` copies resource indices `+0x2e8/+0x2ec/+0x2f0/+0x2f4`; bike initialization `0x2f74820` copies `+0x2e8/+0x2ec/+0x2f0`.

**Established — truck pose writer:** Truck vtable `0x3e26790`, slot `+0x168`, selects `0x2f9e750`. It obtains physics wheel poses through `0x2f6c980` at `0x2f9f133`, obtains contact data at `0x2f9f170`, and invokes joint/suspension geometry helper `0x2f85410` for each wheel at `0x2f9f310`.

**Established — useful lower-level truck boundary:** The complete `0x2f85410..0x2f864f7` helper takes:

```cpp
// Inferred names; argument positions and buffer accesses established.
void TruckWheelGeometry(
    Vehicle* vehicle,             // RCX
    int wheelIndex,               // EDX
    Quaternion* jointRotations,   // R8
    Float4* jointPositions,       // R9
    const Quaternion* wheelRotations, // stack argument 5
    const Float4* wheelCenter);      // stack argument 6
```

**Established:** Its inspected body operates on supplied arrays, skeleton ancestry and bind transforms. It does not directly read vehicle `+0x330`. It follows several wheel ancestors without validating their indices, so this is not a generally safe arbitrary-joint API.

**Established — bike pose writer:** Bike vtable `0x3e25db8`, slot `+0x168`, selects `0x2f9d170`; its ordinary branch calls `0x2fa3370` at `0x2f9d1fd`. The bike builds wheel/fork/handle geometry through `0x2fa1fb0` and related helpers after obtaining complete wheel poses, spin-only quaternions and contacts.

**Established — bike helper hazard:** Calling `0x2fa1fb0` separately does not remove the physics dependency. It calls `0x2f6a800` at `0x2fa2135`; that helper immediately reads `vehicle+0x330`, then dereferences wrapper `+0xf0` at `0x2f6a82b`.

**Established — final pose destination:** Both full writers store local joint rotations and translations into:

```text
M = *(V + 0xb8)
k = int32(M + 0x250)
S = *(M + 0x190 + 0x70*k)       // skeleton
N = int32(S + 0x20)             // joint count
B = *(M + 0x1a0 + 0x70*k)       // writable pose records

joint[j].rotation    = B + 0x30*j
joint[j].translation = B + 0x30*j + 0x10
```

**Established — write evidence:**

```asm
; Truck
2f9f780  movsxd rax,DWORD PTR [rdx+0x250]
2f9f787  imul   rcx,rax,0x70
2f9f78b  mov    rax,QWORD PTR [rcx+rdx+0x1a0]
...
2f9f7ae  vmovdqu XMMWORD PTR [rax-0x30],xmm0 ; rotation
2f9f7b8  vmovdqu XMMWORD PTR [rax-0x20],xmm1 ; translation

; Bike performs the equivalent stores at 0x2fa427e and 0x2fa4288.
```

**Established — animation API distinction:** These inspected wheel writers use direct pose-array arithmetic and stores. Neither full writer directly calls the porter’s `TriggerBool` or float-variable setters. No named wheel-speed/steer variable binding was established here; the vehicle may still use animation components for other purposes.

### Playback gating and the smallest candidate

**Established — separate gates:** The normal vehicle update first bypasses its physics-dependent body when `vehicle+0x330` is null:

```asm
2f78def  cmp    QWORD PTR [rdi+0x330],0
2f78df7  je     0x2f79905
```

**Established:** When that body is reached, the pooled flag independently skips the wheel-pose dispatch:

```asm
2f79004  test   DWORD PTR [rdi+0x560],0x08000000
2f79012  jne    0x2f79069
...
2f79063  call   QWORD PTR [rax+0x168]
```

**Established:** `VehicleMover` mode dispatch at `0x2f899d0` does not perform wheel-pose updates: mode 1 tail-calls the transform application helper. Therefore changing mover mode or its target alone cannot supply spin, steering or suspension state.

**Established — unresolved requirement:** No single native call or scalar field write was established that accepts angular speed and steering angle and safely animates both pooled vehicle types without physics. Writing Havok fields requires objects the pooled path can omit; calling the complete vehicle pose writers can fault.

**Inferred — minimal physics-free candidate, not validated:**

1. Retain the pooled suppression flags and mode-1 transform playback.
2. Maintain mod-owned wheel phases: `phase[i] += angularSpeed[i] * dt`, using radians and seconds. Reset or freeze integration across teleports, stale samples and identity changes.
3. Build wheel rotations from a retained rest pose, using steering followed by spin about each wheel’s correct axle. Convert the resulting transforms into the skeleton’s parent-local space before writing them.
4. Write only the affected joint records at `B+0x30*j`, using the native rotation/translation layout above. The truck geometry helper is a candidate for reproducing its suspension linkage using supplied poses; the bike requires a separately derived physics-free joint calculation.
5. Apply these overrides after the mover has applied the desired chassis transform and after competing pose writers, before skinning consumes the pose.

**Inferred — speed estimate:** Signed longitudinal displacement divided by timestep and wheel radius can supply approximate angular velocity. Bike resource `FrontWheelRadius` is reflected at `+0x2e0`; a radius must be validated for each wheel/variant before use.

**Inferred — steering limitation:** The received porter `Turn_Heading` is not sufficient evidence of the actual road-wheel angle. Truck code separately scales control input by π/2 for its steering-wheel joint at `0x2f9f662`; this pass did not establish the inverse mapping from the porter variable to Havok’s per-wheel steering angles.

**Inferred — suspension limitation:** Chassis transforms and speed cannot uniquely recover individual suspension compression. A display implementation must retain rest suspension, synthesize it, or receive additional suspension data.

### Fault boundaries and unknowns

**Established — fault boundaries:**

- Null wrapper or Havok instance: the full wheel readers and writers contain unchecked dereferences.
- Invalid model, selected skeleton, joint index or ancestor: the pose and geometry paths use unchecked indexing.
- Bike helper reuse: `0x2f6a800` retains an unconditional physics dependency.
- Wheel-center caches are also consumed elsewhere; treating them as spin/steering scalars would corrupt positional state.

**Inferred — remaining risks:** Direct pose writes can be overwritten or race animation/skinning jobs. The exact frame phase, pose-buffer lifetime, required invalidation, rest-pose ownership, wheel-axis signs, bike steering/fork hierarchy and pooled-instance behavior remain **unknown**. Ordering merely after `0x2f801b0` is insufficient: that function sets a target rather than applying it immediately.

**Established — validation:** The executable hash matched; focused MinGW objdump windows corroborated the cited instructions; **29 read-only PE, metadata and disassembly assertions passed**. Runtime safety and visual results were not tested.

## Per-item case models for the partner's cargo — static recipe

**Established — scope:** pinned executable SHA-256 `9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`, image base `0x140000000`. All addresses below are **RVAs**. Assembly was inspected with `x86_64-w64-mingw32-objdump -d -M intel`; nothing was called in the game.

### Resolve the transmitted identifier without creating cargo

**Established:** the definition lookup used by queued cargo creation is **`0x3439df0`**, on the separate global **`0x7c0dd98`**, rather than the cargo manager at `0x7bf4968`.

```cpp
// Inferred descriptive name; established Microsoft x64 argument/result layout.
DSGameBaggageListItem* findBaggageDefinition(
    DefinitionManager* definitions, uint32_t identifier);
// RCX = *[base+0x7c0dd98], EDX = identifier; RAX = item or null.
```

**Established — creation-path evidence:** request processing loads request `+0x48` into `EDX` at `0x2c844ef`. The intervening category/capacity checks preserve that argument, then:

```asm
2c845c7  mov rcx,[7c0dd98]
2c845ce  call 3439df0
2c845da  mov rdx,rax
2c845e0  mov rdi,rax
...
2c846ed  mov [rbx+28],rdi       ; cargo record's item definition
2c846fb  test rdi,rdi
2c846fe  je 2c8599a            ; missing definition leaves this path
...
2c84709  mov eax,[r14+48]
2c8470d  mov [rbx+c8],eax       ; request identifier used when +64 was -1
```

**Established — lookup structure:**

| Location | Meaning |
|---|---|
| definition manager `+0x158` | Entry-array pointer |
| definition manager `+0x164` | Capacity used by lookup |
| entry stride | `0x18` bytes |
| entry `+0x00` | Stored hash; zero means empty |
| entry `+0x08` | Original 32-bit lookup key |
| entry `+0x10` | Definition pointer |

```asm
3439df2  mov r8,[rcx+158]
3439e11  mov r10d,[rcx+164]
3439e18  xor edi,edi
3439e1a  crc32 edi,edx
3439e1f  dec r10d
3439e22  or edi,80000000h
3439e28  mov r9d,r10d
3439e2b  and r9d,edi            ; initial bucket
...
3439e34  lea rcx,[rdx+rdx*2]
3439e38  cmp dword [r8+rcx*8],0 ; 24-byte entries
...
3439e92  cmp edx,edi            ; stored hash
3439e96  cmp [r11+8],esi        ; original key
...
3439ee0  mov rax,[r8+rdx*8+10]
3439ee6  ret
...
3439ef4  xor eax,eax
3439ef6  pop rbx
3439ef7  ret
```

**Established:** hashing is the x86 `CRC32` instruction over the 32-bit argument, seeded with zero, followed by OR `0x80000000`. Lookup masks and wraps with `capacity-1`, and uses stored-bucket ordering for early termination (`0x3439e40–0x3439eba`). Missing entries and a null entry-array pointer return null.

**Established:** the complete lookup contains no calls, allocation, loading, locking, reference acquisition, or writes through game-object pointers. It writes only its stack/register state. It does not validate a null manager pointer or protect against concurrent table replacement.

**Established — signature:** the following 25-byte prefix occurs once in the pinned executable:

```text
40 53 4C 8B 81 58 01 00 00 48 8B D9
48 89 74 24 10 8B F2 48 89 7C 24 18
```

**Established — identifier correction:** item metadata distinguishes **`+0x30 = ID`** from **`+0x34 = NameCode`**, inherited from descriptor `0x4df2bc0`; the item descriptor is `0x4df3100`. Native code also calls this lookup with item `+0x34`, for example `0x2c563aa–0x2c563b4`. Therefore, do not assume those fields are interchangeable or validate a lookup result solely by comparing item `+0x30` with the transmitted value.

**Inferred — concrete read-only alternative:** a bounded linear scan of occupied entries avoids reproducing the native probe ordering:

```cpp
definitions = read_ptr(base + 0x7c0dd98);
entries     = read_ptr(definitions + 0x158);
capacity    = read_u32(definitions + 0x164);

// Apply mod-owned bounds, overflow checks and guarded reads first.
for (uint32_t i = 0; i < capacity; ++i) {
    entry = guarded_copy(entries + uint64_t(i) * 0x18, 0x18);
    if (entry.hash != 0 && entry.key == transmittedIdentifier)
        return validate_definition(entry.item);
}
return nullptr;
```

**Inferred — synchronization:** perform lookup and selection within the existing game-thread callback while the relevant resource set is alive. Recheck the global/table header after guarded copying and abandon an inconsistent result. This reduces races but does not establish an atomic snapshot or arbitrary-thread safety; the engine’s precise permitted phase remains **unknown**.

### Select the carried model and its variant

**Established:** carried realization `0x2c488c0` obtains its variant from **record `+0x204`, bit 5**, calls **`0x2c5acc0(manager,item,variant)`**, then passes the returned wrapper’s **`+0x30` loaded object** to the mesh factory:

```asm
2c48969  mov r8d,[rdi+204]
2c48978  mov rdx,[rdi+28]
2c4897c  mov rcx,rbx
2c4897f  shr r8d,5
2c4898b  and r8b,1
2c489a0  call 2c5acc0
...
2c489e5  mov rax,[rsi+30]
2c48a6e  mov [r14],rax          ; visual parameter +00
```

**Established:** creation sets that bit for item **`+0x3a == 1`**:

```asm
2c84757  mov rax,[rbx+28]
2c8475b  movzx edi,byte [rax+3a]
2c8475f  cmp dil,1
2c84763  jne 2c8476c
2c84765  or dword [rbx+204],20h
```

**Established:** metadata names item `+0x3a` `AttributeOfBaggage`; enum descriptor `0x4de8a20` names value 1 **Personal**.

**Inferred — default without a record:** use `item->AttributeOfBaggage == Personal` to approximate initial native selection. Explicit `false` selects the base branch and is a conservative fallback when reproducing the record’s variant is impossible. Neither choice establishes the current remote record’s appearance: exact matching requires transmitting bit 5 or an equivalent appearance flag.

**Established:** selector `0x2c5acc0` first searches additional assets:

```text
manager +46dfb0 : signed count
manager +46dfb8 : pointer array
asset   +28    : count of NameCode hashes
asset   +30    : u32 hash array
asset   +38    : model-resource wrapper
asset   +40    : ArtPartsVariationResource pointer
```

**Established:** it compares asset hashes against item `+0x34`, returns a matching nonnull model wrapper, or falls back to **`0x2c5ad70(manager->config,item,variant)`** (`0x2c5accf–0x2c5ad46`). Metadata descriptor `0x4c37010` confirms `ListItemNameHashes`, `SkinnedModel`, and `ArtPartsVariation`.

**Established:** fallback resources come from the pointer array at **config `+0x1a0`**, whose array header begins at `+0x198`. Selection has the following precedence (`0x2c5ad70–0x2c5afbb`; switch table `0x2c5afbc`):

| Condition, after preceding rules fail | Returned model |
|---|---|
| NameCode in special group A below | Model index 18 |
| NameCode in special group B below | Model index 29 |
| Case type `0x12` | Model index 30 |
| Volume type 3, case type 9 | Model index 21 |
| Volume type 3, contents-damage type 3 | Model index 22 |
| Other volume type 3 | Model index 13 |
| Case type 1 | Model index 13 |
| Case type 2 | Volume 0 → index 15; volume 1 → 16; otherwise → 17 |
| Case type 6 | First `DeadBodyBagModelResources` entry |
| Case type 9 | Volume 0 → index 18; 1 → 19; 2 → 20; otherwise generic |
| Case type `0xa` | Model index 11 |
| Case type `0xb` | Model index 7 |
| Case type `0xc` | Model index 14 |
| Case type `0x11`, volume 0 | Model index 27 |
| Generic volume 0/1/2 | Base indices 1/2/3; variant indices 4/5/6 |

**Established:** group A is `{093f5c71, 7b54df72, 68042c86, 1a6faf85, 4ea5cb6e, 3cce486d}`; group B is `{03e867fc, 7183e4ff, 62d3170b, 10b89408, 4472f0e3, 361973e0}`. Comparisons occur at `0x2c5ad82–0x2c5ae18`. The generic branch forcibly clears the variant for NameCodes `{1a8b69f4, 4e410d1f, 3c2a8e1c}` (`0x2c5af23–0x2c5af3e`).

**Established:** unrecognized volume values reaching the generic branch fall through to its small-model choice; this is not evidence that malformed values describe valid small cases.

### Parameter `+08` supplies appearance overrides

**Established:** native carried realization also calls **`0x2c541d0(manager,item,variant)`** and stores its result directly at parameter **`+08`**:

```asm
2c48b60  mov r8d,[rdi+204]
2c48b67  mov rcx,rbx
2c48b6a  shr r8d,5
2c48b6e  and r8b,1
2c48b75  call 2c541d0
2c48b7a  mov [r14+8],rax
```

**Established:** this selector searches the same additional-asset list and returns matching asset `+0x40`; otherwise it tail-calls **`0x2c54270(config,item,variant)`** (`0x2c541db–0x2c5425a`). Unlike model selection, a matched asset’s null appearance pointer is returned directly.

**Established:** its type is **`ArtPartsVariationResource`**, descriptor **`0x4ae3a30`**. Config metadata calls the array at `+0x2b0/+0x2b8` **ArtPartsVariations**. The resource describes:

| Resource offset | Metadata field |
|---|---|
| `+0x20` | `ReplaceTextureSet` |
| `+0x30` | `ReplaceShaderVariable` |
| `+0x40` | `OnMesh` |
| `+0x50` | `OffMesh` |

**Established:** the factory copies this pointer unchanged into mesh `+0x18`; it is **not** another wrapper requiring a `+0x30` dereference:

```asm
2cd468e  mov rcx,[rdi+8]
2cd4692  mov [rbx+18],rcx
```

**Established:** `MsgInit` finishes by calling `0x2c6ed00` at `0x2c6fc21`. That routine checks mesh `+0x18`, traverses the appearance resource and applies its entries through `0x2cb0f60`, including to applicable child meshes (`0x2c6ed46–0x2c6ef11`).

**Established:** null is supported: `0x2c6ed4a–0x2c6ed4d` skips appearance processing. The selector itself can return null (`0x2c547cd`), including its early contents-damage-type-3 path.

**Inferred:** this is the mechanism needed to reproduce item-dependent texture/shader/mesh appearance when native selection returns an override. Changing only the model cannot reproduce those overrides. Which replacements correspond specifically to colour or printed labels remains **unknown** without inspecting the loaded resources or testing visually.

**Established:** selection also depends on item `+0x64` (`MissionId`) through global `0x7c0aff8` and lookup `0x333b470`, plus contents parameters and specific NameCodes (`0x2c54386–0x2c547cd`). Therefore, a hand-maintained “material by size” table is insufficient.

**Established:** the ground-only case-type-`0xb` override at `0x2c48b39–0x2c48b56` applies when carrier type is `0xc`. It does not replace the ordinary selector for carried cargo.

### Skip or substitute nonstandard objects

**Established:** item metadata provides case type at `+0x3b` and volume type at `+0x3e`. Case enum descriptor `0x4de8a60` supplies these relevant names:

| Item condition | Native distinction |
|---|---|
| Case type 2, `Weapon` | Dedicated model indices 15–17 in the non-LL branch |
| Case type 6, `BodyBag` | Uses `DeadBodyBagModelResources`; volume helper returns 8 |
| Case type `0xa`, `Cart` | Model index 11; carried hand slots can also supply animation data at parameter `+0x98` |
| Case type `0xb`, `ConstractionMachine` | Model index 7; ground appearance override |
| Case type `0xc`, `Ladder` | Model index 14 |
| Case type `0x11`, `SmallDelicate` | Special small model index 27 |
| Case type `0x12`, `ToxicGas` | Model index 30 |
| Volume type 4, `Human` | Volume helper returns 8; no ordinary-case dimension mapping established |

**Established:** enum values 0–3 for volume are `S`, `M`, `L`, `LL`; value 4 is `Human` (descriptor `0x4de8c50`).

**Established — body-bag model evidence:**

```asm
2c5ae80  mov rax,[r9+258]       ; config +250 array's pointer
2c5ae87  mov rax,[rax]          ; first body-bag wrapper
2c5ae8a  ret
```

**Inferred — conservative display filter:** initially skip/substitute case types 2, 6, `0xa`, `0xb`, and any volume type outside 0–3. Treat the other dedicated models and additional-asset models as requiring separate bounds/child-resource inspection. For a strict ordinary-case implementation, accept only reviewed selected wrappers from generic indices 1–6 and LL indices 13/21/22.

**Inferred:** case-type detection deliberately excludes weapons even when the selector could supply a packaged weapon model. Item hash alone does not establish whether the desired remote display is an ordinary case, an exposed weapon, or a slot-specific attachment.

### Dimensions and mixed-size spacing

**Established:** `0x2c563a0(record)` returns ordinary volume units **1/2/4/6/8** for item volume types **0/1/2/3/4**. Its case-type-6 exception returns 8:

```asm
2c563b1  mov edx,[rbx+34]
2c563b4  call 3439df0
2c563b9  cmp byte [rax+3b],6
2c563bd  je 2c563e2            ; return 8
2c563bf  movzx ecx,byte [rbx+3e]
...
2c563e2  mov eax,8
2c563ed  mov eax,6
2c563f8  mov eax,4
2c56403  mov eax,2
```

**Established:** the ordinary dimension constants are:

| Item `+0x3e` | Units | Model-space dimensions | Constant RVA → initialized global RVA |
|---:|---:|---|---|
| 0 | 1 | `(0.382, 0.132, 0.274)` | `0x3ddab00 → 0x7bf4a50` |
| 1 | 2 | `(0.382, 0.132, 0.548)` | `0x3ddab10 → 0x7bf4a30` |
| 2 | 4 | `(0.382, 0.264, 0.548)` | `0x3ddab20 → 0x7bf49d0` |
| 3 | 6 | `(0.382, 0.396, 0.548)` | `0x3ddab30 → 0x7bf49f0` |
| 4 / body bag | 8 | **Unknown for ordinary-case layout** | Separate treatment required |

**Established:** initializers are `0x18921b0`, `0x1892190`, `0x1892170`, and `0x1891fb0`, respectively. Carried placement selects these constants directly from item `+0x3e` at **`0x2ccd41a–0x2ccd466`**. Native neighboring-stack spacing uses their second local component (`0x2c4cf22–0x2c4cff8`).

**Inferred:** for fixed-orientation ordinary cases, space adjacent centres by half the previous dimension plus half the next dimension along the chosen stacking axis. Rotate that axis with the backpack/helper transform. Volume units are packing keys, not metres; custom models, pivots and native packing rotations need separate treatment.

### Minimal record-free acquisition

**Inferred — proposed sequence, not executed:**

```cpp
item = readOnlyLookup(transmittedIdentifier); // table scan above
if (!item || !acceptedCase(item))
    return substitute_or_skip;

// Initial-native approximation when the remote record flag is unavailable.
bool variant = read_u8(item + 0x3a) == 1;

manager = read_ptr(base + 0x7bf4968);
wrapper = selectCaseModel(manager, item, variant); // RVA 0x2c5acc0
loaded  = guarded_read_ptr(wrapper + 0x30);
appearance = selectCaseAppearance(manager, item, variant); // RVA 0x2c541d0

// After validating resource readiness and lifetime:
parameters.model      = loaded;     // +00
parameters.appearance = appearance; // +08; direct pointer, null permitted
// Keep the established visual-only transform, retention and collision settings.
mesh = meshFactory(&parameters);    // RVA 0x2cd4660
// Follow the existing MsgInit, placement/update and release sequence.
```

**Established:** `selectCaseModel` reads resources and returns a wrapper; it does not load or retain it (`0x2c5acc0–0x2c5afbb`). Appearance selection returns a resource pointer, not proof of readiness.

**Inferred — remaining limits:** use both selectors with identical item/variant inputs on the established game-thread boundary. Preserve the existing residency, initialization, collision and release contracts. Resource lifetime across unload, exact remote variant, per-record damage/effect changes, custom-model dimensions, and the final colours/labels remain **unknown**. The proposed acquisition creates no cargo record and requires no slot insertion.

**Established — validation:** executable hash matched; focused disassembly corroborated the cited instructions; **38 read-only file assertions passed** for instruction bytes, lookup-signature uniqueness, metadata offsets/types, special-case jump targets and dimension constants. No runtime or visual validation ran.

**Live (2026-11-09):** on the Episode 3 save the carried records' `+0x204` read `0x321` (bit 5 set) on every item; with the variant the selector returned the same model objects as without it (config entries 1/4 and 2/5 point to the same loaded objects). One item had an appearance override. The porter's own cases looked darker than the drawn ones; the cause (per-record shader data such as the `+60` identifiers or wear) is not established.

## Likes for the partner — static recipe

**Established:** All addresses below are RVAs for the pinned executable, image base `0x140000000`.

**Established — script entry points:** Registration at `0x2e23801–0x2e23883` associates these names and function pointers:

| Binding | Entry RVA | Implementation RVA |
|---|---:|---:|
| `DSLikeCommand_sReceiveLike` | `0x2e4d770` | `0x2e4eeb0` |
| `DSLikeCommand_sReceiveLikeFromMajorMember` | `0x2e4d750` | `0x2e4ef60` |
| `DSLikeCommand_sReceiveLikeWithOption` | `0x2e4d760` | `0x2e4f060` |

**Established — simplest offline-capable award:** `0x2e4eeb0` accepts an integer amount in ECX, an optional person/resource argument in RDX, and a 16-bit reason in R8W. Its null-RDX branch loads the local likes state from global `0x7bf83b0` and calls `0x2e138e0`:

```asm
RVA 2e4eec0: test rdx,rdx
RVA 2e4eec3: je   2e4ef17
RVA 2e4ef17: mov  rcx,[global 7bf83b0]
RVA 2e4ef25: mov  dword ptr [rsp+68],ffffffff
RVA 2e4ef31: mov  r9b,7
RVA 2e4ef39: movzx r8d,di          ; supplied reason
RVA 2e4ef41: mov  edx,ebx          ; supplied amount
RVA 2e4ef43: call 2e138e0
```

**Established:** The reason enum table at `0x3e05ba0`, stride `0x28`, assigns `Other=17`, `FromBB=6`, `FromPorter=7`, `FromBridges=9`, `FromPreppers=10`, `BuildingUsed=14`, `ContributeToConstruction=15`, and `FromOnlineUser=16`. These are enum values, not their order in the string inventory.

**Established:** For `Other=17`, the classifier at `0x2e1f150` returns false, and `0x2e13990` takes the branch without a resident/person connection object. At `0x2e13acc–0x2e13ad1`, it calls `0x2e07580` with the signed amount and classification false.

**Inferred — minimal game-thread experiment:**

```cpp
// Microsoft x64 ABI. Resolve and validate the pinned code before calling.
using ReceiveLike = void (*)(int32_t amount, void* person, uint16_t reason);

ReceiveLike receive_like = /* image_base + 0x2e4d770 */;
receive_like(N, nullptr, 17); // Other
```

**Inferred:** Require ordinary loaded gameplay, initialized likes/settings/HUD objects, a bounded positive N, and exactly-once handling of the partner’s award event. This is a candidate native invocation, not a proven runtime contract.

**Established — counter fields:** In the object referenced by global `0x7bf83b0`, `0x2e07580` updates clamped signed 64-bit counters:

| Offset | Established operation / inferred meaning |
|---|---|
| `+0x18` | Updated for classification false; inferred local/NPC bucket. |
| `+0x20` | Updated for classification true; inferred online bucket. |
| `+0x28` | Increased for positive classification-false awards; inferred cumulative positive local likes. |
| `+0x60` | Increased for positive classification-true awards; inferred cumulative positive online likes. |
| `+0x10d0 + 8*reason` | Per-reason counter, updated by the receiving path. |
| `+0x1160 + 8*reason` | Second per-reason counter, also updated and serialized. |

**Established:** The stores include `mov [rdi+18],rcx` at `0x2e07667`, `mov [rdi+20],rdx` at `0x2e075e7`, `mov [rdi+28],rax` at `0x2e076ab`, and `mov [rdi+60],rax` at `0x2e07624`. Limits come from the settings object at likes-state `+0x1480`, bounds `+0x28/+0x30`.

**Established — the on-screen counter notification:** Internal flag bit 1 enables the HUD notification:

```asm
RVA 2e07709: test byte ptr [rsp+a0],2
RVA 2e07711: je   2e077b3
...
RVA 2e077a1: call 3088d60
RVA 2e077ae: call 3092810
```

**Established:** `0x3092810` copies a `0x20`-byte notification record into a queue with capacity 32, count at queue `+0x420`. `DSUILikeCounterHUDLogic`’s update at `0x3093b50` consumes that queue; its initialization at `0x308aa70` binds the counter elements. The script wrapper supplies flags `7`, so the minimal call requests this normal animated counter notification.

**Established:** The positive, no-person `Other` path does not itself supply a sender name or enqueue the named InfoLog message used by the online-user path. The counter animation and the named “received likes” message are separate operations.

**Established — named attribution:** Internal receiver `0x2e12d40`, continuing into `0x2e12de0`, takes a native user identity. It:

- Records an amount against that identity through `0x2e2cfb0` at `0x2e12eb5`.
- Calls the total-counter updater at `0x2e12ee9`.
- Resolves the sender name through `0x34ad870` at `0x2e12fe1`.
- Converts that name to UI text and calls InfoLog insertion `0x30929f0` at `0x2e13184`, using a caller-supplied message-format hash.

**Established:** Its text-message branch additionally requires internal flag bit 0 and a format hash other than `0xffffffff` (`0x2e12fac–0x2e12fc6`). Thus an award can update totals/show the counter without producing the named message.

**Inferred:** A partner nickname is not interchangeable with the native user-identity argument. Arbitrary offline attribution should use separately formatted UI text; a single native call accepting `(N, custom_name)` was not established. Fresh online-name resolution while offline is also unproven.

**Established — progression and persistence side effects:**

- **Saving:** Writer `0x2e1e5b0` serializes `+0x18/+0x20/+0x28/+0x60`, both reason arrays, and identity-associated records. Corresponding reads appear at `0x2e11e7b–0x2e11ef2`. These awards change saveable state. An immediate autosave trigger was not established.
- **Porter evaluation:** `0x2f237c0` obtains reason totals via `0x2e0b6d0`, maps reasons through `0x2e1f020`, multiplies them by settings coefficients (`0x2f23841–0x2f238a0`), and accumulates evaluation values. `Other=17` maps to bucket 2. Its coefficient is reflected as `TakeLikeReason_Other`, settings `+0xcc`; the loaded value is unknown.
- **Connection level:** Person/resident routes reach `0x2f40a80`/`0x2f40e80`, which update connection-associated data at returned record `+8`. For example, `0x2f40b62–0x2f40b7f` adds the amount and clamps using the person’s thresholds. The null-person `Other` branch avoids this identified connection update.
- **Achievements:** `0x2e093a0` compares the combined total against settings thresholds `+0xa8/+0xac` and invokes `0x3632a50` with trophy resources. Achievement consequences cannot be treated as absent merely because the game service is blocked.
- **Online bookkeeping:** Identity-associated receiving updates a per-user map. It can also reach `0x2e07eb0`; that helper’s full effects were not traced. The simple `Other` route avoids those identified calls, but absence of every downstream online queue effect has not been proven.

**Inferred — risks:** Use small awards initially. Totals, reason statistics, evaluation and achievements can persist; counter saturation can make the actual increment smaller than N; a full HUD queue can discard the notification after state changes. The minimal recipe gives the stock counter popup, but does not establish a custom named partner popup.

## A name label over the partner — static recipe

**Established — relevant HUD:** The stock owner-name presentation is implemented in `DSUIMarkerHUDLogic`, reflection descriptor `0x4cf0810`, runtime vtable `0x3e53b50`. Its constructor at `0x307f430` creates 256 marker slots and 16 detail slots:

```asm
RVA 307f465: lea  rbx,[rdi+80]
RVA 307f473: call 3081470
RVA 307f478: add  rbx,130
...
RVA 307f48a: lea  rsi,[rdi+13080]
RVA 307f497: call 3081000
RVA 307f49c: add  rsi,490
```

**Established:** Let `L` be an initialized instance of that HUD logic. Initialization `0x308b1f0` stores:

| Field | Established use |
|---|---|
| `L+0x18` | Menu context passed to projection and element lookup. |
| `L+0x1a348` | Parent HUD element used when creating marker/detail subtrees. |
| `L+0x1a368` | `DSUIMarkerHUDLogicMarkerDetailResource` pointer. |
| `L+0x13080 + 0x490*i` | Detail record, `i=0..15`. |

**Established — name elements:** For detail record D and resource R, initialization at `0x308bb35–0x308bb89` resolves:

| Resource field | Detail field | Reflected name |
|---|---|---|
| `R+0x138` | `D+0x118` | `NameRootElement` |
| `R+0x140` | `D+0x120` | `NameTextElement` |
| `R+0x148` | `D+0x128` | `OnlineNameTextElement` |

**Established:** These lookups occur under the Bridges ID subtree at `D+0x38`. Resource `R+0x58` identifies that subtree’s root. Its adjacent fields include the rank/badge presentation.

**Established — owner identity rather than an embedded name:** The HUD copies a native user handle into `D+0x390` via `0x34ac430` (`0x309813d–0x3098144`). Name update `0x308fc40` passes that handle to resolver `0x328f000`:

```asm
RVA 308fddf: lea rcx,[r15+1a500]   ; resolver owned by HUD logic
RVA 308fde6: lea r9,[rsi+178]      ; resolution state
RVA 308fded: lea r8,[rsi+390]      ; native owner identity
RVA 308fdf9: call 328f000
```

**Established:** `0x328f000` calls `0x34ad870` for the display name and supplies resolving/error substitutes for certain states. The HUD selects ordinary versus online text at `0x308fe88–0x308fecb` and applies the resolved string with `0x3646c40` at `0x308fed7`.

**Inferred:** The online owner label is backed by native identity/name-resolution state, rather than a freely replaceable UTF-8 owner-name field on every entity. An arbitrary partner body should not be inserted into this path by inventing an online identity.

**Established — world anchoring:** The marker HUD processes `0x40`-byte input records through `L+0x1a3d0`. At `0x3096dc0–0x3096e14`, it passes the beginning of each record, containing a float-vector world position, to:

```cpp
// Inferred source-level signature; register assignments are established.
Project(screen_out, menu_context, world_float4); // RVA 0x329dd60
```

**Established:** The projector reads x/y/z floats, transforms them through the active view, performs perspective division and returns a screen-space float vector (`0x329de0f–0x329dfeb`). The HUD copies the projected position to `D+0x450` at `0x3098114`; this is screen space, not the original world position.

**Established:** The same projection-to-element pattern is directly visible in the hologram-name HUD at `0x307cd31–0x307cd4c`: call `0x329dd60`, then call element-position setter `0x3701e40`. That setter writes element `+0xf0` and invokes its layout-update virtual method.

**Inferred — minimal independent label recipe:** Create a separate UI subtree and update its position from the partner pose. This avoids native online-name resolution and the HUD’s 16 managed detail slots.

1. **Inferred:** Acquire and validate the active L, its menu, parent and resource pointers. The vtable/resource layout is established; a reliable runtime instance-acquisition method was not established here.
2. **Established primitive; inferred use:** Create a new Bridges ID/name subtree using `0x36fb8b0(parent, *(R+0x58))`. This wrapper invokes factory `0x364f400`; that factory creates the runtime element, attaches it to the parent (`0x364f584–0x364f587`) and recursively creates resource children (`0x364f5c0–0x364f5ce`). Native detail construction uses the same factory at `0x308b942`.
3. **Established primitive; inferred use:** Find the new subtree’s name children with `0x36fec70(new_root, child_resource)`, using `R+0x138/+0x140/+0x148`. This lookup compares resource identity at element `+0x28` and recursively searches children.
4. **Established primitive; inferred use:** Build a native UTF-16 UI string with `0x191ca20(&string_handle, name_utf16)`, then call `0x3646c40(text_element, &string_handle)`. Release the temporary string with `0x191cad0`. Show the chosen name element and root; hide the alternative online text. Visibility routine `0x37028c0(element, visible, duration)` is used by the stock HUD.
5. **Inferred:** Each eligible game-thread tick, project the partner’s world position plus a head-height offset with `0x329dd60`, then position the new root with `0x3701e40`. The UI needs no entity argument: the mod supplies the position. Apply distance, camera/front-facing and gameplay visibility gates.
6. **Established primitive; inferred use:** Remove the subtree through `0x3700750(parent, child_index)`, after locating its current index by pointer identity in parent `+0x48`, count `+0x40`. This routine unregisters descendants, clears the child’s parent, removes the array entry and releases its temporary reference (`0x370075f–0x370080c`). Match any additional references held by the mod.

**Established — encoding trap:** Do not pass UTF-8 directly to narrow UI-string constructor `0x191c9e0`. Its helper `0x191d260` widens individual bytes and recognizes a special `0x01` escape (`0x191d2e7–0x191d310`); it is not a normal UTF-8 decoder. The UTF-16 constructor copies 16-bit code units through `0x191d390`.

**Inferred:** Decode validated UTF-8 to UTF-16 in the mod first. Bound the displayed name and handle UI markup/control characters deliberately.

**Inferred — icon and removal limits:** The cloned Bridges ID subtree provides the stock name/badge presentation. Selecting the exact bike/truck glyph, and presenting a meaningful partner rank, remain unverified. Hiding the subtree is sufficient for temporary suppression; permanent removal should use the native removal routine.

**Inferred — risks:** Menu lifetime, clone visibility defaults, inherited anchors, glyph availability and mutation timing relative to UI jobs need validation. Resource-based cloning also participates in menu resource lookup, so interference with stock lookups must be checked. Clear stale handles across menu destruction/reload; never free native UI objects with the mod’s allocator. No single native “attach custom nameplate to arbitrary entity” API was established—the proposed recipe supplies world anchoring explicitly.

**Live (2026-11-09):** `0x2e4eeb0(1, nullptr, 17)` on the game thread raised `+18`/`+28` by one and the reason-17 counters by one, with the stock like counter popup ("+1").

## Finding the marker HUD and a custom label — static follow-up

**Established — scope:** Static inspection of build 13300582, executable SHA-256 `9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b`. Addresses below are RVAs; add the loaded image base. Disassembly used `x86_64-w64-mingw32-objdump -d -M intel`. Runtime population, display and mutation safety were not tested.

**Established — descriptor correction:** The marker logic’s type descriptor begins at **`0x4cf0800`**. `0x4cf0810` is inside its construction metadata. Its runtime vtable remains `0x3e53b50`.

```asm
307a180: lea rax,[image+4cf0800]
307a187: ret

; Construction metadata:
[image+4cf0820] = image+307c700

307c700: test rdx,rdx
307c703: je   307c70d
307c705: mov  rcx,rdx
307c708: jmp  307f430

307f455: lea rax,[image+3e53b50]
307f462: mov [rdi],rax
```

**Established — construction and ownership:** The menu’s logic factory at `0x36ffb50` obtains each logic type from its resource, constructs it through reflection, initializes it with the resource and menu, and appends it to the menu’s logic array.

```asm
36ffb90: mov  rsi,[r15]             ; logic resource
36ffb96: mov  rax,[rsi]
36ffb99: call qword ptr [rax+68]    ; runtime logic descriptor
36ffb9c: mov  rcx,rax
36ffb9f: call 19f4830              ; reflected allocation/construction
36ffba4: mov  rbx,rax              ; new logic
36ffba7: mov  r8,rdi               ; menu
36ffbaa: mov  rdx,rsi              ; resource
36ffbad: mov  rcx,rbx
36ffbb3: call qword ptr [rax+10]    ; base initialization

36ffbbc: mov  rcx,[rdi+1b0]        ; logic pointer array
...
36ffc20: movsxd rdx,dword ptr [rdi+1a8]
36ffc27: lea  r8,[rcx+rdx*8]
36ffc30: mov  [r8],rbx             ; retain constructed logic
36ffc3c: lea  eax,[rdx+1]
36ffc3f: mov  [rdi+1a8],eax
```

**Established:** For the marker resource, vtable `0x3e4eb20`, slot `+0x68` selects `0x307a180`, completing the route to the constructor above. Reflection allocator `0x19f4830` allocates at `0x19f48f4`, invokes the construction function at `0x19f494c`, and returns the allocated instance at `0x19f4962`.

**Established:** Base initialization `0x36ff800` writes the resource and menu. These fields are already present when marker-specific initialization `0x308b1f0` runs.

```asm
36ff819: mov [rcx+08],rdx           ; logic resource
36ff81d: mov [rcx+18],r8            ; owning menu
```

**Established — global-to-HUD chain:** The player-manager global is `0x7bc7568`. Native UI code obtains player number zero, then follows player `+0xd0` to its view and view `+0x610` to its HUD menu:

```asm
30064c5: mov  rcx,[image+7bc7568]
30064cc: xor  edx,edx
30064d8: call 23c60b0               ; player number zero
30064e4: mov  rdx,[rax+d0]          ; player view
30064eb: mov  rax,[rdx+610]         ; HUD menu
30064f2: mov  rbx,[rax+190]         ; menu resource
```

**Established:** `0x23c60b0` implements the player-number lookup by walking the manager’s pointer array:

```asm
23c60b0: mov    rax,[rcx+40]        ; player array
23c60b4: movsxd rcx,dword ptr [rcx+38]
23c60b8: lea    r8,[rax+rcx*8]
...
23c60c1: mov    rcx,[rax]
23c60c4: cmp    [rcx+88],edx        ; requested player number
23c60ca: je     23c60d8
...
23c60d8: mov    rax,rcx
23c60db: ret
```

**Established — this is the HUD menu:** View initialization `0x3618c10` destroys the previous menu at view `+0x610`, constructs its replacement using menu constructor `0x36fa020`, and stores it back there:

```asm
3618c52: lea  rbx,[rsi+610]
3618c59: mov  rdi,[rbx]
...
3618c64: call 36fb0c0               ; destroy old menu
3618c71: call 1951c70               ; release its allocation
3618c76: mov  [rbx],r12             ; clear slot
3618c79: mov  rax,[image+7bc7df8]
3618c80: mov  rcx,[rax+110]         ; GameViewResource
3618c87: mov  rbp,[rcx+70]          ; HUD resource
...
3618cc3: call 36fa020
3618cc8: mov  rdi,rax
...
3618cfa: mov  [rbx],rdi
```

**Established:** `GameViewResource` descriptor `0x4e634c0` identifies its member table at `0x4e62080`; member record `0x4e62400` names offset `+0x70` **`HUD`**.

**Inferred — concrete acquisition procedure:** Read the following chain with bounded counts, readable-pointer checks and a final chain recheck:

```text
M = *(base + 0x7bc7568)
P = player in *(M+0x40)[0 .. i32(M+0x38))
    whose u32(P+0x88) == 0

V = *(P + 0xd0)
H = *(V + 0x610)

L = unique pointer in *(H+0x1b0)[0 .. i32(H+0x1a8))
    whose first pointer == base + 0x3e53b50
    and whose *(L+0x18) == H
```

**Inferred:** Select the marker logic belonging to this player-zero HUD menu. A process-wide vtable scan could find another view’s instance or an object undergoing destruction. If the selected HUD contains zero or multiple matching initialized logics, refuse acquisition rather than selecting the first match. Actual instance counts remain unknown.

**Established — initialization validation:** Marker initialization supplies useful consistency checks:

```asm
308b234: mov rdx,[rdi+08]
308b238: mov rcx,[rdi+18]
308b23c: mov [rdi+1a350],rdx

308b243: mov rax,[rdx+78]
308b247: mov [rdi+1a358],rax
308b24e: mov rax,[rdx+80]
308b255: mov [rdi+1a360],rax
308b25c: mov rax,[rdx+88]
308b263: mov [rdi+1a368],rax         ; detail resource

308b2f6: mov rdx,[rdx+e0]
308b2fd: call 36fedc0               ; menu element lookup
308b302: mov [rdi+1a348],rax         ; marker parent element
```

**Inferred:** Accept only an initialized L with a nonnull parent and detail resource, `*(L+0x1a350)==*(L+8)`, and `*(L+0x1a368)==*(*(L+8)+0x88)`. Validate expected resource/element identities as well as readability. Reacquire across view/menu replacement; cached addresses alone do not establish object lifetime.

**Established — update dispatch uses jobs:** The view’s HUD update calls `0x3702e40`:

```asm
361b19a: mov  rax,[rbx+610]
361b1a5: mov  [rax+1e50],cl
361b1ab: mov  rcx,[rbx+610]
361b1b2: call 3702e40
```

**Established:** That routine constructs `MsgHUDUpdate`, descriptor `0x4ea8d80`, vtable `0x3f31788`, and dispatches it through `0x37016a0`:

```asm
3702fa0: lea  rax,[image+3f31788]
3702fac: lea  rdx,[rsp+28]
3702fb1: mov  [rsp+28],rax
3702fb6: mov  rcx,rdi               ; menu
3702fb9: vmovss [rsp+38],xmm7       ; delta time
3702fbf: call 37016a0
```

**Established:** For this message, `0x37016a0` builds two task payloads using menu collections at `+0x1e88/+0x1ec8`, installs task callback `0x37042e0`, submits both, then waits for their completion.

```asm
3701752: lea  r15,[r13+1ec8]
3701759: lea  r12,[r13+1e88]
3701763: lea  rbx,[image+37042e0]
...
3701783: mov  [rsi-08],r12
3701787: mov  [rsi],r15
370178a: mov  [rsi+08],r14           ; message
370179d: mov  [rax+08],rbx          ; task callback
37017a1: mov  [rax+10],rbp          ; payload
37017bd: call 1a06140               ; submit
...
3701836: lea  r8d,[r9+1]            ; one task handle
370183a: call 1a08770               ; wait
3701869: jne  3701800               ; both handles
```

**Established:** Wait routine `0x1a08770` removes completed task handles at `0x1a08938–0x1a08964` and loops while handles remain at `0x1a0897d–0x1a08988`. Task callback `0x37042e0` dispatches through `0x237c430`.

**Established:** The inherited HUD handler connects that dispatch to the marker update. Descriptor `0x4cd1240` (`DSUIHUDLogicBase`) maps `MsgHUDUpdate` to thunk `0x3055690`, which jumps to `0x303dad0`. The handler calls virtual `+0x50` for active HUD entries at `0x303db75`. Marker vtable `+0x50` selects:

```asm
2362e00: mov rax,[rcx]
2362e03: jmp qword ptr [rax+58]
```

**Established:** Marker vtable `+0x58` is `0x3096960`.

**Inferred — thread conclusion:** The marker update is scheduled as a HUD task. It is **not established as confined to the mod’s game thread**; a task might execute on a worker or through scheduler assistance. An observed game-thread callback does not by itself permit concurrent mutation of the menu graph.

**Inferred — mutation point:** Queue label intentions from the existing game-thread callback. The strongest local rendezvous identified here is **`0x3702fc4`**, immediately after the joined HUD dispatch returns, conditional on the current menu being H. Another candidate is `0x361b1b7`, after the whole HUD-menu update returns. Drain creation, text, position, visibility and removal operations together at the chosen UI phase. These are candidate integration points, not proven global renderer barriers; render overlap and surrounding scheduling still need validation.

**Established — simpler alternative: hologram-name records:** The hologram-name HUD has three presentation slots, initialized at `0x307c853–0x307c886` and processed by update `0x307cab0`. It consumes records from:

```text
Q = *(*(base + 0x7c00128) + 0xf0)
record count = i32(Q+0x30)
record array = *(Q+0x38)
```

**Established:** Hologram initialization stores Q at logic `+0x88` (`0x307c825–0x307c83c`). Its update copies each selected record’s world vector into a presentation slot (`0x307cc25–0x307cc3b`), then projects and positions it:

```asm
307cd31: mov  rdx,[rbp+18]          ; menu
307cd35: lea  r8,[rbx+18]           ; saved world position
307cd39: lea  rcx,[rsp+30]
307cd3e: call 329dd60
307cd43: mov  rcx,[rbx-58]          ; label root
307cd47: lea  rdx,[rsp+30]
307cd4c: call 3701e40
```

**Established — record creation/removal:** Constructor **`0x307e170(record)`** registers a `0x30`-byte record with Q. It initializes the native string at `+0x10`, assigns a provider-generated ID at `+0x1c`, and appends the record pointer under Q’s lock (`0x307e1bc–0x307e1ef`). Destructor **`0x3081db0(record)`** removes entries matching that ID under the same lock (`0x3081de4–0x3081e67`) and releases the string through `0x191cad0` (`0x3081e72–0x3081e7c`).

**Established:** A native caller demonstrates the complete data contract:

```asm
2d87973: lea  ecx,[rbx+30]
2d87976: call 1a0d9d0               ; allocate 0x30 bytes
...
2d8799c: call 307e170               ; construct/register
...
2d879c8: add  rcx,10
2d879cc: call 191caf0               ; assign native string
...
2d879e6: mov  [rbp+18],al           ; presentation mode
...
2d87a24: vaddps xmm0,xmm3,[rsi+30] ; world position + offset
2d87a29: vmovups [rbx],xmm0
...
2d87a3e: call 3081db0               ; unregister/destruct
2d87a43: mov  edx,30
2d87a4b: call 1951c70               ; release native allocation
```

**Established:** Presentation entry `0x3085d00` applies record `+0x10` to its text elements through `0x3646c40` at `0x3085e31–0x3085e47`.

**Inferred:** This is a simpler candidate than cloning the marker detail: construct/register one record, assign a bounded native string, and update its world float vector. The stock HUD supplies projection, positioning and presentation. It is not a discovered single-call `(world_position,string)` API.

**Established — alternative limits:** The provider sorts records using squared distance (`0x3093340–0x309335a`), while the presentation loop examines three entries (`0x307cc51–0x307cc5f`). Text assignment occurs on presentation entry.

**Inferred:** A custom record competes with stock hologram names; display is not guaranteed when more than three records exist. Changing the record string might require presentation re-entry. No independent record visibility flag was established: `+0x18` selects presentation mode. Unregister/reconstruct is the justified candidate for hide/show. The provider’s container lock does not establish safety for changing or freeing a record after a consumer has obtained its pointer.

**Inferred — other alternatives:** No simpler arbitrary-text creation/removal contract was established for porter tags or the user-marker path in this pass. The hologram record path is the strongest alternative found, but its shared pool makes an independent subtree more controllable.

**Inferred — minimal independent-label recipe:** Perform the following at the proposed serialized UI phase, using intentions queued by the mod’s game-thread callback:

1. **Acquire:** Resolve H and its unique initialized L through the global chain above. Read `parent=*(L+0x1a348)` and `R=*(L+0x1a368)`. Associate cached handles with this exact menu generation.
2. **Create once:** Call `0x36fb8b0(parent, *(R+0x58))` for a separate name subtree. Its established wrapper forwards the parent’s menu and parent to factory `0x364f400`. Resolve the clone’s name root, ordinary text and online text through `0x36fec70`, using `R+0x138/+0x140/+0x148`. Stock lookup evidence is `0x308bb35–0x308bb89`.
3. **Set text once or when changed:** Decode bounded UTF-8 into terminated UTF-16. Construct a temporary native string with `0x191ca20`, call `0x3646c40(text,&handle)`, then release the temporary with `0x191cad0`. Show the name ancestors and chosen text; hide the alternative text through `0x37028c0(element,visible,0.0f)`.
4. **Each eligible tick:** Convert to game coordinates and add the head-height offset to **game Z**. Pass an initialized float4 to `0x329dd60(&screen,H,&world)`, then call `0x3701e40(root,&screen)`. The established projector returns its output pointer, not a visibility boolean.
5. **Hide:** Call `0x37028c0(root,false,0.0f)` for stale partner data, gameplay suppression or rejected camera/distance tests. Define those tests separately; projection alone does not establish front-facing visibility.
6. **Remove:** While the parent/menu generation is still valid, locate the root by pointer identity in parent `+0x48`, bounded by count `+0x40`, then call `0x3700750(parent,index)`. Native removal detaches/unregisters the child and releases its temporary reference (`0x370075f–0x370080c`). Balance any extra retained references. After menu replacement, discard stale handles without calling through them.

**Inferred — risks:** This remains an untested candidate. Principal risks are UI/render scheduling outside the reviewed join, menu destruction, inherited subtree anchors/visibility, duplicate resource identities affecting lookups, unsupported glyphs/markup, and reference ownership. Do not alter the stock 16 detail records or invent an online identity. The global chain establishes where to look; it does not make a cached instance permanently live or certify UI mutation from the existing callback.

**Live (2026-11-09):** the hologram-record route works: a 0x30-byte, 16-aligned record the mod allocates, zeroed, then `0x307e170(record)`, its string set from UTF-16 with `0x191ca20`/`0x191caf0`/`0x191cad0` at `+0x10`, mode byte `+0x18` left 0, and its float4 point (game axes) at `+0x00` 2.1 m above the body: the game drew the name with its hologram-name label over the partner body. `0x3081db0(record)` removed it.

## Death, BTs and voidouts for the partner — static recipe

**Established — scope:** Inspected the pinned executable read-only with narrow `x86_64-w64-mingw32-objdump -d -M intel` windows. SHA-256 matched `9d5306d7eb2f5d3135c63accc3e74b8f9b029f9d76112a6aae4a38fdbb4edd2b` before and after inspection. All addresses below are RVAs relative to `0x140000000`. “Established” means static machine-code or metadata evidence; none of these new state interpretations was tested live.

### Read-only local detection

**Established — native animation predicates:** `0x2717c40(entity, enum)` reads byte:

```text
b = *(uint8_t *)(entity + 0x4dc8 + 12*enum)

if (b & 2) return true;
if (b & 4) return (b & 0x10) != 0;
return (b & 8) != 0;
```

Evidence: `0x2717c40–0x2717c65`. Reading only bit 4 would miss the other native true cases.

**Established — relevant registered controls:**

| Name | Native bool enum | Registration evidence |
|---|---:|---|
| `Is_Dead` | `0x0c` | `0x1883c36–0x1883c4e` |
| `Is_Drag` | `0x12` | `0x1883cc7–0x1883cdf` |
| `Drag_Is_Catch_Ground` | `0x13` | `0x1883ce4–0x1883cfc` |
| `Is_Crouch` | `0x15` | `0x1883d3b–0x1883d53` |
| `Int_Is_GazerCatch` | `0x25` | `0x1883d92–0x1883daa` |
| `Int_Is_GazerCut` | `0x26` | `0x1883daf–0x1883dc7` |

**Inferred — grabbed/dragged presentation:** These provide useful candidate observations for the porter's animation state. `Is_Drag` together with the two named Gazer controls is stronger evidence than movement speed alone, but the exact relationship to physical capture, escape and dragging was not established. Do not classify every `Is_Drag` interval as a proven BT capture.

**Established — generic entity death:** `Entity_ExportedIsDead`, `0x2362c80`, returns bit 7 of entity `+0x88`:

```asm
2362c85  mov rax,[rcx+88]
2362c8c  shr al,7
2362c8f  and al,1
```

**Established — invalid-object caveat:** That export returns **true for null**, at `0x2362c92`. A reader must report an unavailable porter as **unknown**, rather than dead.

**Inferred — porter death:** Combine the generic death flag with native `Is_Dead` and the global phase observations below. Neither flag alone establishes “currently in the Seam” or “repatriating.”

**Established — global phase storage:** `DSGameState_sExportedGetGameState2` is registered to `0x2dd36c0` at `0x2dba487–0x2dba4af`. It returns:

```text
G = *(base + 0x7bf6e88)
currentState = *(uint64_t *)(G + 8)
```

Its constructor installs vtable `base+0x3dfedb0` and publishes that global at `0x2dbd79a–0x2dbd7ce`. State requests also occupy `G+0x10`; these are distinct from the getter's current-state word.

**Established — named sound-state evidence:**

- `0x376a24–0x376a81` extracts **bit 9** from a game-state callback result and publishes `DSSdKnotSpaceState`.
- `0x376a9d–0x376ad8` extracts **bit 17** and publishes `DSSdAnnihilationStateGameState`.
- `0x376af4–0x376b2f` extracts **bit 13** and publishes `DSSdGameOverState`.

**Inferred — phase meanings:** Bit 9 is the Seam/KnotSpace candidate, bit 17 the annihilation candidate, and bit 13 the game-over candidate. The sound callback's runtime import binding was not independently resolved to `0x2dd36c0`, so this semantic correspondence still needs validation.

**Established — repatriation-related fields are insufficient:** `OverrideReturnFromKnotSpaceSequence`, `0x26e6e20`, stores its arguments at `*(base+0x7be7980)+0x20518/+0x2051c`. These are sequence overrides, not an established current repatriation phase. `DebugPlaySequenceOfReturningFromKnotSpace` is registered to the empty stub `0x18e9b10` at `0x266bca5–0x266bccd`.

**Inferred/unknown — repatriation detection:** No independently identified “repatriating now” predicate was established. A falling Seam bit followed by restored porter animation is a possible transition heuristic, not a proven phase detector.

**Established — Odradek pointer chains:** The native arm/body exports give:

```text
P    = *(porter + 0x54c8)
arm  = tracked_pointer_at(P + 0x4c88) - 0x20
body = tracked_pointer_at(P + 0x4ca0) - 0x20
```

Null tracked pointers remain null. Evidence: arm getter `0x26e6ac0–0x26e6ae8`; body getter `0x26e6af0–0x26e6b18`.

**Established — raw Odradek state lead:** The common Odradek update selects a byte at Odradek entity `+0x34a`; examples include stores at `0x295e148`, `0x295e15f`, `0x295e66e`, `0x295e6ae`, `0x295e6bf` and `0x295e711`. Its distance selector, `0x29601a0–0x2960225`, follows Odradek `+0x3f0` and selects parameter fields `+0x4508`, or the minimum of `+0x450c/+0x4500`, under several flags.

**Inferred/unknown — BT alert level:** Neither `+0x34a` nor those distance fields has an established mapping to the requested Odradek alert levels. They must not be exposed as such yet. The sound name `ASdGSOdradekTentionLevel` exists at `0x3f5b12c`, but its native producer/storage was not resolved.

**Inferred — minimal conservative per-tick recipe:**

1. Reuse the existing validated local-porter selection.
2. Read entity `+0x88` and apply the native bool predicate above for enums `0x0c`, `0x12`, `0x13`, `0x25`, `0x26`.
3. Read the validated game-state global and current word; retain the three candidate phase bits separately.
4. Optionally record raw Odradek state through the arm/body chain; leave the interpreted alert level unknown.
5. Re-read roots after sampling. Reject changed identities, unreadable spans or unloaded objects. Do not carry cached entity/component pointers across reloads.

### Showing the state on the owned partner body

**Established — setters:** The existing APIs support these named bool controls through per-manager handles:

- Init bool: `0x2397050`.
- Set bool: `0x23971e0`.
- Init float: `0x23970b0`.
- Set float: `0x2397220`.
- Trigger bool: `0x2397310`.

At `0x23971e5–0x2397209` and `0x2397225–0x2397251`, the setters address `manager+0x58 + 0x30*handle`, store the value at record `+0x20`, then dispatch the bound control ID from record `+8`.

**Established — crouch control:** `Crouch_Rate` is registered as float enum `0x77` at `0x18857b5–0x18857cd`.

**Inferred — candidate presentation recipe:**

| Presentation | Candidate controls |
|---|---|
| Dragged/struggling | Mirror `Is_Drag`, `Drag_Is_Catch_Ground`, `Int_Is_GazerCatch`, `Int_Is_GazerCut` |
| Crouched sneaking | `Is_Crouch`; mirror `Crouch_Rate` |
| Dead/collapsed | Mirror `Is_Dead`; suppress normal locomotion inputs while held |

**Inferred/unknown — pose limits:** Registration proves the controls exist. It does not prove that `Is_Dead` alone produces a collapsed pose, that the Gazer controls reproduce the complete struggle, or that any requires an entry pulse. Ragdoll setup, root motion and required companion controls remain unresolved.

**Established — pulse limit:** `0x2397319–0x239732b` clamps duration against `base+0x7bc6810`, then submits bool true through `0x238a2f0`. A finite pulse is therefore not a substitute for holding a persistent state.

**Inferred — binding requirement:** Resolve and validate each name against the body's active player graph. Native enum numbers above are **not** AnimationManager handles. Reuse the existing active-definition binding checks before testing these controls.

### Detecting and reproducing a voidout

**Established — catcher crater storage:** Let:

```text
C = *(base + 0x7bee060)

active count     = int32(C + 0x280)
active records   = pointer(C + 0x288)
pending count    = int32(C + 0x290)
pending records  = pointer(C + 0x298)
record stride    = 0x60
```

The global is constructed through `0x29f12c0` and published at `0x2e0dacf`. Its constructor installs vtable `base+0x3daf258`.

**Established — record fields:**

| Offset | Static interpretation |
|---|---|
| `+0x18/+0x20/+0x28` | Three float64 world-position coordinates |
| `+0x30` | Float32 radius |
| `+0x3c` | Activation byte |
| `+0x44` | Countdown before activation |
| `+0x48` | Incrementing record identifier |
| `+0x4c` | Progress counter |
| `+0x50` | Area discriminator |
| `+0x58` | Flags |

Evidence: record construction/copy at `0x2a004bc–0x2a0051f` and `0x2a008d9–0x2a00960`; active-table update at `0x2a74fed–0x2a75211`.

**Established — independent position/radius corroboration:** `catcher::sIsInAnnihilationArea` is registered to `0x2a92560`. It reads the active table, filters area and activation, and compares the supplied position with record position/radius at `0x2a92598–0x2a9261f`. The distance test uses the horizontal components.

**Inferred — event detection:** Observe newly queued identifiers and their stored positions, then distinguish “queued” from activation-byte transitions. An existing active record is not evidence of a new voidout. Cache invalidation is necessary when the manager/world changes.

**Established — native enqueue function:** `0x2a003d0(C, catcherIdentifier)` searches `C+0x88/+0x90`, using `0x110`-byte catcher records and identifier `+0x20` (`0x2a00422–0x2a00473`). It prepares a crater record and appends it to the pending table (`0x2a00870–0x2a00960`).

**Established — no arbitrary-position contract here:** The enqueue function derives position from catcher data and resource-dependent calculations. Its second argument is an identifier, not a position pointer.

**Established — crater materialization:** `0x2a014e0(C, record)`:

- Requires the cached `CraterEntityResource` at `C+0x2a0`; resource `+0x80` is cached there at `0x2a1cdd1–0x2a1cdff`.
- Checks world availability through `0x1ddd0f0`.
- Calls resource virtual `+0x88` at `0x2a0154d`.
- Applies the record transform through `0x22dff80` at `0x2a015f7`.
- Sets entity radius-related fields `+0x35c/+0x360`.
- Registers the entity at `0x2a01658–0x2a01674`.
- Registers a world influence through `0x2e59bc0` at `0x2a0168d–0x2a016a5`.

**Established — progress export is not a creator:** `catcher::sAnnihilationProgress`, `0x2a91050`, conditionally adds its integer argument to `C+0x2b0`. The update consumes it into record progress at `0x2a75131–0x2a7515a`.

**Established — persistence:** The environment serialization path calls `0x2a90d80(C, writer)` at `0x2e1e2db–0x2e1e2ea`. That writer serializes active record positions, radius and additional fields at `0x2a90e1a–0x2a90ef4`.

**Inferred — save risk:** Crater records can persist. This does not establish that enqueueing immediately writes a save to disk.

**Established — area side effects:** First activation calls `0x2a1c710(C, position, radius)` at `0x2a75091–0x2a750a1`. That routine performs an area query, processes its results through `0x2d80630`, and calls two additional radius-based handlers, `0x2bdd2b0` and `0x2afff60`.

**Established — local-player/game-flow coupling:** The enclosing catcher annihilation path enqueues the crater at `0x2a57c3f`, selects damage type `0x12e` at `0x2a57c74–0x2a57c7d`, and submits it against the local porter at `0x2a57d44–0x2a57d65`. Metadata names `0x12e` `CatcherAnnihilationAttack`. An alternate branch submits a game-flow request through `0x341f840` at `0x2a57d78–0x2a57dab`.

**Inferred/unknown — remaining side effects:** Exact cargo destruction, structure removal, Chiral-network changes, terrain deformation/collision and the alternate branch's precise game-over behavior were not fully traced. None is established absent. Calling the enclosing annihilation path on receipt of a partner packet risks affecting the local porter and persistent world.

### Separate visual effects and remaining risks

**Established — separate crater-effect component:** `DSCraterEffectComponent` descriptor `0x4c0ba70` names a `0x128`-byte component. Its message table connects initialization to `0x2aba0f0` and updating to `0x2ac59a0`.

**Established — particle creation primitive:** Initialization creates particle instances through `0x1bc7fe0`, using the owning entity's position and particle resources:

```text
0x2aba1d8–0x2aba227   resource +0x20
0x2aba2be–0x2aba313   resource +0x28
0x2aba448–0x2aba496   resource +0x30
0x2aba5c8–0x2aba616   resource +0x38
```

The update positions/restarts an instance through `0x1e0b020` and `0x1bc1e30` at `0x2ac5aff–0x2ac5b5a`.

**Established — annihilation particle resource leads:** Reflected fields named `AnnihilationEffectParticleSystem` occur at member records `0x4bf4828` and `0x4c077a8`.

**Inferred/unknown — visual-only voidout:** Separate particle machinery exists, but this pass did **not** establish a standalone, safe, complete visual voidout explosion. The crater-effect component may supply continuing crater ambience rather than the initial explosion. Resource acquisition, lifetime, complete initialization side effects and teardown remain unresolved.

**Inferred — integration boundary:** The conservative next implementation is read-only telemetry with explicit unknown states, followed by isolated animation experiments. A gameplay voidout or crater call should not yet be wired to received partner events.

---

Changed files: none. Ten pinned instruction assertions passed; executable hash remained unchanged; Git remained clean at the supplied start commit. No orchestrator refinements appeared in the inbox. No live checks, builds or test suite ran.

**Status (2026-11-12):** the read-only detection is wired (`mod/vitals.h`: the dead bit, `Is_Dead`, `Is_Drag`, `Int_Is_GazerCatch`, and `Is_Crouch` as a check). Live: the control bytes are at the porter entity the mod already uses (`Is_Crouch` followed the stance key), and an alive porter read not dead and not caught; a real death or catch has not been observed. Resting, tried and dropped (2026-11-12). Read from the porter's animation manager slots: sitting down (the stance key
held, or by himself after about a minute idle) shows `Is_TakeABreak` 1, `TakeABreak_State` 1.0, `Is_Crouch` 1,
`Crouch_Rate` 1, `Is_Actions` 1, `Is_BasicAction` 0, `Temporary_Bool5` 1, `Temporary_Float8` 1; asleep adds
`Temporary_Float3` 4. Set by name on the partner body (the bool as a replayed action, the three values with it), the
body stood upright for as long as the state was reported, and no longer crouched either; with only the crouch flag it
crouched as before (pictures with the body in view, the stand-in partner's `--flag`). So the break state is entered
but its pose does not play on the body; what else it needs is unknown. A resting partner is shown crouched, as before
(a sitting porter reports crouch). The porter registers 295 animation controls at `0x1883600..0x1886600`.

A way to cause a death in a test, tried and dropped (2026-11-12): `Entity_ExportedKill` (registered as `Kill`, thunk `0x2361bf0` to `0x234c610`: entity, a second pointer, a hit record that may be null) called on the local porter from the game thread, with the second pointer null and then the porter itself. Both times a job thread faulted reading `0x60` at `0x243b139` (called from the player's code at `0x27326c7`, which takes a pointer from `+0x5a0` of an object or falls back to a null register) and the main thread hung; the copy had to be stopped. No real death has been read yet.

Tried and dropped the same day, each checked on screen with the body in view: (1) the bool controls `Is_Dead` and `Is_Drag` set on the partner body's animation manager by name, like `Crouch_Rate`; (2) the entity's own control request table, written the way the game's setter `0x273f7f0(entity, control, value)` does (bit 2 = set, bit 4 = the value; `0x2741c80` forces a control true with bit 1; 0x8c controls of 12 bytes). The body stayed standing with both. (A first read-back of the body's table after (1) proved nothing: that table is the request side, which the named setter does not write.) The mod now lays the body's transform on its back instead (`puppet.h`). Earlier status (2026-11-09): nothing of this is wired. Crouching already reaches the partner body (`Crouch_Rate` from the presence crouch flag); the struggle and death poses, alert levels and voidouts stay parked (a gameplay crater must not be driven by partner events).

## Info log: the message feed, and a custom line in it (2026-11-10)

Static, pinned executable, RVAs relative to `0x140000000`; table and queue contents read live (read-only).

**Established — the node-graph binding:** `DSUINodeGraphBindings_sShowInfoLog` is registered at `0x30159a2` to
`0x30550e0` (a jump to `0x3056110`). With a table item it formats the item's localized text with up to three string
arguments (`0x3092d10`); without one it hashes a symbol name and looks the item up (`0x30929a0` -> `0x30929f0`;
an unknown name shows "Not found info log table item"). Both end in the same add.

**Established — the HUD object:** `0x307a4f0` returns `*(owner + 0x48)`, where `owner = *(base + 0x7c00128)` is
the UI owner global that `0x30125a0` returns (the hologram-name list hangs off the same owner at `+0xf0`). Live,
that object's vtable is `base + 0x3e53850` (`DSUIInfoLogHUDLogic`). Its fields: `+0x20` the item table (count at
table `+0x20`, item pointers at table `+0x28`; 572 items live), `+0x28` the queue, `+0xfd0` a slim reader/writer
lock (the imports at `0x3b3a518/528/538` are `TryAcquire/Acquire/ReleaseSRWLockExclusive`) taken around an add
(`0x3092bae`..`0x3092cf4`).

**Established — a table item:** `+0x20` id (hash of its symbol), `+0x28` localized text resource, `+0x30` icon,
`+0x31` colour, `+0x32` sound, `+0x34` a float added to the UI clock for the entry's time (0 on every item read
live), `+0x38..+0x47` one byte per HUD state the item may show in, `+0x48/+0x49` flag bytes. Live examples:
"20 minutes have passed." (icon 0, sound 3, states `0x8200`), "Cargo damaged." (icon 0, `0xa010`),
"{1} Like(s) received from {0}." (id `0x7a15e274`, icon 3, `0x1200`), "Like(s) awarded to {0}." (icon 0, none).

**Established — the add:** `0x3084d10(queue, UI string*, id, icon, colour, sound, float time, flags, uint32* states)`
(the call at `0x3092cda`: `rcx = hud + 0x28`, `rdx` the formatted text, `r8d` the item id, `r9b` the icon, then on
the stack colour, sound, `float(UI clock) + item +0x34`, `8` if item `+0x48` | `0x20` if `+0x49`, and a pointer to
the state mask: bit i for item `+0x38+i`, bit 0 always). The queue is 100 entries of `0x28` bytes (text handle,
id `+8`, order `+0xc`, icon/colour/sound `+0x14..+0x16`, time `+0x18`, flags `+0x20`, states `+0x24`) with its
count at `+0xfa0`; the add keeps them in time order and copies the text (`0x191caf0`). It needs nothing else: no
item, no record. The UI clock is the double at `*(base + 0x7be6318) + 0xd0`. The text is the native UI string
(`0x191ca20` from UTF-16, `0x191cad0` release), the same type the hologram name uses.

**Used by the mod (`notice.h`):** one add per notice with a finished text, id `0x434f4f50`, icon 0, colour 0, sound 3,
states `0x8201`, flags 0, time = the UI clock, under the lock; the HUD object is re-read and checked by its vtable on
every call. Live result: see STATUS.md.

## Other projects checked against this build, and a private-server trial (2026-11-11)

Pinned build 13300582 (`ds.exe` SHA-256 as above). Three public projects were read and tested against the
executable. Their code is not used here: two carry no license and one is GPL-3.0, so anything taken from them
has to be re-derived on this build. All addresses are RVAs.

### A private server exists and its client hooks fit this build

[Skippeh/parcel-thief](https://github.com/Skippeh/parcel-thief) (commit `01f7cbc`, no license file, last pushed
2024-11) is a server for the Director's Cut's online layer: built objects and vehicles ("Qpid objects"), lost
cargo and shared lockers ("missions"), player paths ("roads"), highway resources, likes and wasted baggage. Its
client is a DLL that does three things in the game process.

Observed (static, file scan of `ds.exe`, each pattern exactly one match):

| What its client uses | RVA here | Note |
|---|---:|---|
| String constructor from a C string | `0x190aa00` | builds the engine's counted string (ref count, CRC, length before the bytes) |
| String destructor | `0x190ad20` | |
| "Read incoming data" (mode, in string, out string) | `0x19da0e0` | the client replaces it: mode must be 2 |
| "Write outgoing data" (mode, in string, out string) | `0x19da6a0` | the client replaces it: mode must be 2 |
| Auth address pointer (its hard-coded Steam offset) | `0x4dfa130` | holds `0x144dfa108`, a string object whose text is `https://prod-pc-15.wws-gs2.com/auth/ds` |

- The same project documents the transport: JSON bodies; in its replacement the body is AES-256-GCM with a
  12-byte random nonce in front and the 16-byte tag at the end, under that project's own fixed key, and vectors
  are integers scaled by 100000. The server answers the auth request with a gateway address for everything else.
- The executable names three more hosts: `clientconfig.api.wwsga.me` and `ds15xs.api.wwsga.me` (with `.dev.`
  twins) next to the strings `TelemetryClientConfig` and `/clientConfig`.

Observed (live, one test copy, enforce network policy, 2026-11-11):

- That project's Linux server build (release `build/replace_pg-embed_dep/2024-07-18_2`, zip SHA-256
  `8ab02795...a15610`) starts on loopback with a placeholder Steam web API key and serves its frontend.
- Its client DLL (same release, zip SHA-256 `e1ea91f1...a3749f`) injected about 100 ms after process start: the
  DLL's initialisation failed and the game exited before the mod started. No error line was written, cause
  unknown.
- Injected into the same copy once it was in gameplay: "Successfully injected", so the four patterns matched in
  memory and its three hooks were installed next to this project's proxy and mod. The copy kept running.
- In that run the game made exactly one outbound attempt of its own, `WinHttpConnect clientconfig.api.wwsga.me`,
  refused by the policy. It never tried the auth address, before or after the injection, so the local server
  received no request from the game.
- Second trial the same day, with a real Steam web API key on the server and an Episode 3 save: the client DLL
  injected about 2.5 s after process start (once this project's mod had started) loaded without a crash, so the
  auth address was replaced before the title screen. The game loaded into gameplay and again made only the one
  refused `clientconfig.api.wwsga.me` attempt; the server received no request.
- The real saves were unchanged after all runs.

Derived:
- A private server can sit behind the existing network policy with no change to it: loopback and listed partner
  addresses already pass, and the publisher's hosts stay refused.
- The game does not attempt to sign in while its configuration request is refused, with either an Episode 1 or
  an Episode 3 save. That the refused request is the cause is a guess; nothing here shows it.

Unknown:
- What makes the game start the sign-in (the title screen's online step, the result of the `clientConfig`
  request, a profile setting), and whether a local answer to `clientConfig` is needed.
- Whether sign-in succeeds: the server checks the Steam session ticket with a real Steam web API key, which
  this trial did not have.
- Two copies under one Steam account would be one player to the server. Whether the server can tell them apart
  is untested.
- Why injection in the first 100 ms fails.

Static leads for the sign-in gate (2026-11-11, nothing called): no code references the auth pointer `0x4dfa130`
directly. A pointer table at `0x4dfa1f0` lists it first, next to three environment names (`prod-pc`,
`prod-qa-pc`, `dev-stable-pc`) and the reflected enum `EOTGCAuthProvider` (`OTGC` is the prefix of the online
request types). Code at `0x348ccdc` and `0x348ccef` reads that table and `0x348cb3d` reads the neighbouring
string pointer `0x4dfa178`; the enum's descriptor `0x4dfa0b8` is referenced from `0x346fd8d`, `0x34709a4`,
`0x3470cc4`, `0x3470f2a` and `0x34afb49`. The function around `0x348cb3d..0x348ccef` is therefore the likely
builder of the sign-in request, and its callers are where to look for what decides to sign in. `/clientConfig`
is referenced from `0x2afe15d`.

Next step for an integration of our own: set the auth address from `mod.json` in the mod itself (the pointer
above), and replace the two data functions with our own AES-GCM (Windows CNG has it), instead of a second
injected DLL. Then find the sign-in trigger.

### Function signatures from a music-player mod: all 31 match

[HamzaEzzRa/Walking-Man](https://github.com/HamzaEzzRa/Walking-Man) (commit `13669fb`, GPL-3.0) locates 31 game
functions by byte pattern. Each pattern matches exactly once in this build's file. The ones relevant here:

| Its name | RVA here | Relation to our notes |
|---|---:|---|
| `GetBooleanFact` (fact resource, fact resource) -> bool | `0x1b09ba0` | reads the fact database global `0x5794720` (the one in "Story progress" above) and calls `0x1afcc10` |
| `GetNotificationPool` | `0x307a4f0` | new here |
| `DrawNotificationText` | `0x3084d10` | new here |
| `CreateRuntimeUITextFromString` | `0x191c9e0` | 0x40 before our UTF-16 builder `0x191ca20` |
| `TryFreeRuntimeUIText` / `AssignRuntimeUIText` | `0x191cad0` / `0x191caf0` | |
| `UpdateRuntimeUIText` | `0x3646c40` | the same function as our "set text element" primitive |
| `InGameUIDrawElement` / `InGameUIUpdateElement` | `0x3049850` / `0x30354a0` | |
| `RenderTask` | `0x197f330` | |
| `GamePreLoad` / `GamePreExit` | `0x1d1f090` / `0x1a12a20` | |
| `ProcessControllerInput` | `0x2dc6440` | |
| `InGameFlagUpdate` / `InGameAreaUpdate` | `0x36adb4` / `0x2bf11d0` | |
| `PostMenuExit` | `0x2694a70` | |
| `PlayUISound` | `0x303e8e0` | |

Two of them land on functions this file had found independently, which supports the rest. None was called or
hooked here; argument meanings beyond the table are that project's and unverified.

### A 2022 type dump: names carry over, offsets often do not

[Nukem9/HZDCoreEditor](https://github.com/Nukem9/HZDCoreEditor) (commit `af45dd8`, no license file) contains a
dump of the original release's reflected types: 4470 classes with member offsets, including the online ones
(`DSNetOnlineObject*`, `DSNetSync*`, `DSQpid*`, `DSOnline*Mission*`, `OTGC*` requests) and a save-state reader.

Method: this build keeps a member descriptor of 0x38 bytes per reflected field in `.data` (type pointer at +0,
member offset in the low 16 bits at +8, name pointer at +0x10), in contiguous runs per class; the known
`CurrentEpisodeHashFact` descriptor at `0x4dc3cf0` has this shape. A scan found 30635 descriptors in 6555 runs.
Each dumped class was matched to the run sharing most of its member names (at least 60 %).

Result: 2448 classes have every dumped offset unchanged, 641 only gained members, 686 have at least one member
at a different offset, 695 found no run. Of 90 online, cargo and road classes: 39 unchanged, 18 gained members,
19 moved, 14 unmatched. Example: `DSMissionGoalsResource.CurrentEpisodeHashFact` is `+0x68` in both, while
`CurrentEpisodeIndexFact` (`+0xa0` here) is absent from the dump.

Derived: use the dump for names and structure, and read every offset from this build's own descriptors. The
run matching is a heuristic (adjacent small classes can share a run), so the counts are approximate.

## Sign-in and the online protocol (2026-11-11)

Pinned build 13300582; all addresses are RVAs. This replaces the open questions of the trial above: the game signs in
to a server of our own, with no code from other projects.

### Why the game never signed in

Observed (static, then live in one test copy):

- The online manager is the global at `0x7c192f0` (size 0xb0, constructor `0x348c550`). Its bytes `+0x59..+0x5b` start
  as 1, 1, 0; `+0x98` holds the value of the command-line option `-otgc_s`. `0x4dfa138` points at the global.
- The login task (`DSNetLoginManager`) is the global at `0x7c16740` (`0x4df8d78` points at it). Its update `0x346f760`
  runs only while the task state at `+8` is 2 and switches on a step byte at `+0x38` (table `0x346fae0`): 0 start,
  2 waiting for the Steam ticket, 5 waiting for the sign-in answer, 6 waiting for the version answer, 7 signed in,
  8 failed.
- Step 0 (`0x346f440`) gives up before any request when the manager's `+0x5b` is set (error 0xcc4e), when the service
  map at `0x7c1ea48` has no `net.param` entry (0xea61), or when that entry's byte `+0x20` is 1 (0xcc4d) or 2 (0xcc4e).
- The task is started by `0x346f690(task, force)`. Its callers are the title menu's message handler `OnLogin`
  (`0x322f830`, in the handler table at `0x4d55288`) and a lazy path for single online objects (`0x349d860`).
- Live, with the network policy on `enforce`: `+0x5b` was 0 and `net.param` `+0x20` was 0, but the task state stayed
  0 through the title screen, Continue (pressed by scripted key) and loading. Nothing asked for a sign-in.

Derived: the refused configuration request is not the gate. The `OnLogin` message does not arrive in a modded copy;
what sends it in a normal game is unknown.

### What the mod does (`mod/online.h`)

- The sign-in address is the engine string object at `0x4dfa130` (a pointer to the characters; the header in front is
  reference count, hash or 0xffffffff for "not computed", length, capacity). The request builder `0x348cb10` reads it
  through the table at `0x4dfa1f0` on every sign-in, so replacing the pointer with a mod-owned string is enough.
- Every gateway body passes `0x19da0e0` (incoming) and `0x19da6a0` (outgoing): one caller each (`0x19d8d4b`,
  `0x19d9187`), always mode 2, which is base64 of an authenticated cipher (12-byte nonce, 16-byte tag) under a 32-byte
  key string at `0x4593388`. Both are replaced by a pass-through that assigns the input string to the output
  (`0x190adf0`) and returns true.
- `0x346f690(task, 0)` is called on the game thread while the task is idle.

Observed (live, loopback): with these three, the game sent the sign-in request, accepted the answer, asked the
gateway for its version and reached step 7.

### The protocol

Observed (live, against a logging server and then `world/`):

1. `GET <address>?provider=steam&display_name=&code=<hex of the Steam session ticket>` with a fixed `Authorization:
   Basic` header, `x-game-id: ds`, `User-Agent: Guerrilla`. The answer is `OTGCAuthResponse` as JSON: `user`
   (`provider`, `providerId`, `id`, `entity`, `displayName`, `display_name`) and `session` (`zone`, `token`,
   `gateway`, `properties`, `roles`).
2. `GET <gateway>/e/getVersion` with `Authorization: Bearer <token>`. The answer must carry `major` 0 and `minor` 12
   (compared at `0x346f83a`; anything else fails the login with 0xcb23).
3. Then `POST <gateway>/e/<name>` calls. With plain bodies the request body is `{"data":"<JSON>"}` with the inner
   quotes not escaped (the game puts what used to be base64 text between the quotes), and a plain JSON answer is
   accepted. Seen with an Episode 3 save right after loading: `getRankingSchedules`, `getOrderedMissions`,
   `getWastedBaggages`, `findMissions`, `lookup`, `findQpidObjects`, `getHighwayResources`, `reverseLookup` (the Steam
   friends list), `getLikeHistory`.
4. The ticket's bytes 12..19 are the 64-bit Steam id (little-endian).

The executable names 45 gateway calls (`strings`, prefix `otgc://gateway/`).

### Message layouts from the reflection data

Method: a reflected class has a descriptor whose first qword ends in `ffffffff` with kind 4 in byte 4, the number of
bases in byte 6 and the number of members in byte 7; the size is the dword at `+0x14`, the name pointer at `+0x40`,
the bases at `+0x60` and the members at `+0x68` (0x38 bytes each: type, offset, name). A second block that names the
class points (at name `+8`) to the JSON keys: 0x30-byte records of member name and key. Enum descriptors (kind 3) have
the value count at `+0xc`, the name at `+0x10` and 0x28-byte value records (value, name) at `+0x18`.

Result: 129 classes named `OTGC…` with 470 members, 23 of them without a JSON key. The ones used so far:

| Class | Members (JSON key: type) |
|---|---|
| `OTGCAuthResponseUser` | `provider`, `providerId`: WString; `id`: String; `entity`: map; `displayName`: WString; `display_name`: String |
| `OTGCAuthResponseSession` | `zone`, `token`, `gateway`: WString; `properties`: map; `roles`: array |
| `OTGCGetVersionResponse` | `version`: WString; `major`, `minor`: int32 |
| `OTGCCreateObjectPost` | `x`, `y`, `m`, `px`, `py`, `pz`, `rx`, `ry`, `rz`: int; `t`, `st`: String; `exp`, `q`, `p`, `lp`: int32; `c` comment, `ri` rope, `si` stone, `bi` bridge, `pi` parking, `vi` vehicle, `ei` extra, `ci` customize; `tags`; `mt`, `rmt` materials |
| `OTGCObjectResponse` | `id`, `c`, `t`, `st`: String; `m`, `l`, `r`: int arrays; `exp`, `lp`, `q`: int32; `ut`: int64; `mt`, `rmt`, `bgs`, `cm`, the same info blocks, `tags` |
| `OTGCFindQpidObjectsBody` | `q`, `aid`: int32; `u`: strings; `ro` road, `m` missions, `o` (`ct`: map of type to count) |
| `OTGCFindQpidObjectsResponseSet` | `ro` roads, `m` missions, `oa`, `ob`, `od`, `oe`, `op`: object arrays |

Object types on the wire are the value names of the enum `OTGCCommonObjectType` (`0x4d82c30`), in value order:
`m z c p a r l s w b t v k n h e u i o x g j d`.

Observed (static): the function that fills an upload's query (`OTGCCreateObjectQuery`: grid `x`, `y`, `m` and type `t`;
the switch at `0x2caebe6` on the construction type byte at `+0x34`, tables `0x2cafe64` and `0x2cafe28`) picks the
letter from the structure's `EDSConstructionPointType` (the type number of the construction table, "Structures"
above; enum descriptor `0x4c33550`, 49 names):

| Construction type | Letter |
|---|---|
| 3 `PlayerSafetyHouse` | `s` |
| 6 `PlayerPost`, 7 `NetPost` | `p` |
| 9 `PlayerWatchTower` | `w` |
| 12 (unnamed, `_#1`) | `a` |
| 15 `PlayerCharger` | `c` |
| 18 `PlayerRainShelter` | `n` |
| 25 `PlayerLadder` | `l` |
| 28 `PlayerFieldRope` | `r` |
| 36 `RoadRebuilder` | `h` |
| 22 `PlayerZipline`, 31 and 34 (bridges), 41, 44 (unnamed) | own code blocks with extra data; letters not read yet (`b`, `g` and `j` are set in that function) |

Every other construction type takes the default path (no upload query). The game's own list of online object kinds is
the enum `EDSNetSyncObjectType` (`0x4dfaba0`, 24 names: Post, Zipline, Charger, Catapult, SafetyHouse, WatchTower,
Bridge, RainShelter, FieldRope, Ladder, VehicleBike, VehicleTruck, Signboard, NationalRoute, PileStone, Road,
PersonalBaggage, DeliveryBaggage, SupplyMission, Mushroom, ChiralBridge, JumpStand, SpeedCar, CatapultShell).

Observed (live): with an Episode 3 save at Lake Knot the game asked `findQpidObjects` for area `q` 226 (`aid` 22123)
with the counts `t` 33, `x` 3, `b` 2, `v` 4, `n` 1, `h` 5, `e` 5: an area asks only for some kinds. A ladder made by
calling `CreateConstruction` (the mod's test command) was not uploaded within four minutes.

### What the game does with a served object (2026-11-11, live, one test copy, Episode 3 save)

Method: a logging server on loopback answered `findQpidObjects` for area 226 with hand-made `OTGCObjectResponse`
objects of type `n`; state was read from the process (the construction table, and the lists below).

Observed:

- The online object manager is the global at `0x7c1abf0`. At `+0x1e8` it has a map from area id to three lists
  (24-byte entries, area id at `+8`, value at `+0x10`; lists at value `+8/+0x10`, `+0x18/+0x20`, `+0x28/+0x30`). A list
  element has the object id string at `+8`, its `EDSNetSyncObjectType` at `+0x10` and its state at `+0x11`
  (`EDSNetSyncObjectState`, descriptor `0x4dfa2f0`: 0 Downloaded, 1 Activating, 2 Activated, 3 Zombie, 4 Fail,
  5 AlwaysFail, 6 Removed). The set-state function is `0x349a040` (lists, id, state, area); states from 4 up move the
  element to the third list. With the save alone, area 226 had 89 elements in state 2 in the first list; their ids
  are the type letter and 12 characters (some `o` and 22).
- Served objects of type `n` arrive as type 7 (RainShelter): `n` is the rain shelter.
- The position is `l[i] / 10^exp` in the game's x, y, z (static: `0x3482500`, three divisions by `pow(10, exp)`;
  an `exp` of zero or less counts as 2). `exp` is a decimal exponent, not a time. An object whose `l` does not have
  exactly three numbers is set to AlwaysFail (`0x34824ea`). Live: with `exp` 2 and `l` in centimetres a construction
  record of type 19 (`NetRainShelter`) appeared at exactly the served position.
- An area asks for a number per kind (`ct`); with `n` 1 only the first of five served shelters was tried, the other
  four went to Fail.
- Before that, an object with at least `net.param +0xf0` (3 when absent) tags equal to one fixed string is set to
  Fail (`0x3482150`).
- The created construction record (state 0) was gone again within about three seconds and the element went to state
  6 (Removed), at each of four outdoor positions. The removal method is the manager's virtual at `0x3487d70`
  (state 6, or 3 when its flag is set).
- All five arrays of the answer (`oa`, `ob`, `od`, `oe`, `op`) are read; an object in any of them is kept. After the
  objects arrive the game sends `lookup` with their creators' ids.

- The short-lived shelter record, watched every 50 ms: created, flag bytes `+0x92` 0x20 to 0x60 and `+0x93` 4 to 5,
  `+0x97` 4 to 0 about a second later, gone after about two seconds; the same with material data (`mt`) supplied.
  Positions far from every existing structure gave AlwaysFail and no record.
- Five cairns (`t` `e`, `si` `{"r":1}`, the same positions): two went to the first list as Activated (state 2) and
  stayed; three went to Fail. Then the same through `world/`: two cairns another player had built on `coop-world`
  were Activated in the game and stayed, and one built for another area id never appeared.
- Letters, from the 150 online objects the save holds (id letter against the element's type): `p` Post, `c` Charger,
  `s` SafetyHouse, `w` WatchTower, `b` Bridge, `n` RainShelter, `r` FieldRope, `l` Ladder, `v` VehicleBike,
  `e` PileStone (cairn), `o` Road (23-character ids), `x` Mushroom, `g` ChiralBridge, `j` JumpStand. Ids starting
  with a capital (`B`, `L`) are in the second list.

- Uploads. The third argument of `CreateConstruction` (the bool at request `+640`) is the upload flag: with it set,
  a `PlayerLadder` built outdoors was uploaded at once with
  `{"x":9,"y":7,"m":22123,"t":"l","px":…,"py":…,"pz":…,"rx":…,"ry":…,"rz":…,"exp":5,"q":226,"st":"0","p":0,"lp":0}`:
  positions and angles (degrees) times 10^5 with `exp` 5, `m` the area's `aid`, `x`/`y` a grid cell. Without the
  flag nothing is uploaded.
- The game reads an object's kind from the first letter of the id the server answers with. An id not starting with
  the type letter gave type 24 (none) in the builder's list, state Removed, and a `deleteObject` two seconds after
  the upload. With ids of the game's own shape (the letter and 12 characters) the ladder stayed: state 2 in the
  first list, no delete.
- A kind the area's request did not ask for is refused: a second copy of the same save asked for no ladders (the
  area holds its full number), and a ladder served anyway went to Fail in each of the five arrays.
- A `PlayerRainShelter` built the same way was uploaded (`"t":"n"`, only `rz` set, `"st":""`); a second copy as
  another player received it, made a `NetRainShelter` record at its place and removed it after about three seconds
  (state Removed), like the hand-made ones. The builder's own shelter was in construction state 0.

- Two copies (2026-11-12). A `PlayerBridge30m` built with the upload flag was uploaded as
  `"t":"b","st":"30","bi":{"a":0}` (only `rz` set). A second copy of the same save, signed in as another player,
  asked for bridges (`b` 2), got it, listed it in its first list as type 6 (Bridge), state 2, and made a
  `NetBridge30m` construction record at the builder's position to the decimetre; both were still there 30 s later and
  the builder's game was not given its own bridge back. Both records (the builder's and the net copy) were in
  construction state 0, as heavy structures made through `CreateConstruction` are until the next load ("Structures");
  on screen neither copy drew the bridge with its porter standing next to it (screenshots in both): a structure in
  state 0 is not drawn. The pause menu's System page has a "Log In" item, greyed out while signed in: the title
  menu's `OnLogin` is not the only place the game offers a sign-in. Resting outdoors for six minutes and getting up produced no cairn upload.
- Why it stays pending (2026-11-12, the request queue at manager `+0x56a278` sampled from outside while a copy
  loaded: count, capacity, pointer; 0x6c8-byte requests). The requests for the save's own built net bridges carry
  flag bytes `+0x63d`, `+0x63e`, `+0x641`, `+0x644`, `+0x647` = 1 (most built structures of the save: `+0x641`,
  `+0x643`, `+0x644`, `+0x647`); the game's request for the downloaded bridge has none of those and instead
  `+0x62d` and `+0x639` = 1. `CreateConstruction` sets only `+0x640` (its bool) and `+0x645`. The downloaded bridge's
  request was the same with full material data (`mt`) on the server, and its record stayed in state 0. Other
  producers of requests: `0x2c47b2c`, `0x2c482f0` (clears `+0x647`), `0x2c60312`, `0x2c823c2`, `0x2c8877d` (sets
  `+0x64a`), `0x2c9b86f` (sets `+0x644`); which one the build menu uses, and what the flags mean, is unknown.
  (Offsets here are into the request; `CreateConstruction`'s bool is request `+0x620` and its constant 1 is `+0x645`:
  the "+640" and "+665" of the Structures section above are stack offsets of that function.)
- The flags are not the cause: requests built by the mod (the research command `request`, same steps as
  `CreateConstruction`) with the built set, the save's extra two and the bytes common to both, for a bridge, a rain
  shelter and a postbox outdoors, all gave records in state 0, not drawn. Ladders reach state 2 at once. Solved
  later the same day: request `+0x645` is the wait ("Structures", "What completes a structure"). The game's own
  request for a downloaded object does not set it (live: the received Net bridge's flag bytes at object `+0xa8` are
  `22 c0 20 04 2c 00 01 04 00 00`, no `+0xab & 0x40`, durability 360000, level 1, against `21 c0 10 04 2c 12 00 24 20 00`
  and level 2 for the save's built Net bridges), so something else holds it in state 0, with the receiver's porter
  far away and next to it alike. It is not marked for removal (`0x2c8822a`: `+0xaa |= 0x40`, `+0xab |= 1`, what a failed
  placement check `0x2c3ecb0` does, the likely end of received rain shelters). Candidates: the range test of
  `0x2c35fb0` for objects without `+0xaa & 0x10` (from `0x2c36121`), the globals at `[0x7bf6e88]+0xf/+0x17`, or the
  object not being in the pending list. The builder's side is solved: a request with the upload flag and no wait is
  built at once and uploaded.
- Likes. `OTGCGetLikeHistoryOneResponse` (missed by the first dump): `t` int64, `oid`, `lp`, `lp2` int, `uid` (from),
  `sc` int32 (summarised count), `lt` (like type). The game asks `getLikeHistory` once, about a minute after signing
  in, with `{"uid":"","since":0}`. Live: after another player's `sendLike` for a ladder this game had uploaded in an
  earlier run, the next run's request was answered with that entry and the game kept running; whether it showed or
  counted the like was not observed (that ladder was not in the save the second run loaded).

Derived: a served object does persist, and the game uploads what is built with the flag; ladders and cairns work
both ways. Rain shelters are taken down by the receiving game after it has made them, with genuine upload data too;
the cause is unknown (the builder's shelter being unfinished is a candidate).

Unknown: why received shelters are removed; the letters of the remaining kinds (zipline, catapult, truck,
signboard, highway); what distinguishes the five arrays and the second list; the grid cell `x`/`y`; the key names of
the `lookup` answer; and whether a build through the game's own menu sets the same flag (assumed, not seen).

