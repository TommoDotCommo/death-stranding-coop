# Roadmap and to-dos

What is left between the closed alpha and a public, worldwide service, and the features planned on the way. Items
marked **(owner)** need a decision or an action from the project's owner; everything else is development work.
Status of what exists: [STATUS.md](../STATUS.md). Words: [GLOSSARY.md](GLOSSARY.md).

## Closed alpha (now)

- [x] **(owner)** Rent the test server (a small Linux VPS near the testers: Ubuntu 24.04, 1 GB, public IPv4) and
      give the deploy SSH access; then `deploy/vps/deploy.sh` and send testers the `DSG-` invite privately.
- [x] **(owner)** Publish `v0.1.0-alpha.1` for testers: a GitHub pre-release (2026-11-18).
- [x] Group play shows other players' gear and carried cargo (next release; off in `v0.1.0-alpha.1`).
- [x] No Odradek arm left floating by a hidden player's body (next release).
- [x] First run of `deploy/vps` on a real server (2026-11-18): two PCs on one home network joined over the internet,
      met through the relay (24 ms round trip), F2 marks reached the server's log.
- [ ] Players joining from other homes, behind ordinary routers.
- [ ] A real game crash caught by the crash recorder (dump and marker) and read with `tools/diag_report.py`.
- [ ] Setup on a PC that downloaded it from GitHub: SmartScreen's wording, the firewall option's administrator prompt.
- [ ] `tools/diag_report.py --align` with marks shared by more than two logs (group play).

## Before going public

Security and safety:

- [ ] **Encryption.** HTTP to `coop-world` is plain, and so is the UDP relay: a sign-in ticket, session or relay
      token seen on the path (public Wi-Fi) can be used within its lifetime. TLS for the HTTP side (a certificate per
      server, or a TLS front), and an encrypted relay (keys from the player's signed session).
- [ ] Keyless (friends) servers hand the game's Steam ids to other players through `lookup`: answer without
      `boundAccount` if the game accepts it, or require player keys everywhere public.
- [ ] Rate limits per player key (today per address), and a cap on new accounts per address and day.
- [ ] Moderation: nickname filter, a report button (in the launcher and an F-key in the game), bans shared across
      the project's servers, an owner's view of reports.
- [ ] Hosting that withstands attacks on a public UDP port (providers with DDoS mitigation); server addresses served
      by the directory API, not printed on the website.
- [ ] **(owner)** A privacy policy (server logs keep addresses 14 days; Australian privacy law and the GDPR once
      players come from the EU) and a data-removal path.
- [ ] Code signing for Setup and the binaries (the release script has `SIGN_CMD`; a certificate is needed).
- [ ] **(owner)** Think about how the game's publisher may see a public service for its game: free, no game
      files or assets, never on the publisher's servers.

Service:

- [ ] **Server directory website**: static site plus a small API (Cloudflare Pages, Workers and D1 planned); servers
      register with heartbeats signed by their owner key; the directory checks a listed server answers; region,
      rules preset, version, players.
- [ ] Launcher: a "Public servers" list from the directory, with its own ping to each server.
- [ ] Official servers in three regions (Asia-Pacific, Americas, Europe), linked with `--link` so structures travel
      worldwide while real-time presence stays regional.
- [ ] Several servers per region when one fills (a world holds 16 players today): the directory points new players
      at the least full one; linked presence between a region's servers.
- [ ] Automatic updates (the launcher checks the release page; MDK2 and Witness have the pattern).
- [ ] Router port mapping (UPnP / NAT-PMP) for players who host their own server; Steam friends discovery.
- [ ] Controller navigation in the launcher; Steam Deck (co-op mod standard section K).
- [ ] Chain-loading another mod's `avrt.dll` instead of refusing to install next to it.

## Features in progress

- [ ] **Shareable collections ("kits")**: choose structures you built, export them as a `DSK1-...` string, import
      one into a world (here or at the original place), within the server's rules (`docs/COLLECTIONS.md`).
- [ ] **Route challenges**: record a route, set a corridor width (for example 5 m over a mountain), a time limit and
      rules, share it as a `DSR1-...` string or on a server; the mod checks the runner stays inside the corridor and
      the server keeps the best times (`docs/ROUTES.md`).

- [ ] Route challenge results from a game in test mode (unlimited stamina) are marked, and boards keep them apart.

## Requested next

In the game's own UI wherever it can carry them, rather than a display of our own.

- [ ] **Server info in the game**: which server this is, its rules and settings, and who is online.
- [ ] **Text chat in the game** between the players on a server (with the moderation it needs before going public).
- [ ] **Debug menu in the game**: open it and switch infinite stamina, blood, battery and the like on and off.
- [ ] **Hold F1 for a help page**: the mod's keys and useful information.

## Later features

- [ ] Native custom orders: an order that appears at a terminal with its cargo and is handed in at another, with a
      route challenge's rules (needs research: creating order cargo, the order's state, the terminal's list).
- [ ] Collections without a server (solo), and collections of cargo and equipment loadouts.
- [ ] Roads through the server: the game asks and is answered but takes nothing yet (STATUS, "Roads, signs and lost
      cargo").
- [ ] Received structures can overlap the player's own (the game's spacing check is skipped for them).
- [ ] Story, order and reward sharing stay opt-in settings and are not developed further.

## Development

- [ ] Developers who test the player build on the same PC as the lab copies must remove it before lab deploys (the
      lab checks that the Steam install it links from is unchanged).
