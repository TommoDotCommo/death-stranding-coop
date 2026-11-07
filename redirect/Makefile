CXX = x86_64-w64-mingw32-g++
FLAGS = -std=c++17 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++
LIBS = -lshell32 -lole32 -luuid -lws2_32 -lwinhttp -lbcrypt -lwinmm
PROXY_SRC = path_map.cpp module_policy.cpp audit_throttle.cpp objects.cpp network_policy.cpp crash_record.cpp redirect.cpp proxy.cpp ../mod/config.cpp
PROXY_DEPS = $(PROXY_SRC) ../mod/config.h redirect.h startup_policy.h release_startup_win.h hook_list.h module_policy.h audit_throttle.h objects.h network_policy.h thread_trace.h virtual_pad.h frame_cap.h iat_patch.h crash_record.h ../mod/game_thread.h ../mod/mod.h
all: build/thread_trace_probe_module.dll build/thread_trace_probe.exe build/d3dcompiler_47.dll build/avrt.dll build/mutant-avrt-forward-broken.dll build/mutant-missing-config-closes.dll build/proxy_fixture.exe build/redirect_probe.exe build/redirect_probe_module.dll build/hooks-disabled.dll build/mutant-queries-unmapped.dll build/mutant-find-unmapped.dll build/mutant-attributes-unmapped.dll build/mutant-net-unhooked.dll build/net_probe.exe build/pad_probe.exe build/research-crash-fault.dll
build:
	mkdir -p build
build/d3dcompiler_47.dll: $(PROXY_DEPS) d3dcompiler_47.def | build
	$(CXX) $(FLAGS) -shared $(PROXY_SRC) d3dcompiler_47.def -o $@ $(LIBS)
AVRT_DEPS = $(PROXY_DEPS) avrt.def avrt_stubs.inc
# Proxy named after a system DLL the game imports but does not ship: installs without replacing a game file.
build/avrt.dll: $(AVRT_DEPS) | build
	$(CXX) $(FLAGS) -DCOOP_PROXY_AVRT -shared $(PROXY_SRC) avrt.def -o $@ $(LIBS)
build/mutant-avrt-forward-broken.dll: $(AVRT_DEPS) | build
	$(CXX) $(FLAGS) -DCOOP_PROXY_AVRT -DMUTANT_FORWARD_BROKEN -shared $(PROXY_SRC) avrt.def -o $@ $(LIBS)
build/mutant-missing-config-closes.dll: $(AVRT_DEPS) | build
	$(CXX) $(FLAGS) -DCOOP_PROXY_AVRT -DMUTANT_MISSING_CONFIG_CLOSES -shared $(PROXY_SRC) avrt.def -o $@ $(LIBS)
build/proxy_fixture.exe: proxy_fixture.cpp | build
	$(CXX) $(FLAGS) $< -o $@ -lavrt
# Research only (never the default proxy): the crash recorder's stamp-guarded forced fault (crash_record.cpp).
build/research-crash-fault.dll: $(PROXY_DEPS) d3dcompiler_47.def | build
	$(CXX) $(FLAGS) -DCRASH_RECORD_RESEARCH -shared $(PROXY_SRC) d3dcompiler_47.def -o $@ $(LIBS)
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
# Native Windows (WSL interop): concurrent import writes on one page; the unlocked mutant must fault.
iat-race-test: iat_patch.h iat_race_test.cpp | build
	$(CXX) $(FLAGS) iat_race_test.cpp -o build/iat-race-test.exe
	$(CXX) $(FLAGS) -DMUTANT_IAT_UNLOCKED iat_race_test.cpp -o build/mutant-iat-unlocked.exe
	@./build/iat-race-test.exe >/dev/null 2>&1; code=$$?; if test $$code -eq 126 -o $$code -eq 127; then echo "iat-race-test: no Windows interop, skipped"; exit 0; fi; \
	 ./build/iat-race-test.exe || exit 1; \
	 for i in 1 2 3; do ./build/mutant-iat-unlocked.exe >/dev/null 2>&1; code=$$?; \
	 if test $$code -ne 3 -a $$code -ne 1; then echo "SURVIVED IAT_UNLOCKED (exit $$code, run $$i)"; exit 1; fi; done
.PHONY: iat-race-test
# Native Windows (WSL interop): the avrt.dll proxy loads first, forwards, initialises the redirect; two negative controls
# (no proxy, and a mutant with broken forwarding) must fail the same fixture.
avrt-proxy-test: build/avrt.dll build/mutant-avrt-forward-broken.dll build/proxy_fixture.exe
	python3 -B -m unittest -q test_gen_proxy_stubs
	@if ! ./build/proxy_fixture.exe >/dev/null 2>&1 && test $$? -eq 126; then echo "avrt-proxy-test: no Windows interop, skipped"; exit 0; fi; \
	 w=$$(wslpath "$$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')") && \
	 python3 -B test_proxy_windows.py --work-dir "$$w" --fixture build/proxy_fixture.exe --proxy build/avrt.dll --mutant build/mutant-avrt-forward-broken.dll >build/avrt-proxy-test.log || { cat build/avrt-proxy-test.log; exit 1; }
