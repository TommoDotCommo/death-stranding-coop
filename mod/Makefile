.DEFAULT_GOAL := all
CXX ?= g++
# Debian names the POSIX-thread cross-compiler with a -posix suffix; MSYS2 has the plain name only.
MINGW ?= $(shell command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 && echo x86_64-w64-mingw32-g++-posix || echo x86_64-w64-mingw32-g++)
FLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
WINFLAGS = $(FLAGS) -D_WIN32_WINNT=0x0602 -static-libgcc -static-libstdc++ -static
SRC = received.cpp own_strand.cpp strand_notices.cpp others.cpp crowd.cpp fights.cpp ../core/broadcast_client.cpp mule_goal.cpp enemy_poses.cpp rewards.cpp signals.cpp ui.cpp orderload.cpp carts.cpp wheels.cpp controls.cpp voidout.cpp passenger.cpp carry_place.cpp roads.cpp bts.cpp enemies.cpp orders.cpp story.cpp ../launcher/saves.cpp vitals.cpp online.cpp notice.cpp nametag.cpp likes.cpp cargo_stack.cpp weather.cpp ride.cpp environment.cpp join.cpp game_thread.cpp puppet.cpp double.cpp body.cpp gear.cpp outfit.cpp pad.cpp cargo.cpp carry_visual.cpp marker.cpp structures.cpp vehicle.cpp discovery.cpp mod.cpp config.cpp providers.cpp pose.cpp ../core/core.cpp
HEADERS = received.h own_strand.h strand_notices.h ../world/json.h others.h crowd.h fights.h ../core/broadcast.h ../core/broadcast_client.h ../core/relay.h mule_goal.h enemy_poses.h rewards.h signals.h ui.h orderload.h carts.h wheels.h controls.h link.h guard_win.h voidout.h passenger.h carry_place.h roads.h bts.h enemies.h orders.h story.h ../launcher/saves.h vitals.h online.h notice.h nametag.h likes.h cargo_stack.h weather.h ride.h environment.h join.h game_thread.h puppet.h double.h body.h gear.h outfit.h auto_continue.h pad.h cargo.h carry_visual.h marker.h structures.h vehicle.h pose.h fake_memory.h discovery_options.h discovery.h discovery_signatures.h timing.h mod.h config.h providers.h signature.h identity.h ../core/core.h ../core/build_id.h
build:
	mkdir -p $@
identity:
	python3 -B generate_discovery_signatures.py
	python3 build_id.py
	python3 ../core/build_id.py
all: identity build/puppet-test.exe build/mod.dll build/mod_probe.exe build/mod-no-build.dll build/mod-no-interpolation.dll build/coop-partner.exe build/coop-swarm.exe
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
# The fake world with one protection of the others' seam taken out (test_others_partner_mutants.py).
build/fake-others-to-partner.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_OTHERS_TO_PARTNER -fPIC -shared $(SRC) -o $@
build/fake-others-no-relay.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_OTHERS_NO_RELAY -fPIC -shared $(SRC) -o $@
build/fake-group-sends.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_GROUP_SENDS -fPIC -shared $(SRC) -o $@
build/fake-others-undressed.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_OTHERS_UNDRESSED -fPIC -shared $(SRC) -o $@
# relay_mode "broadcast" ignored (test_others_broadcast.py must fail on it).
build/fake-relay-mode.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_RELAY_MODE -fPIC -shared $(SRC) -o $@
# Strand notices with one protection taken out (test_strand_notices.py): told again for every answer, lines only beside
# the partner's notices, and reads by a game whose config does not ask for them.
build/fake-strand-once.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMUTANT_STRAND_ONCE -fPIC -shared $(SRC) -o $@
build/fake-strand-partner.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_STRAND_PARTNER -fPIC -shared $(SRC) -o $@
build/fake-strand-off.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_STRAND_OFF -fPIC -shared $(SRC) -o $@
# Fights with visitors with one protection taken out (test_fights.py): a visitor that announces the fight it shows, the
# shown fight ended with a cleared sphere when the owner goes, and join_fights / host_fights ignored.
build/fake-fight-visitor-hosts.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMUTANT_FIGHT_VISITOR_HOSTS -fPIC -shared $(SRC) -o $@
build/fake-fight-gone-clears.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMUTANT_FIGHT_GONE_CLEARS -fPIC -shared $(SRC) -o $@
build/fake-fight-settings.so: $(SRC) $(HEADERS) | build
	$(CXX) $(FLAGS) -DMOD_FAKE_NETWORK -DMOD_MUTANT_FIGHT_SETTINGS -fPIC -shared $(SRC) -o $@
