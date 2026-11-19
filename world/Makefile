.DEFAULT_GOAL := test
CXX ?= g++
# Debian names the POSIX-thread cross-compiler with a -posix suffix; MSYS2 has the plain name only.
MINGW ?= $(shell command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 && echo x86_64-w64-mingw32-g++-posix || echo x86_64-w64-mingw32-g++)
FLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread
# The collection fuzzer runs under AddressSanitizer and UndefinedBehaviorSanitizer when the compiler has them.
SANITIZE ?= $(shell echo 'int main(){}' | $(CXX) -x c++ -fsanitize=address,undefined - -o /dev/null >/dev/null 2>&1 && echo -fsanitize=address,undefined)
WINFLAGS := $(FLAGS) -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++ -static
build:
	mkdir -p build
build/world-test: world_test.cpp world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) world_test.cpp world.cpp collections.cpp collection.cpp -o $@
build/policy-test: policy_test.cpp world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) policy_test.cpp world.cpp collections.cpp collection.cpp -o $@
build/curate-test: curate_test.cpp world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) curate_test.cpp world.cpp collections.cpp collection.cpp -o $@
# Roads paved together (README "Roads paved together"): the game's paver materials between players.
build/highway-test: highway_test.cpp world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) highway_test.cpp world.cpp collections.cpp collection.cpp -o $@
# Builder names (README "Builder names"): display names, the label lookup gives, the home in exports.
build/names-test: names_test.cpp world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) names_test.cpp world.cpp collections.cpp collection.cpp -o $@
LINK := link.cpp link.h
build/link-test: link_test.cpp $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) link_test.cpp link.cpp world.cpp collections.cpp collection.cpp -o $@
# Players' keys (identity.h): Ed25519 and SHA-512 from the vendored Monocypher (../vendor/monocypher).
MONO := ../vendor/monocypher/monocypher.c ../vendor/monocypher/monocypher-ed25519.c
KEYS := identity.cpp identity.h signatures.cpp signatures.h $(MONO) ../vendor/monocypher/monocypher.h ../vendor/monocypher/monocypher-ed25519.h
build/identity-test: identity_test.cpp $(KEYS) | build
	$(CXX) $(FLAGS) identity_test.cpp identity.cpp signatures.cpp $(MONO) -o $@
API := api.cpp api.h relay.h presence.h ../core/relay.h ../core/broadcast.h $(KEYS)
build/front-test: front_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) front_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/api-test: api_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) api_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
# What the game is told (README "What the game is told"): the notice token and the mod's read.
build/map-test: map_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) map_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/serverworld-test: serverworld_test.cpp idtag.h front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) serverworld_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/chat-test: chat_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) chat_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/notices-test: notices_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) notices_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
# Kick and ban (README "Kick and ban"): the owner's calls end a player's sessions and passes; a ban refuses new ones.
build/ban-test: ban_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) ban_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
# Route challenges (challenges.h, ../docs/ROUTES.md "On the server"): the store, the API through the Front's hook, and
# the mod's own route core (../mod/route.*) that replays a run before it counts; a mutant for every check.
CHALLENGES := challenges.cpp challenges.h ../mod/route.cpp ../mod/route.h
CHALLENGE_SOURCES := challenges_test.cpp challenges.cpp ../mod/route.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO)
build/challenges-test: challenges_test.cpp $(CHALLENGES) front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) $(CHALLENGE_SOURCES) -o $@
challenges-test: build/challenges-test
	./build/challenges-test
	python3 -B ../mod/mutants.py --sources "$(CHALLENGE_SOURCES)" --prefix build/mutant-challenge- CHALLENGE_LIMITS CHALLENGE_SAME CHALLENGE_VERIFY CHALLENGE_BEST CHALLENGE_UNSIGNED CHALLENGE_POLICY && echo KILLED 6 challenge mutants
.PHONY: challenges-test
# Limits per address (README "Limits per address"): connections, request rate, blocks after wrong guesses.
build/limits-test: limits_test.cpp limits.h | build
	$(CXX) $(FLAGS) limits_test.cpp -o $@
# Collections (../docs/COLLECTIONS.md): the DSK string and its fuzzer (with the sanitizers), and the server's calls.
build/collection-test: collection_test.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) $(SANITIZE) -g collection_test.cpp collection.cpp -o $@
build/collections-test: collections_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h | build
	$(CXX) $(FLAGS) collections_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/keys-test: keys_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h $(RELAY) | build
	$(CXX) $(FLAGS) keys_test.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp identity.cpp signatures.cpp relay.cpp $(MONO) -o $@
