#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "net_win.h"
#include "launch_win.h"
#include "firewall_win.h"
#include "strand.h"
#include "owner.h"
#include "testsaves.h"
#include "install.h"
#include "../world/json.h"
#include "../common/crash_handler.h"
#include <sstream>
#include <memory>
// The release version (tools/release.sh passes the tag through the Makefile's VERSION).
#ifndef COOP_VERSION
#define COOP_VERSION "0.1.0-dev"
#endif
using namespace launcher;
namespace {
diag::Session diagnostic;
HWND window,tab;
HFONT font,titleFont;
std::unique_ptr<Network> net;
LaunchOptions options;
fs::path settingsFile,executable;
int selectedTab=0,choice=0;
bool lan=true,tailscale=true,privacy=false,sharedWorld=false,groupHost=false,buildReceived=true,pasteGroup=false,initializing=true,joinAfterScan=false,joinScanStarted=false,remembered=false;
std::string joinQuery;
unsigned port=49000;
std::wstring player=L"Porter",gameFolder;
std::string pasted,lastState,lastRows,saveNote;   // saveNote: what happened to the guest's saves, kept on the lobby page
uint32_t requestedSecret=0,lastScan=0,groupSecret=0;   // groupSecret: the key of the group this player hosts (settings)
uint64_t playerNumber=0;                                // this player's number on a group's relay (core.h playerId; settings)
std::string groupPolicy="friends";                      // the rules the host's launcher gives a new group world (settings)
Session strandSession;bool haveStrand=false;            // the group server this launcher last hosted or joined: "My strand on this server"
world::identity::Keys keys;                             // this install's key pair (player.key beside launcher.ini; core.h playerKeys)
std::vector<HWND> controls[4];
enum {Name=101,Host,Join,Solo,Primary,Paste,Rows,Status,Invite,Copy,Leave,Ready,BackUp,OpenSaves,GamePath,Port,Lan,Tailscale,Privacy,Protocol,Guide,Home,Diagnose,World,HostSave,Import,Remove,GroupHost,GroupPolicy,Strand,Owner,BuildReceived,TestList,UseTest,GetTest};
const char* PolicyNames[]={"friends","solo","public"};
HWND child(int id){return GetDlgItem(window,id);}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,0);GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;}
void set(int id,const std::wstring& s){if(text(child(id))!=s)SetWindowTextW(child(id),s.c_str());}
void error(const std::exception& e){diagnostic.Line(std::string("error: ")+e.what());MessageBoxW(window,wtext(e.what()).c_str(),L"Death Stranding co-op - try again",MB_OK|MB_ICONERROR);}
HWND control(int page,int id,const wchar_t* klass,const wchar_t* label,DWORD style=0){auto h=CreateWindowExW(wcscmp(klass,L"EDIT")==0?WS_EX_CLIENTEDGE:0,klass,label,WS_CHILD|(wcscmp(klass,L"STATIC")?WS_TABSTOP:0)|style,0,0,0,0,window,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);controls[page].push_back(h);return h;}
void label(int page,int id,const wchar_t* s){control(page,id,L"STATIC",s,SS_LEFT);}
void place(int id,int x,int y,int w,int h){MoveWindow(child(id),x,y,w,h,TRUE);}
void show(int id,bool yes){ShowWindow(child(id),yes?SW_SHOW:SW_HIDE);}
// Where this player's co-op saves are, and whose they are. The account is the one signed in to Steam; without Steam
// running it is the only account folder the co-op saves (or, failing that, the ordinary saves) have.
fs::path coopGame(){return saves::gameFolder(options.data/"redirect-data"/"local");}
fs::path ordinaryGame(){return saves::gameFolder(known(FOLDERID_LocalAppData));}
uint32_t steamAccount(){
    DWORD value=0,size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS&&value)return value;
    for(auto& folder:{coopGame(),ordinaryGame()}){auto found=saves::accounts(folder);if(found.size()==1)return found[0];}
    return 0;
}
fs::path coopAccount(){auto id=steamAccount();if(!id)throw std::runtime_error("Your Steam account is not known yet. Start Steam, then try again.");return coopGame()/std::to_string(id);}
std::optional<saves::Checkpoint> ownSave(){try{return saves::newest(coopAccount());}catch(const std::exception&){return std::nullopt;}}
std::string hostSaveLine(){
    if(!net->offer.offering())return "You have no co-op save yet, so the game starts a new story. Saves: Copy my Steam saves.";
    auto mine=ownSave();return "Your friend can start from your save: "+(mine&&!mine->title.empty()?mine->title:std::string("your newest co-op save"));
}
void placeHostSave(){
    try {
        auto aside=options.data/"backups"/("before-join-"+std::to_string(std::time(nullptr)));
        int moved=saves::install(coopAccount(),steamAccount(),net->incoming.bytes(),aside);
        net->saveInstalled();
        saveNote=moved>0?"The host's save is in place. Your "+std::to_string(moved)+" earlier co-op save(s) are in backups\\"+aside.filename().string()+".":"The host's save is in place.";
    }catch(const std::exception& e){net->saveRefused(std::string("The host's save was not used: ")+e.what()+" Choose Ready to play from your own.");}
}
void importSaves(HWND h){
    if(!backupAllowed(net->lobby,gameRunning(gameFolder)))throw std::runtime_error("Leave the lobby and close the game before copying saves.");
    auto id=steamAccount();if(!id)throw std::runtime_error("Your Steam account is not known yet. Start Steam, then try again.");
    auto from=ordinaryGame()/std::to_string(id),to=coopGame()/std::to_string(id);
    bool have=bool(saves::newest(to));
    if(MessageBoxW(h,have?L"Copy your ordinary Death Stranding saves into co-op?\n\nYour current co-op saves move to a backups folder. Your ordinary saves are only read, never changed."
                         :L"Copy your ordinary Death Stranding saves into co-op?\n\nYour ordinary saves are only read, never changed.",L"Copy my Steam saves",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    auto aside=options.data/"backups"/("before-import-"+std::to_string(std::time(nullptr)));
    int copied=saves::importOrdinary(from,to,aside);
    MessageBoxW(h,(std::to_wstring(copied)+L" save(s) copied into co-op. Continue in the game loads the newest."+(have?L"\n\nEarlier co-op saves: "+aside.wstring():L"")).c_str(),L"Copy complete",MB_OK);
}
// Test saves (testsaves.h): the pack beside the launcher (or --test-saves-pack), listed on the Saves tab.
fs::path testPackGiven;                       // --test-saves-pack PATH: a pack somewhere else
std::optional<testsaves::Pack> testPack;std::string testPackNote;
std::optional<fs::path> testPackPath(){if(!testPackGiven.empty())return testPackGiven;return testsaves::find(executable.parent_path());}
std::string testSaveText(int index){
    if(!testPack||index<0||size_t(index)>=testPack->sets.size())return testPackNote;
    auto& s=testPack->sets[size_t(index)];std::string facts;
    if(s.episode)facts+="Episode "+std::to_string(s.episode);
    if(!s.playTime.empty())facts+=(facts.empty()?"":", ")+s.playTime+" played";
    return s.title+(facts.empty()?"":"\n"+facts)+(s.description.empty()?"":"\n\n"+s.description)+(s.source.empty()?"":"\n\nFrom: "+s.source);
}
void showTestSave(){auto i=int(SendMessageW(child(TestList),LB_GETCURSEL,0,0));set(213,wtext(testSaveText(i)));EnableWindow(child(UseTest),testPack&&i>=0);}
void loadTestSaves(){
    testPack.reset();SendMessageW(child(TestList),LB_RESETCONTENT,0,0);
    auto where=testPackPath();
    if(!where)testPackNote="No test saves yet. Get test saves... opens the release page: download deathstranding-coop-test-saves-<version>.zip and put it beside the launcher (no need to extract it).";
    else try{testPack=testsaves::Pack::open(*where);testPackNote="Choose a test save.";}catch(const std::exception& e){testPackNote=e.what();}
    if(testPack)for(auto& s:testPack->sets)SendMessageW(child(TestList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(wtext(s.title).c_str()));
    if(testPack&&!testPack->sets.empty())SendMessageW(child(TestList),LB_SETCURSEL,0,0);
    showTestSave();
}
void useTestSave(HWND h){
    auto i=int(SendMessageW(child(TestList),LB_GETCURSEL,0,0));if(!testPack||i<0||size_t(i)>=testPack->sets.size())return;
    auto set=testPack->sets[size_t(i)];
    if(!backupAllowed(net->lobby,gameRunning(gameFolder)))throw std::runtime_error("Leave the lobby and close the game before using a test save.");
    if(MessageBoxW(h,wtext("Use the test save \""+set.title+"\"?\n\nYour current co-op saves move to a backups folder (never deleted). The test save is signed for your Steam account. Your ordinary Steam saves are not touched.").c_str(),L"Use this test save",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    auto o=testsaves::useCommand(testPack->location(),set.id,options.data,known(FOLDERID_LocalAppData),steamAccount(),backupAllowed(net->lobby,gameRunning(gameFolder)),std::time(nullptr));
    if(o.code!=testsaves::ExitOk)throw std::runtime_error(o.message);
    MessageBoxW(h,wtext(o.message+"\n\nStart the game with Play solo, Host or Join.").c_str(),L"Test save ready",MB_OK);
}
// Setup: the package this launcher was started from goes into the game, by the install logic Setup uses too
// (install.h). Asked once per start while needed. The launcher's own folder stays where it is (Setup's install puts a
// copy into the co-op data folder's app folder).
install::Layout ownLayout(){return install::Layout{executable.parent_path(),gameFolder,options.data,false};}
std::wstring lines(const std::vector<std::string>& v){std::wstring out;for(auto& s:v)out+=L"\n\n"+wtext(s);return out;}
void offerSetup(HWND h){
    if(options.direct||gameFolder.empty())return;
    auto l=ownLayout();
    if(!fs::is_regular_file(l.package/install::ManifestName))return;   // a development build: no package beside the launcher
    auto r=install::status(l);
    if(r.state==install::State::Conflict){MessageBoxW(h,(L"Co-op cannot be installed into this game folder yet."+lines(r.problems)).c_str(),L"Death Stranding co-op setup",MB_OK|MB_ICONWARNING);return;}
    if(r.state!=install::State::NotInstalled&&r.state!=install::State::UpdateAvailable&&r.state!=install::State::NeedsRepair)return;
    std::wstring ask=std::wstring(r.state==install::State::UpdateAvailable?L"Update the co-op mod in your game to this package's version?":r.state==install::State::NeedsRepair?L"Repair the co-op mod in your game?":L"Install the co-op mod into your game?")
        +L"\n\nGame: "+gameFolder+L"\nCo-op data and saves: "+options.data.wstring()
        +L"\n\nIt adds one file (avrt.dll) to the game folder; no file of the game is replaced, and your ordinary saves are not touched.";
    if(MessageBoxW(h,ask.c_str(),L"Death Stranding co-op setup",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    install::install(l,[&]{return gameRunning(gameFolder);});
    MessageBoxW(h,L"Installed. Choose Host, Join or Play solo.\n\nTo remove it later: About, Remove co-op from the game.",L"Death Stranding co-op setup",MB_OK);
}
void removeSetup(HWND h){
    if(net->lobby)throw std::runtime_error("Leave the lobby first.");
    if(MessageBoxW(h,L"Remove the co-op mod from the game folder?\n\nYour co-op saves and settings are kept.",L"Remove co-op",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    auto u=install::uninstall(ownLayout(),[&]{return gameRunning(gameFolder);});
    std::wstring done=L"Removed. The game starts without co-op.";
    if(!u.kept.empty())done=L"Removed, except files changed since they were installed (left in place):"+lines(u.kept);
    if(!u.foreign.empty())done+=L"\n\nLeft alone (not the co-op mod's files):"+lines(u.foreign);
    MessageBoxW(h,done.c_str(),L"Remove co-op",MB_OK);
}
// Setup's verbs, without a window (../dist/deathstranding-coop-setup.ps1 runs them; co-op mod standard J9):
//   --setup status [--json] [<game folder>]   without a folder: every install found in the Steam libraries
//   --setup install <game folder> --yes | --setup uninstall <game folder> --yes
// The package is this launcher's folder; the files go to the game folder and %LOCALAPPDATA%\DeathStrandingCoop, the
// launcher with its package into its app folder. Exit codes as install.h Exit: 0 done, 1 failed, 2 usage, 3 unsupported
// game build, 4 the game is running.
void out(HANDLE h,const std::string& s){DWORD n=0;if(h&&h!=INVALID_HANDLE_VALUE)WriteFile(h,s.data(),DWORD(s.size()),&n,nullptr);}
std::string plain(const install::Report& r){std::string s=std::string(install::stateName(r.state))+" ("+install::button(r.state)+")\n";for(auto& p:r.problems)s+="  "+p+"\n";return s;}
int setupVerb(int argc,wchar_t** argv){
    HANDLE so=GetStdHandle(STD_OUTPUT_HANDLE),se=GetStdHandle(STD_ERROR_HANDLE);
    bool json=false,yes=false;std::vector<std::wstring> rest;fs::path data=known(FOLDERID_LocalAppData)/"DeathStrandingCoop";
    for(int i=2;i<argc;++i){std::wstring a=argv[i];if(a==L"--json")json=true;else if(a==L"--yes")yes=true;
        else if(a==L"--data-dir"&&i+1<argc)data=argv[++i];   // tests only: a scratch data folder instead of the player's
        else rest.push_back(a);}
    auto usage=[&](const char* why){out(se,std::string(why)+"\nUse: --setup status [--json] [<game folder>] | --setup install <game folder> --yes | --setup uninstall <game folder> --yes\n");return int(install::Usage);};
    if(rest.empty())return usage("No setup verb.");
    wchar_t self[32768];if(!GetModuleFileNameW(nullptr,self,32768))return install::Failed;
    install::Layout l{fs::path(self).parent_path(),{},data,true};
    try {
        if(rest[0]==L"status") {
            if(rest.size()>2)return usage("status takes one game folder at most.");
            if(rest.size()==2) {
                l.game=rest[1];auto r=install::status(l);out(so,json?install::reportJson(r)+"\n":plain(r));
                using S=install::State;
                return r.state==S::UnsupportedBuild?install::UnsupportedBuild:r.state==S::UpToDate||r.state==S::NotInstalled||r.state==S::UpdateAvailable||r.state==S::ByModManager?install::Ok:install::Failed;
            }
            std::string list;
            for(auto& g:install::findGames(steamRoots(),{steamGame()})){l.game=g;auto r=install::status(l);list+=json?(list.empty()?"":",")+install::reportJson(r):utf8(g.wstring())+": "+plain(r);}
            out(so,json?"{\"schema\":1,\"games\":["+list+"]}\n":list.empty()?"No DEATH STRANDING DIRECTOR'S CUT found in the Steam libraries.\n":list);
            return install::Ok;
        }
        if(rest[0]==L"install"||rest[0]==L"uninstall") {
            if(rest.size()!=2)return usage("Give the game folder.");
            if(!yes)return usage("install and uninstall need --yes.");
            l.game=rest[1];auto running=[&]{return gameRunning(l.game);};
            if(rest[0]==L"install"){auto r=install::install(l,running);out(so,json?install::reportJson(r)+"\n":plain(r));return install::Ok;}
            auto u=install::uninstall(l,running);
            auto array=[](const std::vector<std::string>& v){json::Value a=json::Value::array();for(auto& s:v)a.push(s);return a;};
            json::Value r=json::Value::object();r.set("removed",array(u.removed));r.set("kept",array(u.kept));r.set("foreign",array(u.foreign));
            out(so,json?r.dump()+"\n":"removed "+std::to_string(u.removed.size())+" files"+(u.kept.empty()?std::string():"; kept (changed since install): "+std::to_string(u.kept.size()))+"\n");
            return u.kept.empty()?install::Ok:install::Failed;
        }
        return usage("Unknown setup verb.");
    }catch(const install::Error& e){out(se,std::string(e.what())+"\n");return e.code;}
    catch(const std::exception& e){out(se,std::string(e.what())+"\n");return install::Failed;}
}
// launcher.ini: the [launcher] settings, and a [server-ID] section per group server with the player's choices there.
std::string readIni(){std::ifstream f(settingsFile,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
void writeSettings(){
    std::string name=utf8(player),game=utf8(gameFolder);
    if(name.find_first_of("\r\n")!=std::string::npos||game.find_first_of("\r\n")!=std::string::npos)return;
    atomicWrite(settingsFile,settingsText(readIni(),{{"name",name},{"game",game},{"choice",std::to_string(choice)},{"port",std::to_string(port)},{"lan",std::to_string(lan)},{"tailscale",std::to_string(tailscale)},
        {"privacy",std::to_string(privacy)},{"world",std::to_string(sharedWorld)},{"group",std::to_string(groupHost)},{"group_secret",std::to_string(groupSecret)},{"player_id",std::to_string(playerNumber)},{"group_policy",groupPolicy},{"build_received",std::to_string(buildReceived)}}));
}
void saveSettings(){if(!initializing)writeSettings();}
// A headless report (one line of JSON): to standard output for "-", else to that file.
void writeReport(const std::string& where,const std::string& line){
    if(where=="-"){DWORD n=0;HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);if(out&&out!=INVALID_HANDLE_VALUE)WriteFile(out,line.data(),DWORD(line.size()),&n,nullptr);return;}
    std::ofstream f(where,std::ios::binary|std::ios::trunc);f<<line;if(!f)throw std::runtime_error("Cannot write the report file.");}
// Group play: this launcher's player number, made once and kept (never the Steam id), and the key of the group this
// player hosts, made the first time it is needed and kept, so the group's invite stays good between sessions.
uint64_t settingNumber(const wchar_t* key){wchar_t b[32];GetPrivateProfileStringW(L"launcher",key,L"",b,32,settingsFile.c_str());auto v=utf8(b);
    return !v.empty()&&v.size()<=15&&v.find_first_not_of("0123456789")==std::string::npos?std::stoull(v):0;}
void identity(){
    playerNumber=settingNumber(L"player_id");auto key=settingNumber(L"group_secret");groupSecret=key<=UINT32_MAX?uint32_t(key):0;
    if(!playerIdValid(playerNumber)){playerNumber=playerId((uint64_t(random32())<<32)|random32());writeSettings();}
    keys=playerKeys(options.data/"player.key");   // made once; on servers that take keys it is this player
}
// A group server as this launcher talks to it: the account the game signs in as, and this install's key for signing.
strand::Target targetOf(const Session& s){auto t=strand::target(s,steamAccount());t.keys=&keys;return t;}
// The host's own game on the server his launcher just started: on a server that takes keys, its ticket and relay token.
void hostIdentity(Session& s){
    auto t=targetOf(s);strand::Passes p;bool keyed=false;std::string why;
    if(!strand::hostPasses(t,strand::transport(t),options.prepareOnly?1:20,p,keyed,why)){if(options.prepareOnly)return;throw std::runtime_error(why);}
    if(keyed)strand::usePasses(s,p);
    strand::useNoticeToken(s,p);   // what his game is told about his builds there, when the server gave a token
}
uint32_t groupKey(){if(!groupSecret){groupSecret=random32();writeSettings();}return groupSecret;}
// Windows Firewall's verdict for the game's two UDP ports, read when the launcher starts and when a lobby opens.
Firewall firewall=Firewall::Unknown;
void readFirewallNow(){if(!gameFolder.empty())firewall=readFirewall(fs::path(gameFolder)/"ds.exe",{port,port+1});}
Session chosenSession(){Session s;s.name=utf8(player);s.port=port;s.sharedWorld=sharedWorld;s.playerId=playerNumber;s.policy=groupPolicy;s.buildReceived=buildReceived;return s;}   // a guest's choice is replaced by the host's (net_win.h)
void render();
// ---- A group server's rules (strand.h): the join screen and "My strand on this server", each a window of its own
// over the launcher, which is disabled while one is open. Built without being seen on a screen (README).
struct Popup {HWND h=nullptr;bool done=false;int result=0;std::function<void(int)> command;};
Popup* popup=nullptr;
LRESULT CALLBACK popupProcedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    try{
        if(msg==WM_COMMAND&&popup&&popup->h==h&&HIWORD(wp)==BN_CLICKED){popup->command(LOWORD(wp));return 0;}
        if(msg==WM_CLOSE&&popup&&popup->h==h){popup->done=true;return 0;}
    }catch(const std::exception& e){MessageBoxW(h,wtext(e.what()).c_str(),L"Death Stranding co-op - try again",MB_OK|MB_ICONERROR);}
    return DefWindowProcW(h,msg,wp,lp);
}
HWND popupControl(HWND parent,int id,const wchar_t* klass,const wchar_t* label,DWORD style,int x,int y,int w,int h){
    bool framed=wcscmp(klass,L"EDIT")==0||wcscmp(klass,L"LISTBOX")==0;
    auto c=CreateWindowExW(framed?WS_EX_CLIENTEDGE:0,klass,label,WS_CHILD|WS_VISIBLE|(wcscmp(klass,L"STATIC")?WS_TABSTOP:0)|style,x,y,w,h,parent,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;}
// One popup until it is closed (Esc or the close box: result 0). Its owner (the launcher, or the popup it was opened
// from) waits meanwhile.
int runPopup(Popup& p,const wchar_t* title,int width,int height,const std::function<void(HWND)>& build){
    static bool registered=false;
    if(!registered){WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DeathStrandingCoopPopup";cls.lpfnWndProc=popupProcedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);registered=true;}
    HWND owner=popup?popup->h:window;RECT r;GetWindowRect(owner,&r);
    p.h=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_DLGMODALFRAME,L"DeathStrandingCoopPopup",title,WS_POPUP|WS_CAPTION|WS_SYSMENU,r.left+30,r.top+30,width,height,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!p.h)throw std::runtime_error("Cannot open the window.");
    auto previous=popup;popup=&p;
    try{build(p.h);}catch(...){popup=previous;DestroyWindow(p.h);throw;}
    EnableWindow(owner,FALSE);ShowWindow(p.h,SW_SHOW);
    MSG msg{};BOOL got=1;
    while(!p.done&&(got=GetMessageW(&msg,nullptr,0,0))>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){p.done=true;p.result=0;continue;}
        if(!IsDialogMessageW(p.h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    EnableWindow(owner,TRUE);DestroyWindow(p.h);popup=previous;SetForegroundWindow(owner);
    if(got==0)PostQuitMessage(int(msg.wParam));   // the launcher was asked to close meanwhile
    return p.result;
}
// The join screen, and "My choices": the server's rules in words, what the player has there, his two choices for
// this server. true: he chose (in `c`); false: he cancelled.
bool choicesScreen(const strand::Policy& rules,const std::string& mine,strand::Choices& c,const wchar_t* go){
    enum {Rules=401,BuildYes,BuildNo,BuildAsk,OldYes,OldNo,Go,Cancel};
    Popup pop;auto checked=[&](int id){return SendMessageW(GetDlgItem(pop.h,id),BM_GETCHECK,0,0)==BST_CHECKED;};
    pop.command=[&](int id){
        if(id==Go){c.offerBuilds=checked(BuildYes)?"yes":checked(BuildNo)?"no":"ask";c.offerOld=checked(OldYes)?"yes":"no";pop.result=1;pop.done=true;}
        if(id==Cancel)pop.done=true;};
    return runPopup(pop,L"Before you join: this server's rules",800,650,[&](HWND h){
        popupControl(h,Rules,L"EDIT",wtext(rules.text()+"\r\n\r\n"+mine).c_str(),ES_READONLY|ES_MULTILINE|WS_VSCROLL,20,20,744,300);
        popupControl(h,0,L"STATIC",L"Share what I build here:",SS_LEFT,20,335,744,24);
        popupControl(h,BuildYes,L"BUTTON",L"Yes: the other players can be handed it, as the rules above say",BS_AUTORADIOBUTTON|WS_GROUP,40,362,720,26);
        popupControl(h,BuildNo,L"BUTTON",L"No: keep it for me alone (a personal archive on this server)",BS_AUTORADIOBUTTON,40,390,720,26);
        popupControl(h,BuildAsk,L"BUTTON",L"Ask me: it waits until I offer it (Play, My strand on this server)",BS_AUTORADIOBUTTON,40,418,720,26);
        popupControl(h,0,L"STATIC",L"Let this server take my older strand (the structures already in my save):",SS_LEFT,20,456,744,24);
        popupControl(h,OldYes,L"BUTTON",L"Yes: my game uploads them to this server one by one while I play",BS_AUTORADIOBUTTON|WS_GROUP,40,483,720,26);
        popupControl(h,OldNo,L"BUTTON",L"No: only what I build while connected goes to this server",BS_AUTORADIOBUTTON,40,511,720,26);
        popupControl(h,Go,L"BUTTON",go,BS_DEFPUSHBUTTON|WS_GROUP,20,555,480,40);popupControl(h,Cancel,L"BUTTON",L"Cancel",0,520,555,244,40);
        SendMessageW(GetDlgItem(h,c.offerBuilds=="no"?BuildNo:c.offerBuilds=="ask"?BuildAsk:BuildYes),BM_SETCHECK,BST_CHECKED,0);
        SendMessageW(GetDlgItem(h,c.offerOld=="yes"?OldYes:OldNo),BM_SETCHECK,BST_CHECKED,0);SetFocus(GetDlgItem(h,Go));})==1;
}
// Before a joiner's game connects: the server's rules and his choices for it (remembered per server, sent to it).
// false: he cancelled, nothing is started. An older server without rules to show is said so and joined.
bool agreeToServer(Session& s){
    auto t=targetOf(s);
    auto send=strand::transport(t);strand::Policy rules;auto fetched=strand::fetchPolicy(t,send,rules);
    if(fetched==strand::Fetch::Old){
        if(t.account.empty())throw std::runtime_error("Your Steam account is not known yet. Start Steam, then try again.");
        MessageBoxW(window,wtext(strand::fetchText(fetched)).c_str(),L"Group server",MB_OK|MB_ICONINFORMATION);return true;}
    if(fetched!=strand::Fetch::Ok)throw std::runtime_error(strand::fetchText(fetched));
    {std::string why;if(!strand::identify(t,rules,int64_t(std::time(nullptr)),why))throw std::runtime_error(why);}   // a server that takes keys: as this install's key
    if(t.account.empty())throw std::runtime_error("Your Steam account is not known yet. Start Steam, then try again.");
    strand::Me me;std::string why;auto chosen=strand::remembered(readIni(),rules);
    std::string mine=strand::fetchMe(t,send,me,why)?me.text():why;
    if(me.known&&!me.offerBuilds.empty())chosen.offerBuilds=me.offerBuilds;   // what the server has for him, when he chose there before
    if(me.known&&!me.offerOld.empty())chosen.offerOld=me.offerOld;
    if(!choicesScreen(rules,mine,chosen,L"Join with these choices"))return false;
    auto j=strand::join(t,send,readIni(),chosen);atomicWrite(settingsFile,j.ini);
    if(!j.start)throw std::runtime_error(j.message);
    if(j.keyed)strand::usePasses(s,j.passes);   // the game signs in with its ticket and says HELLO with its relay token
    strand::useNoticeToken(s,j.passes);         // and is told what becomes of his builds there
    saveNote=j.message;return true;
}
// "Who I see": who of the players near him in the game the server's relay shows him (everyone near, at most the
// server's cap; only his friends; nobody; or his game's own setting) and his friend list on this server, a friend
// added by the short form of his key. Each change is one call (strand.h changeFriends), signed under identity keys.
void friendsView(const strand::Target& t,const strand::Transport& send){
    enum {Info=701,Everyone,FriendsOnly,Nobody,Default,SaveChoice,List,Key,Add,Remove,Note,Close};
    Popup pop;strand::Friends now;bool shown=false;
    auto item=[&](int id){return GetDlgItem(pop.h,id);};
    auto note=[&](const std::string& text){SetWindowTextW(item(Note),wtext(text).c_str());};
    auto show=[&](const strand::Friends& f){
        now=f;SendMessageW(item(List),LB_RESETCONTENT,0,0);
        for(auto& x:f.list){auto w=wtext(x.text());SendMessageW(item(List),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(w.c_str()));}
        for(int id:{Everyone,FriendsOnly,Nobody,Default})SendMessageW(item(id),BM_SETCHECK,BST_UNCHECKED,0);
        SendMessageW(item(f.show=="everyone"?Everyone:f.show=="friends"?FriendsOnly:f.show=="nobody"?Nobody:Default),BM_SETCHECK,BST_CHECKED,0);};
    auto refresh=[&]{
        std::string why;shown=strand::fetchFriends(t,send,now,why);
        if(shown){show(now);note(now.list.empty()?"No friends on this server yet. Add one by the short form of his key (he finds it here too).":std::to_string(now.list.size())+" friends on this server.");}
        else note(why);
        for(int id:{Everyone,FriendsOnly,Nobody,Default,SaveChoice,Add,Remove})EnableWindow(item(id),shown);};
    auto change=[&](const std::vector<std::string>& add,const std::vector<std::string>& remove,const std::string& choice,const std::string& done){
        strand::Friends f;std::string why;
        if(!strand::changeFriends(t,send,add,remove,choice,f,why))throw std::runtime_error("The server did not take it: "+why);
        show(f);note(done);};
    pop.command=[&](int id){
        auto checked=[&](int x){return SendMessageW(item(x),BM_GETCHECK,0,0)==BST_CHECKED;};
        if(id==SaveChoice)change({},{},checked(Everyone)?"everyone":checked(FriendsOnly)?"friends":checked(Nobody)?"nobody":"default","Saved: the server applies it from your game's next HELLO (within a second).");
        if(id==Add){
            wchar_t typed[80]{};GetWindowTextW(item(Key),typed,79);std::string text;for(wchar_t c:std::wstring(typed))if(c<128)text+=char(c);
            auto key=strand::shortKeyText(text);
            if(key.empty())throw std::runtime_error("Type the short form of your friend's key: 16 letters and digits, as \"a1b2 c3d4 e5f6 a7b8\".");
            change({key},{},"","Added.");SetWindowTextW(item(Key),L"");}
        if(id==Remove){
            int at=int(SendMessageW(item(List),LB_GETCURSEL,0,0));
            if(at<0||size_t(at)>=now.list.size())throw std::runtime_error("Select a friend first.");
            change({},{now.list[size_t(at)].key},"","Removed.");}
        if(id==Close)pop.done=true;};
    runPopup(pop,L"Who I see on this server",760,640,[&](HWND h){
        std::string mine=t.sign&&t.keys?"Your key's short form, for your friends: "+world::identity::shortText(t.keys->pub)+".":"This server has no player keys: friends are recognised on servers that do (identity keys).";
        popupControl(h,Info,L"STATIC",wtext("Of the players near you in the game, the server shows you:\r\n"+mine).c_str(),SS_LEFT,20,16,704,44);
        popupControl(h,Everyone,L"BUTTON",L"Everyone near (at most the server's number, your friends first, then the nearest)",BS_AUTORADIOBUTTON|WS_GROUP,40,66,690,26);
        popupControl(h,FriendsOnly,L"BUTTON",L"Only my friends",BS_AUTORADIOBUTTON,40,94,690,26);
        popupControl(h,Nobody,L"BUTTON",L"Nobody (they still see you)",BS_AUTORADIOBUTTON,40,122,690,26);
        popupControl(h,Default,L"BUTTON",L"My game's own setting (show_others in its config; everyone when it has none)",BS_AUTORADIOBUTTON,40,150,690,26);
        popupControl(h,SaveChoice,L"BUTTON",L"Save this choice",WS_GROUP,40,184,260,34);
        popupControl(h,0,L"STATIC",L"My friends on this server:",SS_LEFT,20,232,704,24);
        popupControl(h,List,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,20,258,704,200);
        popupControl(h,Key,L"EDIT",L"",ES_AUTOHSCROLL,20,468,300,30);
        popupControl(h,Add,L"BUTTON",L"Add by short key",0,330,466,190,34);popupControl(h,Remove,L"BUTTON",L"Remove selected",0,530,466,194,34);
        popupControl(h,Note,L"STATIC",L"",SS_LEFT,20,510,704,44);
        popupControl(h,Close,L"BUTTON",L"Close",BS_DEFPUSHBUTTON,530,560,194,36);
        refresh();});
}
// "My strand on this server": what the player built on the group server he last hosted or joined, its state and
// likes; his offer and withdrawal of the selected rows or of all; his choices there.
void strandView(){
    if(!haveStrand)throw std::runtime_error("Host or join a group server first.");
    auto t=targetOf(strandSession);
    auto send=strand::transport(t);
    enum {Info=501,List,Note,OfferSelected,WithdrawSelected,OfferAll,WithdrawAll,Mine,Refresh,Close,WhoISee};
    Popup pop;strand::Policy rules;bool shown=false;strand::Me me;std::vector<strand::Object> rows;
    auto item=[&](int id){return GetDlgItem(pop.h,id);};
    auto note=[&](const std::string& text){SetWindowTextW(item(Note),wtext(text).c_str());};
    auto refresh=[&]{
        std::string why;auto fetched=strand::fetchPolicy(t,send,rules);shown=fetched==strand::Fetch::Ok;std::string info=shown?rules.text():strand::fetchText(fetched);
        if(shown&&!strand::identify(t,rules,int64_t(std::time(nullptr)),why)){shown=false;info+="\r\n\r\n"+why;}
        if(shown&&t.account.empty()){shown=false;info+="\r\n\r\nYour Steam account is not known yet. Start Steam, then try again.";}
        me=strand::Me();
        if(shown&&strand::fetchMe(t,send,me,why))info+="\r\n\r\n"+me.text()+(me.known?"\r\nYour choices here: "+strand::choicesText({me.offerBuilds.empty()?rules.value("offer_builds"):me.offerBuilds,me.offerOld.empty()?rules.value("offer_old"):me.offerOld})+".":"");
        SetWindowTextW(item(Info),wtext(info).c_str());
        SendMessageW(item(List),LB_RESETCONTENT,0,0);rows.clear();
        if(shown&&strand::fetchMine(t,send,rows,why)){for(auto& o:rows){auto w=wtext(o.text());SendMessageW(item(List),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(w.c_str()));}
            note(rows.empty()?"Nothing you built is on this server yet.":std::to_string(rows.size())+" of your objects on this server. Select rows, then Offer or Withdraw.");}
        else note(shown?why:"");
        for(int id:{OfferSelected,WithdrawSelected,OfferAll,WithdrawAll,Mine,WhoISee})EnableWindow(item(id),shown);};
    auto act=[&](const char* call,bool all){
        std::vector<std::string> ids;
        if(all)ids.push_back("");
        else{int n=int(SendMessageW(item(List),LB_GETSELCOUNT,0,0));std::vector<int> at(size_t(n>0?n:0));
            if(n>0)SendMessageW(item(List),LB_GETSELITEMS,WPARAM(n),reinterpret_cast<LPARAM>(at.data()));
            for(int i:at)if(i>=0&&size_t(i)<rows.size())ids.push_back(rows[size_t(i)].id);}
        if(ids.empty())throw std::runtime_error("Select one or more rows first.");
        if(std::string(call)=="withdraw"&&MessageBoxW(pop.h,rules.value("withdrawal")=="stay"
                ?L"Take these back from this server?\n\nWhat you shared stays for the other players as a remnant under your name that nobody owns, not even you. The rest is removed. The server does not touch your save."
                :L"Take these back from this server?\n\nThey are removed for every player. The server does not touch your save.",L"Withdraw",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
        std::string said;
        for(auto& id:ids){std::string result;strand::act(t,send,call,id,result);said+=(said.empty()?"":" ")+result;}
        refresh();note(said);};
    pop.command=[&](int id){
        if(id==OfferSelected)act("offer",false);
        if(id==WithdrawSelected)act("withdraw",false);
        if(id==OfferAll)act("offer",true);
        if(id==WithdrawAll)act("withdraw",true);
        if(id==Refresh)refresh();
        if(id==Close)pop.done=true;
        if(id==WhoISee)friendsView(t,send);
        if(id==Mine){
            auto chosen=strand::remembered(readIni(),rules);
            if(me.known&&!me.offerBuilds.empty())chosen.offerBuilds=me.offerBuilds;
            if(me.known&&!me.offerOld.empty())chosen.offerOld=me.offerOld;
            if(!choicesScreen(rules,me.text(),chosen,L"Save my choices for this server"))return;
            auto j=strand::join(t,send,readIni(),chosen,false);atomicWrite(settingsFile,j.ini);refresh();note(j.message);
            if(!j.start)throw std::runtime_error(j.message);}};
    runPopup(pop,L"My strand on this server",860,700,[&](HWND h){
        popupControl(h,Info,L"EDIT",L"",ES_READONLY|ES_MULTILINE|WS_VSCROLL,20,20,804,210);
        popupControl(h,List,L"LISTBOX",L"",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,20,240,804,250);
        popupControl(h,Note,L"STATIC",L"",SS_LEFT,20,498,804,44);
        popupControl(h,OfferSelected,L"BUTTON",L"Offer selected",0,20,550,195,36);popupControl(h,WithdrawSelected,L"BUTTON",L"Withdraw selected",0,223,550,195,36);
        popupControl(h,OfferAll,L"BUTTON",L"Offer all",0,426,550,195,36);popupControl(h,WithdrawAll,L"BUTTON",L"Withdraw all",0,629,550,195,36);
        popupControl(h,Mine,L"BUTTON",L"My choices for this server",0,20,600,195,36);popupControl(h,WhoISee,L"BUTTON",L"Who I see",0,223,600,195,36);
        popupControl(h,Refresh,L"BUTTON",L"Refresh",0,426,600,195,36);
        popupControl(h,Close,L"BUTTON",L"Close",BS_DEFPUSHBUTTON,629,600,195,36);
        refresh();});
}
// "Server owner": the owner's actions on the group server this launcher hosts and gave its key as the owner's
// (owner.h): the players with their counts, members, removing all of a player's objects, the review queue and the
// rules, each a call signed with this install's key. Built without being seen on a screen (README).
void ownerView(){
    if(!haveStrand||!owner::available(strandSession,keys))throw std::runtime_error("Host a group server from this launcher first: only its owner key makes these calls.");
    auto t=targetOf(strandSession);auto send=strand::transport(t);
    {strand::Policy first;std::string why;if(!owner::connect(t,send,first,why))throw std::runtime_error(why);}
    enum {Info=601,Players,MemberYes,MemberNo,AcceptPlayer,RejectPlayer,RemovePlayer,Queue,AcceptSelected,RejectSelected,Rules,Value,SetRule,Preset,UsePreset,Note,Refresh,Close};
    Popup pop;owner::View view;bool shown=false;
    auto item=[&](int id){return GetDlgItem(pop.h,id);};
    auto note=[&](const std::string& text){SetWindowTextW(item(Note),wtext(text).c_str());};
    auto fill=[&](int id,const std::vector<std::string>& rows){SendMessageW(item(id),LB_RESETCONTENT,0,0);for(auto& r:rows){auto w=wtext(r);SendMessageW(item(id),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(w.c_str()));}};
    auto refresh=[&]{
        std::string why;shown=owner::fetch(t,send,view,why);
        std::string info=shown?"Server "+(view.name.empty()?std::string("without a name"):"\""+view.name+"\"")+" ("+view.policy.server+"), "+view.policy.preset+" preset, "
                               +std::to_string(view.policy.players)+" of "+std::to_string(view.policy.limit)+" players. You are its owner: every action here is a call signed with your key."
                         :why;
        SetWindowTextW(item(Info),wtext(info).c_str());
        std::vector<std::string> rows;
        if(shown)for(auto& p:view.players)rows.push_back(p.text());
        fill(Players,rows);rows.clear();
        if(shown)for(auto& w:view.waiting)rows.push_back(w.text());
        fill(Queue,rows);
        fill(Rules,shown?owner::rules(view.policy):std::vector<std::string>());
        for(int id:{MemberYes,MemberNo,AcceptPlayer,RejectPlayer,RemovePlayer,AcceptSelected,RejectSelected,SetRule,UsePreset})EnableWindow(item(id),shown);};
    auto player=[&]()->const owner::Player&{
        int i=int(SendMessageW(item(Players),LB_GETCURSEL,0,0));
        if(i<0||size_t(i)>=view.players.size())throw std::runtime_error("Select a player first.");
        return view.players[size_t(i)];};
    auto run=[&](const owner::Action& a){auto done=owner::act(t,send,a);refresh();note(done.message);};
    pop.command=[&](int id){
        if(id==Close){pop.done=true;return;}
        if(id==Refresh){refresh();note("");return;}
        if(id==MemberYes||id==MemberNo||id==AcceptPlayer){
            auto& p=player();if(!p.known)throw std::runtime_error(p.name+" is a builder from a linked server, not a player of this server.");
            run({id==AcceptPlayer?"accept-player":"member",p.id,id==AcceptPlayer?"":id==MemberYes?"yes":"no","",{}});}
        if(id==RejectPlayer){
            auto& p=player();
            if(MessageBoxW(pop.h,wtext("Reject everything of "+p.name+" that waits for you?\n\nIt is kept for him alone, not removed, and nobody else is handed it.").c_str(),L"Reject all of a player",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
            run({"reject-player",p.id,"","",{}});}
        if(id==RemovePlayer){
            auto& p=player();
            if(MessageBoxW(pop.h,wtext(owner::removalText(view.policy,p)).c_str(),L"Remove all of a player's objects",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
            run({"remove",p.id,"","",{}});}
        if(id==AcceptSelected||id==RejectSelected){
            int n=int(SendMessageW(item(Queue),LB_GETSELCOUNT,0,0));std::vector<int> at(size_t(n>0?n:0));
            if(n>0)SendMessageW(item(Queue),LB_GETSELITEMS,WPARAM(n),reinterpret_cast<LPARAM>(at.data()));
            std::vector<owner::Waiting> chosen;for(int i:at)if(i>=0&&size_t(i)<view.waiting.size())chosen.push_back(view.waiting[size_t(i)]);
            if(chosen.empty())throw std::runtime_error("Select one or more objects first.");
            std::string said;int skipped=0;
            for(auto& w:chosen){
                if(w.waitingFor=="builder"){++skipped;continue;}   // its builder answers first, not the owner
                auto done=owner::act(t,send,{id==AcceptSelected?"accept":"reject",w.id,"","",{}});said+=(said.empty()?"":" ")+done.message;}
            if(skipped)said+=(said.empty()?"":" ")+std::to_string(skipped)+" wait for their builder's answer first: not yours to answer.";
            refresh();note(said);}
        if(id==SetRule){
            int i=int(SendMessageW(item(Rules),LB_GETCURSEL,0,0));
            if(i<0||size_t(i)>=view.policy.settings.size())throw std::runtime_error("Select a rule first.");
            auto value=utf8(text(item(Value)));if(value.empty())throw std::runtime_error("Type the rule's new value first.");
            run({"policy","","","",{view.policy.settings[size_t(i)].key+"="+value}});}
        if(id==UsePreset){
            auto i=SendMessageW(item(Preset),CB_GETCURSEL,0,0);if(i<0||i>2)throw std::runtime_error("Choose a preset first.");
            if(MessageBoxW(pop.h,wtext(std::string("Use the ")+PolicyNames[i]+" preset? Every setting becomes the preset's; what you changed is lost. It applies to uploads from now on.").c_str(),L"Server rules",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
            run({"policy","","",PolicyNames[i],{}});}};
    runPopup(pop,L"Server owner",940,700,[&](HWND h){
        popupControl(h,Info,L"EDIT",L"",ES_READONLY|ES_MULTILINE,20,15,884,52);
        popupControl(h,0,L"STATIC",L"Players (select one):",SS_LEFT,20,74,884,22);
        popupControl(h,Players,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL,20,96,884,126);
        int x=20;for(auto b:std::initializer_list<std::pair<int,const wchar_t*>>{{MemberYes,L"Make member"},{MemberNo,L"Not a member"},{AcceptPlayer,L"Accept all of player"},{RejectPlayer,L"Reject all of player"},{RemovePlayer,L"Remove all of player..."}}){
            popupControl(h,b.first,L"BUTTON",b.second,0,x,228,170,32);x+=178;}
        popupControl(h,0,L"STATIC",L"Waiting for you, or held by the rules (Shift and Ctrl select several):",SS_LEFT,20,268,884,22);
        popupControl(h,Queue,L"LISTBOX",L"",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL,20,290,884,110);
        popupControl(h,AcceptSelected,L"BUTTON",L"Accept selected",0,20,406,220,32);popupControl(h,RejectSelected,L"BUTTON",L"Reject selected (kept for its builder)",0,248,406,330,32);
        popupControl(h,0,L"STATIC",L"Rules (select one, type its new value, Set):",SS_LEFT,20,446,884,22);
        popupControl(h,Rules,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL,20,468,884,100);
        popupControl(h,Value,L"EDIT",L"",ES_AUTOHSCROLL,20,574,160,30);popupControl(h,SetRule,L"BUTTON",L"Set selected rule",0,188,574,200,30);
        auto combo=popupControl(h,Preset,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,450,574,220,120);
        for(auto name:{L"Friends",L"Solo archive",L"Public"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        popupControl(h,UsePreset,L"BUTTON",L"Use this preset",0,678,574,226,30);
        popupControl(h,Note,L"STATIC",L"",SS_LEFT,20,612,560,44);
        popupControl(h,Refresh,L"BUTTON",L"Refresh",0,590,614,150,36);popupControl(h,Close,L"BUTTON",L"Close",BS_DEFPUSHBUTTON,750,614,154,36);
        SendMessageW(item(Players),LB_SETHORIZONTALEXTENT,1600,0);SendMessageW(item(Queue),LB_SETHORIZONTALEXTENT,1600,0);SendMessageW(item(Rules),LB_SETHORIZONTALEXTENT,2400,0);
        refresh();});
}
// Group play (README "A group on one server"). The host's invite: the group's key, the server's port and this PC's
// addresses. It is the same every session while the key and the addresses stay the same.
bool hostingGroup(){return groupHost&&choice==0&&!net->lobby;}
std::string groupCode(){
    Session s=chosenSession();s.secret=groupKey();auto code=groupInvite(s.secret,worldPort(s),localRoutes());
    if(code.empty())throw std::runtime_error("No LAN or Tailscale IPv4 address is available. Connect to a network and try again.");
    return code;
}
void startGroup(){
    Session s=chosenSession();s.group=Group::Host;s.secret=groupKey();s.ownerKey=world::identity::hex(keys.pub);options.game=gameFolder;
    launchGame(options,s,false,hostIdentity);strandSession=s;haveStrand=true;
    net->status=options.prepareOnly?"Configuration prepared; no game started.":"Group server started on this PC; Steam launch requested using separate co-op saves. Friends can join with the invite at any time.";
}
void joinGroup(const ci_invite& invite){
    auto s=groupJoin(invite,groupRoute(invite,probeServers(invite)),chosenSession());
    saveNote.clear();if(!agreeToServer(s)){net->status="Not joined: you cancelled before the game started.";return;}
    strandSession=s;haveStrand=true;
    options.game=gameFolder;launchGame(options,s);
    net->status=(options.prepareOnly?"Configuration prepared; no game started.":"Joining the group's server at "+s.server+". Steam launch requested using separate co-op saves.")+(saveNote.empty()?"":" "+saveNote);
}
const wchar_t* primaryLabel(){return net->lobby?L"Start game":choice==0?(groupHost?L"Start group server and play":L"Open lobby"):choice==1?(pasteGroup?L"Join group":L"Join selected host"):L"Play solo";}
// The report for a bug report (core.h `diagnostics`), saved in the co-op data folder. The header is what only the
// launcher can tell: its version, the game folder, whether its executable is the supported build, the installed parts.
fs::path saveDiagnostics(){
    std::error_code ec;fs::path game=gameFolder,exe=game/"ds.exe";std::string build="missing",running="unknown";
    if(fs::is_regular_file(exe,ec)){try{build=fileHash(exe)==mod::discovery::PinnedHash?"supported build 13300582":"another build (unsupported)";}catch(const std::exception&){build="unreadable";}}
    if(!gameFolder.empty()){try{running=gameRunning(game)?"yes":"no";}catch(const std::exception&){}}
    std::string head="launcher: Death Stranding co-op 0.1.0-dev\ngame folder: "+(gameFolder.empty()?std::string("(not set)"):utf8(gameFolder))+"\nds.exe: "+build
        +"\navrt.dll in the game folder: "+(fs::is_regular_file(game/"avrt.dll",ec)?"present":"missing")
        +"\nmod.dll: "+(fs::is_regular_file(options.data/"redirect-data/mod/mod.dll",ec)?"present":"missing")
        +"\ngame running: "+running+"\nfirewall: "+firewallText(firewall)+"\nsaved (UTC seconds): "+std::to_string(std::time(nullptr))+"\n";
    auto file=options.data/("diagnostics-"+std::to_string(std::time(nullptr))+".txt");
    atomicWrite(file,diagnostics(options.data,head));return file;
}
// The matching exporter and privacy rules travel inside this executable. No upload.
fs::path saveDiagnosticsZip(){
    auto report=saveDiagnostics();
    auto run=options.data/("diagnostics-run-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    fs::create_directories(run);
    bool keepRun=false;
    auto cleanup=[&]{if(!keepRun){std::error_code ec;fs::remove_all(run,ec);}};
    try {
        for(auto item:{std::pair<int,const char*>{101,"diag.ps1"},{102,"diag-rules.json"}}){
            auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(item.first),RT_RCDATA);
            auto loaded=resource?LoadResource(nullptr,resource):nullptr;
            auto bytes=loaded?LockResource(loaded):nullptr;
            if(!bytes)throw std::runtime_error("Diagnostics exporter is missing. Reinstall the launcher and try Send diagnostics again.");
            atomicWrite(run/item.second,std::string(static_cast<const char*>(bytes),SizeofResource(nullptr,resource)));
        }
        wchar_t system[MAX_PATH];if(!GetSystemDirectoryW(system,MAX_PATH))throw std::runtime_error("Windows system folder is unavailable.");
        auto powershell=fs::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
        auto quoted=[](const fs::path& p){
            std::wstring out=L"\"";size_t slashes=0;
            for(auto c:p.wstring()){
                if(c==L'\\'){++slashes;continue;}
                out.append(c==L'"'?2*slashes+1:slashes,L'\\');out+=c;slashes=0;
            }
            out.append(2*slashes,L'\\');return out+L"\"";
        };
        auto command=quoted(powershell)+L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+quoted(run/"diag.ps1")
            +L" -DataDir "+quoted(options.data)+L" -ReportFile "+quoted(report)+L" -ResultFile "+quoted(run/"result.txt")+L" -NoOpen";
        if(!gameFolder.empty())command+=L" -GameDir "+quoted(gameFolder);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
            throw std::runtime_error("Windows could not start the diagnostics exporter. Check Windows PowerShell, then try Send diagnostics again.");
        CloseHandle(process.hThread);
        // A bounded wait; the exporter never starts or stops a game. Keep About responsive.
        bool done=false;auto began=GetTickCount64();
        while(GetTickCount64()-began<120000){
            DWORD wait=MsgWaitForMultipleObjects(1,&process.hProcess,FALSE,100,QS_PAINT);
            if(wait==WAIT_OBJECT_0){done=true;break;}
            MSG msg;while(PeekMessageW(&msg,nullptr,WM_PAINT,WM_PAINT,PM_REMOVE))DispatchMessageW(&msg);
        }
        if(!done){keepRun=true;CloseHandle(process.hProcess);throw std::runtime_error("Diagnostics is taking longer than expected. The exporter is still working; check the Desktop for the zip.");}
        DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
        std::string result;{std::ifstream f(run/"result.txt",std::ios::binary);result.assign((std::istreambuf_iterator<char>(f)),{});}
        // (closed here: Windows will not delete an open file, and the run folder was left behind in the data folder)
        if(result.compare(0,3,"\xef\xbb\xbf")==0)result.erase(0,3);
        if(code||result.rfind("OK ",0)!=0)throw std::runtime_error("Diagnostics export failed. Check that the Desktop is writable, then try Send diagnostics again.");
        auto zip=fs::path(wtext(result.substr(3)));cleanup();
        ShellExecuteW(window,L"open",L"explorer.exe",(L"/select,\""+zip.wstring()+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
        return zip;
    }catch(...){cleanup();throw;}
}
void switchTab(int next){selectedTab=next;TabCtrl_SetCurSel(tab,next);if(next==1)loadTestSaves();render();}
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
        bool strandShown=haveStrand&&!net->lobby,ownerShown=strandShown&&owner::available(strandSession,keys);show(Strand,strandShown);show(Owner,ownerShown);
        place(Strand,30,h-182,ownerShown?(w-75)/2:w-60,40);place(Owner,45+(w-75)/2,h-182,(w-75)/2,40);
        place(201,30,230,w-60,28);place(Paste,30,260,w-60,30);place(202,30,298,w-60,25);place(Rows,30,330,w-60,h-480-(strandShown?50:0));
        place(Status,30,h-132,w-60,50);place(Primary,30,h-70,w-60,40);
        bool join=choice==1&&!net->lobby;show(201,join);show(Paste,join);show(202,join);show(Rows,join);show(Primary,!net->lobby||net->hosting);
        set(Primary,primaryLabel());
        EnableWindow(child(Primary),!net->lobby||(net->joined&&net->peerReady));EnableWindow(child(Name),!net->lobby);
        place(203,30,235,w-60,72);place(Invite,30,310,w-60,64);place(HostSave,30,310,w-60,64);place(Copy,30,390,(w-75)/2,36);place(Leave,45+(w-75)/2,390,(w-75)/2,36);place(Ready,30,450,w-60,40);
        show(203,net->lobby);show(Invite,(net->lobby&&net->hosting)||hostingGroup());show(Copy,(net->lobby&&net->hosting)||hostingGroup());show(Leave,net->lobby);show(Ready,net->lobby&&!net->hosting);
        {bool offered=net->lobby&&!net->hosting&&net->joined&&net->incoming.offered&&net->incoming.size;show(HostSave,offered);
         if(offered){set(HostSave,wtext("Start from the host's save: "+(net->incoming.title.empty()?std::string("their newest co-op save"):net->incoming.title)+"\n(your own co-op saves are kept in a backups folder)"));
             SendMessageW(child(HostSave),BM_SETCHECK,net->wantSave?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(child(HostSave),!net->ready&&!net->fetchingSave());}}
        set(203,wtext(std::string(net->hosting?"Friends can join by: same network or Tailscale. Internet needs a VPN.\n":"Your host decides when to start. Choose Ready, then keep this launcher open.\n")+firewallText(firewall)+(net->hosting?"\n"+hostSaveLine():saveNote.empty()?std::string():"\n"+saveNote)));
        EnableWindow(child(Ready),net->joined);set(Ready,net->fetchingSave()?L"Stop":net->ready?L"Not ready":L"Ready");
        if(!net->lobby&&choice!=1){set(203,choice==0?(groupHost?L"Group server (experimental): this PC runs the server; everyone plays their own save and sees the players who are near.\nSend the invite below. Friends paste it under Join a friend, at any time. Allow coop-world.exe in Windows Firewall (TCP and UDP).":L"Open a two-player lobby, copy its invite, then start when your friend is ready."):L"Play by yourself with the separate co-op saves. Your ordinary Steam saves stay separate.");show(203,true);}
        if(hostingGroup()){std::string code;try{code=groupCode();}catch(const std::exception& e){code=e.what();}set(Invite,wtext(code));}
        set(Status,wtext(net->status));refreshRows();
    }
    if(selectedTab==1){int half=(w-75)/2;place(210,30,115,w-60,45);place(211,30,160,w-60,85);place(OpenSaves,30,250,half,36);place(BackUp,45+half,250,half,36);place(Import,30,292,w-60,36);
        place(212,30,342,w-60,25);place(TestList,30,370,half,150);place(213,45+half,370,half,150);place(UseTest,30,530,half,36);place(GetTest,45+half,530,half,36);}
    if(selectedTab==2){place(220,30,115,w-60,55);place(GamePath,30,170,w-60,30);place(221,30,220,280,25);place(Port,320,215,100,30);place(Lan,30,265,w-60,30);place(Tailscale,30,300,w-60,30);place(Privacy,30,335,w-60,30);place(World,30,370,w-60,30);place(GroupHost,30,405,w-60,30);place(BuildReceived,30,440,w-60,46);place(223,30,497,330,25);place(GroupPolicy,370,493,w-400,120);place(224,30,535,w-60,40);place(Protocol,30,580,w-60,36);place(222,30,622,w-60,50);
        bool edit=!net->lobby;for(int id:{GamePath,Port,Lan,Tailscale,Privacy,World,GroupHost,BuildReceived,GroupPolicy})EnableWindow(child(id),edit);}
    if(selectedTab==3){place(230,30,120,w-60,240);place(Guide,30,390,w-60,40);place(Home,30,445,w-60,40);place(Diagnose,30,500,w-60,40);place(Remove,30,555,w-60,40);}
}
void copyInvite(){
    bool group=hostingGroup();auto code=group?groupCode():net->invite();char msg[1024];ci_message(group?utf8(player).c_str():net->session.name.c_str(),"Death Stranding co-op","https://tommodotcommo.github.io/death-stranding-coop/join/",code.c_str(),msg,sizeof(msg));
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
    ci_invite invite{};char name[33],prompt[256];const char* prefix=GroupPrefix;   // a group invite (DSG-...) or a lobby invite (DS-...)
    if(!ci_find_invite(value.data(),value.size(),prefix,&invite,name)){prefix=Prefix;if(!ci_find_invite(value.data(),value.size(),prefix,&invite,name))return;}
    ci_clipboard_prompt(name,prompt,sizeof(prompt));
    if(MessageBoxW(window,wtext(prompt).c_str(),L"Death Stranding co-op invite",MB_YESNO|MB_ICONQUESTION)==IDYES){char code[CI_CODE_MAX];ci_encode(&invite,prefix,code,sizeof(code));set(Paste,wtext(code));choice=1;switchTab(0);scan(true);}
}
void beginJoin(){
    int i=int(SendMessageW(child(Rows),LB_GETCURSEL,0,0));if(i<0||i>=net->hosts.n)throw std::runtime_error("Select a host from the list, or paste an invite and wait for discovery.");
    saveNote.clear();auto& row=net->hosts.r[i];uint32_t secret=row.group==1?requestedSecret:0;net->joinHost(row,chosenSession(),secret);remembered=false;
}
void primary(){
    if(net->lobby){net->start();return;}
    if(choice!=2)readFirewallNow();
    if(choice==0&&groupHost)startGroup();
    else if(choice==0){net->openHost(chosenSession(),privacy);net->offer.offer(ownSave());set(Invite,wtext(net->invite()));}
    else if(choice==2){auto s=chosenSession();s.secret=random32();options.game=gameFolder;launchGame(options,s,true);net->status=options.prepareOnly?"Configuration prepared; no game started.":"Steam launch requested using separate co-op saves.";}
    else {
        auto paste=utf8(text(child(Paste)));
        if(!paste.empty()){
            cr_result r{};bool group=false;int kind=recognise(paste,r,group);
            if(kind==CR_ERROR){char b[256];throw std::runtime_error(ci_error_text(r.error,r.other,b,sizeof(b)));}
            if(group){joinGroup(r.invite);render();return;}   // a group invite: no lobby, the server is the meeting place
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
    if(net->saveArrived)placeHostSave();
    if(net->launch){options.game=gameFolder;launchGame(options,net->session);net->launched();}
    scan();
    std::string state=std::to_string(net->lobby)+std::to_string(net->hosting)+std::to_string(net->joined)+std::to_string(net->ready)+std::to_string(net->peerReady)+std::to_string(net->incoming.offered)+std::to_string(net->fetchingSave())+net->incoming.title+saveNote+net->status;
    if(state!=lastState){diagnostic.Line("STATE "+state);lastState=state;render();}
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    try {
        if(msg==WM_SIZE){render();return 0;}
        if(msg==WM_GETMINMAXINFO){auto p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={800,730};return 0;}
        if(msg==WM_ACTIVATE&&LOWORD(wp)!=WA_INACTIVE&&!initializing){PostMessageW(h,WM_APP+2,0,0);}
        if(msg==WM_APP+2){offerClipboard();return 0;}
        if(msg==WM_TIMER){timer();return 0;}
        if(msg==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==tab&&reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){switchTab(TabCtrl_GetCurSel(tab));scan(true);return 0;}
        if(msg==WM_COMMAND){int id=LOWORD(wp),event=HIWORD(wp);
            if(id==TestList&&event==LBN_SELCHANGE)showTestSave();
            if(id==Name&&event==EN_CHANGE){player=text(child(Name));saveSettings();}
            if(id==GamePath&&event==EN_CHANGE){gameFolder=text(child(GamePath));saveSettings();}
            if(id==Port&&event==EN_CHANGE){auto v=utf8(text(child(Port)));if(!v.empty()&&v.find_first_not_of("0123456789")==std::string::npos&&v.size()==5){auto p=std::stoul(v);if(p>=49000&&p<=49014){port=unsigned(p);saveSettings();}}}
            if(id==GroupPolicy&&event==CBN_SELCHANGE){auto i=SendMessageW(child(GroupPolicy),CB_GETCURSEL,0,0);if(i>=0&&i<3){groupPolicy=PolicyNames[i];saveSettings();}}
            if(id==Paste&&event==EN_CHANGE){auto s=utf8(text(child(Paste)));cr_result r{};bool group=false;int kind=recognise(s,r,group);char desc[256];
                pasteGroup=group&&(kind==CR_INVITE||kind==CR_LINK);
                set(202,pasteGroup?wtext("Group invite: "+std::to_string(r.invite.n)+(r.invite.n==1?" route":" routes")+" to the group's server. Join starts your game there; there is no lobby."):wtext(cr_describe(&r,desc,sizeof(desc))));
                if(!net->lobby)set(Primary,primaryLabel());}
            if(event==BN_CLICKED){
                if(id>=Host&&id<=Solo){choice=id-Host;saveSettings();render();scan(true);}
                if(id==Primary)primary();
                if(id==Strand)strandView();
                if(id==Owner)ownerView();
                if(id==Copy)copyInvite();
                if(id==Leave){net->leave();render();}
                if(id==Ready){net->setReady();render();}
                if(id==HostSave){net->wantSave=SendMessageW(child(id),BM_GETCHECK,0,0)==BST_CHECKED;}
                if(id==Import)importSaves(h);
                if(id==UseTest)useTestSave(h);
                if(id==GetTest)ShellExecuteW(h,L"open",wtext(testsaves::ReleasePage).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
                if(id==Remove)removeSetup(h);
                if(id==Lan||id==Tailscale||id==Privacy||id==World||id==GroupHost||id==BuildReceived){auto checked=SendMessageW(child(id),BM_GETCHECK,0,0)==BST_CHECKED;if(id==Lan)lan=checked;if(id==Tailscale)tailscale=checked;if(id==Privacy)privacy=checked;if(id==World)sharedWorld=checked;if(id==GroupHost)groupHost=checked;if(id==BuildReceived)buildReceived=checked;saveSettings();}
                if(id==Protocol){registerHandler(executable);MessageBoxW(h,L"Invite links now open this launcher for your Windows account.",L"Invite links",MB_OK);}
                if(id==OpenSaves){fs::create_directories(options.data/"redirect-data");ShellExecuteW(h,L"open",(options.data/"redirect-data").c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
                if(id==BackUp){if(!backupAllowed(net->lobby,gameRunning(gameFolder)))throw std::runtime_error("Leave the lobby and close the game before backing up.");auto dest=options.data/"backups"/std::to_string(std::time(nullptr));backup(options.data,dest);MessageBoxW(h,(L"Co-op saves backed up to:\n"+dest.wstring()).c_str(),L"Backup complete",MB_OK);}
                if(id==Guide)MessageBoxW(h,L"Host: Open lobby, copy invite, wait for Ready, Start game.\nJoin: paste an invite or choose a LAN/Tailscale host, Join, Ready.\nGroup (experimental): Settings, Group server; Host starts the server and your game; friends paste the group invite under Join.\nTab moves focus; Enter confirms; Esc returns to Play.\nCo-op saves are separate. Back up with the game closed.\nThis development build exchanges read-only presence.\nNo internet matchmaking, Steam friends, voice or shared story yet.",L"Quick guide",MB_OK);
                if(id==Home)ShellExecuteW(h,L"open",L"https://github.com/TommoDotCommo/death-stranding-coop",nullptr,nullptr,SW_SHOWNORMAL);
                if(id==Diagnose){EnableWindow(child(Diagnose),FALSE);try{auto file=saveDiagnosticsZip();
                    MessageBoxW(h,(L"Saved:\n"+file.wstring()+L"\n\nAttach this zip to a bug report. Nothing was uploaded.").c_str(),L"Diagnostics saved",MB_OK);
                    }catch(...){EnableWindow(child(Diagnose),TRUE);throw;}EnableWindow(child(Diagnose),TRUE);}
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
    label(0,203,L"");control(0,Strand,L"BUTTON",L"My strand on this server");control(0,Owner,L"BUTTON",L"Server owner");control(0,Invite,L"EDIT",L"",ES_READONLY|ES_MULTILINE);control(0,Copy,L"BUTTON",L"Copy invite");control(0,Leave,L"BUTTON",L"Leave lobby");control(0,Ready,L"BUTTON",L"Ready");control(0,HostSave,L"BUTTON",L"",BS_AUTOCHECKBOX|BS_MULTILINE|BS_TOP);
    label(1,210,L"Co-op saves\nOnly the separate co-op save trees are included in backups. Close the game first.");
    label(1,211,(L"Co-op data: "+options.data.wstring()+L"\nSave trees: redirect-data\\local, roaming, documents, savedgames, profile. Backups: backups\\<time>\nYour ordinary game saves are opened only when you choose Copy my Steam saves, and then only read.").c_str());
    control(1,OpenSaves,L"BUTTON",L"Open co-op save folder");control(1,BackUp,L"BUTTON",L"Back up co-op saves");control(1,Import,L"BUTTON",L"Copy my Steam saves into co-op");
    label(1,212,L"Test saves: ready-made co-op saves to start from (a separate download beside each release)");
    control(1,TestList,L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_BORDER);label(1,213,L"");
    control(1,UseTest,L"BUTTON",L"Use this test save");control(1,GetTest,L"BUTTON",L"Get test saves...");
    label(2,220,L"Launcher / game folder (applies next start)\nSteam mode must use the folder registered with Steam.");control(2,GamePath,L"EDIT",gameFolder.c_str(),ES_AUTOHSCROLL);
    label(2,221,L"Network / host UDP port (49000..49014)");control(2,Port,L"EDIT",std::to_wstring(port).c_str(),ES_NUMBER|ES_AUTOHSCROLL);
    control(2,Lan,L"BUTTON",L"Find games on my network",BS_AUTOCHECKBOX);control(2,Tailscale,L"BUTTON",L"Find friends on Tailscale",BS_AUTOCHECKBOX);control(2,Privacy,L"BUTTON",L"Invite only (host decides)",BS_AUTOCHECKBOX);control(2,World,L"BUTTON",L"Shared world (experimental; the host decides): each other's ladders and cairns through the game's own online layer",BS_AUTOCHECKBOX);
    SendMessageW(child(Lan),BM_SETCHECK,lan?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(child(Tailscale),BM_SETCHECK,tailscale?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(child(Privacy),BM_SETCHECK,privacy?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(child(World),BM_SETCHECK,sharedWorld?BST_CHECKED:BST_UNCHECKED,0);
    control(2,GroupHost,L"BUTTON",L"Group server (experimental): Host a game starts a server for many players, each in their own save, instead of a two-player lobby",BS_AUTOCHECKBOX);SendMessageW(child(GroupHost),BM_SETCHECK,groupHost?BST_CHECKED:BST_UNCHECKED,0);
    control(2,BuildReceived,L"BUTTON",L"Build other players' structures when they arrive (group servers). They skip the game's spacing and area checks, so one may overlap something in your world",BS_AUTOCHECKBOX|BS_MULTILINE|BS_TOP);
    SendMessageW(child(BuildReceived),BM_SETCHECK,buildReceived?BST_CHECKED:BST_UNCHECKED,0);
    label(2,223,L"Rules of a new group world:");control(2,GroupPolicy,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL);
    for(auto name:{L"Friends: everything shared",L"Solo archive: one player",L"Public: members, review, limits"})SendMessageW(child(GroupPolicy),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    for(int i=0;i<3;++i)if(groupPolicy==PolicyNames[i])SendMessageW(child(GroupPolicy),CB_SETCURSEL,i,0);
    label(2,224,wtext("Your player key (made by this launcher; its secret half never leaves this PC): "+world::identity::shortText(keys.pub)+". On group servers that take keys it is you, and the host's key is the owner's.").c_str());
    control(2,Protocol,L"BUTTON",L"Open deathstranding-coop invite links in this launcher");label(2,222,L"Settings are saved on change. Each game uses two adjacent UDP ports. A group world that exists keeps its own rules.\nCo-op networking is always enforced offline. Voice, controller navigation and router mapping are not available yet.");
    label(3,230,(L"Death Stranding co-op "+wtext(COOP_VERSION)+L"\nSteam Director's Cut build 13300582\n\nExperimental; not a playable release. Native Windows launcher.\nCo-op mod standard: Play, Saves, Settings, About; DS invites and LAN/Tailscale discovery.\n\nInvite/discovery reference code: coop-mod-kit (MIT).\nNot affiliated with Kojima Productions or the game's publishers.\n\nWire version: "+wtext(wireVersion())).c_str());
    control(3,Guide,L"BUTTON",L"Quick guide (F1)");control(3,Home,L"BUTTON",L"Project and issue tracker");control(3,Diagnose,L"BUTTON",L"Send diagnostics");control(3,Remove,L"BUTTON",L"Remove co-op from the game");
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    struct EndLog {~EndLog(){diagnostic.Close();}} endLog;
    try {
        {int n=0;auto v=CommandLineToArgvW(GetCommandLineW(),&n);   // Setup's verbs: no window, no network
         if(v&&n>=2&&std::wstring(v[1])==L"--setup"){auto c=setupVerb(n,v);LocalFree(v);return c;}
         if(v)LocalFree(v);}
        SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);WSADATA ws;if(WSAStartup(MAKEWORD(2,2),&ws))return 1;net=std::make_unique<Network>();
        options.data=known(FOLDERID_LocalAppData)/"DeathStrandingCoop";settingsFile=options.data/"launcher.ini";
        auto maxMb=GetPrivateProfileIntW(L"launcher",L"max_log_mb",diag::DefaultMaxLogMb,settingsFile.c_str());
        if(!diag::ValidMaxLogMb(maxMb))maxMb=diag::DefaultMaxLogMb;
        if(diagnostic.Open(diag::DefaultLogsDir(),"launcher",maxMb))diag::crash::Install(diagnostic);
        auto read=[&](const wchar_t* key,const wchar_t* fallback){wchar_t b[32768];GetPrivateProfileStringW(L"launcher",key,fallback,b,32768,settingsFile.c_str());return std::wstring(b);};
        player=read(L"name",L"Porter");gameFolder=read(L"game",steamGame().c_str());
        choice=int(GetPrivateProfileIntW(L"launcher",L"choice",0,settingsFile.c_str()));if(choice<0||choice>2)choice=0;
        port=GetPrivateProfileIntW(L"launcher",L"port",49000,settingsFile.c_str());if(port<49000||port>49014)port=49000;
        lan=GetPrivateProfileIntW(L"launcher",L"lan",1,settingsFile.c_str());tailscale=GetPrivateProfileIntW(L"launcher",L"tailscale",1,settingsFile.c_str());privacy=GetPrivateProfileIntW(L"launcher",L"privacy",0,settingsFile.c_str());sharedWorld=GetPrivateProfileIntW(L"launcher",L"world",0,settingsFile.c_str())!=0;groupHost=GetPrivateProfileIntW(L"launcher",L"group",0,settingsFile.c_str())!=0;
        buildReceived=GetPrivateProfileIntW(L"launcher",L"build_received",1,settingsFile.c_str())!=0;   // on unless turned off
        {auto kept=utf8(read(L"group_policy",L"friends"));for(auto name:PolicyNames)if(kept==name)groupPolicy=name;}
        std::string listTests,useTest;bool listTestsOnly=false;   // --list-test-saves, --use-test-save ID
        bool registerOnly=false,devStart=false,diagnosticsOnly=false,diagnosticsZipOnly=false,firewallOnly=false,forceWorld=false,devSessionGiven=false;std::string devPeer="127.0.0.1",groupTarget;uint32_t devSession=1;fs::path takeSave;
        int groupMode=0;   // --group-host (1), --group-join INVITE-OR-ADDRESS (2)
        strand::Choices given;std::string strandReport;bool policyGiven=false;   // --strand-builds, --strand-old, --strand-policy FILE|-; --group-policy
        owner::Action ownerAction;std::string ownerReport;   // --owner-... (one action) and --owner-report FILE|-: the server owner's calls without the window
        std::string ownerError;   // reported in --owner-report, never in a dialog
        auto ownerKind=[&](const char* kind){if(!ownerAction.kind.empty()&&ownerAction.kind!=kind)ownerError="One --owner-... action at a time.";ownerAction.kind=kind;};
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Cannot read launcher arguments.");
        for(int i=1;i<argc;++i){std::wstring a=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing launcher option value.");return std::wstring(argv[i]);};
            if(a==L"--dev-direct")options.direct=true;
            else if(a==L"--game-dir")gameFolder=value();
            else if(a==L"--data-dir")options.data=value();
            else if(a==L"--prepare-only")options.prepareOnly=true;
            else if(a==L"--shared-world")forceWorld=true;
            else if(a==L"--register-protocol")registerOnly=true;
            else if(a==L"--diagnostics")diagnosticsOnly=true;
            else if(a==L"--diagnostics-zip")diagnosticsZipOnly=true;
            else if(a==L"--firewall-status")firewallOnly=true;
            else if(a==L"--list-test-saves")listTestsOnly=true;
            else if(a==L"--use-test-save")useTest=utf8(value());
            else if(a==L"--test-saves-pack")testPackGiven=value();
            else if(a==L"--dev-start")devStart=true;
            else if(a==L"--dev-peer")devPeer=utf8(value());
            else if(a==L"--dev-session"){auto v=utf8(value());if(v.empty()||v.size()>10||v.find_first_not_of("0123456789")!=std::string::npos||std::stoull(v)>UINT32_MAX)throw std::runtime_error("Invalid development session.");devSession=uint32_t(std::stoull(v));devSessionGiven=true;}
            else if(a==L"--group-host")groupMode=1;
            else if(a==L"--group-join"){groupMode=2;groupTarget=utf8(value());}
            else if(a==L"--dev-client")choice=1;
            else if(a==L"--strand-builds")given.offerBuilds=utf8(value());
            else if(a==L"--strand-old")given.offerOld=utf8(value());
            else if(a==L"--strand-policy")strandReport=utf8(value());
            else if(a==L"--group-policy"){auto v=utf8(value());bool known=false;for(auto name:PolicyNames)known|=v==name;if(!known)throw std::runtime_error("--group-policy is friends, solo or public.");groupPolicy=v;policyGiven=true;}
            else if(a==L"--dev-take-save")takeSave=value();
            else if(a==L"--owner-list")ownerKind("list");
            else if(a==L"--owner-member"){ownerKind("member");ownerAction.target=utf8(value());ownerAction.value=utf8(value());}
            else if(a==L"--owner-remove"){ownerKind("remove");ownerAction.target=utf8(value());}
            else if(a==L"--owner-accept"){ownerKind("accept");ownerAction.target=utf8(value());}
            else if(a==L"--owner-reject"){ownerKind("reject");ownerAction.target=utf8(value());}
            else if(a==L"--owner-accept-player"){ownerKind("accept-player");ownerAction.target=utf8(value());}
            else if(a==L"--owner-reject-player"){ownerKind("reject-player");ownerAction.target=utf8(value());}
            else if(a==L"--owner-preset"){ownerKind("policy");ownerAction.preset=utf8(value());}
            else if(a==L"--owner-set"){ownerKind("policy");ownerAction.sets.push_back(utf8(value()));}
            else if(a==L"--owner-report")ownerReport=utf8(value());
            else if(a==L"--port"){auto raw=utf8(value());if(raw.size()!=5||raw.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Host port must be 49000..49014.");auto v=std::stoul(raw);if(v<49000||v>49014)throw std::runtime_error("Host port must be 49000..49014.");port=unsigned(v);}
            else if(a.rfind(L"deathstranding-coop://",0)==0){pasted=utf8(a);choice=1;}
            else throw std::runtime_error("Unknown launcher option.");
        }LocalFree(argv);
        if(forceWorld)sharedWorld=true;
        if(groupMode==1){groupHost=true;choice=0;}
        if(groupMode==2){pasted=groupTarget;choice=1;}
        auto defaultData=known(FOLDERID_LocalAppData)/"DeathStrandingCoop";
        if(!options.direct&&!equalPath(options.data,defaultData))throw std::runtime_error("Custom data folders require --dev-direct.");
        if(devStart&&!options.direct)throw std::runtime_error("--dev-start requires --dev-direct.");
        if((!given.offerBuilds.empty()||!given.offerOld.empty()||!strandReport.empty())&&!(devStart&&groupMode==2))throw std::runtime_error("--strand-builds, --strand-old and --strand-policy go with --dev-direct --dev-start --group-join.");
        if(policyGiven&&!(devStart&&groupMode==1))throw std::runtime_error("--group-policy goes with --dev-direct --dev-start --group-host.");
        if((!ownerAction.kind.empty()||!ownerReport.empty())&&ownerError.empty()&&(!options.direct||devStart||groupMode||ownerAction.kind.empty()||ownerReport.empty()))
            ownerError="--owner-... is one action with --owner-report FILE (or -) and --dev-direct, without --dev-start.";
        // Headless owner calls: whatever goes wrong is in the report (exit 3), never a dialog nobody is there to close.
        auto ownerRefused=[&](const std::string& why){
            if(ownerReport.empty())throw std::runtime_error(why);
            json::Value r=json::Value::object();r.set("action",ownerAction.kind);r.set("ok",false);r.set("message",why);writeReport(ownerReport,r.dump()+"\n");return 3;};
        if(!ownerError.empty())return ownerRefused(ownerError);
        if(options.direct&&equalPath(options.data,defaultData)&&!environment(L"COOP_MODULE_INSTANCE").empty())options.data=environment(L"COOP_MODULE_INSTANCE");
        wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);executable=self;options.world=executable.parent_path()/L"coop-world.exe";
        if(registerOnly){registerHandler(executable);return 0;}
        // Headless: write the report and exit (no window), for a player who cannot get as far as the About tab.
        readFirewallNow();
        // Headless: the firewall sentence to standard output (when redirected) and the exit code (the verdict's number).
        if(firewallOnly){std::string line=std::string(firewallText(firewall))+"\n";DWORD n=0;HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);if(out&&out!=INVALID_HANDLE_VALUE)WriteFile(out,line.data(),DWORD(line.size()),&n,nullptr);return int(firewall);}
        if(diagnosticsZipOnly){options.game=gameFolder;saveDiagnosticsZip();return 0;}
        if(diagnosticsOnly){options.game=gameFolder;saveDiagnostics();return 0;}
        // Headless test saves (testsaves.h): the list, or one set installed; the message to standard output (when
        // redirected) and the exit code: 0 done, 1 failed, 3 pack missing or damaged, 4 the game is running.
        if(listTestsOnly||!useTest.empty()){
            auto o=listTestsOnly?testsaves::listCommand(testPackPath())
                :testsaves::useCommand(testPackPath(),useTest,options.data,known(FOLDERID_LocalAppData),steamAccount(),backupAllowed(false,gameRunning(gameFolder)),std::time(nullptr));
            auto line=o.message+(o.message.empty()||o.message.back()=='\n'?"":"\n");DWORD n=0;HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);
            if(out&&out!=INVALID_HANDLE_VALUE)WriteFile(out,line.data(),DWORD(line.size()),&n,nullptr);
            return o.code;
        }
        std::ifstream recents(options.data/"recents.tsv",std::ios::binary);char store[CD_RECENT_BYTES+1];recents.read(store,sizeof(store));auto count=recents.gcount();if(count<=CD_RECENT_BYTES)cd_recents_decode(&net->recents,store,size_t(count));
        settingsFile=options.data/"launcher.ini";
        options.game=gameFolder;
        if(!takeSave.empty()) {   // test path for "start from the host's save": the same install the lobby does, from a file
            if(!options.direct)throw std::runtime_error("--dev-take-save requires --dev-direct.");
            std::ifstream f(takeSave,std::ios::binary);if(!f)throw std::runtime_error("Cannot read the save to take.");
            std::string bytes((std::istreambuf_iterator<char>(f)),{});
            saves::install(coopAccount(),steamAccount(),bytes,options.data/"backups"/("before-join-"+std::to_string(std::time(nullptr))));
            if(!devStart)return 0;
        }
        if(!ownerAction.kind.empty()){try{identity();}catch(const std::exception& e){return ownerRefused(e.what());}}   // a damaged player.key, reported
        else identity();
        if(!ownerAction.kind.empty()) {   // the server owner's call on the group server this launcher hosts, without the window
            Session s=chosenSession();s.group=Group::Host;s.ownerKey=world::identity::hex(keys.pub);
            if(!devSessionGiven&&!groupSecret)return ownerRefused("This launcher has hosted no group server yet: give its key with --dev-session N.");
            s.secret=devSessionGiven?devSession:groupSecret;
            json::Value report;
            try{auto t=targetOf(s);report=owner::run(t,strand::transport(t),ownerAction);}catch(const std::exception& e){return ownerRefused(e.what());}
            writeReport(ownerReport,report.dump()+"\n");
            return report.get("ok").boolean()?0:3;   // refused: no dialog
        }
        if(devStart&&groupMode) {   // group play without the window: host the server, or join one by invite or by address
            Session s=chosenSession();
            if(groupMode==1){s.group=Group::Host;s.secret=devSessionGiven?devSession:groupKey();s.ownerKey=world::identity::hex(keys.pub);launchGame(options,s,false,hostIdentity);return 0;}
            else {
                cr_result r{};bool group=false;int kind=recognise(groupTarget,r,group);
                if(group&&(kind==CR_INVITE||kind==CR_LINK))s=groupJoin(r.invite,groupRoute(r.invite,probeServers(r.invite)),s);
                else if(!group&&kind==CR_ADDRESS&&!r.code[0]) {   // ADDRESS[:SERVER-PORT] with --dev-session as the group's key; not probed
                    ci_invite v{};v.secret=devSession;v.port=uint16_t(r.port?r.port:worldPort(s));v.n=1;v.route[0].kind=CI_LAN;memcpy(v.route[0].ip,r.ip,4);
                    s=groupJoin(v,0,s);
                } else throw std::runtime_error("--group-join needs a group invite (DSG-...) or the server's numeric address.");
                // The server's rules and this player's choices before the game connects, without a window: the
                // remembered choices with the flags over them, sent (also with --prepare-only), and a report.
                auto t=targetOf(s);auto before=readIni();auto j=strand::join(t,strand::transport(t),before,given);
                if(j.ini!=before)atomicWrite(settingsFile,j.ini);
                if(!strandReport.empty()){
                    const char* names[]={"ok","old","wrong-key","unreachable","bad"};
                    json::Value report=json::Value::object();report.set("fetched",names[int(j.fetched)]);report.set("start",j.start);report.set("message",j.message);report.set("account",j.target.account);report.set("keyed",j.keyed);
                    json::Value chosen=json::Value::object();chosen.set("offer_builds",j.choices.offerBuilds);chosen.set("offer_old",j.choices.offerOld);report.set("choices",chosen);
                    json::Value policy;try{if(!j.raw.empty())policy=json::parse(j.raw);}catch(const std::exception&){}
                    report.set("policy",policy);writeReport(strandReport,report.dump()+"\n");}
                if(!j.start)return 3;   // refused: no game, no dialog
                if(j.keyed)strand::usePasses(s,j.passes);
            }
            launchGame(options,s);return 0;
        }
        if(devStart){Session s=chosenSession();s.host=choice!=1;s.peer=devPeer;s.secret=devSession;launchGame(options,s);return 0;}
        INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TAB_CLASSES};InitCommonControlsEx(&cc);
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");titleFont=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"DeathStrandingCoopLauncher";cls.lpfnWndProc=procedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
        window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Death Stranding co-op",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,960,740,nullptr,nullptr,instance,nullptr);if(!window)throw std::runtime_error("Cannot create launcher window.");
        buildControls();if(!pasted.empty())set(Paste,wtext(pasted));initializing=false;render();ShowWindow(window,showCommand);SetTimer(window,1,100,nullptr);scan(true);
        try{offerSetup(window);}catch(const std::exception& e){error(e);}
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){
            try{
                if(msg.message==WM_KEYDOWN&&msg.wParam==VK_F1){SendMessageW(window,WM_COMMAND,Guide,0);continue;}
                if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){if(net->lobby&&MessageBoxW(window,L"Leave this lobby?",L"Leave co-op",MB_YESNO)!=IDYES)continue;if(net->lobby)SendMessageW(window,WM_COMMAND,Leave,0);switchTab(0);continue;}
                if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN){auto focus=GetFocus();if(selectedTab==0&&(focus==child(Name)||focus==child(Paste)||focus==child(Rows))){primary();continue;}}
                if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            }catch(const std::exception& e){error(e);}
        }
        net.reset();DeleteObject(font);DeleteObject(titleFont);WSACleanup();CoUninitialize();return 0;
    }catch(const std::exception& e){diagnostic.Line(std::string("fatal: ")+e.what());MessageBoxW(nullptr,wtext(e.what()).c_str(),L"Death Stranding co-op",MB_OK|MB_ICONERROR);return 1;}
}