FIGHT_FAKES = build/fake-fight-visitor-hosts.so build/fake-fight-gone-clears.so build/fake-fight-settings.so
# The real server (Linux build) for the broadcast fake-world test.
../world/build/coop-world: FORCE
	$(MAKE) -C ../world build/coop-world
FORCE:
build/unit: unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp $(HEADERS) | build
	$(CXX) $(FLAGS) unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o $@
host: identity build/mod.so build/mod-no-build.so build/mod-no-interpolation.so build/mod_probe build/unit
test: environment-test join-test vehicle-test game-thread-test discovery-test pose-test timing-test host build/fake.so build/fake-no-build.so build/fake-no-interpolation.so build/fake-others-to-partner.so build/fake-others-no-relay.so build/fake-group-sends.so build/fake-others-undressed.so build/fake-relay-mode.so build/fake-strand-once.so build/fake-strand-partner.so build/fake-strand-off.so $(FIGHT_FAKES) build/coop-swarm ../world/build/coop-world
	./build/unit
	@$(CXX) $(FLAGS) -DMUTANT_CONFIG_PARTNER unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-config-partner && if ./build/unit-config-partner >/dev/null 2>&1; then echo SURVIVED CONFIG_PARTNER; exit 1; else echo KILLED MUTANT_CONFIG_PARTNER; fi
	@$(CXX) $(FLAGS) -DMUTANT_CONFIG_MARKER unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-config-marker && if ./build/unit-config-marker >/dev/null 2>&1; then echo SURVIVED CONFIG_MARKER; exit 1; else echo KILLED MUTANT_CONFIG_MARKER; fi
	@$(CXX) $(FLAGS) -DMUTANT_CONFIG_ENEMY_POSITIONS unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-config-enemy-positions && if ./build/unit-config-enemy-positions >/dev/null 2>&1; then echo SURVIVED CONFIG_ENEMY_POSITIONS; exit 1; else echo KILLED MUTANT_CONFIG_ENEMY_POSITIONS; fi
	@$(CXX) $(FLAGS) -DMUTANT_PEER_ANY unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-peer-any && if ./build/unit-peer-any >/dev/null 2>&1; then echo SURVIVED PEER_ANY; exit 1; else echo KILLED MUTANT_PEER_ANY; fi
	@$(CXX) $(FLAGS) -DMUTANT_CONFIG_OTHERS unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-config-others && if ./build/unit-config-others >/dev/null 2>&1; then echo SURVIVED CONFIG_OTHERS; exit 1; else echo KILLED MUTANT_CONFIG_OTHERS; fi
	@$(CXX) $(FLAGS) -DMUTANT_CONFIG_RELAY_ADDRESS unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-config-relay-address && if ./build/unit-config-relay-address >/dev/null 2>&1; then echo SURVIVED CONFIG_RELAY_ADDRESS; exit 1; else echo KILLED MUTANT_CONFIG_RELAY_ADDRESS; fi
	@$(CXX) $(FLAGS) -DMUTANT_CONFIG_RELAY_NEEDS unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o build/unit-config-relay-needs && if ./build/unit-config-relay-needs >/dev/null 2>&1; then echo SURVIVED CONFIG_RELAY_NEEDS; exit 1; else echo KILLED MUTANT_CONFIG_RELAY_NEEDS; fi
	python3 -B mutants.py --sources "unit.cpp config.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp" --prefix build/mutant-unit- CONFIG_GROUP CONFIG_SERVER_ANY CONFIG_OTHERS_SHOW CONFIG_RELAY_MODE CONFIG_SHOW_OTHERS CONFIG_STRAND CONFIG_FIGHTS CONFIG_RECEIVED && echo KILLED MUTANT_CONFIG_GROUP MUTANT_CONFIG_SERVER_ANY MUTANT_CONFIG_OTHERS_SHOW MUTANT_CONFIG_RELAY_MODE MUTANT_CONFIG_SHOW_OTHERS MUTANT_CONFIG_STRAND MUTANT_CONFIG_FIGHTS MUTANT_CONFIG_RECEIVED
	python3 -B test_probe.py
	python3 -B -m unittest -q test_game_thread_api test_probe_puppet test_actor_resources test_actor_reader_windows
	python3 -B test_actor_resource_mutants.py
	python3 -B test_others_partner.py
	python3 -B test_others_partner_mutants.py
	python3 -B test_others_broadcast.py ../world/build/coop-world build/coop-swarm
	python3 -B test_strand_notices.py ../world/build/coop-world
	python3 -B test_fights.py ../world/build/coop-world
