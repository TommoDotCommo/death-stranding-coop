#include "pose.h"
#include <cmath>
namespace mod {
Pose PoseReader::read() const {
    Pose p;
    auto fail=[&](const char* reason){p.available=false;p.coherent=false;p.reason=reason;return p;};
    const auto& s=sampler_;
    if(s.signatures.size()!=6) return fail("sampler not validated");
    auto managerSlot=s.signatures[0].targets.at("player_manager_global");
    uint64_t manager=0;
    if(!s.read(managerSlot,&manager,sizeof manager)) return fail("manager unreadable");
    if(!manager||manager>=0x0000800000000000ULL) return fail("manager null");
    auto chosen=s.selectPlayer(manager);
    if(*chosen.reason) return fail(chosen.reason);
    auto player=chosen.player;
    unsigned char human[2]={};
    if(!s.read(player+0x78,human,sizeof human)) return fail("player unreadable");
    if(human[0]!=1||human[1]!=0) return fail("not local human");
    uint64_t entity=0;
    if(!s.read(player+0x70,&entity,sizeof entity)) return fail("player unreadable");
    if(!entity||entity>=0x0000800000000000ULL) return fail("entity null");
    if(!s.read(entity+0xc8,p.position.data(),24)) return fail("pose unreadable");
    if(!s.read(entity+0xe0,p.rotation.data(),36)) return fail("pose unreadable");
    for(double v:p.position) if(!std::isfinite(v)) return fail("non-finite pose");
    for(float v:p.rotation) if(!std::isfinite(v)) return fail("non-finite pose");
    // Chain recheck after the copy: a changed pointer means the values may belong to another or a freed object.
    uint64_t manager2=0,player2=0,entity2=0;
    bool same=s.read(managerSlot,&manager2,sizeof manager2)&&manager2==manager
        &&s.read(chosen.slot,&player2,sizeof player2)&&player2==player
        &&s.read(player+0x70,&entity2,sizeof entity2)&&entity2==entity;
    if(!same) return fail("incoherent chain");
    p.available=true;p.coherent=true;p.reason="";
    return p;
}
}
