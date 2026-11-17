.DEFAULT_GOAL := check
CXX ?= g++
FLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
SCRIPTS := install.sh coop-admin.sh deploy.sh admin.sh
build:
	mkdir -p build
# The group invite for a rented server, made with the launcher's invite code.
build/vps-invite: vps_invite.cpp ../../launcher/vendor/coop_invite.h | build
	$(CXX) $(FLAGS) vps_invite.cpp -o $@
../../world/build/coop-world-linux: FORCE
	$(MAKE) -C ../../world linux
../../mod/build/coop-swarm: FORCE
	$(MAKE) -C ../../mod build/coop-swarm
FORCE:
# The scripts parse (and are shellcheck-clean where shellcheck is installed), then the smoke on loopback (README.md).
check: build/vps-invite ../../world/build/coop-world-linux ../../mod/build/coop-swarm
	for s in $(SCRIPTS); do sh -n $$s || exit 1; done
	if command -v shellcheck >/dev/null 2>&1; then shellcheck -s sh $(SCRIPTS); else echo "shellcheck not installed: skipped"; fi
	python3 -B test_vps.py ../../world/build/coop-world-linux ../../mod/build/coop-swarm build/vps-invite
clean:
	rm -rf build
.PHONY: check clean FORCE
