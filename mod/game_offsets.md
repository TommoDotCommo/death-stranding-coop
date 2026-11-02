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
