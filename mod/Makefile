.DEFAULT_GOAL := all
CXX ?= g++
MINGW ?= x86_64-w64-mingw32-g++-posix
FLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
WINFLAGS = $(FLAGS) -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++ -static
SRC = game_thread.cpp puppet.cpp discovery.cpp mod.cpp config.cpp providers.cpp pose.cpp ../core/core.cpp
HEADERS = game_thread.h puppet.h pose.h fake_memory.h discovery_options.h discovery.h discovery_signatures.h timing.h mod.h config.h providers.h signature.h identity.h ../core/core.h ../core/build_id.h
build:
	mkdir -p $@
identity:
	python3 -B generate_discovery_signatures.py
	python3 build_id.py
	python3 ../core/build_id.py
all: identity build/puppet-test.exe build/mod.dll build/mod_probe.exe build/mod-no-build.dll build/mod-no-interpolation.dll
build/mod.dll: $(SRC) $(HEADERS) | build
	$(MINGW) $(WINFLAGS) -shared $(SRC) -o $@ -lws2_32 -lbcrypt -lwinmm
build/mod-no-build.dll: $(SRC) $(HEADERS) | build
	$(MINGW) $(WINFLAGS) -DDS_TESTING -DMOD_MUTANT_BUILD -shared $(SRC) -o $@ -lws2_32 -lbcrypt -lwinmm
build/mod-no-interpolation.dll: $(SRC) $(HEADERS) | build
	$(MINGW) $(WINFLAGS) -DDS_TESTING -DMOD_MUTANT_INTERPOLATION -shared $(SRC) -o $@ -lws2_32 -lbcrypt -lwinmm
build/mod_probe.exe: mod_probe.cpp mod.h timing.h | build
	$(MINGW) $(WINFLAGS) $< -o $@ -lwinmm
build/mod.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -fPIC -shared $(SRC) -o $@
build/mod-no-build.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DDS_TESTING -DMOD_MUTANT_BUILD -fPIC -shared $(SRC) -o $@
build/mod-no-interpolation.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DDS_TESTING -DMOD_MUTANT_INTERPOLATION -fPIC -shared $(SRC) -o $@
build/mod_probe: mod_probe.cpp mod.h timing.h | build
	$(CXX) $(FLAGS) $< -o $@ -ldl
build/fake.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -fPIC -shared $(SRC) -o $@
build/fake-no-build.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DDS_TESTING -DMOD_MUTANT_BUILD -fPIC -shared $(SRC) -o $@
build/fake-no-interpolation.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DDS_TESTING -DMOD_MUTANT_INTERPOLATION -fPIC -shared $(SRC) -o $@
build/unit: unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp $(HEADERS) | build
	$(CXX) $(FLAGS) unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o $@
host: identity build/mod.so build/mod-no-build.so build/mod-no-interpolation.so build/mod_probe build/unit
test: game-thread-test discovery-test pose-test timing-test host build/fake.so build/fake-no-build.so build/fake-no-interpolation.so
	./build/unit
	python3 -B test_probe.py
	python3 -B -m unittest -q test_game_thread_api test_probe_puppet test_actor_resources test_actor_reader_windows
	python3 -B test_actor_resource_mutants.py
.PHONY: all identity host test


build/timing-test: timing_test.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp $(HEADERS) | build
	$(CXX) $(FLAGS) -DDS_TESTING timing_test.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o $@
timing-test: build/timing-test
	python3 -B test_timing.py
.PHONY: timing-test

build/discovery-test: discovery_test.cpp discovery.cpp discovery.h discovery_signatures.h | build
	$(CXX) $(FLAGS) discovery_test.cpp discovery.cpp -o $@
discovery-test: build/discovery-test
	./build/discovery-test
	python3 -B test_discovery_mutants.py
.PHONY: discovery-test

build/pose-test: pose_test.cpp pose.cpp providers.cpp discovery.cpp ../core/core.cpp $(HEADERS) | build
	$(CXX) $(FLAGS) -DDS_TESTING pose_test.cpp pose.cpp providers.cpp discovery.cpp ../core/core.cpp -o $@
pose-test: build/pose-test
	./build/pose-test
	python3 -B test_pose_mutants.py
.PHONY: pose-test

build/game-thread-test: game_thread_test.cpp game_thread.cpp discovery.cpp game_thread.h mod.h fake_memory.h | build
	$(CXX) $(FLAGS) game_thread_test.cpp game_thread.cpp discovery.cpp -o $@
build/puppet-test: puppet_test.cpp puppet.cpp pose.cpp discovery.cpp puppet.h | build
	$(CXX) $(FLAGS) puppet_test.cpp puppet.cpp pose.cpp discovery.cpp -o $@
game-thread-test: build/game-thread-test build/puppet-test
	./build/game-thread-test
	./build/puppet-test
	python3 -B test_game_thread_mutants.py
.PHONY: game-thread-test

build/puppet-test.exe: puppet_test.cpp puppet.cpp pose.cpp discovery.cpp puppet.h | build
	$(MINGW) $(WINFLAGS) puppet_test.cpp puppet.cpp pose.cpp discovery.cpp -o $@ -lbcrypt -lwinmm