.PHONY: all identity host test FORCE

build/vehicle-test: vehicle_test.cpp vehicle.cpp vehicle.h discovery.cpp fake_memory.h discovery_signatures.h | build
	$(CXX) $(FLAGS) vehicle_test.cpp vehicle.cpp discovery.cpp -o $@
vehicle-test: build/vehicle-test
	./build/vehicle-test
	python3 -B test_vehicle_mutants.py
.PHONY: vehicle-test


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
build/double-test: double_test.cpp double.cpp puppet.cpp pose.cpp discovery.cpp double.h controls.h puppet.h fake_memory.h | build
	$(CXX) $(FLAGS) double_test.cpp double.cpp puppet.cpp pose.cpp discovery.cpp -o $@
build/structures-fuzz: structures_fuzz.cpp structures.cpp structures.h discovery.cpp | build
	$(CXX) $(FLAGS) structures_fuzz.cpp structures.cpp discovery.cpp -o $@
build/world-fuzz: world_fuzz.cpp cargo.cpp cargo.h discovery.cpp | build
	$(CXX) $(FLAGS) world_fuzz.cpp cargo.cpp discovery.cpp -o $@
build/sync-fuzz: sync_fuzz.cpp cargo.cpp cargo.h structures.cpp structures.h discovery.cpp | build
	$(CXX) $(FLAGS) sync_fuzz.cpp cargo.cpp structures.cpp discovery.cpp -o $@
build/structures-test: structures_test.cpp structures.cpp structures.h discovery.cpp | build
	$(CXX) $(FLAGS) structures_test.cpp structures.cpp discovery.cpp -o $@
build/cargo-test: cargo_test.cpp cargo.cpp cargo.h discovery.cpp | build
	$(CXX) $(FLAGS) cargo_test.cpp cargo.cpp discovery.cpp -o $@
build/marker-test: marker_test.cpp marker.cpp marker.h discovery.cpp | build
	$(CXX) $(FLAGS) marker_test.cpp marker.cpp discovery.cpp -o $@
# The empty carrier's critical sections on Windows (WSL interop; skipped without it); the uninitialised mutant must fail.
gear-windows-test: gear_test.cpp gear.cpp gear.h puppet.cpp pose.cpp discovery.cpp | build
	$(MINGW) $(WINFLAGS) gear_test.cpp gear.cpp puppet.cpp pose.cpp discovery.cpp -o build/gear-test.exe -lbcrypt -lwinmm
	$(MINGW) $(WINFLAGS) -DMUTANT_GEAR_LOCKS gear_test.cpp gear.cpp puppet.cpp pose.cpp discovery.cpp -o build/mutant-gear-locks.exe -lbcrypt -lwinmm
	@./build/gear-test.exe >/dev/null 2>&1; code=$$?; if test $$code -eq 126 -o $$code -eq 127; then echo "gear-windows-test: no Windows interop, skipped"; exit 0; fi; \
	 ./build/gear-test.exe || exit 1; \
	 ./build/mutant-gear-locks.exe >/dev/null 2>build/gear-locks.err; code=$$?; if test $$code -ne 1 || ! grep -q oracle build/gear-locks.err; then echo "SURVIVED GEAR_LOCKS (exit $$code)"; exit 1; fi; echo KILLED MUTANT_GEAR_LOCKS
