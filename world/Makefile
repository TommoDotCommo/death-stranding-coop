.DEFAULT_GOAL := test
CXX ?= g++
MINGW ?= x86_64-w64-mingw32-g++-posix
FLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread
WINFLAGS := $(FLAGS) -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++ -static
build:
	mkdir -p build
build/world-test: world_test.cpp world.cpp world.h json.h | build
	$(CXX) $(FLAGS) world_test.cpp world.cpp -o $@
build/front-test: front_test.cpp front.cpp front.h world.cpp world.h json.h | build
	$(CXX) $(FLAGS) front_test.cpp front.cpp world.cpp -o $@
build/coop-world: main.cpp front.cpp front.h world.cpp world.h json.h | build
	$(CXX) $(FLAGS) main.cpp front.cpp world.cpp -o $@
build/coop-world.exe: main.cpp front.cpp front.h world.cpp world.h json.h | build
	$(MINGW) $(WINFLAGS) main.cpp front.cpp world.cpp -o $@ -lws2_32
test: build/world-test build/front-test build/coop-world
	./build/world-test
	./build/front-test
	python3 -B test_server.py build/coop-world
	python3 -B test_mutants.py
all: build/coop-world.exe
clean:
	rm -rf build
.PHONY: test all clean
