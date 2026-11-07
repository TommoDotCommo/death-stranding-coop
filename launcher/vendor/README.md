# Vendored co-op reference

Copied from [TommoDotCommo/coop-mod-kit](https://github.com/TommoDotCommo/coop-mod-kit)
at commit `edf48385b7929636c0c72a263586374bbe831716` (MIT; see LICENSE).

`coop_invite.h` has one intentional extension to `ci_game_at`: prefix `DS`, product
`Death Stranding`, probe id `DSDC`. The payload format, codec and recognizer are
unchanged. Other headers and tests are copied unchanged. The launcher compiles and
runs the portable tests directly; its extra tests exercise the new game's entry.
`docs/join/index.html` derives from the same kit commit and adds the DS scheme.
Future kit updates must preserve that small game-table extension or upstream it.
