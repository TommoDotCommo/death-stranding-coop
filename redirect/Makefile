CXX = x86_64-w64-mingw32-g++
FLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++
LIBS = -lshell32 -lole32 -luuid -lws2_32 -lwinhttp -lbcrypt -lwinmm
PROXY_SRC = path_map.cpp module_policy.cpp audit_throttle.cpp objects.cpp network_policy.cpp redirect.cpp proxy.cpp ../mod/config.cpp
PROXY_DEPS = $(PROXY_SRC) ../mod/config.h redirect.h hook_list.h module_policy.h audit_throttle.h objects.h network_policy.h thread_trace.h ../mod/game_thread.h ../mod/mod.h
all: build/thread_trace_probe_module.dll build/thread_trace_probe.exe build/d3dcompiler_47.dll build/redirect_probe.exe build/redirect_probe_module.dll build/hooks-disabled.dll build/mutant-queries-unmapped.dll build/mutant-find-unmapped.dll build/mutant-attributes-unmapped.dll build/mutant-net-unhooked.dll build/net_probe.exe
build:
	mkdir -p build
build/d3dcompiler_47.dll: $(PROXY_DEPS) d3dcompiler_47.def | build
	$(CXX) $(FLAGS) -shared $(PROXY_SRC) d3dcompiler_47.def -o $@ $(LIBS)
build/hooks-disabled.dll: $(PROXY_DEPS) | build
	$(CXX) $(FLAGS) -DMUTANT_HOOKS_DISABLED -shared $(PROXY_SRC) -o $@ $(LIBS)
build/mutant-queries-unmapped.dll: $(PROXY_DEPS) | build
	$(CXX) $(FLAGS) -DMUTANT_QUERY_READS -shared $(PROXY_SRC) -o $@ $(LIBS)
build/mutant-find-unmapped.dll: $(PROXY_DEPS) | build
	$(CXX) $(FLAGS) -DMUTANT_NO_FIND -shared $(PROXY_SRC) -o $@ $(LIBS)
build/mutant-attributes-unmapped.dll: $(PROXY_DEPS) | build
	$(CXX) $(FLAGS) -DMUTANT_NO_ATTRIBUTES -shared $(PROXY_SRC) -o $@ $(LIBS)
build/mutant-net-unhooked.dll: $(PROXY_DEPS) | build
	$(CXX) $(FLAGS) -DMUTANT_NET_UNHOOKED -shared $(PROXY_SRC) -o $@ $(LIBS)
build/net_probe.exe: net_probe.cpp | build
	$(CXX) $(FLAGS) -municode $< -o $@ $(LIBS)
build/redirect_probe.exe: redirect_probe.cpp | build
	$(CXX) $(FLAGS) -municode $< -o $@ $(LIBS)
build/redirect_probe_module.dll: redirect_probe.cpp | build
	$(CXX) $(FLAGS) -DPROBE_LIBRARY -shared $< -o $@ $(LIBS)
build/thread-trace-test: thread_trace_test.cpp thread_trace.h | build
	g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread $< -o $@
thread-trace-test: build/thread-trace-test
	./build/thread-trace-test
	@for mutant in TRACE_DEPTH TRACE_RATE TRACE_CAP TRACE_PUBLISH; do \
	 g++ -std=c++17 -O2 -pthread -DMUTANT_$$mutant thread_trace_test.cpp -o build/mutant-$$mutant || exit 1; \
	 ./build/mutant-$$mutant >build/mutant-$$mutant.log 2>&1; code=$$?; \
	 if test $$code -ne 1 || ! rg -q 'trace oracle' build/mutant-$$mutant.log; then echo "SURVIVED or crashed $$mutant ($$code)"; exit 1; fi; done
	python3 -B -m unittest -q test_read_thread_trace
.PHONY: thread-trace-test
test: thread-trace-test | build
	g++ -std=c++17 -Wall -Wextra -Werror network_policy.cpp network_test.cpp -o build/network-test
	./build/network-test
	@for mutant in NET_ALLOW_ALL NET_EXACT_LOOPBACK NET_NO_PEERS NET_ANY_V6 NET_MAPPED_LOCAL NET_NAME_PREFIX NET_NEVER_REFUSE NET_REFUSE_LOG_ONLY; do \
	 g++ -std=c++17 -DMUTANT_$$mutant network_policy.cpp network_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror objects.cpp objects_test.cpp -o build/objects-test
	./build/objects-test
	@for mutant in OBJECT_NONE OBJECT_ALL OBJECT_CASE OBJECT_PREFIX; do \
	 g++ -std=c++17 -DMUTANT_$$mutant objects.cpp objects_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror path_map.cpp module_policy.cpp module_test.cpp -o build/module-test
	./build/module-test module_fixture.txt
	@for mutant in MODULE_ALL MODULE_ROOT MODULE_PATCH MODULE_DEFENDER MODULE_REQUEST MODULE_FLAGS; do \
	 g++ -std=c++17 -DMUTANT_$$mutant path_map.cpp module_policy.cpp module_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant module_fixture.txt; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror path_map.cpp path_test.cpp -o build/path-test
	./build/path-test
	@for mutant in DENY ROOT CASE TRAVERSAL PREFIX DEVICE NVROOT REPARSE QUERY_READS QUERY_ROOT0 FIND SUFFIX HEX; do \
	 g++ -std=c++17 -DMUTANT_$$mutant path_map.cpp path_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror hook_list_test.cpp -o build/hook-list-test
	./build/hook-list-test
	@for mutant in NO_FIND NO_ATTRIBUTES; do \
	 g++ -std=c++17 -DMUTANT_$$mutant hook_list_test.cpp -o build/mutant-hooks-$$mutant || exit 1; \
	 if ./build/mutant-hooks-$$mutant >/dev/null 2>&1; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror audit_throttle.cpp audit_test.cpp -o build/audit-test
	./build/audit-test
	@for mutant in NO_DEDUP NO_RATE; do \
	 g++ -std=c++17 -DMUTANT_$$mutant audit_throttle.cpp audit_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null 2>&1; then echo "SURVIVED $$mutant"; exit 1; fi; done
.PHONY: all test

build/thread_trace_probe.exe: thread_trace_probe.cpp | build
	$(CXX) $(FLAGS) -municode $< -o $@ -luser32

build/thread_trace_probe_module.dll: thread_trace_probe_module.cpp | build
	$(CXX) $(FLAGS) -shared $< -o $@ -luser32
