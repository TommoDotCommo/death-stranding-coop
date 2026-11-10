# For agents

You are working on a co-op mod for DEATH STRANDING DIRECTOR'S CUT (PC). Read [README.md](README.md),
[STATUS.md](STATUS.md) and the README of the folder you touch. Follow the
[coop-anything](https://github.com/cranebeaker/coop-anything) playbook (`AGENTS.md` there) for how co-op mods are
built and tested.

Rules:

1. No game files, assets, saves, logs or dumps in this repository. Addresses and layouts in notes are fine.
2. Work only in disposable test copies of the game; never install the proxy into the real game folder and never
   write to the real saves. Hash the real saves before and after every live run.
3. Keep modded copies offline: the network policy defaults to `enforce`; do not ship a change that weakens it, and
   never run a memory-writing feature under `log-only`.
4. Every protection has a mutant that must fail its test. `make test` must stay green.
5. Changes to `mod/config.*` need a rebuilt proxy (it embeds the config parser) and fresh probes before a launch.
   Changes to any source in `mod/build_id.py`'s list change the wire identity: run `python3 mod/build_id.py --write`
   deliberately and deploy the same build to both copies.
6. Verify, then claim: write `unknown` rather than guess, and report only checks that actually ran.
7. Live tests drive a copy through the mod's `pad.script` (keys, mouse, buttons; `mod/pad.h`) and its test command
   files, and read state from the process or the mod log. Never send input through the desktop (`SendInput`, focus
   changes): it fails when someone uses the PC and cannot drive two copies at once.
8. `make test` runs the mutants side by side (`mod/mutants.py`); a new mutant family goes into an existing runner
   or uses that helper, not a serial loop.
