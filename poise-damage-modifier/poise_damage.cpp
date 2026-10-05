#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "../mod-settings-menu/mod_settings_api.h"

namespace {
constexpr uint32_t effect_ids[2]{100620,100621};
constexpr uintptr_t sa_attack_power_rate_offset=820;
constexpr uint32_t effect_row_size=912;
HMODULE self=nullptr;
uintptr_t game=0;
wchar_t ini_path[32768]{},log_path[32768]{};
SRWLOCK rate_lock=SRWLOCK_INIT;
SRWLOCK log_lock=SRWLOCK_INIT;
uintptr_t rate_addresses[2]{};
float configured_rate=1;

void log(const char* message){
    AcquireSRWLockExclusive(&log_lock);
    if(FILE* f=_wfopen(log_path,L"a")){
        std::fprintf(f,"%s\n",message);
        std::fclose(f);
    }
    ReleaseSRWLockExclusive(&log_lock);
}
bool read_bytes(uintptr_t address,void* out,size_t size){
    // This DLL runs inside Elden Ring. Probe only its own process so a stale
    // game pointer fails cleanly instead of causing an access violation.
    SIZE_T done=0;
    return address&&size<=UINTPTR_MAX-address&&
        ReadProcessMemory(GetCurrentProcess(),(void*)address,out,size,&done)&&done==size;
}
template<class T>bool read(uintptr_t address,T& value){return read_bytes(address,&value,sizeof(value));}
bool write_rate(uintptr_t address,float value){
    // The caller has validated this address as saAttackPowerRate in one of the
    // two intended SpEffectParam rows. No other process is opened or written.
    SIZE_T done=0;
    return address&&WriteProcessMemory(GetCurrentProcess(),(void*)address,&value,sizeof(value),&done)&&done==sizeof(value);
}
bool replace_extension(wchar_t* path,const wchar_t* extension){
    wchar_t* slash=wcsrchr(path,L'\\');
    wchar_t* dot=wcsrchr(slash?slash+1:path,L'.');
    if(!dot)dot=path+wcslen(path);
    if(size_t(dot-path)+wcslen(extension)>=32768)return false;
    wcscpy(dot,extension);return true;
}
bool supported_game(){
    game=uintptr_t(GetModuleHandleW(L"eldenring.exe"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    return read(game,dos)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=0&&
        dos.e_lfanew<0x1000&&read(game+dos.e_lfanew,nt)&&nt.Signature==IMAGE_NT_SIGNATURE&&
        nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        nt.FileHeader.TimeDateStamp==0x6a96b418&&nt.OptionalHeader.SizeOfImage==0x5e0da00;
}
bool find_effect_rates(uintptr_t (&addresses)[2]){
    addresses[0]=addresses[1]=0;
    // Verified executable's SoloParamRepository pointer. Layout and table
    // offsets follow Erd-Tools-CPP ParamEditor and the 912-byte ER SpEffect row.
    constexpr unsigned char signature[]{0x48,0x8b,0x0d,0xf5,0x5e,0x05,0x03,0x48,0x85,0xc9};
    unsigned char actual[sizeof(signature)]{};
    if(!read_bytes(game+0xd3005c,actual,sizeof(actual))||memcmp(actual,signature,sizeof(signature)))return false;
    uintptr_t repository=0;
    if(!read(game+0x3d85f58,repository)||!repository)return false;
    for(unsigned i=0;i<186;++i){
        uintptr_t entry=repository+0x80+i*0x48,resource=0,name_ptr=0,info=0,header=0;
        uint8_t loaded=0;
        if(!read(entry,loaded)||!loaded||!read(entry+8,resource)||!resource||
           !read(resource+0x18,name_ptr)||!name_ptr)continue;
        wchar_t name[sizeof(L"SpEffectParam")/sizeof(wchar_t)]{};
        if(!read_bytes(name_ptr,name,sizeof(name))||wcscmp(name,L"SpEffectParam"))continue;
        if(!read(resource+0x80,info)||!info||!read(info+0x80,header)||!header)continue;
        uint16_t count=0;uint64_t type_offset=0;uint32_t data_offset=0;
        if(!read(header+0x0a,count)||count<2||count>60000||
           !read(header+0x10,type_offset)||!read(header+0x30,data_offset)||
           type_offset<=data_offset||type_offset>64*1024*1024||
           data_offset<0x40+uint64_t(count)*0x18||
           type_offset-data_offset<uint64_t(count)*effect_row_size)return false;
        char type[sizeof("SP_EFFECT_PARAM_ST")]{};
        if(!read_bytes(header+type_offset,type,sizeof(type))||strcmp(type,"SP_EFFECT_PARAM_ST"))return false;
        uint32_t first=0,second=0;
        if(!read(header+0x48,first)||!read(header+0x60,second)||
           first<data_offset||second-first!=effect_row_size)return false;
        // The table is sorted in these PARAMs, but a bounded linear lookup
        // avoids depending on that property. Menu saves use cached addresses.
        for(uint32_t row=0;row<count;++row){
            uintptr_t record=header+0x40+uintptr_t(row)*0x18;
            uint32_t id=0,offset=0;
            if(!read(record,id)||!read(record+8,offset))return false;
            int index=id==effect_ids[0]?0:id==effect_ids[1]?1:-1;
            if(index<0)continue;
            if(addresses[index]||offset<data_offset||uint64_t(offset)+effect_row_size>type_offset)return false;
            float current=0;
            uintptr_t address=header+offset+sa_attack_power_rate_offset;
            if(!read(address,current)||!std::isfinite(current)||current<0||current>100)return false;
            addresses[index]=address;
            if(addresses[0]&&addresses[1])return true;
        }
        return false;
    }
    return false;
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
    const ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<60000){
        HMODULE host=GetModuleHandleW(L"mod-settings-menu.dll");
        auto get=host?reinterpret_cast<MMS_GetAPI>(GetProcAddress(host,"ModSettings_GetAPI")):nullptr;
        const MMS_API2* api=get?reinterpret_cast<const MMS_API2*>(get(MMS_API_VERSION_2)):nullptr;
        if(api&&api->base.size==sizeof(MMS_API2)&&api->base.version==MMS_API_VERSION_2){
            MMS_Setting setting{sizeof(MMS_Setting),"poise-damage-modifier","multiplier",
                L"Poise Damage",L"Multiplies the poise damage by this value.",
                0,100,1,configured_rate,apply_menu_rate,nullptr};
            if(api->base.register_setting(&setting))api->set_mod_title("poise-damage-modifier",L"Poise Damage Modifier");
            else log("Mod Settings registration failed");
            return 0;
        }
        Sleep(1000);
    }
    log("Mod Settings absent; INI setting remains active");
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
    DWORD length=GetModuleFileNameW(self,ini_path,32768);
    if(!length||length>=32760)return 0;
    wcscpy(log_path,ini_path);
    if(!replace_extension(ini_path,L".ini")||!replace_extension(log_path,L".log"))return 0;
    if(FILE* f=_wfopen(log_path,L"w")){
        std::fprintf(f,"Poise Damage Modifier started\n");std::fclose(f);
    }
    wchar_t text[128]{};
    GetPrivateProfileStringW(L"PoiseDamage",L"Multiplier",L"1",text,128,ini_path);
    wchar_t* end=nullptr;double value=wcstod(text,&end);
    while(end&&(*end==L' '||*end==L'\t'))++end;
    if(end==text||!end||*end||!valid_rate(value)){
        log("Invalid Multiplier: expected a finite value from 0 to 100");return 0;
    }
    configured_rate=static_cast<float>(value);
    if(!GetPrivateProfileIntW(L"PoiseDamage",L"Enabled",1,ini_path)){
        log("Disabled; PARAM untouched");return 0;
    }
    if(!supported_game()){
        log("Unsupported executable profile; PARAM untouched");return 0;
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