RELAY := relay.cpp relay.h presence.h ../core/relay.h ../core/broadcast.h
build/relay-test: relay_test.cpp $(RELAY) | build
	$(CXX) $(FLAGS) relay_test.cpp relay.cpp -o $@
# The broadcast mode against the real player's side (core/broadcast_client.*), which needs the presence core.
PLAYER := ../core/broadcast_client.cpp ../core/broadcast_client.h ../core/core.cpp ../core/core.h ../core/build_id.h
build/broadcast-test: broadcast_test.cpp $(RELAY) $(PLAYER) | build
	$(CXX) $(FLAGS) broadcast_test.cpp relay.cpp ../core/broadcast_client.cpp ../core/core.cpp -o $@
# Fights with visitors: the fight channel against real players' sides.
build/fight-test: fight_test.cpp $(RELAY) $(PLAYER) | build
	$(CXX) $(FLAGS) fight_test.cpp relay.cpp ../core/broadcast_client.cpp ../core/core.cpp -o $@
# Linked servers' presence: two and three relays in memory with real players' sides.
build/presence-test: presence_test.cpp $(RELAY) $(PLAYER) | build
	$(CXX) $(FLAGS) presence_test.cpp relay.cpp ../core/broadcast_client.cpp ../core/core.cpp -o $@
SERVER := ../common/diag_log.h ../common/crash_handler.h main.cpp front.cpp front.h limits.h $(API) $(LINK) world.cpp world.h json.h collections.cpp collection.cpp collection.h $(RELAY) $(CHALLENGES)
SERVER_SOURCES := main.cpp front.cpp api.cpp link.cpp world.cpp collections.cpp collection.cpp relay.cpp identity.cpp signatures.cpp challenges.cpp ../mod/route.cpp $(MONO)
build/coop-world: $(SERVER) | build
	$(CXX) $(FLAGS) $(SERVER_SOURCES) -o $@
# The server for a rented Linux machine (../deploy/vps/README.md): x86_64, linked statically so it needs nothing of
# the machine's libraries and runs on any recent Debian or Ubuntu. Built on the development PC, copied by deploy.sh.
build/coop-world-linux: $(SERVER) | build
	$(CXX) $(FLAGS) -static -static-libgcc -static-libstdc++ $(SERVER_SOURCES) -o $@
linux: build/coop-world-linux
# WORLD_VERSION_RES: the release's VERSIONINFO resource object (tools/release.sh), empty otherwise.
build/coop-world.exe: $(SERVER) | build
	$(MINGW) $(WINFLAGS) $(SERVER_SOURCES) $(WORLD_VERSION_RES) -o $@ -lws2_32 -lbcrypt
# The swarm of stand-in players (mod/swarm.cpp) talks to the built server over real sockets.
../mod/build/coop-swarm: FORCE
	$(MAKE) -C ../mod build/coop-swarm
FORCE:
test: build/chat-test build/map-test build/serverworld-test build/identity-test build/world-test build/policy-test build/curate-test build/highway-test build/names-test build/link-test build/front-test build/api-test build/notices-test build/ban-test build/challenges-test build/limits-test build/keys-test build/relay-test build/broadcast-test build/fight-test build/presence-test build/coop-world ../mod/build/coop-swarm build/collection-test build/collections-test
	./build/identity-test
	./build/collection-test 3000
	./build/collections-test
	./build/world-test
	./build/policy-test
	./build/curate-test
	./build/highway-test
	./build/names-test
	./build/link-test
	./build/front-test
	./build/api-test
	./build/chat-test
	./build/map-test
	./build/serverworld-test
	./build/notices-test
	./build/ban-test
	$(MAKE) challenges-test
	./build/limits-test
	./build/keys-test
	./build/relay-test
	./build/broadcast-test
	./build/fight-test
	./build/presence-test
	python3 -B test_server.py build/coop-world
	python3 -B test_link.py build/coop-world
	python3 -B test_names.py build/coop-world
	python3 -B test_relay.py build/coop-world ../mod/build/coop-swarm
	python3 -B test_broadcast.py build/coop-world ../mod/build/coop-swarm
	python3 -B test_presence.py build/coop-world ../mod/build/coop-swarm
	python3 -B test_limits.py build/coop-world
	python3 -B test_challenges.py build/coop-world
	python3 -B test_collections.py build/coop-world
	python3 -B test_mutants.py
all: build/coop-world.exe
clean:
	rm -rf build
.PHONY: test all clean linux FORCE
