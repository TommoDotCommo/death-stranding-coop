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

The double has no backpack or hood. The porter's backpack, pouches and other gear are about 30 attached child
entities (plain `Entity` and groups of `DSAttachOrPhysicsEntity`), not body art parts, so outfit parity means
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

## Vehicles (static, 2026-11-07; read-only provider, no vehicle created or live vehicle checked)

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
settings, which this pass did not read. So whether V, middle mouse, Tab or F1 is bound by default is **unknown** from
static evidence. Middle mouse does have a menu meaning (`EMenuInputFunction_FUNCTION_MOUSE_MIDDLE`, map menu
`OnMouseMiddle...` handlers). The mod's ping key is V by default and changeable at run time (`key <vk>` command), read
only while the game window has the focus; a live check of the key-assign menu is the next step.

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
