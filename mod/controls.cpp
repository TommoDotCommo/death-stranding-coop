#include "controls.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
namespace mod::controls {
// Indices below 135 (bools) and 138 (floats) are the game's own enum values (research-pose, the registration at
// 0x18838b0..); the rest are records the porter's manager holds beyond it (live dump, game_offsets.md).
const char* const BoolNames[BoolCount]={
    nullptr,"Gun_Hold","DSMove","Is_Fire","Is_Fire_Rewind","Turn_Rewind","Is_Jump","Is_Landed",
    "Is_Tackle","Is_Tackle2","Is_Stepon","Is_Fall","Is_Dead","Is_Pickup","Is_Slide","Pickup_Is_Drop",
    "Is_Loading_Over","Is_Loading_Back_Over","Is_Drag","Drag_Is_Catch_Ground","Is_LeftFoot","Is_Crouch","Is_Damage","Is_Interactive",
    "Is_Elude","Is_Vehicle","Temporary_Bool0","Temporary_Bool1","Temporary_Bool2","Temporary_Bool3","Temporary_Bool4","Temporary_Bool5",
    "Temporary_Bool6","Temporary_Bool7","Temporary_Bool8","Temporary_Bool9","Temporary_Bool10","Int_Is_GazerCatch","Int_Is_GazerCut","Is_Natural",
    "Is_StanceChange","Action_Rewind","Action_Move","Is_Down_To_Getup","Is_Brake","Is_HandIK","Is_Flinch","Can_Flinch",
    "Is_Inertia","Is_PreInertia","Is_BasicAction","Is_Recover","Is_Slip_End","Is_Down_Change_Pose","Is_TakeABreak","Is_TakeABreak_LoopEnd",
    "Is_InGameGesture","Is_Special_Floor","Is_Little_Turn","Is_Turn90","Is_ActiveSkeleton","Is_AttackHand_Switch","Is_Attack_Hand_Reset","Is_FootDown_Left",
    "Tmp_Basic_Upper_Bool0","Is_Water_Start","Action_Rewind_Long","Action_Rewind_Long_Long","Action_Rewind_NoInter","Is_Tire_Hold_Reaction","Is_Actions","Is_AutoStepDown",
    "Is_Try_Guard","Is_Try_Guard_Safe","Is_PrivateRoom","Is_Dash_Ready","Is_Pickup_Upper_Rewind","IsPickupUpperRewindImmediately","Is_Pickup_Upper_Freeze","Pickup_Upper_IsUnsafe",
    "Is_Drink",nullptr,"Is_Levitate","Is_Aim_Turn","Is_Aim_Turn_Rewind","Is_Aim_Move","Is_Aim_Move_Start","Is_Aim_Move_Change",
    "Is_Aim_Move_Loop_2","Is_Aim_Start","Is_Aim_End","IsAimEndImmediately","Is_Aim_Rewind","Is_Aim_Throwable_Empty","Is_Reload_Resume","Is_Keep_Aim_Pose",
    "Is_Drop_Weapon","Is_Use_Basic_Upper","Is_Guard_Left","Is_Guard_Right","Is_Upper_Left","Is_Upper_Right","Is_Upper_Left_Rewind","Is_Upper_Right_Rewind",
    "Is_Harmonica","Is_Harmonica_Play","Is_Use_SubMagazine","Is_Has_SubMagazine","Is_UpperPickSimple","UsedLeftHand","UsedRightHand","Is_Equip_View",
    "Is_AKukan","Is_TireOneShot","Is_Ragdoll","Is_Little_Turn_One_Step","Is_Water_Calm","Is_Narrow_Arms","Is_HiggsPhase","Is_SeqForceReset",
    "IsUseForceBasiceUpper","Idle_GuardL_Active","Idle_GuardR_Active","Force_Gun_Aim","InOnsen","Is_Force_Upper_Reset","Is_Disable_Keep_Equip_Action","Is_ChangeBullet",
    "Is_ThrowReGrip","Is_ForceLayer","Is_Force_Upper_Pass","Is_Wind_Hand","IsBasicNaturalMoveLoop","Is_NaturalMoveReset","Is_PeeZipperOn",
    // Beyond the enum: records the porter's network uses (live record dump, 2026-11-13) that the mod already bound on the
    // body safely. Creating a record for some others (BalanceIK_Weight and the like, values the network computes) reads
    // an unset value slot in the body's network and faults (live 2026-11-13, ds.exe+0x1a4ba9f under InitFloat).
    "Sprint",
};

const char* const FloatNames[FloatCount]={
    nullptr,"Unbalance_Front_Rate","Unbalance_Side_Rate","Unbalance_Rate","Turn_Heading","Stop_Speed","Nukarumi_Rate","Pickup_Type",
    "Pickup_Speed_Modifier","Pickup_Upper_Type","Pickup_Upper_Tmp0","Pickup_Upper_Tmp1","Pickup_Upper_Tmp2","Pickup_Upper_VehicleType","Aim_Type","Damage_Type",
    "Temporary_Float0","Temporary_Float1","Temporary_Float2","Temporary_Float3","Temporary_Float4","Temporary_Float5","Temporary_Float6","Temporary_Float7",
    "Temporary_Float8","Temporary_Float9","Temporary_Float10","Vehicle_Type","Vehicle_Seat","Damage_Heading","DSSlope_Angle","Slope_Angle_Interp",
    "Natural_Type","Stance_Change_Level","Stop_Level","Action_Move_Speed","Move_Float","Move_Float_Interp","Move_No_Stop_Timer","Pee_Dir_Part",
    "Pee_Dir_X","Pee_Dir_Y","Flinch_Heading","Tired_Type","Loading_Dash_Rate","TakeABreak_Level","TakeABreak_Loop_Ver","TakeABreak_State",
    "TakeABreak_Type","InGameGesture_Type","Special_Floor_Type","Special_Floor_Tmp0","Turn90_Heading","Interactive_Type","Basic_Upper_Type","Basic_Upper_Rate",
    "Basic_Upper_Heading","Basic_Upper_Force_Type","Basic_Upper_Force_Tmp0","Basic_Upper_Force_Tmp1","Water_Depth","Attack_Is_LeftHand","Fire_Type","Fire_Hold_Rate",
    "Fire_Move_Rate","Fire_Aim_Heading","Loading_Rate","Flinch_Type","Water_Side_Power","Danger_Slope_Rate","RunToDash_Rate","RunToDash_Rate_Interp",
    "Elude_Type","Gun_Hold_State","Gun_Hold_Ready_Type","Fall_Type","DSMove_Start_Heading","Slope_Heavy_Rate","DSMove_Loop_Heading","Shoes_Broken_Type",
    "IdleLookType",nullptr,"Wind_Power_Rate","Is_Opening_Foot_Step","Aim_Move_Heading","Aim_Move_Speed","Aim_Turn_Heading","Aim_Crouch_Weight",
    "Aim_Move_Start_Right","Aim_Move_Change_Heading","Aim_Move_Input_Rate","Aim_Turn_Speed_Modifier","Is_Aiming_Enemy","TEST_Motion_Rate","Upper_Left_Type","Upper_Left_Type_Old",
    "Upper_Left_Tmp0","Upper_Left_Tmp1","Upper_Left_Tmp2","Upper_Right_Type","Upper_Right_Type_Old","Upper_Right_Tmp0","Upper_Right_Tmp1","Upper_Right_Tmp2",
    "Equip_View_Type","Equip_Pose_Type","Lung_Rate","Baggage_Throw_Speed_Modifier","Baggage_Throw_Stagger_Type","Tire_Rate","Tire_Walk_Rate","TireOneShot_Tmp0",
    "TireOneShot_FullBodyRate","Water_Remove_Rate","Tire_One_Shot_Type","Water_MoveSlowWalkRate","LookIK_X","LookIK_Y","LookIK_Rate","Crouch_Rate",
    "RealMoveSpeedInterp","FlinchRate","Slide_Type","Idle_GuardL_Rate","Idle_GuardR_Rate","Seq_ResetType","JumpSpeedRate","SlopeUpSlowRate",
    "Move_Speed_Config","OnsenMoveRate","RealSpeedFrame","PickupAnimSpeedRate","Force_Layer_Type","Force_Layer_Rate","ToSleepLArmTimer","TarrySupperDashRate",
    "SlopeUp_RHandRate","SlopeUp_LHandRate",
    // Beyond the enum (see the bools).
    "Speed",
};


namespace {
// Live: the porter's flush pulses these with 0x2741c80 and never sets them (static, research-pose's setter map).
constexpr unsigned Pulses[]={4,5,40,41,46,65,67,68,71,76,84,92,97,125,127,128,133};
}
bool pulseOnly(unsigned i){for(unsigned p:Pulses)if(p==i)return true;return false;}
bool skipBool(unsigned i){
    if(i>=BoolCount||!BoolNames[i])return true;
    for(const char* n:{"Is_Ragdoll","Is_ActiveSkeleton","Is_AKukan","Is_PrivateRoom"})if(!std::strcmp(BoolNames[i],n))return true;
    return false;
}
bool skipFloat(unsigned i){return i>=FloatCount||!FloatNames[i];}
void Gather::add(const std::array<uint8_t,ds::AnimationBoolBytes>& held,const std::array<float,ds::AnimationFloats>& floats){
#ifndef MUTANT_CONTROLS_ROSE
    if(have_)for(size_t k=0;k<held.size();++k)now_.rose[k]|=uint8_t(held[k]&~now_.held[k]);
#endif
    now_.held=held;now_.floats=floats;have_=true;
}
namespace {
bool q(const discovery::Sampler& s,uint64_t a,uint64_t& v){return a&&a<0x800000000000ULL&&s.read(a,&v,8);}
}
bool readPorter(const discovery::Sampler& s,uint64_t e,uint64_t animator,Binding& b,
                std::array<uint8_t,ds::AnimationBoolBytes>& held,std::array<float,ds::AnimationFloats>& floats){
    uint64_t manager=0;
    if(b.manager&&b.entity==e){uint64_t v=0,owner=0;if(q(s,b.manager,v)&&v==animator&&q(s,b.manager+0x48,owner)&&owner==e)manager=b.manager;}
    if(!manager){uint32_t components=0;uint64_t list=0;
        if(!s.read(e+0x90,&components,4)||components>4096||!q(s,e+0x98,list))return false;
        for(uint32_t i=0;i<components&&!manager;++i){uint64_t m=0,v=0,owner=0;if(q(s,list+8*i,m)&&q(s,m,v)&&v==animator&&q(s,m+0x48,owner)&&owner==e)manager=m;}}
    uint32_t count=0;uint64_t slots=0;
    if(!manager||!s.read(manager+0x50,&count,4)||!count||count>4096||!q(s,manager+0x58,slots))return false;
    if(b.manager!=manager||b.count!=count||b.entity!=e){
        static const auto byName=[]{std::unordered_map<std::string,int16_t> m;
            for(unsigned i=0;i<BoolCount;++i)if(BoolNames[i])m[BoolNames[i]]=int16_t(i);
            for(unsigned i=0;i<FloatCount;++i)if(FloatNames[i])m[FloatNames[i]]=int16_t(-int(i)-2);
            return m;}();
        b=Binding{manager,e,count,std::vector<int16_t>(count,-1),{},true};
        for(uint32_t i=0;i<count;++i){uint64_t name=0;char text[48]{};
            if(!q(s,slots+48ull*i,name)||!s.read(name,text,sizeof text-1))continue;
            auto it=byName.find(text);if(it!=byName.end())b.slot[i]=it->second;else b.unknown.push_back(text);}
    }
    std::vector<uint8_t> raw(48ull*count);
    if(!s.read(slots,raw.data(),raw.size()))return false;
    held={};floats={};
    for(uint32_t i=0;i<count;++i){int c=b.slot[i];if(c==-1)continue;const uint8_t* r=raw.data()+48ull*i+0x20;
        if(c>=0){if(r[0])setBit(held,unsigned(c),true);}
        else{float v;std::memcpy(&v,r,4);if(std::isfinite(v)&&std::abs(v)<=ds::AnimationFloatMax)floats[unsigned(-c-2)]=v;}}
    return true;
}
namespace {
// The player's per-frame action set (research-pose, 0x2803e1a..): Is_Actions is its OR, and the game pulses
// Action_Rewind whenever it changes to a nonzero set.
const char* const ActionSet[]={"Is_Pickup","Is_Jump","Is_Damage","Is_Fall","Is_Tackle","Is_Dead","Is_Stepon","Is_Drag","Is_Interactive",
    "Is_Elude","Is_Vehicle","Is_Natural","Is_TakeABreak","Is_InGameGesture","Is_PrivateRoom"};
unsigned indexOf(const char* name){for(unsigned i=0;i<BoolCount;++i)if(BoolNames[i]&&!std::strcmp(BoolNames[i],name))return i;return BoolCount;}
uint32_t actionSet(const ds::Animation& a){
    static const auto idx=[]{std::vector<unsigned> v;for(auto n:ActionSet)v.push_back(indexOf(n));return v;}();
    uint32_t s=0;for(size_t k=0;k<idx.size();++k)if(idx[k]<BoolCount&&bit(a.held,idx[k]))s|=1u<<k;return s;
}
}
std::vector<Write> plan(const ds::Animation* last,const ds::Animation& now,bool fresh){
    std::vector<Write> out;
    // The mod's tick is slower than the game's frames, so the porter's own one-frame pulses are often missed: the
    // body gets the game's own rewind whenever the action set changes, unless the sample carries one already.
    static const unsigned rewind=indexOf("Action_Rewind");
#ifndef MUTANT_CONTROLS_AGGREGATE
    {uint32_t was=last?actionSet(*last):0,is=actionSet(now);
     if(is&&is!=was&&!(fresh&&rewind<BoolCount&&bit(now.rose,rewind)))out.push_back({true,rewind,1,true});}
#endif
    for(unsigned i=0;i<FloatCount;++i){if(skipFloat(i))continue;
        if(!last||last->floats[i]!=now.floats[i])out.push_back({false,i,now.floats[i],false});}
    for(unsigned i=0;i<BoolCount;++i){if(skipBool(i))continue;
        bool held=bit(now.held,i),rose=fresh&&bit(now.rose,i);
        if(pulseOnly(i)){
#ifndef MUTANT_CONTROLS_PULSE_HELD
            if(rose)out.push_back({true,i,1,true});
#else
            if(!last||bit(last->held,i)!=held)out.push_back({true,i,float(held),false});
#endif
            continue;}
        if(!last||bit(last->held,i)!=held)out.push_back({true,i,float(held),false});
        // On and off again between two samples: it was a pulse there too.
#ifndef MUTANT_CONTROLS_SHORT
        if(rose&&!held)out.push_back({true,i,1,true});
#endif
    }
    return out;
}
bool continuous(unsigned i){
    if(skipFloat(i))return false;
    std::string n=FloatNames[i];
#ifndef MUTANT_CONTROLS_BLEND_KINDS
    for(const char* step:{"Type","State","Seat","Level","_Ver","Temporary","Config","Part"})if(n.find(step)!=std::string::npos)return false;
    if(n.rfind("Is_",0)==0)return false;
#endif
    return true;
}
bool angular(unsigned i){
    if(skipFloat(i))return false;
    std::string n=FloatNames[i];return n.find("Heading")!=std::string::npos||n.find("Angle")!=std::string::npos;
}
ds::Animation blend(const ds::AnimationShown& s){
    ds::Animation out=s.animation;if(!s.between)return out;
    float f=std::clamp(s.fraction,0.f,1.f);
    for(unsigned i=0;i<FloatCount;++i){if(!continuous(i))continue;
        float a=out.floats[i],z=s.next.floats[i];
#ifndef MUTANT_CONTROLS_BLEND_WRAP
        float d=angular(i)?float(std::remainder(double(z)-double(a),360.)):z-a;
#else
        float d=z-a;
#endif
        out.floats[i]=a+f*d;}
    for(size_t h=0;h<out.handCount;++h)for(size_t j=0;j<s.next.handCount;++j){
        auto& a=out.hands[h];const auto& z=s.next.hands[j];if(a.item!=z.item||a.tag!=z.tag)continue;
        for(int k=0;k<3;++k)a.position[k]=int16_t(std::lround(a.position[k]+f*(z.position[k]-a.position[k])));}
    return out;
}
std::vector<Write> clear(const ds::Animation& last){
    std::vector<Write> out;
    for(unsigned i=0;i<FloatCount;++i)if(!skipFloat(i)&&last.floats[i]!=0)out.push_back({false,i,0,false});
    for(unsigned i=0;i<BoolCount;++i)if(!skipBool(i)&&!pulseOnly(i)&&bit(last.held,i))out.push_back({true,i,0,false});
    return out;
}
}
