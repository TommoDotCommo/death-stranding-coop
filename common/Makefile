.DEFAULT_GOAL := test
CXX ?= g++
FLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread
build:
	mkdir -p build
build/diag-test: diag_test.cpp diag_log.h ../mod/bugmark.h ../mod/config.cpp ../mod/config.h ../core/core.cpp ../core/core.h ../core/build_id.h | build
	$(CXX) $(FLAGS) diag_test.cpp ../mod/config.cpp ../core/core.cpp -o $@
test: build/diag-test windows-test
	./build/diag-test
	python3 -B ../mod/mutants.py --sources "diag_test.cpp ../mod/config.cpp ../core/core.cpp" --prefix build/mutant- DIAG_CHAIN DIAG_REDACT DIAG_PRUNE_CRASH DIAG_PRUNE_ORDER DIAG_PRUNE_SCOPE DIAG_PRUNE_ACTIVE DIAG_CAP DIAG_FLIGHT DIAG_FLIGHT_CAP DIAG_EXCLUSIVE DIAG_PATH DIAG_CLEAN DIAG_REPEAT DIAG_CONFIG DIAG_CONSUME DIAG_HOST DIAG_KEY
windows-test: | build
	python3 -B test_windows.py
.PHONY: test windows-test
