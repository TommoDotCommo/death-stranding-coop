CXX = x86_64-w64-mingw32-g++
FLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++
SRC = core.cpp ../mod/config.cpp ../redirect/path_map.cpp
HEADERS = core.h net_win.h launch_win.h ../mod/config.h ../mod/identity.h ../redirect/startup_policy.h vendor/coop_invite.h vendor/coop_discover.h vendor/coop_discover_win.h
all: build/deathstranding-coop.exe build/net-fixture.exe build/discover-win-test.exe
build:
	mkdir -p build
build/deathstranding-coop.exe: main.cpp $(SRC) $(HEADERS) launcher.rc | build
	x86_64-w64-mingw32-windres launcher.rc -o build/launcher-res.o
	$(CXX) $(FLAGS) -municode -mwindows main.cpp $(SRC) build/launcher-res.o -o $@ -lcomctl32 -lshell32 -lole32 -luuid -lws2_32 -liphlpapi -lbcrypt
build/core-test: core_test.cpp $(SRC) $(HEADERS) | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror core_test.cpp $(SRC) -o $@
build/invite-test: vendor/tests/coop_invite_test.c vendor/coop_invite.h | build
	cc -std=c99 -O1 -g -Wall -Wextra -Ivendor $< -o $@
build/discover-test: vendor/tests/coop_discover_test.c vendor/coop_discover.h | build
	cc -std=c99 -O1 -g -Wall -Wextra -Ivendor $< -o $@
test: build/core-test build/invite-test build/discover-test
	python3 -B test_core.py
	./build/invite-test 20000
	./build/discover-test 10000
	python3 -B test_mutants.py
.PHONY: all test

build/net-fixture.exe: net_fixture.cpp net_win.h $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) net_fixture.cpp $(SRC) -o $@ -lws2_32 -liphlpapi -lbcrypt

build/discover-win-test.exe: vendor/tests/coop_discover_win_test.c $(HEADERS) | build
	x86_64-w64-mingw32-gcc -std=c99 -O1 -Wall -Wextra -Ivendor $< -o $@ -lws2_32 -liphlpapi