.PHONY: gear-windows-test
build/nametag-test: nametag_test.cpp nametag.cpp nametag.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) nametag_test.cpp nametag.cpp discovery.cpp ../core/core.cpp -o $@
build/notice-test: notice_test.cpp notice.cpp notice.h nametag.cpp nametag.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) notice_test.cpp notice.cpp nametag.cpp discovery.cpp ../core/core.cpp -o $@
build/strand-notices-test: strand_notices_test.cpp strand_notices.cpp strand_notices.h ../world/json.h nametag.cpp nametag.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) strand_notices_test.cpp strand_notices.cpp nametag.cpp discovery.cpp ../core/core.cpp -o $@
build/link-test: link_test.cpp link.h | build
	$(CXX) $(FLAGS) link_test.cpp -o $@
build/wheels-test: wheels_test.cpp wheels.cpp wheels.h | build
	$(CXX) $(FLAGS) wheels_test.cpp wheels.cpp -o $@
build/controls-test: controls_test.cpp controls.cpp controls.h discovery.cpp | build
	$(CXX) $(FLAGS) controls_test.cpp controls.cpp discovery.cpp -o $@
build/voidout-test: voidout_test.cpp voidout.cpp voidout.h discovery.cpp | build
	$(CXX) $(FLAGS) voidout_test.cpp voidout.cpp discovery.cpp -o $@

robustness-test: build/double-test build/voidout-test
	./build/double-test
	./build/voidout-test
	python3 -B test_game_thread_mutants.py double voidout
.PHONY: robustness-test

build/passenger-test: passenger_test.cpp passenger.cpp passenger.h discovery.cpp | build
	$(CXX) $(FLAGS) passenger_test.cpp passenger.cpp discovery.cpp -o $@

build/place-test: carry_place_test.cpp carry_place.cpp carry_place.h structures.cpp discovery.cpp | build
	$(CXX) $(FLAGS) carry_place_test.cpp carry_place.cpp structures.cpp discovery.cpp -o $@

build/roads-test: roads_test.cpp roads.cpp roads.h discovery.cpp | build
	$(CXX) $(FLAGS) roads_test.cpp roads.cpp discovery.cpp -o $@
# The player's own strand (own_strand.h): a reader not wired into the mod yet.
build/own-strand-test: own_strand_test.cpp own_strand.cpp own_strand.h structures.cpp structures.h discovery.cpp | build
	$(CXX) $(FLAGS) own_strand_test.cpp own_strand.cpp structures.cpp discovery.cpp -o $@
# Received structures built (received.h, config build_received): the decision and the scan over fake memory.
build/received-test: received_test.cpp received.cpp received.h own_strand.cpp own_strand.h structures.cpp structures.h discovery.cpp | build
	$(CXX) $(FLAGS) received_test.cpp received.cpp own_strand.cpp structures.cpp discovery.cpp -o $@

# Offering the existing strand to a server (offer_strand.h): which records and how, and the game's body; not wired in.
build/offer-strand-test: offer_strand_test.cpp offer_strand.cpp offer_strand.h discovery.cpp | build
	$(CXX) $(FLAGS) offer_strand_test.cpp offer_strand.cpp discovery.cpp -o $@

build/carts-test: carts_test.cpp carts.cpp carts.h carry_place.cpp carry_place.h discovery.cpp | build
	$(CXX) $(FLAGS) carts_test.cpp carts.cpp carry_place.cpp discovery.cpp -o $@
build/bts-test: bts_test.cpp bts.cpp bts.h discovery.cpp | build
	$(CXX) $(FLAGS) bts_test.cpp bts.cpp discovery.cpp -o $@

build/enemies-test: enemies_test.cpp enemies.cpp enemies.h discovery.cpp | build
	$(CXX) $(FLAGS) enemies_test.cpp enemies.cpp discovery.cpp -o $@
ENEMYPOS_SRC = enemy_poses_test.cpp enemy_poses.cpp enemies.cpp discovery.cpp ../core/core.cpp
MULEGOAL_SRC = mule_goal_test.cpp mule_goal.cpp discovery.cpp ../core/core.cpp
build/mule-goal-test: $(MULEGOAL_SRC) mule_goal.h discovery.h | build
	$(CXX) $(FLAGS) $(MULEGOAL_SRC) -o $@
