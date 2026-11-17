"""Every protection of the world server has a mutant that its test must fail."""
from pathlib import Path
import os,subprocess,sys,tempfile
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parent
families=[('world_test.cpp',['world.cpp','collections.cpp','collection.cpp'],['WORLD_NO_TOKEN','WORLD_OWNER','WORLD_OWN','WORLD_OBJECT_CAP','WORLD_PLAYER_CAP','WORLD_LIKE_RANGE','WORLD_ID_LETTER','WORLD_LIKES_MINE','WORLD_LIKES_SINCE','WORLD_ID_SHAPE','WORLD_ID_AGAIN','WORLD_ID_TAKEN','WORLD_ID_KEEP','WORLD_STRIP_STORE','WORLD_STRIP_SERVE']),
          ('policy_test.cpp',['world.cpp','collections.cpp','collection.cpp'],['POLICY_PLAYERS','POLICY_BUILD_NOBODY','POLICY_BUILD_MEMBERS','POLICY_LIMIT','POLICY_ASK','POLICY_REVIEW','POLICY_PENDING_SERVED','POLICY_OFFER_NO_SERVED',
                                             'POLICY_REMNANT_CHANGE','POLICY_REMNANT_LIKES','POLICY_PLAYER_ANY','POLICY_WITHDRAW_STAY_REMOVES','POLICY_WITHDRAW_GO_KEEPS','POLICY_REMOVE_PLAYER_OTHERS',
                                             'POLICY_EXPORT_PENDING','POLICY_SAVE_LOST','POLICY_HELD_SERVED','POLICY_HELD_RELEASE']),
          ('curate_test.cpp',['world.cpp','collections.cpp','collection.cpp'],['CURATE_OWN','CURATE_UNSHARED','CURATE_ORDER','CURATE_COUNTS','CURATE_UNSTABLE','CURATE_BUILDER_CAP','CURATE_SPREAD',
                                            'CURATE_SAVE_LOST','CURATE_UNBOUNDED']),
          ('highway_test.cpp',['world.cpp','collections.cpp','collection.cpp'],['HIGHWAY_OWN','HIGHWAY_ASKED','HIGHWAY_RANGE','HIGHWAY_WHOLE','HIGHWAY_KEYS','HIGHWAY_CHOICE','HIGHWAY_REVIEW','HIGHWAY_BUILD',
                                             'HIGHWAY_LIKES','HIGHWAY_WITHDRAW','HIGHWAY_REMOVE','HIGHWAY_SAVE_LOST']),
          ('names_test.cpp',['world.cpp','collections.cpp','collection.cpp'],['LOOKUP_HOME_MISSING','LOOKUP_LOCAL_SUFFIX','LOOKUP_CUT']),
          ('link_test.cpp',['link.cpp','world.cpp','collections.cpp','collection.cpp'],['LINK_KEEP_ID','LINK_DUPLICATE','LINK_PASS_IMPORTED','LINK_ECHO','LINK_IMPORT_OWN','LINK_IMPORT_OWNER','LINK_FILTER_TYPE','LINK_FILTER_OWNER',
                                                      'LINK_FILTER_AREA','LINK_FILTER_AGE','LINK_NO_PRUNE','LINK_FORGET_ANY','LINK_CAP','LINK_SIZE','LINK_STRICT','LINK_LIKES','LINK_VERSION']),
          ('front_test.cpp',['front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],['FRONT_PREFIX','FRONT_BEARER','FRONT_LINK_KEY']),
          ('api_test.cpp',['front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],['API_METHOD','API_KEY','API_STRICT','API_CHOICES_MEMBER','API_CHOICES_ROOM','API_ALL_IMPLIED','API_MINE_OTHERS','API_MINE_IMPORTS','FRONT_PREFIX',
                                                                                                                                                                         'FRIENDS_SAVE_LOST','CURATE_FRIENDS_NONE']),
          ('notices_test.cpp',['front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],
           ['NOTICES_OTHERS','NOTICES_IMPORTS','NOTICES_TOKEN_TICKET','NOTICES_EXPIRY','NOTICES_STAMP','NOTICES_SINCE','NOTICES_KEPT_WHY','NOTICES_EPOCH','NOTICES_SAVE','TICKET_REPLACE','NOTICES_OFFER_OLD']),
          ('identity_test.cpp',['identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],
           ['SIGN_NO_VERIFY','SIGN_REPLAY','SIGN_WINDOW','SIGN_BODY','SIGN_TARGET','SIGN_SERVER','SIGN_NONCE_CAP']),
          ('keys_test.cpp',['front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','relay.cpp','identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],
           ['IDENTITY_NAME_SIGN_IN','IDENTITY_KEY_NAME','IDENTITY_OLD_SESSION','TICKET_EXPIRY','TICKET_REUSE','TICKET_REPLACE','RELAY_TOKEN_ID',
            'RELAY_TOKEN_GROUP_KEY','RELAY_TOKEN_EXPIRY','RELAY_ADMIT','RELAY_EVICT','BAN_RELAY_KEEP','API_UNSIGNED','API_SIGNER_PLAYER','API_OWNER','API_FRIENDS_ANY','TICKET_NAME','TICKET_STEAM_NAME']),
          ('ban_test.cpp',['front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],
           ['BAN_PASS','BAN_SIGN_IN','BAN_SAVE_LOST','BAN_KEEP_SESSION','BAN_KEEP_PASSES','BAN_API','BAN_RELAY_KEEP','API_OWNER']),
          ('collection_test.cpp',['collection.cpp'],['COLLECTION_CRC','COLLECTION_VERSION','COLLECTION_COUNT','COLLECTION_RANGE','COLLECTION_ORDER','COLLECTION_TRAILING',
                                                    'COLLECTION_SIZE','COLLECTION_CANONICAL','COLLECTION_TURN_SIGN']),
          ('collections_test.cpp',['front.cpp','api.cpp','link.cpp','world.cpp','collections.cpp','collection.cpp','identity.cpp','signatures.cpp','../vendor/monocypher/monocypher.c','../vendor/monocypher/monocypher-ed25519.c'],
           ['COLLECTION_NOT_SELF','COLLECTION_OTHERS','COLLECTION_IMPORT_ANYONE','COLLECTION_NO_TURN','COLLECTION_CAP','COLLECTION_SPREAD','COLLECTION_SAVE_LOST','COLLECTION_SPOT_AXES']),
          ('limits_test.cpp',[],['GATE_BLOCK','GATE_CONNECTIONS','GATE_RATE','GATE_STRIKES','GATE_UNBOUNDED']),
          ('relay_test.cpp',['relay.cpp'],['RELAY_NO_TOKEN','RELAY_REBIND','RELAY_ANY_SOURCE','RELAY_FORWARD_FAR','RELAY_SOURCE_ID','RELAY_NO_HYSTERESIS','RELAY_AREA','RELAY_NO_POSITION',
                                           'RELAY_NO_TIMEOUT','RELAY_NO_LEAVE','RELAY_NO_RATE','RELAY_PLAYER_CAP','RELAY_HELLO_STALE','RELAY_ROSTER_ONCE','RELAY_SPOT_NO_POSITION']),
          ('broadcast_test.cpp',['relay.cpp','../core/broadcast_client.cpp','../core/core.cpp'],['RELAY_BCAST_MODE','RELAY_BCAST_BUILD','RELAY_BCAST_SPOOF','RELAY_BCAST_ORDER','RELAY_BCAST_STAMP',
                                           'RELAY_BCAST_ANIMATION_FAR','RELAY_BCAST_TELEPORT','RELAY_BCAST_INTEREST','RELAY_BCAST_ROSE','RELAY_BCAST_BUDGET','RELAY_BCAST_ENTER_STATE',
                                           'RELAY_BCAST_STATE_OLD','RELAY_BCAST_FETCH_FAR','RELAY_NEAR_CAP','RELAY_NEAR_NEAREST','RELAY_NEAR_HYSTERESIS','RELAY_NEAR_HIDDEN',
                                           'RELAY_NEAR_FRIENDS_FIRST','RELAY_SHOW_FRIENDS','RELAY_SHOW_NOBODY','RELAY_SHOW_HELLO_FIRST',
                                           'RELAY_MARK_FAR','RELAY_MARK_HIDDEN','RELAY_MARK_RATE','RELAY_MARK_ANY_SOURCE','RELAY_SPOT_YAW']),
          ('fight_test.cpp',['relay.cpp','../core/broadcast_client.cpp','../core/core.cpp'],['RELAY_FIGHT_REACH','RELAY_FIGHT_OWNER_GONE','RELAY_FIGHT_ONE_ROLE','RELAY_FIGHT_CAP',
                                           'RELAY_FIGHT_STATE_ANY','RELAY_FIGHT_UNJOINED','RELAY_FIGHT_SOURCE','RELAY_FIGHT_VISITOR_STATE','RELAY_FIGHT_SILENT','RELAY_BCAST_SPOOF']),
          ('presence_test.cpp',['relay.cpp','../core/broadcast_client.cpp','../core/core.cpp'],['LINK_PRESENCE_MAC','LINK_PRESENCE_REPLAY','LINK_PRESENCE_REMAP','LINK_PRESENCE_LOCAL_FIRST',
                                           'LINK_PRESENCE_STATE_ANY','LINK_PRESENCE_BUILD','LINK_PRESENCE_NAME','LINK_PRESENCE_INTEREST','LINK_PRESENCE_BUDGET','LINK_PRESENCE_TIMEOUT',
                                           'LINK_PRESENCE_GONE','LINK_PRESENCE_REEXPORT','LINK_PRESENCE_CLOCK','RELAY_NEAR_LINKED'])]
with tempfile.TemporaryDirectory() as d:
    def one(job):
        test,sources,name=job;exe=Path(d)/name
        subprocess.run(['g++','-std=c++17','-O1','-pthread','-DMUTANT_'+name,str(ROOT/test),*[str(ROOT/s) for s in sources],'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],text=True,capture_output=True,timeout=120)
        assert r.returncode==1 and 'oracle' in r.stderr,('SURVIVED',name,r.returncode,r.stderr[-300:])
        return name
    with ThreadPoolExecutor(max(1,int(os.environ.get('MUTANT_JOBS',8)))) as pool:
        for name in pool.map(one,[(t,s,n) for t,s,names in families for n in names]):print('KILLED MUTANT_'+name,flush=True)
