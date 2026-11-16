# Monocypher (vendored)

[Monocypher](https://monocypher.org) 4.0.3, by Loup Vaillant and contributors: `src/monocypher.c`,
`src/monocypher.h` and the optional `src/optional/monocypher-ed25519.c` and `monocypher-ed25519.h`, copied
unchanged from the release archive `monocypher-4.0.3.tar.gz` (SHA-512 `40904ada…f8965`, the same from
monocypher.org and from the GitHub release). Licence: CC0 1.0 or the 2-clause BSD licence, whichever one chooses
([LICENCE.md](LICENCE.md)); both allow use in this MIT project.

Used for Ed25519 (RFC 8032: EdDSA over Curve25519 with SHA-512) and SHA-512 by the players' keys
(`world/identity.h`): the launcher signs its calls to a group's server, the server checks them. Why this library: it
is small (two C files), has no dependencies, was audited (Cure53, 2017), compiles as C++17 with the project's warning
flags on g++ and MinGW, and its licence is public domain or BSD. Nothing else of it is used, and nothing in it is
changed: an update replaces the four files and this note, and `make -C world test` checks the RFC 8032 vectors again.
