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
LINK := link.cpp link.h
build/link-test: link_test.cpp $(LINK) world.cpp world.h json.h | build
	$(CXX) $(FLAGS) link_test.cpp link.cpp world.cpp -o $@
build/front-test: front_test.cpp front.cpp front.h $(LINK) world.cpp world.h json.h | build
	$(CXX) $(FLAGS) front_test.cpp front.cpp link.cpp world.cpp -o $@
RELAY := relay.cpp relay.h ../core/relay.h ../core/broadcast.h
build/relay-test: relay_test.cpp $(RELAY) | build
	$(CXX) $(FLAGS) relay_test.cpp relay.cpp -o $@
# The broadcast mode against the real player's side (core/broadcast_client.*), which needs the presence core.
PLAYER := ../core/broadcast_client.cpp ../core/broadcast_client.h ../core/core.cpp ../core/core.h ../core/build_id.h
build/broadcast-test: broadcast_test.cpp $(RELAY) $(PLAYER) | build
	$(CXX) $(FLAGS) broadcast_test.cpp relay.cpp ../core/broadcast_client.cpp ../core/core.cpp -o $@
build/coop-world: main.cpp front.cpp front.h $(LINK) world.cpp world.h json.h $(RELAY) | build
	$(CXX) $(FLAGS) main.cpp front.cpp link.cpp world.cpp relay.cpp -o $@
build/coop-world.exe: main.cpp front.cpp front.h $(LINK) world.cpp world.h json.h $(RELAY) | build
	$(MINGW) $(WINFLAGS) main.cpp front.cpp link.cpp world.cpp relay.cpp -o $@ -lws2_32
# The swarm of stand-in players (mod/swarm.cpp) talks to the built server over real sockets.
../mod/build/coop-swarm: FORCE
	$(MAKE) -C ../mod build/coop-swarm
FORCE:
test: build/world-test build/policy-test build/curate-test build/link-test build/front-test build/relay-test build/broadcast-test build/coop-world ../mod/build/coop-swarm
	./build/world-test
	./build/policy-test
	./build/curate-test
	./build/link-test
	./build/front-test
	./build/relay-test
	./build/broadcast-test
	python3 -B test_server.py build/coop-world
	python3 -B test_link.py build/coop-world
	python3 -B test_relay.py build/coop-world ../mod/build/coop-swarm
	python3 -B test_broadcast.py build/coop-world
	python3 -B test_mutants.py
all: build/coop-world.exe
clean:
	rm -rf build
.PHONY: test all clean FORCE
