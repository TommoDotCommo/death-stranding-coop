# Testing Death Stranding Co-op (closed alpha)

Thank you for testing. This page is for the testers of the closed alpha: how to install, how to join the test
server, what is recorded, how to report a problem, and how to stay safe. The alpha is unfinished software: expect
bugs, crashes and features that do not work yet.

## What you need

- DEATH STRANDING DIRECTOR'S CUT on Steam (Windows), updated by Steam to the current version. The launcher checks the
  game's version and tells you if yours is not the one the mod supports.
- The alpha's files from the link the organiser sent you: the installer (`DeathStrandingCoop-Setup.exe`, or the zip
  package) and, if you want them, the test saves.
- The **group invite** for the test server (a code that starts with `DSG-`), sent to you privately.

## Install

1. Run `DeathStrandingCoop-Setup.exe` (Windows may warn that it is not signed: More info, Run anyway) and choose
   **Install**. It finds the game through Steam, adds one small file (`avrt.dll`) to the game's folder and puts the
   launcher and the mod in `%LOCALAPPDATA%\DeathStrandingCoop\`, with a desktop shortcut **Death Stranding Co-op**.
   (Or extract the zip and start `deathstranding-coop.exe`: it offers the same install.)
2. The co-op game keeps its own saves in that folder; your normal saves are not changed. The launcher's **Saves** tab
   lets you start from one of the test saves.
3. To remove it: run the Setup again and choose **Uninstall** (or the launcher's About page, **Remove co-op from the
   game**). Starting the game from Steam as usual then plays without the mod.
4. The first start of the game asks about the publisher's telemetry (Cancel is fine) and shows two notices, and the
   first load shows a few "New additional content found" notices: press OK through them. On a slow PC, lower the
   graphics once in the game (Options, Graphics Settings): the co-op game keeps its own settings.

## Join the test server

1. Start **Death Stranding Co-op** (the desktop shortcut). In **Play**, set **your name**: use a nickname, not your real name (see "Stay
   safe"). It is shown over your character to the players near you.
2. Choose **Join a friend**, paste the `DSG-...` invite and choose **Join group**.
3. The launcher shows the server's rules and asks two things: whether to share what you build there, and whether the
   server may take the structures already in your save. Choose, then **Join with these choices**. The game starts.
4. Play as usual. Players you meet near you appear in your world with their nicknames; structures other testers built
   (once the organiser has accepted them) appear in yours. On a server with public rules you see only the server's
   structures: the ones in your save are hidden while you play there and are all back when you play without it. What
   another tester builds appears the next time your game loads that area, and some kinds (generators) not at all yet.

**Meeting the others.** Everyone plays their own save, and you see only the players near you in the game. For a
test session the organiser names one test save (for example *Episode 3: Lake Knot distribution centre*): before you
join, choose it in the launcher's **Saves** tab (**Use this test save**), so everybody starts in the same place. Your
own co-op saves are kept in a backups folder.

Use only the test server's `DSG-` invite. Do **not** use "Host a game" or a `DS-...` invite with people you do not
know: a two-player lobby connects your PC directly to the other player's, and each of you can then see the other's
IP address. Through the test server nobody but the server sees your address.

Your launcher makes a **player key** the first time (`player.key` in the co-op folder). It is who you are on the
server: nobody else can play or build in your name. Do not share that file; if you lose it, you start as a new
player.

## Keys in the game

Hold **F1** in the world for a list of the co-op keys that work in your session. Press **P** to see which server you are on,
who is online and the server's rules. Where the server's rules allow text chat, press **Y**: type, **Enter** sends to
everyone on the server, **Esc** cancels. The server's owner can read what is said. Where the rules allow it, the game's map
shows the other players on the same part of the world, with their nicknames.

## Test mode

The test server may run in **test mode**, so you can try everything without gathering materials or resting. The
server's rules say so before you join ("Test mode: unlimited materials (U), stamina and blood for everyone here"), and
the game tells you once in its message feed at the side of the screen ("Test mode is on: ...").

- Press **U** in the game: a set of material crates is put on the ground two metres behind you. Pick them up as any
  cargo (stand next to them, E). One set every 5 seconds; at most 20 of your crates lie on the ground at once, so pick
  some up before you ask for more (the game tells you when you have 20 out). Crates are never taken away. A set is one
  crate of each material: chiral crystals, metals, ceramics, chemicals, special alloys and resins.
- **Blood, stamina and your own battery stay full**, except while a cutscene runs, while you are down or while BTs hold
  you. Press **F3** to switch each of them on or off: **Switch** changes the marked line, **Next** moves the mark, **Close** or
  Esc leaves.
- Not in test mode (yet): your vehicle's battery, cargo damage, your equipment wearing out and timefall still work as
  in the normal game.

Test mode changes your co-op save (the crates you pick up stay in it). Your normal saves are not touched.

To try it outside the test server, in your own solo or two-player games: Settings, **Test mode (alpha)**.

## What is recorded

On your PC (in `%LOCALAPPDATA%\DeathStrandingCoop\`):

- logs of the mod, the launcher and the game's co-op parts: what the mod did, errors, and where your character was;
- if the game crashes, a crash report and possibly a memory dump of the game (it can hold what was in the game's
  memory at that moment: send it only with the diagnostics, to the organiser);
- your settings, your player key and the co-op saves.

Nothing of this is uploaded by itself. You decide whether to send a diagnostics file (below).

On the test server, which the organiser runs:

- your nickname, your player key (a random number your launcher made, not your Steam account), what you build and like
  there, and your choices for that server;
- while you play: where you are in the game, what you carry and wear, so the players near you can see it;
- your **IP address**, in the server's log, kept 14 days, so the organiser can block an address that attacks the server.
  Other players never see it.
- The game itself sends its Steam sign-in data to the server it is given; the test server does not store your Steam
  id and never shows it to other players.

## Other programs inside the game

Antivirus, overlays and capture tools often put themselves into running games. The mod lets the game go on: it notes the
file in its log and holds that file to the mod's own limits on network and file access, so such a tool may not work fully
inside the game. If the game misbehaves with one of them, try without it and mention it in your report.

## Report a problem

1. If something goes wrong while playing, press **F2** in the game: it marks the moment in your logs, so the organiser
   can find it. In a two-player game it also marks your partner's logs; in group play it marks the logs of the players
   near you who can see you, and the server's log too. One press every 2 seconds counts (at most 30 an hour).
2. In the launcher: About → **Send diagnostics**. This saves a zip on your Desktop (`DeathStrandingCoop-diagnostics-...zip`)
   and shows it in Explorer. Before it is saved, it is cleaned: your Windows user name and computer name, network
   addresses, Steam ids and account numbers, and e-mail addresses are replaced; open the zip and look if you like. Nothing is sent by the launcher: you send the zip yourself.
3. Send it to the organiser the way you were told, with a few lines: what you did, what you expected, what happened,
   and roughly when.

## Stay safe

- **Never share personal information** with other testers: not your real name, age, school, where you live, your
  social media or your Steam profile. Your nickname is all the game shows; keep it that way.
- The server shows other players only your **nickname**, your character and what you build. It never shows them your
  IP address, your Steam name or your Steam id.
- **Text chat is off on the organiser's test server** unless the organiser turns it on (P shows the rules). Where it is on,
  everyone on that server reads what you type, the server's owner can read it too and keeps it in the server's log, and
  there are no private messages. Keep personal information out of it. If someone asks you to talk somewhere else (Discord,
  Steam, social media) because of the game, you do not have to, and you can tell the organiser.
- **Players on the map are off on the organiser's test server** unless the organiser turns them on (P shows the rules).
  Where they are on, everyone on that server can see where you are in the game while you play, and you cannot hide from it.
- Tell the organiser about anyone who makes you uncomfortable (an offensive nickname, following you around). The
  organiser can remove a player from the server and ban them.
- Do not pass the invite on. Whoever has it can join the server.
- The connection to the test server is not encrypted, as for many game servers: someone on the same network as you
  (for example a public Wi-Fi) could read what the game sends. Prefer your home network.

### For parents and guardians of testers under 18

- The mod has no voice and no private messages. It has text chat, which is off on the organiser's test server unless the
  organiser turns it on; where it is on, every player on that server can read what is typed, and the organiser can read
  it in the server's log. Other testers see your child's chosen nickname and character in the game, and the structures
  your child builds; they do not see any contact details, real name or IP address.
- The test server is run by the organiser on a rented server, not on anyone's home connection; your child's PC
  connects only to that server, never directly to another tester's PC (as long as only the `DSG-` invite is used, see
  above).
- The organiser can see, on the server, the IP address and nickname your child uses, for up to 14 days.
- Please make sure your child uses a nickname, does not share personal details, and tells you or the organiser about
  anything that bothers them. The organiser can remove and ban any player.
- To stop: remove the co-op from the game (About → Remove co-op from the game); the game then runs as normal.
