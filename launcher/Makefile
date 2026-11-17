CXX = x86_64-w64-mingw32-g++
# MSYS2 ships the resource compiler as plain windres.
WINDRES ?= $(shell command -v x86_64-w64-mingw32-windres >/dev/null 2>&1 && echo x86_64-w64-mingw32-windres || echo windres)
FLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++ -static
# -static: MSYS2's toolchain otherwise links libwinpthread-1.dll dynamically and the binaries fail to start (0xC000007B).
# Players' keys: ../world/identity.* on the vendored Monocypher (../vendor/monocypher).
SRC = core.cpp install.cpp strand.cpp owner.cpp saves.cpp testsaves.cpp ../mod/config.cpp ../redirect/path_map.cpp ../world/identity.cpp ../vendor/monocypher/monocypher.c ../vendor/monocypher/monocypher-ed25519.c
HEADERS = ../common/diag_log.h ../common/crash_handler.h core.h install.h ../dist/sha256.h strand.h owner.h testsaves.h ../world/json.h ../world/identity.h ../vendor/monocypher/monocypher.h ../vendor/monocypher/monocypher-ed25519.h saves.h net_win.h launch_win.h firewall_win.h ../mod/config.h ../mod/identity.h ../redirect/startup_policy.h vendor/coop_invite.h vendor/coop_discover.h vendor/coop_discover_win.h
# The release version (tools/release.sh passes the tag): the About page and the VERSIONINFO resource.
VERSION ?= 0.1.0-dev
all: build/deathstranding-coop.exe build/net-fixture.exe build/discover-win-test.exe
build:
	mkdir -p build
build/version.rc: FORCE | build
	python3 -B ../tools/versioninfo.py "$(VERSION)" deathstranding-coop.exe "Death Stranding Co-op launcher" $@
build/deathstranding-coop.exe: main.cpp $(SRC) $(CHALLENGE_SRC) challenges.h ../mod/route.h $(HEADERS) launcher.rc build/version.rc ../dist/diag.ps1 ../dist/diag-rules.json | build
	$(WINDRES) launcher.rc -o build/launcher-res.o
	$(CXX) $(FLAGS) -DCOOP_VERSION='"$(VERSION)"' -municode -mwindows main.cpp $(SRC) $(CHALLENGE_SRC) build/launcher-res.o -o $@ -lcomctl32 -lshell32 -lole32 -loleaut32 -luuid -lws2_32 -liphlpapi -lbcrypt
build/core-test: core_test.cpp $(SRC) $(HEADERS) ../world/signatures.cpp ../world/signatures.h | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror core_test.cpp $(SRC) ../world/signatures.cpp -o $@
# The install core shared with Setup (install.h), in fake folders.
build/install-test: install_test.cpp install.cpp install.h ../dist/sha256.h ../world/json.h | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror install_test.cpp install.cpp -o $@
build/invite-test: vendor/tests/coop_invite_test.c vendor/coop_invite.h | build
	cc -std=c99 -O1 -g -Wall -Wextra -Ivendor $< -o $@
build/discover-test: vendor/tests/coop_discover_test.c vendor/coop_discover.h | build
	cc -std=c99 -O1 -g -Wall -Wextra -Ivendor $< -o $@
build/saves-test: saves_test.cpp saves_vectors.h $(SRC) $(HEADERS) | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror saves_test.cpp $(SRC) -o $@
build/testsaves-test: testsaves_test.cpp saves_vectors.h $(SRC) $(HEADERS) | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror testsaves_test.cpp $(SRC) -o $@
# The Challenges tab without its window (challenges.h, ../docs/ROUTES.md "The launcher"), with the mod's route core.
CHALLENGE_SRC = challenges.cpp ../mod/route.cpp
build/challenges-test: challenges_test.cpp challenges.h $(CHALLENGE_SRC) ../mod/route.h $(SRC) $(HEADERS) | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror challenges_test.cpp $(CHALLENGE_SRC) $(SRC) ../world/signatures.cpp -o $@
challenges-test: build/challenges-test
	./build/challenges-test
	python3 -B ../mod/mutants.py --sources "challenges_test.cpp $(CHALLENGE_SRC) $(SRC) ../world/signatures.cpp" --prefix build/mutant-challenges- CHALLENGES_EDIT_POINTS CHALLENGES_SEND_FAILED CONFIG_ROUTE_DROPPED && echo KILLED 3 launcher challenge mutants
.PHONY: challenges-test
# Group play over real sockets: the launcher's server command line with the built server and stand-in players.
../world/build/coop-world: FORCE
	$(MAKE) -C ../world build/coop-world
../mod/build/coop-swarm: FORCE
	$(MAKE) -C ../mod build/coop-swarm
FORCE:
test: build/core-test build/install-test build/saves-test build/testsaves-test build/invite-test build/discover-test ../world/build/coop-world ../mod/build/coop-swarm
	python3 -B test_core.py
	$(MAKE) challenges-test
	python3 -B test_testsaves.py build/testsaves-test
	python3 -B test_group.py build/core-test ../world/build/coop-world ../mod/build/coop-swarm
	python3 -B test_identity.py build/core-test ../world/build/coop-world
	python3 -B test_owner.py build/core-test ../world/build/coop-world
	./build/invite-test 20000
	./build/discover-test 10000
	python3 -B test_mutants.py
.PHONY: all test FORCE

build/net-fixture.exe: net_fixture.cpp saves_vectors.h net_win.h $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) net_fixture.cpp $(SRC) -o $@ -lws2_32 -liphlpapi -lbcrypt

build/discover-win-test.exe: vendor/tests/coop_discover_win_test.c $(HEADERS) | build
	x86_64-w64-mingw32-gcc -std=c99 -O1 -Wall -Wextra -Ivendor $< -o $@ -lws2_32 -liphlpapi
