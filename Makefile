.PHONY: test all
# Game-free: the presence core (simulator, fuzzer, mutants), the mod adapter and every proxy protection with its mutant.
test:
	cd mod && python3 -B -m unittest -q test_find_message_calls
	$(MAKE) -C core test        # also runs the mod's host tests
	$(MAKE) -C mod discovery-test
	$(MAKE) -C redirect test
	cd mod && python3 -B -m unittest -q test_analyse_presence
# Windows binaries (MinGW): the proxy, the mod, the probes and the mutant DLLs.
all:
	$(MAKE) -C mod all
	$(MAKE) -C redirect all
