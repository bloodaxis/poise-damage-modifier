#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "../shared/mod_settings_api.h"
#include "../shared/game_scan.h"
#include "../shared/mod_common.h"

namespace {
constexpr uint32_t effect_ids[2]{100620,100621};
constexpr uintptr_t sa_attack_power_rate_offset=820;
constexpr uint32_t effect_row_size=912;
HMODULE self=nullptr;
// Address of the SoloParamRepository pointer, resolved by signature.
uintptr_t param_repository=0;
wchar_t ini_path[32768]{};
SRWLOCK rate_lock=SRWLOCK_INIT;
uintptr_t rate_addresses[2]{};
float configured_rate=1;

mod::Log logger;
void log(const char* message){logger.line("%s",message);}
using mod::read;
bool write_rate(uintptr_t address,float value){
    // The caller has validated this address as saAttackPowerRate in one of the
    // two intended SpEffectParam rows.
    return mod::write(address,value);
}
bool resolve(const game_scan::Module& game){
    param_repository=game_scan::param_repository_from(
        game_scan::unique(game,game_scan::param_repository_pattern()));
    return param_repository!=0;
}
bool supported_game(){
    game_scan::Module game;
    return game_scan::open_game(game)&&resolve(game);
}
// saAttackPowerRate of rows 100620/100621; both or neither.
bool find_effect_rates(uintptr_t (&addresses)[2]){
    addresses[0]=addresses[1]=0;
    mod::ParamTable table;
    if(mod::find_param_table(param_repository,L"SpEffectParam","SP_EFFECT_PARAM_ST",effect_row_size,table)!=mod::Probe::found)
        return false;
    for(int i=0;i<2;++i){
        const uintptr_t row=mod::param_row(table,effect_ids[i]);
        float current=0;
        if(!row||!read(row+sa_attack_power_rate_offset,current)||!std::isfinite(current)||current<0||current>100)return false;
        addresses[i]=row+sa_attack_power_rate_offset;
    }
    return true;
}
bool valid_rate(double value){return std::isfinite(value)&&value>=0&&value<=100;}
bool write_both(const uintptr_t (&addresses)[2],float value,float (&previous)[2]){
    if(!read(addresses[0],previous[0])||!read(addresses[1],previous[1]))return false;
    if(!write_rate(addresses[0],value))return false;
    if(write_rate(addresses[1],value))return true;
    write_rate(addresses[0],previous[0]);
    return false;
}
int MMS_CALL apply_menu_rate(void*,double value){
    if(!valid_rate(value))return 0;
    AcquireSRWLockExclusive(&rate_lock);
    uintptr_t addresses[2]{rate_addresses[0],rate_addresses[1]};
    if((!addresses[0]||!addresses[1])&&!find_effect_rates(addresses)){
        ReleaseSRWLockExclusive(&rate_lock);log("Menu save failed: game data unavailable");return 0;
    }
    float previous[2]{};
    float next=static_cast<float>(value);
    wchar_t text[64]{};swprintf(text,64,L"%.9g",value);
    if(!write_both(addresses,next,previous)){
        ReleaseSRWLockExclusive(&rate_lock);log("Menu save failed: game data write rejected");return 0;
    }
    if(!WritePrivateProfileStringW(L"PoiseDamage",L"Multiplier",text,ini_path)){
        write_rate(addresses[0],previous[0]);write_rate(addresses[1],previous[1]);
        ReleaseSRWLockExclusive(&rate_lock);log("Menu save failed: INI write rejected");return 0;
    }
    configured_rate=next;rate_addresses[0]=addresses[0];rate_addresses[1]=addresses[1];
    ReleaseSRWLockExclusive(&rate_lock);
    log("Setting saved and applied");
    return 1;
}
DWORD WINAPI connect_menu(void*){
    mod::Menu menu;
    if(!mod::wait_for_menu(menu)){log("Mod Settings absent; INI setting remains active");return 0;}
    MMS_Setting setting{sizeof(MMS_Setting),"poise-damage-modifier","multiplier",
        L"Poise Damage",L"Multiplies the poise damage by this value.",
        0,100,1,configured_rate,apply_menu_rate,nullptr};
    if(mod::register_setting(menu,setting))mod::set_mod_title(menu,"poise-damage-modifier",L"Poise Damage Modifier");
    else log("Mod Settings registration failed");
    return 0;
}
DWORD WINAPI apply_at_launch(void*){
    for(unsigned attempt=0;attempt<600;++attempt){
        AcquireSRWLockExclusive(&rate_lock);
        uintptr_t addresses[2]{};
        if(find_effect_rates(addresses)){
            float original[2]{};
            bool ok=write_both(addresses,configured_rate,original);
            if(ok){rate_addresses[0]=addresses[0];rate_addresses[1]=addresses[1];}
            ReleaseSRWLockExclusive(&rate_lock);
            if(ok)log("Setting applied");
            else log("Game data write failed");
            return 0;
        }
        ReleaseSRWLockExclusive(&rate_lock);
        Sleep(200);
    }
    log("Game data unavailable after startup wait");
    return 0;
}
DWORD WINAPI start(void*){
    if(!mod::companion_path(self,L".ini",ini_path)||!logger.open(self))return 0;
    log("Poise Damage Modifier 2.1 started");
    double value=1;
    if(!mod::read_number(ini_path,L"PoiseDamage",L"Multiplier",L"1",0,100,value)){
        log("Invalid Multiplier: expected a finite value from 0 to 100");return 0;
    }
    configured_rate=static_cast<float>(value);
    if(!GetPrivateProfileIntW(L"PoiseDamage",L"Enabled",1,ini_path)){
        log("Disabled; PARAM untouched");return 0;
    }
    if(!supported_game()){
        log("SoloParamRepository signature missing or ambiguous; PARAM untouched");return 0;
    }
    HANDLE write_thread=CreateThread(nullptr,0,apply_at_launch,nullptr,0,nullptr);
    if(write_thread)CloseHandle(write_thread);
    else log("Could not start PARAM initialization thread");
    HANDLE menu_thread=CreateThread(nullptr,0,connect_menu,nullptr,0,nullptr);
    if(menu_thread)CloseHandle(menu_thread);
    else log("Could not start Mod Settings registration thread");
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        self=module;DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,start,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    }
    return TRUE;
}
