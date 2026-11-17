Death Stranding Co-op @VERSION@ (alpha, for testers)

Co-op for DEATH STRANDING DIRECTOR'S CUT on PC. Each player plays their own save and story; players on the same
server see each other, ride along and share what they build. This is an ALPHA: expect bugs, and keep a backup of
anything you care about. Co-op keeps its own saves; your ordinary saves are never written.

You need
  - DEATH STRANDING DIRECTOR'S CUT on Steam, build 13300582 (the current Steam build; betas: None), and Steam running.
  - Windows 10 or 11, 64-bit.
  - The other players' invite, or a server address from the person running the group's server.

Install with Setup (recommended)
  1. Download DeathStrandingCoop-Setup.exe from the release page. Optional: check it against
     DeathStrandingCoop-Setup.exe.sha256 (PowerShell: Get-FileHash .\DeathStrandingCoop-Setup.exe).
  2. Run it. Windows SmartScreen may say "Windows protected your PC" because the alpha is not code-signed:
     choose "More info", then "Run anyway". Setup needs no administrator rights.
  3. Setup lists every DEATH STRANDING it finds in your Steam libraries, with a build check. Pick yours (or Browse)
     and choose Install. The options:
       - Desktop shortcut to the co-op launcher (ticked)
       - Open invite links (deathstranding-coop://) in the co-op launcher (ticked)
       - Allow hosting through Windows Firewall (not ticked; asks for administrator permission once)
  4. Choose Play. The co-op launcher opens.

What Setup changes
  - The game folder gets one file, avrt.dll (the game loads it; the game does not ship a file of that name, so no game
    file is replaced), and avrt.dll.coop-receipt.json beside it.
  - %LOCALAPPDATA%\DeathStrandingCoop\ gets the launcher (app\), the mod (redirect-data\mod\mod.dll), co-op saves,
    settings and logs. Every installed file has a .coop-receipt.json beside it, so Uninstall removes exactly those.
  - Another mod's avrt.dll is never replaced: Setup stops and says so (the co-op proxy cannot load another avrt.dll yet).
  - Steam starts the game as usual: without the co-op launcher it plays as before, co-op off.

Without Setup
  Extract the whole zip anywhere, keep the files together and start deathstranding-coop.exe from that folder. It offers
  to install the mod into your game (the same install as Setup, without the shortcut, firewall and link options).
  DeathStrandingCoop-Setup.exe in the zip also works from the extracted folder.
  For scripts: DeathStrandingCoop-Setup.exe status [--json] [<game folder>] | install <game folder> --yes |
  uninstall <game folder> --yes. Exit codes: 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running.

Uninstall
  Run Setup and choose Uninstall. It removes the co-op files, the shortcut, the invite links and the firewall rules.
  (The launcher's About tab, "Remove co-op from the game", takes only the mod out of the game.) Your co-op saves and
  settings stay in
  %LOCALAPPDATA%\DeathStrandingCoop: delete that folder by hand to remove them too. A file you changed after
  installing, or another program's file, is left where it is and listed.

Updating (manual for now)
  Download the new DeathStrandingCoop-Setup.exe and run it: the button says Update. Your co-op saves stay. Every player
  of a group needs the same version.

Playing
  First time: Saves, "Copy my Steam saves into co-op", so co-op continues your story.
  A group: one player hosts the group's server (Play, Host a game), the others paste the invite (Play, Join a friend).
  Keys in the game: F2 marks a problem (see below), J rides along on another player's vehicle, B pings the map.

Known issues (alpha)
  - Only Steam build 13300582 is supported; other builds are refused and the game plays without co-op.
  - Tested mostly as two copies on one PC; two PCs are new. Report what happens.
  - Structures other players built may overlap things in your world (they skip the game's spacing checks).
  - Traffic between players and the server is not encrypted: play on a LAN, a VPN, or a server you trust.
  - Unsigned alpha: SmartScreen and some antivirus programs may warn about it.

Reporting a bug
  1. When something goes wrong in the game, press F2: it marks the moment in the co-op log.
  2. In the launcher: About, "Save diagnostics for a bug report". It writes DeathStrandingCoop-diagnostics-<time>.zip
     to your Desktop, with user names, computer names, addresses and Steam ids taken out. Look inside before you send it.
  3. Open an issue with the bug form: https://github.com/TommoDotCommo/death-stranding-coop/issues/new/choose
     and attach the zip. Say what you did, what you expected and what happened.

Privacy
  The mod and the launcher send nothing to the mod's author and have no telemetry. The game talks only to the co-op
  server you choose (your own, or your group's) and to the other players in your session; the publisher's online
  service is blocked while co-op runs. Setup makes no network connection at all. Diagnostics are only what you send.

Licence: MIT (LICENSE.txt); third-party notices: THIRD-PARTY-NOTICES.txt. Not affiliated with Kojima Productions or the
game's publishers. Source: https://github.com/TommoDotCommo/death-stranding-coop