build/enemy-poses-test: $(ENEMYPOS_SRC) enemy_poses.h enemies.h ../core/core.h | build
	$(CXX) $(FLAGS) $(ENEMYPOS_SRC) -o $@
FIGHTS_SRC = fights_test.cpp fights.cpp bts.cpp discovery.cpp ../core/core.cpp ../core/broadcast_client.cpp ../world/relay.cpp
build/fights-test: $(FIGHTS_SRC) fights.h bts.h ../core/broadcast.h ../core/broadcast_client.h ../world/relay.h | build
	$(CXX) $(FLAGS) $(FIGHTS_SRC) -o $@
CROWD_SRC = crowd_test.cpp crowd.cpp others.cpp double.cpp puppet.cpp pose.cpp providers.cpp controls.cpp nametag.cpp gear.cpp carry_visual.cpp carry_place.cpp cargo_stack.cpp discovery.cpp ../core/core.cpp ../core/broadcast_client.cpp ../world/relay.cpp
build/crowd-test: $(CROWD_SRC) crowd.h others.h double.h puppet.h nametag.h gear.h carry_visual.h carry_place.h cargo_stack.h controls.h providers.h ../core/core.h ../core/broadcast.h ../core/broadcast_client.h ../world/relay.h | build
	$(CXX) $(FLAGS) $(CROWD_SRC) -o $@
OTHERS_SRC = others_test.cpp others.cpp double.cpp puppet.cpp pose.cpp providers.cpp controls.cpp nametag.cpp gear.cpp carry_visual.cpp carry_place.cpp cargo_stack.cpp discovery.cpp ../core/core.cpp
build/others-test: $(OTHERS_SRC) others.h double.h puppet.h nametag.h gear.h carry_visual.h carry_place.h cargo_stack.h controls.h providers.h ../core/core.h | build
	$(CXX) $(FLAGS) $(OTHERS_SRC) -o $@
build/orders-test: orders_test.cpp orders.cpp orders.h discovery.cpp | build
	$(CXX) $(FLAGS) orders_test.cpp orders.cpp discovery.cpp -o $@
build/guard-test: guard_test.cpp guard_win.h | build
	$(CXX) $(FLAGS) guard_test.cpp -o $@
build/vitals-test: vitals_test.cpp vitals.cpp vitals.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) vitals_test.cpp vitals.cpp discovery.cpp ../core/core.cpp -o $@
build/online-test: online_test.cpp online.cpp online.h config.cpp discovery.cpp | build
	$(CXX) $(FLAGS) online_test.cpp online.cpp config.cpp discovery.cpp -o $@
build/signals-test: signals_test.cpp signals.cpp signals.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) signals_test.cpp signals.cpp discovery.cpp ../core/core.cpp -o $@
build/likes-test: likes_test.cpp likes.cpp likes.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) likes_test.cpp likes.cpp discovery.cpp ../core/core.cpp -o $@
build/stack-test: cargo_stack_test.cpp cargo_stack.cpp cargo_stack.h puppet.cpp pose.cpp discovery.cpp | build
	$(CXX) $(FLAGS) cargo_stack_test.cpp cargo_stack.cpp puppet.cpp pose.cpp discovery.cpp -o $@
build/weather-test: weather_test.cpp weather.cpp weather.h discovery.cpp | build
	$(CXX) $(FLAGS) weather_test.cpp weather.cpp discovery.cpp -o $@
build/ride-test: ride_test.cpp ride.cpp ride.h providers.cpp pose.cpp discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) ride_test.cpp ride.cpp providers.cpp pose.cpp discovery.cpp ../core/core.cpp -o $@
build/outfit-test: outfit_test.cpp outfit.cpp outfit.h discovery.cpp | build
	$(CXX) $(FLAGS) outfit_test.cpp outfit.cpp discovery.cpp -o $@
build/gear-test: gear_test.cpp gear.cpp gear.h puppet.cpp puppet.h pose.cpp discovery.cpp | build
	$(CXX) $(FLAGS) gear_test.cpp gear.cpp puppet.cpp pose.cpp discovery.cpp -o $@