.PHONY: avrt-proxy-test
# Crash and hang recorder (crash_record.h). Host: the pure rules and 3 mutants. Native Windows (WSL interop, no game):
# the fixture exe drives off / handled / handled-dump / unhandled / hang / research (the default build must ignore
# the research command; the research build must record a recovered fault); 4 mutant builds must fail the driver.
CRASH_PROBE_DEPS = crash_record.h crash_record.cpp crash_record_probe.cpp
build/crash_record_probe.exe: $(CRASH_PROBE_DEPS) | build
	$(CXX) $(FLAGS) -municode crash_record_probe.cpp crash_record.cpp -o $@
build/crash_record_probe_research.exe: $(CRASH_PROBE_DEPS) | build
	$(CXX) $(FLAGS) -DCRASH_RECORD_RESEARCH -municode crash_record_probe.cpp crash_record.cpp -o $@
build/mutant-crash-%.exe: $(CRASH_PROBE_DEPS) | build
	$(CXX) $(FLAGS) -DMUTANT_CRASH_$* -municode crash_record_probe.cpp crash_record.cpp -o $@
CRASH_WINDOWS_MUTANTS = UNINSTALLED BEHIND NO_CHAIN HANG_SELF
crash-record-test: crash_record_test.cpp test_crash_record_windows.py build/crash_record_probe.exe build/crash_record_probe_research.exe $(CRASH_WINDOWS_MUTANTS:%=build/mutant-crash-%.exe) | build
	g++ -std=c++17 -Wall -Wextra -Werror crash_record_test.cpp -o build/crash-record-test
	./build/crash-record-test
	@for mutant in CRASH_NO_DEDUPE CRASH_NO_CAP CRASH_FIND_EDGE; do \
	 g++ -std=c++17 -DMUTANT_$$mutant crash_record_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null 2>&1; then echo "SURVIVED $$mutant"; exit 1; fi; done
	@./build/crash_record_probe.exe --interop-check >/dev/null 2>&1; code=$$?; if test $$code -eq 126 -o $$code -eq 127; then echo "crash-record-test: no Windows interop, Windows part skipped"; exit 0; fi; \
	 test $$code -eq 0 || { echo "crash-record-test: probe does not start ($$code)"; exit 1; }; \
	 python3 -B test_crash_record_windows.py --exe build/crash_record_probe.exe --work build/crash-fixture || exit 1; \
	 python3 -B test_crash_record_windows.py --exe build/crash_record_probe_research.exe --research --case research --work build/crash-fixture-research || exit 1; \
	 for mutant in $(CRASH_WINDOWS_MUTANTS); do \
	 if python3 -B -u test_crash_record_windows.py --exe build/mutant-crash-$$mutant.exe --work build/crash-fixture-mutant >build/mutant-crash-$$mutant.log 2>&1; then echo "SURVIVED CRASH_$$mutant"; exit 1; fi; done
.PHONY: crash-record-test
test: thread-trace-test iat-race-test avrt-proxy-test crash-record-test portable-test
portable-test: thread-trace-test | build
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
	@for mutant in MODULE_ALL MODULE_ROOT MODULE_PATCH MODULE_DEFENDER MODULE_REQUEST MODULE_FLAGS MODULE_NO_PREFIX MODULE_ANY_PREFIX MODULE_RELEASE_ROOT MODULE_RELEASE_WIDE; do \
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
	g++ -std=c++17 -Wall -Wextra -Werror pad_test.cpp -o build/pad-test
	./build/pad-test
	@for mutant in PAD_ANY_NAME PAD_ANY_MODULE PAD_ALL_USERS PAD_STATIC_PACKET; do \
	 g++ -std=c++17 -DMUTANT_$$mutant pad_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null 2>&1; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror frame_cap_test.cpp -o build/frame-cap-test
	./build/frame-cap-test
	@for mutant in CAP_FOCUSED CAP_BURST; do \
	 g++ -std=c++17 -DMUTANT_$$mutant frame_cap_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null 2>&1; then echo "SURVIVED $$mutant"; exit 1; fi; done
	g++ -std=c++17 -Wall -Wextra -Werror audit_throttle.cpp audit_test.cpp -o build/audit-test
	./build/audit-test
	@for mutant in NO_DEDUP NO_RATE; do \
	 g++ -std=c++17 -DMUTANT_$$mutant audit_throttle.cpp audit_test.cpp -o build/mutant-$$mutant || exit 1; \
	 if ./build/mutant-$$mutant >/dev/null 2>&1; then echo "SURVIVED $$mutant"; exit 1; fi; done
.PHONY: all test portable-test

build/thread_trace_probe.exe: thread_trace_probe.cpp | build
	$(CXX) $(FLAGS) -municode $< -o $@ -luser32

build/thread_trace_probe_module.dll: thread_trace_probe_module.cpp | build
	$(CXX) $(FLAGS) -shared $< -o $@ -luser32

build/pad_probe.exe: pad_probe.cpp | build
	$(CXX) $(FLAGS) -municode $< -o $@
