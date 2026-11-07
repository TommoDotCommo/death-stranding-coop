.DEFAULT_GOAL := test
HUB ?= $(HOME)/source/coop-anything
CXX ?= g++
MINGW ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
FUZZ := $(HUB)/tools/fuzz
BUILD := build

$(BUILD):
	mkdir -p $@
$(BUILD)/core.o: core.cpp core.h build_id.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c core.cpp -o $@
$(BUILD)/net.o: $(FUZZ)/src/net.cpp $(FUZZ)/include/coopfuzz/net.h $(FUZZ)/include/coopfuzz/rng.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(FUZZ)/include -c $< -o $@
$(BUILD)/tests: tests/test.cpp core.cpp core.h build_id.h $(BUILD)/net.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DDS_TESTING -I$(FUZZ)/include tests/test.cpp core.cpp $(BUILD)/net.o -o $@
test: check-build $(BUILD)/core.o $(BUILD)/tests environment-test
	python3 tests/run.py $(BUILD)/tests
	$(MAKE) -C ../mod test
$(BUILD)/core-win64.o: core.cpp core.h build_id.h | $(BUILD)
	$(MINGW) $(CXXFLAGS) -c core.cpp -o $@
mingw: check-build $(BUILD)/core-win64.o
sanitize: check-build | $(BUILD)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -DDS_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer -I$(FUZZ)/include tests/test.cpp core.cpp $(FUZZ)/src/net.cpp -o $(BUILD)/tests-sanitize
	python3 tests/run.py $(BUILD)/tests-sanitize
clean:
	rm -rf $(BUILD)
check-build:
	python3 build_id.py
.PHONY: test mingw sanitize clean check-build

$(BUILD)/environment-tests: tests/environment.cpp core.cpp core.h build_id.h $(BUILD)/net.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DDS_TESTING -I$(FUZZ)/include tests/environment.cpp core.cpp $(BUILD)/net.o -o $@
environment-test: $(BUILD)/environment-tests
	python3 tests/environment_run.py $(BUILD)/environment-tests
.PHONY: environment-test