build/carry-visual-test: carry_visual_test.cpp carry_visual.cpp carry_visual.h discovery.cpp ../core/core.cpp ../core/core.h | build
	$(CXX) $(FLAGS) carry_visual_test.cpp carry_visual.cpp discovery.cpp ../core/core.cpp -o $@
build/ui-test: ui_test.cpp ui.cpp ui.h discovery.cpp | build
	$(CXX) $(FLAGS) ui_test.cpp ui.cpp discovery.cpp -o $@
build/pad-test: pad_test.cpp pad.cpp pad.h ui.h | build
	$(CXX) $(FLAGS) pad_test.cpp pad.cpp -o $@
build/body-test: body_test.cpp body.cpp body.h discovery.cpp | build
	$(CXX) $(FLAGS) body_test.cpp body.cpp discovery.cpp -o $@
build/auto-continue-test: auto_continue_test.cpp auto_continue.h | build
	$(CXX) $(FLAGS) auto_continue_test.cpp -o $@
build/partner-test: partner_test.cpp partner_logic.cpp partner.h identity.h ../core/core.h ../core/core.cpp | build
	$(CXX) $(FLAGS) partner_test.cpp partner_logic.cpp ../core/core.cpp -o $@
build/coop-partner: environment.cpp environment.h partner.cpp partner_logic.cpp partner.h identity.h structures.cpp structures.h carry_visual.cpp carry_visual.h signals.cpp signals.h discovery.cpp ../core/core.cpp ../core/core.h | build
	$(CXX) $(FLAGS) partner.cpp partner_logic.cpp environment.cpp structures.cpp carry_visual.cpp signals.cpp discovery.cpp ../core/core.cpp -o $@
build/coop-partner.exe: environment.cpp environment.h partner.cpp partner_logic.cpp partner.h identity.h structures.cpp structures.h carry_visual.cpp carry_visual.h signals.cpp signals.h discovery.cpp ../core/core.cpp ../core/core.h | build
	$(MINGW) $(WINFLAGS) partner.cpp partner_logic.cpp environment.cpp structures.cpp carry_visual.cpp signals.cpp discovery.cpp ../core/core.cpp -o $@ -lws2_32 -lbcrypt
# coop-swarm: many stand-in players for one relay (swarm.cpp; world/README.md, "The relay").
# Broadcast mode (--mode broadcast): the broadcast client, and the name posted as the mod posts it (crowd.h, nametag.cpp).
SWARM_SRC = swarm.cpp partner_logic.cpp nametag.cpp discovery.cpp ../core/core.cpp ../core/broadcast_client.cpp
SWARM = $(SWARM_SRC) partner.h identity.h crowd.h nametag.h ../core/core.h ../core/relay.h ../core/broadcast.h ../core/broadcast_client.h
build/coop-swarm: $(SWARM) | build
	$(CXX) $(FLAGS) $(SWARM_SRC) -o $@
build/coop-swarm.exe: $(SWARM) | build
	$(MINGW) $(WINFLAGS) $(SWARM_SRC) -o $@ -lws2_32 -lbcrypt
build/orderload-test: orderload_test.cpp orderload.cpp orderload.h cargo.cpp cargo.h discovery.cpp | build
	$(CXX) $(FLAGS) orderload_test.cpp orderload.cpp cargo.cpp discovery.cpp -o $@
build/story-test: story_test.cpp story.cpp story.h ../launcher/saves.cpp ../launcher/saves.h ../launcher/saves_vectors.h discovery.cpp ../core/core.cpp ../core/core.h | build
	$(CXX) $(FLAGS) story_test.cpp story.cpp ../launcher/saves.cpp discovery.cpp ../core/core.cpp -o $@
build/wire-test: wire_test.cpp rewards.cpp rewards.h signals.cpp signals.h orderload.cpp orderload.h cargo.cpp cargo.h discovery.cpp ../core/core.cpp ../core/core.h | build
	$(CXX) $(FLAGS) wire_test.cpp rewards.cpp signals.cpp orderload.cpp cargo.cpp discovery.cpp ../core/core.cpp -o $@
build/rewards-test: rewards_test.cpp rewards.cpp rewards.h discovery.cpp ../core/core.cpp ../core/core.h | build
	$(CXX) $(FLAGS) rewards_test.cpp rewards.cpp discovery.cpp ../core/core.cpp -o $@
