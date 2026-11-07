#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "net_win.h"
#include "launch_win.h"
#include <sstream>
#include <memory>
using namespace launcher;
namespace {
HWND window,tab;
HFONT font,titleFont;
std::unique_ptr<Network> net;
LaunchOptions options;
fs::path settingsFile,executable;
int selectedTab=0,choice=0;
bool lan=true,tailscale=true,privacy=false,initializing=true,joinAfterScan=false,joinScanStarted=false,remembered=false;
std::string joinQuery;
unsigned port=49000;
std::wstring player=L"Porter",gameFolder;
std::string pasted,lastState,lastRows;
uint32_t requestedSecret=0,lastScan=0;
std::vector<HWND> controls[4];
enum {Name=101,Host,Join,Solo,Primary,Paste,Rows,Status,Invite,Copy,Leave,Ready,BackUp,OpenSaves,GamePath,Port,Lan,Tailscale,Privacy,Protocol,Guide,Home};
HWND child(int id){return GetDlgItem(window,id);}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;}
void set(int id,const std::wstring& s){if(text(child(id))!=s)SetWindowTextW(child(id),s.c_str());}
void error(const std::exception& e){MessageBoxW(window,wtext(e.what()).c_str(),L"Death Stranding co-op - try again",MB_OK|MB_ICONERROR);}
HWND control(int page,int id,const wchar_t* klass,const wchar_t* label,DWORD style=0){auto h=CreateWindowExW(wcscmp(klass,L"EDIT")==0?WS_EX_CLIENTEDGE:0,klass,label,WS_CHILD|(wcscmp(klass,L"STATIC")?WS_TABSTOP:0)|style,0,0,0,0,window,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls[page].push_back(h);return h;}
void label(int page,int id,const wchar_t* s){control(page,id,L"STATIC",s,SS_LEFT);}
void place(int id,int x,int y,int w,int h){MoveWindow(child(id),x,y,w,h,TRUE);}
void show(int id,bool yes){ShowWindow(child(id),yes?SW_SHOW:SW_HIDE);}
void saveSettings(){
    if(initializing)return;
    std::string name=utf8(player),game=utf8(gameFolder);
    if(name.find_first_of("\r\n")!=std::string::npos||game.find_first_of("\r\n")!=std::string::npos)return;
    atomicWrite(settingsFile,"[launcher]\nname="+name+"\ngame="+game+"\nchoice="+std::to_string(choice)+"\nport="+std::to_string(port)+"\nlan="+std::to_string(lan)+"\ntailscale="+std::to_string(tailscale)+"\nprivacy="+std::to_string(privacy)+"\n");
}
Session chosenSession(){Session s;s.name=utf8(player);s.port=port;return s;}
void render();
void switchTab(int next){selectedTab=next;TabCtrl_SetCurSel(tab,next);render();}
void scan(bool force=false){
    if(choice!=1||net->lobby||selectedTab!=0||joinAfterScan)return;
    auto now=GetTickCount();if(!net->scanning&&(force||now-lastScan>=5000)){lastScan=now;net->scan(lan,tailscale,port,utf8(text(child(Paste))));}
}
void refreshRows(){
    std::string fingerprint;
    for(int i=0;i<net->hosts.n;++i){auto& r=net->hosts.r[i];fingerprint+=std::string(r.name)+r.version+std::to_string(r.ip)+std::to_string(r.port)+std::to_string(r.state)+std::to_string(r.players)+std::to_string(r.rtt);}
    if(fingerprint==lastRows)return;
    lastRows=fingerprint;
    int selected=int(SendMessageW(child(Rows),LB_GETCURSEL,0,0));SendMessageW(child(Rows),LB_RESETCONTENT,0,0);
    for(int i=0;i<net->hosts.n;++i){auto& r=net->hosts.r[i];std::string state=r.players>=r.limit?"full":r.state==CD_IN_LOBBY?"in lobby":"in game";
        std::string row=std::string(r.name[0]?r.name:"Invite-only host")+" | "+state+" | "+std::to_string(r.players)+"/"+std::to_string(r.limit)+" | "+cd_route_name(r.route)+" | "+std::to_string(r.rtt)+" ms";
        if(r.mismatch)row+=" | different version "+std::string(r.version);
        auto w=wtext(row);SendMessageW(child(Rows),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(w.c_str()));}
    if(net->hosts.n)SendMessageW(child(Rows),LB_SETCURSEL,selected>=0&&selected<net->hosts.n?selected:0,0);
}
void render(){
    if(!window||!tab)return;
    RECT rect;GetClientRect(window,&rect);int w=rect.right,h=rect.bottom;
    MoveWindow(tab,20,65,w-40,32,TRUE);
    for(int page=0;page<4;++page)for(auto c:controls[page])ShowWindow(c,page==selectedTab?SW_SHOW:SW_HIDE);
    TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(net->lobby?L"Lobby":L"Play");TabCtrl_SetItem(tab,0,&item);
    if(selectedTab==0){
        place(200,30,115,120,24);place(Name,155,110,w-185,28);
        for(int i=0;i<3;++i){place(Host+i,30+i*(w-60)/3,155,(w-75)/3,56);SendMessageW(child(Host+i),BM_SETCHECK,i==choice?BST_CHECKED:BST_UNCHECKED,0);show(Host+i,!net->lobby);}
        place(201,30,230,w-60,28);place(Paste,30,260,w-60,30);place(202,30,298,w-60,25);place(Rows,30,330,w-60,h-480);
        place(Status,30,h-132,w-60,50);place(Primary,30,h-70,w-60,40);
        bool join=choice==1&&!net->lobby;show(201,join);show(Paste,join);show(202,join);show(Rows,join);show(Primary,!net->lobby||net->hosting);
        set(Primary,net->lobby?L"Start game":choice==0?L"Open lobby":choice==1?L"Join selected host":L"Play solo");
        EnableWindow(child(Primary),!net->lobby||(net->joined&&net->peerReady));EnableWindow(child(Name),!net->lobby);
        place(203,30,235,w-60,60);place(Invite,30,310,w-60,64);place(Copy,30,390,(w-75)/2,36);place(Leave,45+(w-75)/2,390,(w-75)/2,36);place(Ready,30,450,w-60,40);
        show(203,net->lobby);show(Invite,net->lobby&&net->hosting);show(Copy,net->lobby&&net->hosting);show(Leave,net->lobby);show(Ready,net->lobby&&!net->hosting);
        set(203,net->hosting?L"Friends can join by: same network or Tailscale. Allow this launcher and ds.exe through Windows Firewall. Internet needs a VPN.":L"Your host decides when to start. Choose Ready, then keep this launcher open.");
        EnableWindow(child(Ready),net->joined);set(Ready,net->ready?L"Not ready":L"Ready");
        if(!net->lobby&&choice!=1){set(203,choice==0?L"Open a two-player lobby, copy its invite, then start when your friend is ready.":L"Play by yourself with the separate co-op saves. Your ordinary Steam saves stay separate.");show(203,true);}
        set(Status,wtext(net->status));refreshRows();
    }
    if(selectedTab==1){place(210,30,120,w-60,65);place(211,30,200,w-60,180);place(OpenSaves,30,400,(w-75)/2,40);place(BackUp,45+(w-75)/2,400,(w-75)/2,40);}
    if(selectedTab==2){place(220,30,115,w-60,55);place(GamePath,30,170,w-60,30);place(221,30,220,280,25);place(Port,320,215,100,30);place(Lan,30,270,w-60,30);place(Tailscale,30,310,w-60,30);place(Privacy,30,350,w-60,30);place(Protocol,30,410,w-60,40);place(222,30,475,w-60,80);
        bool edit=!net->lobby;for(int id:{GamePath,Port,Lan,Tailscale,Privacy})EnableWindow(child(id),edit);}
    if(selectedTab==3){place(230,30,120,w-60,240);place(Guide,30,390,w-60,40);place(Home,30,445,w-60,40);}
}
void copyInvite(){
    auto code=net->invite();char msg[1024];ci_message(net->session.name.c_str(),"Death Stranding co-op","https://tommodotcommo.github.io/death-stranding-coop/join/",code.c_str(),msg,sizeof(msg));
    auto value=wtext(msg);if(!OpenClipboard(window))throw std::runtime_error("Clipboard is busy. Try Copy invite again.");
    auto memory=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));if(!memory){CloseClipboard();throw std::runtime_error("Cannot allocate the invite clipboard.");}
    auto p=GlobalLock(memory);if(!p){GlobalFree(memory);CloseClipboard();throw std::runtime_error("Cannot write the invite clipboard.");}
    memcpy(p,value.c_str(),(value.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory)){GlobalFree(memory);CloseClipboard();throw std::runtime_error("Cannot copy the invite. Try again.");}CloseClipboard();net->status="Invite copied. Send it to your friend.";
}
void offerClipboard(){
    static DWORD lastClipboard=0;
    auto sequence=GetClipboardSequenceNumber();
    if(net->lobby||sequence==lastClipboard||!OpenClipboard(window))return;
    lastClipboard=sequence;
    auto h=GetClipboardData(CF_UNICODETEXT);std::string value;
    if(h&&GlobalSize(h)<=CI_SCAN_MAX*2){auto p=static_cast<const wchar_t*>(GlobalLock(h));if(p){auto cap=GlobalSize(h)/sizeof(wchar_t);size_t n=0;while(n<cap&&p[n])++n;if(n<cap)value=utf8(std::wstring(p,n));GlobalUnlock(h);}}CloseClipboard();
    ci_invite invite{};char name[33],prompt[256];if(!ci_find_invite(value.data(),value.size(),Prefix,&invite,name))return;
    ci_clipboard_prompt(name,prompt,sizeof(prompt));
    if(MessageBoxW(window,wtext(prompt).c_str(),L"Death Stranding co-op invite",MB_YESNO|MB_ICONQUESTION)==IDYES){char code[CI_CODE_MAX];ci_encode(&invite,Prefix,code,sizeof(code));set(Paste,wtext(code));choice=1;switchTab(0);scan(true);}
}
void beginJoin(){
    int i=int(SendMessageW(child(Rows),LB_GETCURSEL,0,0));if(i<0||i>=net->hosts.n)throw std::runtime_error("Select a host from the list, or paste an invite and wait for discovery.");
    auto& row=net->hosts.r[i];uint32_t secret=row.group==1?requestedSecret:0;net->joinHost(row,chosenSession(),secret);remembered=false;
}
void primary(){
    if(net->lobby){net->start();return;}
    if(choice==0){net->openHost(chosenSession(),privacy);set(Invite,wtext(net->invite()));}
    else if(choice==2){auto s=chosenSession();s.secret=random32();options.game=gameFolder;launchGame(options,s,true);net->status=options.prepareOnly?"Configuration prepared; no game started.":"Steam launch requested using separate co-op saves.";}
    else {
        auto paste=utf8(text(child(Paste)));
        if(!paste.empty()){
            cr_result r{};int kind=coop_recognise(paste.data(),paste.size(),Prefix,&r);
            if(kind==CR_ERROR){char b[256];throw std::runtime_error(ci_error_text(r.error,r.other,b,sizeof(b)));}
            if(kind==CR_STEAM_LOBBY)throw std::runtime_error("Steam lobby invites are not available yet. Ask your friend for a DS invite or use LAN/Tailscale.");
            requestedSecret=(kind==CR_INVITE||kind==CR_LINK)?r.invite.secret:0;
            joinAfterScan=true;joinScanStarted=false;joinQuery=paste;
            if(!net->scanning){net->scan(false,false,port,paste);joinScanStarted=true;}
            net->status="Checking the invite routes...";
        }else {requestedSecret=0;beginJoin();}
    }
    render();
}
void timer(){
    net->tick();
    if(net->collectScan()){
        refreshRows();
        if(joinAfterScan&&!joinScanStarted){net->scan(false,false,port,joinQuery);joinScanStarted=true;}
        else if(joinAfterScan){joinAfterScan=false;int selected=-1;for(int i=0;i<net->hosts.n;++i)if(net->hosts.r[i].group==1&&!net->hosts.r[i].mismatch&&(selected<0||net->hosts.r[i].route<net->hosts.r[selected].route))selected=i;
            if(selected<0)net->status="The host did not answer with this version. Ask them to open their lobby, check the invite and Windows Firewall, then try again.";
            else{SendMessageW(child(Rows),LB_SETCURSEL,selected,0);beginJoin();}}
    }
    if(net->joined&&!net->hosting&&!remembered){remembered=true;cd_recent_add(&net->recents,net->joinedName.c_str(),net->session.peer.c_str(),net->session.port,CD_R_ADDRESS,uint64_t(std::time(nullptr)));char bytes[CD_RECENT_BYTES];int n=cd_recents_encode(&net->recents,bytes,sizeof(bytes));if(n>0)atomicWrite(options.data/"recents.tsv",std::string(bytes,n));}
    if(net->launch){options.game=gameFolder;launchGame(options,net->session);net->launched();}
    scan();
    std::string state=std::to_string(net->lobby)+std::to_string(net->hosting)+std::to_string(net->joined)+std::to_string(net->ready)+std::to_string(net->peerReady)+net->status;
    if(state!=lastState){lastState=state;render();}
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    try {
        if(msg==WM_SIZE){render();return 0;}
        if(msg==WM_GETMINMAXINFO){auto p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={800,680};return 0;}
        if(msg==WM_ACTIVATE&&LOWORD(wp)!=WA_INACTIVE&&!initializing){PostMessageW(h,WM_APP+2,0,0);}
        if(msg==WM_APP+2){offerClipboard();return 0;}
        if(msg==WM_TIMER){timer();return 0;}
        if(msg==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==tab&&reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){switchTab(TabCtrl_GetCurSel(tab));scan(true);return 0;}
        if(msg==WM_COMMAND){int id=LOWORD(wp),event=HIWORD(wp);
            if(id==Name&&event==EN_CHANGE){player=text(child(Name));saveSettings();}
            if(id==GamePath&&event==EN_CHANGE){gameFolder=text(child(GamePath));saveSettings();}
            if(id==Port&&event==EN_CHANGE){auto v=utf8(text(child(Port)));if(!v.empty()&&v.find_first_not_of("0123456789")==std::string::npos&&v.size()==5){auto p=std::stoul(v);if(p>=49000&&p<=49014){port=unsigned(p);saveSettings();}}}
            if(id==Paste&&event==EN_CHANGE){auto s=utf8(text(child(Paste)));cr_result r{};coop_recognise(s.data(),s.size(),Prefix,&r);char desc[256];set(202,wtext(cr_describe(&r,desc,sizeof(desc))));}
            if(event==BN_CLICKED){
                if(id>=Host&&id<=Solo){choice=id-Host;saveSettings();render();scan(true);}
                if(id==Primary)primary();
                if(id==Copy)copyInvite();
                if(id==Leave){net->leave();render();}
                if(id==Ready){net->setReady();render();}
                if(id==Lan||id==Tailscale||id==Privacy){auto checked=SendMessageW(child(id),BM_GETCHECK,0,0)==BST_CHECKED;if(id==Lan)lan=checked;if(id==Tailscale)tailscale=checked;if(id==Privacy)privacy=checked;saveSettings();}
                if(id==Protocol){registerHandler(executable);MessageBoxW(h,L"Invite links now open this launcher for your Windows account.",L"Invite links",MB_OK);}
                if(id==OpenSaves){fs::create_directories(options.data/"redirect-data");ShellExecuteW(h,L"open",(options.data/"redirect-data").c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
                if(id==BackUp){if(!backupAllowed(net->lobby,gameRunning(gameFolder)))throw std::runtime_error("Leave the lobby and close the game before backing up.");auto dest=options.data/"backups"/std::to_string(std::time(nullptr));backup(options.data,dest);MessageBoxW(h,(L"Co-op saves backed up to:\n"+dest.wstring()).c_str(),L"Backup complete",MB_OK);}
                if(id==Guide)MessageBoxW(h,L"Host: Open lobby, copy invite, wait for Ready, Start game.\nJoin: paste an invite or choose a LAN/Tailscale host, Join, Ready.\nTab moves focus; Enter confirms; Esc returns to Play.\nCo-op saves are separate. Back up with the game closed.\nThis development build exchanges read-only presence.\nNo internet matchmaking, Steam friends, voice or shared story yet.",L"Quick guide",MB_OK);
                if(id==Home)ShellExecuteW(h,L"open",L"https://github.com/TommoDotCommo/death-stranding-coop",nullptr,nullptr,SW_SHOWNORMAL);
            }return 0;
        }
        if(msg==WM_CLOSE){if(net->lobby&&MessageBoxW(h,net->hosting?L"Close the lobby? Your friend will need to join again.":L"Leave the lobby? The host can keep playing.",L"Leave co-op",MB_YESNO)!=IDYES)return 0;net->leave();DestroyWindow(h);return 0;}
        if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    }catch(const std::exception& e){net->launch=false;joinAfterScan=false;error(e);}
    return DefWindowProcW(h,msg,wp,lp);
}
void buildControls(){
    tab=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,nullptr,nullptr,nullptr);SendMessageW(tab,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(auto name:{L"Play",L"Saves",L"Settings",L"About"}){TCITEMW t{};t.mask=TCIF_TEXT;t.pszText=const_cast<wchar_t*>(name);TabCtrl_InsertItem(tab,TabCtrl_GetItemCount(tab),&t);}
    auto title=CreateWindowExW(0,L"STATIC",L"DEATH STRANDING  /  CO-OP",WS_CHILD|WS_VISIBLE,25,18,710,32,window,nullptr,nullptr,nullptr);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
    label(0,200,L"Your name");control(0,Name,L"EDIT",player.c_str(),ES_AUTOHSCROLL);SendMessageW(child(Name),EM_SETLIMITTEXT,32,0);
    control(0,Host,L"BUTTON",L"Host a game",BS_AUTORADIOBUTTON|WS_GROUP);control(0,Join,L"BUTTON",L"Join a friend",BS_AUTORADIOBUTTON);control(0,Solo,L"BUTTON",L"Play solo",BS_AUTORADIOBUTTON);
    label(0,201,L"Paste an invite code, link, address or Tailscale name");control(0,Paste,L"EDIT",L"",ES_AUTOHSCROLL);SendMessageW(child(Paste),EM_SETLIMITTEXT,CR_INPUT_MAX,0);
    label(0,202,L"LAN and Tailscale hosts appear below.");control(0,Rows,L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_BORDER);label(0,Status,L"");control(0,Primary,L"BUTTON",L"Open lobby",BS_DEFPUSHBUTTON);
    label(0,203,L"");control(0,Invite,L"EDIT",L"",ES_READONLY|ES_MULTILINE);control(0,Copy,L"BUTTON",L"Copy invite");control(0,Leave,L"BUTTON",L"Leave lobby");control(0,Ready,L"BUTTON",L"Ready");
    label(1,210,L"Co-op saves\nOnly the separate co-op save trees are included in backups. Close the game first.");
    label(1,211,(L"Co-op data:\n"+options.data.wstring()+L"\n\nSave trees: redirect-data\\local, roaming, documents, savedgames, profile\n\nBackups: backups\\<time>\nThe launcher never opens your ordinary game saves.").c_str());
    control(1,OpenSaves,L"BUTTON",L"Open co-op save folder");control(1,BackUp,L"BUTTON",L"Back up co-op saves");
    label(2,220,L"Launcher / game folder (applies next start)\nSteam mode must use the folder registered with Steam.");control(2,GamePath,L"EDIT",gameFolder.c_str(),ES_AUTOHSCROLL);
    label(2,221,L"Network / host UDP port (49000..49014)");control(2,Port,L"EDIT",std::to_wstring(port).c_str(),ES_NUMBER|ES_AUTOHSCROLL);
    control(2,Lan,L"BUTTON",L"Find games on my network",BS_AUTOCHECKBOX);control(2,Tailscale,L"BUTTON",L"Find friends on Tailscale",BS_AUTOCHECKBOX);control(2,Privacy,L"BUTTON",L"Invite only (host decides)",BS_AUTOCHECKBOX);
    SendMessageW(child(Lan),BM_SETCHECK,lan?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(child(Tailscale),BM_SETCHECK,tailscale?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(child(Privacy),BM_SETCHECK,privacy?BST_CHECKED:BST_UNCHECKED,0);
    control(2,Protocol,L"BUTTON",L"Open deathstranding-coop invite links in this launcher");label(2,222,L"Settings are saved on change. Each game uses two adjacent UDP ports.\nCo-op networking is always enforced offline. Other co-op rules, voice, controller navigation and router mapping are not available yet.");
    label(3,230,(L"Death Stranding co-op 0.1.0-dev\nSteam Director's Cut build 13300582\n\nExperimental; not a playable release. Native Windows launcher.\nCo-op mod standard: Play, Saves, Settings, About; DS invites and LAN/Tailscale discovery.\n\nInvite/discovery reference code: coop-mod-kit (MIT).\nNot affiliated with Kojima Productions or the game's publishers.\n\nWire version: "+wtext(wireVersion())).c_str());
    control(3,Guide,L"BUTTON",L"Quick guide (F1)");control(3,Home,L"BUTTON",L"Project and issue tracker");
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    try {
        SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);WSADATA ws;if(WSAStartup(MAKEWORD(2,2),&ws))return 1;net=std::make_unique<Network>();
        options.data=known(FOLDERID_LocalAppData)/"DeathStrandingCoop";settingsFile=options.data/"launcher.ini";
        auto read=[&](const wchar_t* key,const wchar_t* fallback){wchar_t b[32768];GetPrivateProfileStringW(L"launcher",key,fallback,b,32768,settingsFile.c_str());return std::wstring(b);};
        player=read(L"name",L"Porter");gameFolder=read(L"game",steamGame().c_str());
        choice=int(GetPrivateProfileIntW(L"launcher",L"choice",0,settingsFile.c_str()));if(choice<0||choice>2)choice=0;
        port=GetPrivateProfileIntW(L"launcher",L"port",49000,settingsFile.c_str());if(port<49000||port>49014)port=49000;
        lan=GetPrivateProfileIntW(L"launcher",L"lan",1,settingsFile.c_str());tailscale=GetPrivateProfileIntW(L"launcher",L"tailscale",1,settingsFile.c_str());privacy=GetPrivateProfileIntW(L"launcher",L"privacy",0,settingsFile.c_str());
        bool registerOnly=false,devStart=false;std::string devPeer="127.0.0.1";uint32_t devSession=1;
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Cannot read launcher arguments.");
        for(int i=1;i<argc;++i){std::wstring a=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing launcher option value.");return std::wstring(argv[i]);};
            if(a==L"--dev-direct")options.direct=true;
            else if(a==L"--game-dir")gameFolder=value();
            else if(a==L"--data-dir")options.data=value();
            else if(a==L"--prepare-only")options.prepareOnly=true;
            else if(a==L"--register-protocol")registerOnly=true;
            else if(a==L"--dev-start")devStart=true;
            else if(a==L"--dev-peer")devPeer=utf8(value());
            else if(a==L"--dev-session"){auto v=utf8(value());if(v.empty()||v.size()>10||v.find_first_not_of("0123456789")!=std::string::npos||std::stoull(v)>UINT32_MAX)throw std::runtime_error("Invalid development session.");devSession=uint32_t(std::stoull(v));}
            else if(a==L"--dev-client")choice=1;
            else if(a==L"--port"){auto raw=utf8(value());if(raw.size()!=5||raw.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Host port must be 49000..49014.");auto v=std::stoul(raw);if(v<49000||v>49014)throw std::runtime_error("Host port must be 49000..49014.");port=unsigned(v);}
            else if(a.rfind(L"deathstranding-coop://",0)==0){pasted=utf8(a);choice=1;}
            else throw std::runtime_error("Unknown launcher option.");
        }LocalFree(argv);
        auto defaultData=known(FOLDERID_LocalAppData)/"DeathStrandingCoop";
        if(!options.direct&&!equalPath(options.data,defaultData))throw std::runtime_error("Custom data folders require --dev-direct.");
        if(devStart&&!options.direct)throw std::runtime_error("--dev-start requires --dev-direct.");
        if(options.direct&&equalPath(options.data,defaultData)&&!environment(L"COOP_MODULE_INSTANCE").empty())options.data=environment(L"COOP_MODULE_INSTANCE");
        wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);executable=self;
        if(registerOnly){registerHandler(executable);return 0;}
        std::ifstream recents(options.data/"recents.tsv",std::ios::binary);char store[CD_RECENT_BYTES+1];recents.read(store,sizeof(store));auto count=recents.gcount();if(count<=CD_RECENT_BYTES)cd_recents_decode(&net->recents,store,size_t(count));
        settingsFile=options.data/"launcher.ini";
        options.game=gameFolder;
        if(devStart){Session s=chosenSession();s.host=choice!=1;s.peer=devPeer;s.secret=devSession;launchGame(options,s);return 0;}
        INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TAB_CLASSES};InitCommonControlsEx(&cc);
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");titleFont=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"DeathStrandingCoopLauncher";cls.lpfnWndProc=procedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
        window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Death Stranding co-op",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,960,740,nullptr,nullptr,instance,nullptr);if(!window)throw std::runtime_error("Cannot create launcher window.");
        buildControls();if(!pasted.empty())set(Paste,wtext(pasted));initializing=false;render();ShowWindow(window,showCommand);SetTimer(window,1,100,nullptr);scan(true);
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){
            try{
                if(msg.message==WM_KEYDOWN&&msg.wParam==VK_F1){SendMessageW(window,WM_COMMAND,Guide,0);continue;}
                if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){if(net->lobby&&MessageBoxW(window,L"Leave this lobby?",L"Leave co-op",MB_YESNO)!=IDYES)continue;if(net->lobby)SendMessageW(window,WM_COMMAND,Leave,0);switchTab(0);continue;}
                if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN){auto focus=GetFocus();if(selectedTab==0&&(focus==child(Name)||focus==child(Paste)||focus==child(Rows))){primary();continue;}}
                if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            }catch(const std::exception& e){error(e);}
        }
        net.reset();DeleteObject(font);DeleteObject(titleFont);WSACleanup();CoUninitialize();return 0;
    }catch(const std::exception& e){MessageBoxW(nullptr,wtext(e.what()).c_str(),L"Death Stranding co-op",MB_OK|MB_ICONERROR);return 1;}
}
