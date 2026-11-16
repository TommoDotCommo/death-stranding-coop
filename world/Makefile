.DEFAULT_GOAL := test
CXX ?= g++
# Debian names the POSIX-thread cross-compiler with a -posix suffix; MSYS2 has the plain name only.
MINGW ?= $(shell command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 && echo x86_64-w64-mingw32-g++-posix || echo x86_64-w64-mingw32-g++)
FLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread
WINFLAGS := $(FLAGS) -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++ -static
build:
	mkdir -p build
build/world-test: world_test.cpp world.cpp world.h json.h | build
	$(CXX) $(FLAGS) world_test.cpp world.cpp -o $@
build/policy-test: policy_test.cpp world.cpp world.h json.h | build
	$(CXX) $(FLAGS) policy_test.cpp world.cpp -o $@
build/curate-test: curate_test.cpp world.cpp world.h json.h | build
	$(CXX) $(FLAGS) curate_test.cpp world.cpp -o $@
# Builder names (README "Builder names"): display names, the label lookup gives, the home in exports.
build/names-test: names_test.cpp world.cpp world.h json.h | build
	$(CXX) $(FLAGS) names_test.cpp world.cpp -o $@
LINK := link.cpp link.h
build/link-test: link_test.cpp $(LINK) world.cpp world.h json.h | build
	$(CXX) $(FLAGS) link_test.cpp link.cpp world.cpp -o $@
# Players' keys (identity.h): Ed25519 and SHA-512 from the vendored Monocypher (../vendor/monocypher).
MONO := ../vendor/monocypher/monocypher.c ../vendor/monocypher/monocypher-ed25519.c
KEYS := identity.cpp identity.h signatures.cpp signatures.h $(MONO) ../vendor/monocypher/monocypher.h ../vendor/monocypher/monocypher-ed25519.h
build/identity-test: identity_test.cpp $(KEYS) | build
	$(CXX) $(FLAGS) identity_test.cpp identity.cpp signatures.cpp $(MONO) -o $@
API := api.cpp api.h $(KEYS)
build/front-test: front_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h | build
	$(CXX) $(FLAGS) front_test.cpp front.cpp api.cpp link.cpp world.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/api-test: api_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h | build
	$(CXX) $(FLAGS) api_test.cpp front.cpp api.cpp link.cpp world.cpp identity.cpp signatures.cpp $(MONO) -o $@
# What the game is told (README "What the game is told"): the notice token and the mod's read.
build/notices-test: notices_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h | build
	$(CXX) $(FLAGS) notices_test.cpp front.cpp api.cpp link.cpp world.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/keys-test: keys_test.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h $(RELAY) | build
	$(CXX) $(FLAGS) keys_test.cpp front.cpp api.cpp link.cpp world.cpp identity.cpp signatures.cpp relay.cpp $(MONO) -o $@
RELAY := relay.cpp relay.h presence.h ../core/relay.h ../core/broadcast.h
build/relay-test: relay_test.cpp $(RELAY) | build
	$(CXX) $(FLAGS) relay_test.cpp relay.cpp -o $@
# The broadcast mode against the real player's side (core/broadcast_client.*), which needs the presence core.
PLAYER := ../core/broadcast_client.cpp ../core/broadcast_client.h ../core/core.cpp ../core/core.h ../core/build_id.h
build/broadcast-test: broadcast_test.cpp $(RELAY) $(PLAYER) | build
	$(CXX) $(FLAGS) broadcast_test.cpp relay.cpp ../core/broadcast_client.cpp ../core/core.cpp -o $@
# Linked servers' presence: two and three relays in memory with real players' sides.
build/presence-test: presence_test.cpp $(RELAY) $(PLAYER) | build
	$(CXX) $(FLAGS) presence_test.cpp relay.cpp ../core/broadcast_client.cpp ../core/core.cpp -o $@
build/coop-world: main.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h $(RELAY) | build
	$(CXX) $(FLAGS) main.cpp front.cpp api.cpp link.cpp world.cpp relay.cpp identity.cpp signatures.cpp $(MONO) -o $@
build/coop-world.exe: main.cpp front.cpp front.h $(API) $(LINK) world.cpp world.h json.h $(RELAY) | build
	$(MINGW) $(WINFLAGS) main.cpp front.cpp api.cpp link.cpp world.cpp relay.cpp identity.cpp signatures.cpp $(MONO) -o $@ -lws2_32 -lbcrypt
# The swarm of stand-in players (mod/swarm.cpp) talks to the built server over real sockets.
../mod/build/coop-swarm: FORCE
	$(MAKE) -C ../mod build/coop-swarm
FORCE:
test: build/identity-test build/world-test build/policy-test build/curate-test build/names-test build/link-test build/front-test build/api-test build/notices-test build/keys-test build/relay-test build/broadcast-test build/presence-test build/coop-world ../mod/build/coop-swarm
	./build/identity-test
	./build/world-test
	./build/policy-test
	./build/curate-test
	./build/names-test
	./build/link-test
	./build/front-test
	./build/api-test
	./build/notices-test
	./build/keys-test
	./build/relay-test
	./build/broadcast-test
	./build/presence-test
	python3 -B test_server.py build/coop-world
	python3 -B test_link.py build/coop-world
	python3 -B test_names.py build/coop-world
	python3 -B test_relay.py build/coop-world ../mod/build/coop-swarm
	python3 -B test_broadcast.py build/coop-world
	python3 -B test_presence.py build/coop-world ../mod/build/coop-swarm
	python3 -B test_mutants.py
all: build/coop-world.exe
clean:
	rm -rf build
.PHONY: test all clean FORCE