game-thread-test: build/wire-test build/rewards-test build/ui-test build/orderload-test build/guard-test build/carts-test build/wheels-test build/controls-test build/voidout-test build/mule-goal-test build/passenger-test build/place-test build/roads-test build/own-strand-test build/offer-strand-test build/received-test build/bts-test build/enemies-test build/orders-test build/game-thread-test build/puppet-test build/double-test build/pad-test build/cargo-test build/carry-visual-test build/structures-test build/sync-fuzz build/world-fuzz build/structures-fuzz build/partner-test build/coop-partner build/body-test build/auto-continue-test build/marker-test build/gear-test build/outfit-test build/ride-test build/weather-test build/stack-test build/online-test build/link-test build/vitals-test build/likes-test build/nametag-test build/notice-test build/strand-notices-test build/signals-test build/story-test build/enemy-poses-test build/others-test build/crowd-test build/fights-test
	./build/story-test
	./build/wire-test
	./build/rewards-test
	./build/orderload-test
	python3 -B mutants.py --sources "orderload_test.cpp orderload.cpp cargo.cpp discovery.cpp" --prefix build/mutant-load- LOAD_CANCEL LOAD_DECODE_RANGE LOAD_DELIVERED LOAD_DUPLICATE LOAD_ENDED LOAD_HANDED_IN LOAD_LINK LOAD_STALE LOAD_STRIP_CARRIED LOAD_STRIP_SHARED LOAD_STRIP_TAKER && echo KILLED 11 order load mutants
	./build/game-thread-test
	./build/puppet-test
	./build/double-test
	./build/pad-test
	./build/ui-test
	./build/cargo-test
	./build/carry-visual-test
	./build/structures-test
	./build/sync-fuzz
	./build/world-fuzz
	./build/structures-fuzz
	./build/partner-test
	python3 -B test_partner_carry.py
	./build/body-test
	./build/auto-continue-test
	./build/marker-test
	./build/gear-test
	./build/outfit-test
	./build/ride-test
	./build/weather-test
	./build/stack-test
	./build/likes-test
	./build/signals-test
	./build/online-test
	./build/vitals-test
	./build/wheels-test
	./build/controls-test
	./build/voidout-test
	./build/passenger-test
	./build/place-test
	./build/roads-test
	./build/own-strand-test
	./build/received-test
	./build/offer-strand-test
	./build/bts-test
	./build/carts-test
	for m in CARTS_VTABLE CARTS_ANY CARTS_ECHO CARTS_MANY; do $(CXX) $(FLAGS) -DMUTANT_$$m carts_test.cpp carts.cpp carry_place.cpp discovery.cpp -o build/carts-mutant && if ./build/carts-mutant >/dev/null 2>&1; then echo SURVIVED $$m; exit 1; else echo KILLED MUTANT_$$m; fi; done
	./build/enemies-test
	./build/enemy-poses-test
	python3 -B mutants.py --sources "$(ENEMYPOS_SRC)" --prefix build/mutant-enemypos- ENEMYPOS_AWAKE ENEMYPOS_RANGE ENEMYPOS_KEEP ENEMYPOS_NEAREST ENEMYPOS_KEYS ENEMYPOS_STALE ENEMYPOS_DOWN ENEMYPOS_FAST ENEMYPOS_REACH ENEMYPOS_LAND ENEMYPOS_FARCAP ENEMYPOS_DEADBAND ENEMYPOS_COOLDOWN ENEMYPOS_HOLD ENEMYPOS_BUDGET ENEMYPOS_VTABLE ENEMYPOS_LIVE ENEMYPOS_STEERHOLD ENEMYPOS_STEERROOM ENEMYPOS_STEERSNAP ENEMYPOS_STEERPULL ENEMYPOS_STEERBUDGET ENEMYPOS_STUCK ENEMYPOS_RIDES_HOST ENEMYPOS_RIDES_GUEST ENEMIES_RIDES && echo KILLED 25 enemy position mutants
	./build/mule-goal-test
	python3 -B mutants.py --sources "$(MULEGOAL_SRC)" --prefix build/mutant-mulegoal- MULEGOAL_COMPONENT MULEGOAL_CAPACITY MULEGOAL_KEY MULEGOAL_INDEX MULEGOAL_OFFSET MULEGOAL_NEAR MULEGOAL_DSCOMPONENT MULEGOAL_AGREE MULEGOAL_AI_VTABLE MULEGOAL_WALKING MULEGOAL_D_VTABLE MULEGOAL_WRITABLE MULEGOAL_SAME MULEGOAL_REACH MULEGOAL_RUNNING MULEGOAL_PENDING && echo KILLED 16 mule goal mutants
	./build/others-test
	python3 -B mutants.py --sources "$(OTHERS_SRC)" --prefix build/mutant-others- OTHERS_CAP OTHERS_UNKNOWN OTHERS_BUILD OTHERS_SELF OTHERS_LEFT OTHERS_ROLE OTHERS_PAIR BODIES_ONE_NEW OTHERS_STICKY OTHERS_KINDS OTHERS_STALE BODIES_HIDE BODIES_REMOVE BODIES_ONCE BODIES_RATE BODIES_MIX OTHERS_LABEL_BODY OTHERS_LABEL_NAME OTHERS_EVENTS_OFF OTHERS_EVENTS_MIX OTHERS_WEAR_BODY OTHERS_WEAR_MIX OTHERS_CARRY_LOST OTHERS_CARRY_LIMIT OTHERS_CARRY_NEAREST OTHERS_CARRY_FLAP OTHERS_CARRY_HIDDEN OTHERS_CARRY_PACK OTHERS_CARRY_MIX OTHERS_UNDRESS OTHERS_PLACE_FRESH && echo KILLED 31 others mutants
	./build/crowd-test
	./build/fights-test
	python3 -B mutants.py --sources "$(FIGHTS_SRC)" --prefix build/mutant-fights- FIGHT_VISITOR_HOSTS FIGHT_OWN FIGHT_HEAL FIGHT_KILL FIGHT_ECHO FIGHT_DOUBLE FIGHT_KILL_CLAIM FIGHT_RELEASE FIGHT_GONE_CLEARS && echo KILLED 9 fights mutants
	python3 -B mutants.py --sources "$(CROWD_SRC)" --prefix build/mutant-crowd- CROWD_BLOB_LENGTH CROWD_BLOB_TYPE CROWD_BLOB_RANGE CROWD_WHOLE CROWD_EVENTS_OFF CROWD_SEND_OFF CROWD_MIX CROWD_CAP CROWD_LEFT CROWD_CARRY_LOST CROWD_CARRY_BACK CROWD_ROSE CROWD_GATED && echo KILLED 13 crowd mutants
	./build/guard-test
	./build/orders-test
	./build/link-test
	./build/notice-test
	./build/strand-notices-test
	./build/nametag-test
	$(MAKE) gear-windows-test
	python3 -B test_game_thread_mutants.py
