#include "passenger.h"
#include <cmath>
#include <cstdio>
using namespace mod::passenger;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"passenger oracle: %s (line %d)\n",#x,__LINE__);++failures;}}while(0)
static Look near(bool key=false){Look l;l.permitted=true;l.partnerInTruck=true;l.truck=0x7000;l.distance=2;l.key=key;return l;}
int main(){
    // Getting on: only with the key, next to the partner's truck, permitted, mover free.
    { Rider r;CHECK(r.tick(near())==Do::Nothing);
      Look far=near(true);far.distance=Reach+0.5;CHECK(r.tick(far)==Do::Nothing);
      Look walking=near(true);walking.partnerInTruck=false;CHECK(r.tick(walking)==Do::Nothing);
      Look busy=near(true);busy.permitted=false;CHECK(r.tick(busy)==Do::Nothing);
      Look held=near(true);held.mover=Held;CHECK(r.tick(held)==Do::Nothing);
      CHECK(r.tick(near(true))==Do::GetOn&&r.riding()&&r.truck()==0x7000); }
    // Getting off: the key, the partner out, the truck replaced, not permitted; the game letting go ends it quietly.
    { auto on=[]{Rider r;r.tick(near(true));return r;};
      { Rider r=on();Look l=near();l.mover=Held;CHECK(r.tick(l)==Do::Nothing);CHECK(r.tick(near(true))==Do::GetOff&&!r.riding()); }
      { Rider r=on();Look l=near();l.partnerInTruck=false;CHECK(r.tick(l)==Do::GetOff&&r.why()=="the partner got out"); }
      { Rider r=on();Look l=near();l.truck=0x8000;CHECK(r.tick(l)==Do::GetOff); }
      { Rider r=on();Look l=near();l.truck=0;l.partnerInTruck=false;CHECK(r.tick(l)==Do::GetOff); }
      { Rider r=on();Look l=near();l.permitted=false;CHECK(r.tick(l)==Do::GetOff); }
      { Rider r=on();Look l=near();l.distance=50;l.mover=Held;CHECK(r.tick(l)==Do::Nothing&&r.riding()); }   // far from where he got on: still riding
      { Rider r=on();Look l=near();CHECK(r.tick(l)==Do::Nothing&&r.tick(l)==Do::Nothing&&r.riding());      // still blending on: not over
        l.mover=Held;r.tick(l);l.mover=Free;CHECK(r.tick(l)==Do::Nothing&&r.riding());CHECK(r.tick(l)==Do::Nothing&&!r.riding()&&r.why()=="the game let go"); }
      { Rider r=on();r.forget();CHECK(!r.riding()&&r.tick(near())==Do::Nothing); } }
    // Kept out of the truck: inside, out through the nearest side by the margin; outside, on the roof or below: nothing.
    { Transform t;t.position={100,200,10};t.rotation={0,-1,0, 1,0,0, 0,0,1};   // right = -y, forward = +x
      CHECK(!pushOut(t,{100,203,10}));CHECK(!pushOut(t,{105,200,10}));CHECK(!pushOut(t,{100,200,14}));CHECK(!pushOut(t,{100,200,8}));
      auto a=pushOut(t,{100,198.8,10.5});                                 // 1.2 m to the right (-y), near the side
      CHECK(a&&std::fabs((*a)[1]-(200-1.65))<1e-9&&std::fabs((*a)[0]-100)<1e-9&&std::fabs((*a)[2]-10.5)<1e-9);
      auto b=pushOut(t,{103.5,200.2,10});                                 // near the front: out along forward
      CHECK(b&&std::fabs((*b)[0]-(100+3.95))<1e-9&&std::fabs((*b)[1]-200.2)<1e-9);
      auto c=pushOut(t,{96.5,200.5,10});CHECK(c&&std::fabs((*c)[0]-(100-3.95))<1e-9);
      auto d=pushOut(t,{100,201.2,10});CHECK(d&&std::fabs((*d)[1]-(200+1.65))<1e-9); }
    {   // seats and boxes by vehicle kind (core VehicleKind: 1 bike, 2 truck, 3 car)
      auto truck=seat(2),bike=seat(1);
      CHECK(truck&&std::string(truck->helper)=="HLP_Enemy"&&truck->forward==0);
      CHECK(bike&&bike->place&&bike->forward<-0.9f&&bike->right==0&&!truck->place);   // held at a place behind the driver
      {Transform v;v.position={10,20,30};v.rotation={0,1,0, -1,0,0, 0,0,1};   // facing -x
       auto p=seatPlace(v,*bike);CHECK(std::fabs(p.position[0]-(10-double(bike->forward)))<1e-6&&std::fabs(p.position[1]-20)<1e-6&&std::fabs(p.position[2]-(30+double(bike->up)))<1e-6&&p.rotation==v.rotation);}
      CHECK(!seat(0)&&!seat(3)&&!box(0)&&!box(3));
      {auto cart=seat(CartKind);CHECK(cart&&cart->place&&cart->up>0.5f&&cart->forward==0&&cart->right==0&&!box(CartKind));}   // standing on another player's carrier
      CHECK(box(2)&&box(1)&&box(1)->halfRight<box(2)->halfRight&&box(1)->halfForward<box(2)->halfForward);
      Transform t;t.position={100,200,10};t.rotation={1,0,0, 0,1,0, 0,0,1};
      CHECK(pushOut(t,{100.2,200.5,10.2},*box(1)));CHECK(!pushOut(t,{100.6,200.5,10.2},*box(1)));CHECK(pushOut(t,{100.6,200.5,10.2},*box(2)));
    }
    {   // Group play, getting on from above or below the seat: held within SeatStep, moved first up to SeatClimb, else refused.
      CHECK(board(144.5,144.5,true)==Board::Hold&&board(145.9,144.5,true)==Board::Hold&&board(143.1,144.5,false)==Board::Hold);
      CHECK(board(154.0,144.5,true)==Board::MoveFirst);                 // live: 9.5 m up on a roof
      CHECK(board(157.5,144.5,true)==Board::MoveFirst&&board(134.5,144.5,true)==Board::MoveFirst);   // 13 m up, 10 m below
      CHECK(board(154.0,144.5,false)==Board::Refuse);                   // no teleport: refused, not held up there
      CHECK(board(144.5+SeatClimb+1,144.5,true)==Board::Refuse&&board(144.5-SeatClimb-1,144.5,true)==Board::Refuse);   // a bridge, a cliff
      CHECK(board(std::nan(""),144.5,true)==Board::Refuse); }
    {   // Where he is put: beside the vehicle, at its height plus the lift, on his side, upright.
      Transform v;v.position={100,200,143.65};v.rotation={0,-1,0, 1,0,0, 0,0,1};   // right = -y, forward = +x
      Transform up;up.position={100.3,199.5,157.0};up.rotation={0,-1,0, 1,0,0, 0,0,1};   // held 13 m up, on the right
      auto t=beside(v,CartKind,up);
      CHECK(std::fabs(t.position[2]-(143.65+SetDownLift))<1e-9);         // the vehicle's height, not his
      CHECK(std::fabs(t.position[0]-100)<1e-9&&std::fabs(t.position[1]-(200-1.5))<1e-9);
      Transform left=up;left.position[1]=201;CHECK(std::fabs(beside(v,CartKind,left).position[1]-(200+1.5))<1e-9);
      auto truck=beside(v,2,up);CHECK(std::fabs(truck.position[1]-(200-box(2)->halfRight-SetDownOut))<1e-9);   // out of the truck's box
      CHECK(!pushOut(v,truck.position,*box(2)));
      Transform tilted=v;tilted.rotation={0,-0.8f,0.6f, 1,0,0, 0,-0.6f,0.8f};   // rolled on a slope
      auto u=beside(tilted,2,std::nullopt);CHECK(u.rotation[2]==0&&u.rotation[5]==0&&u.rotation[8]==1&&std::fabs(u.position[2]-(143.65+SetDownLift))<1e-9);
      CHECK(std::fabs(u.position[1]-(200-box(2)->halfRight-SetDownOut))<1e-6);   // no porter read: the vehicle's right
      // Moved first onto a place seat: the seat's place, upright; a helper seat (the truck): beside it.
      auto s=boardPlace(v,CartKind,*seat(CartKind),up);CHECK(std::fabs(s.position[2]-(143.65+0.85))<1e-6&&std::fabs(s.position[0]-100)<1e-9&&s.rotation[8]==1);
      auto h=boardPlace(v,2,*seat(2),up);CHECK(std::fabs(h.position[2]-(143.65+SetDownLift))<1e-9&&std::fabs(h.position[1]-(200-2.5))<1e-9); }
    {   // Set down after the ride: from the vehicle now, else its last known place (gone with an event or the fleet).
      Ridden r;Transform v;v.position={10,20,30};v.rotation={1,0,0, 0,1,0, 0,0,1};
      Transform rider;rider.position={10,20,42};rider.rotation=v.rotation;
      CHECK(!r.setDown(std::nullopt,rider));                              // never seen: nothing
      r.see(v,2);
      auto gone=r.setDown(std::nullopt,rider);CHECK(gone&&std::fabs(gone->position[2]-(30+SetDownLift))<1e-9&&std::fabs(gone->position[0]-(10+box(2)->halfRight+SetDownOut))<1e-9);
      Transform moved=v;moved.position={50,60,35};
      auto now=r.setDown(moved,rider);CHECK(now&&std::fabs(now->position[1]-60)<1e-9&&std::fabs(now->position[2]-36)<1e-9);
      r.clear();CHECK(!r.setDown(std::nullopt,rider)); }
    if(failures){std::fprintf(stderr,"passenger oracle: %d failures\n",failures);return 1;}
    std::puts("passenger ok");return 0;
}