.PHONY: game-thread-test

build/puppet-test.exe: puppet_test.cpp puppet.cpp pose.cpp discovery.cpp puppet.h | build
	$(MINGW) $(WINFLAGS) puppet_test.cpp puppet.cpp pose.cpp discovery.cpp -o $@ -lbcrypt -lwinmm

# Join decisions and static read-only progress provider; no game or native move backend.
build/join-test: join_test.cpp join.cpp join.h discovery.cpp ../core/core.cpp | build
	$(CXX) $(FLAGS) join_test.cpp join.cpp discovery.cpp ../core/core.cpp -o $@
join-test: build/join-test
	./build/join-test
	python3 -B test_join_mutants.py
.PHONY: join-test

# Read-only environment provider and opt-in setter guards, game-free.
build/environment-test: environment_test.cpp environment.cpp config.cpp discovery.cpp $(HEADERS) | build
	$(CXX) $(FLAGS) environment_test.cpp environment.cpp config.cpp discovery.cpp -o $@
environment-test: build/environment-test build/coop-partner
	./build/environment-test
	python3 -B test_environment_mutants.py
	python3 -B test_environment_partner.py --cli-only
.PHONY: environment-test
environment-udp-test: build/coop-partner
	python3 -B test_environment_partner.py
.PHONY: environment-udp-test
