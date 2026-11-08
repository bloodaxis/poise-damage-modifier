#pragma once
// Shared helpers for the Elden Ring DLL mods: companion files (.ini/.log next
// to the DLL), INI number parsing, a thread-safe log, checked in-process
// memory access, SoloParamRepository table lookup and the optional Mod
// Settings Menu connection. Header-only. Author: Bloodaxis.
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "mod_settings_api.h"

namespace mod {
constexpr size_t path_capacity=32768;

// Replaces the extension of the file name in path (or appends one).
inline bool replace_extension(wchar_t* path,const wchar_t* extension) {
    wchar_t* slash=std::wcsrchr(path,L'\\');
    wchar_t* dot=std::wcsrchr(slash?slash+1:path,L'.');
    if (!dot) dot=path+std::wcslen(path);
    if (size_t(dot-path)+std::wcslen(extension)>=path_capacity) return false;
    std::wcscpy(dot,extension); return true;
}
// Path of the file next to this DLL with the DLL's name and the given extension.
inline bool companion_path(HMODULE module,const wchar_t* extension,wchar_t* out) {
    const DWORD length=GetModuleFileNameW(module,out,DWORD(path_capacity));
    return length && length<path_capacity-64 && replace_extension(out,extension);
}

// A finite number from minimum through maximum, optionally followed by blanks.
inline bool parse_number(const wchar_t* text,double minimum,double maximum,double& out) {
    wchar_t* end=nullptr;
    const double value=std::wcstod(text,&end);
    while (end && (*end==L' ' || *end==L'\t')) ++end;
    if (!end || end==text || *end || !std::isfinite(value) || value<minimum || value>maximum) return false;
    out=value; return true;
}
// False when the key holds something other than a valid number.
inline bool read_number(const wchar_t* ini,const wchar_t* section,const wchar_t* key,const wchar_t* fallback,
                        double minimum,double maximum,double& out) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section,key,fallback,text,64,ini);
    return parse_number(text,minimum,maximum,out);
}
inline bool write_number(const wchar_t* ini,const wchar_t* section,const wchar_t* key,double value) {
    wchar_t text[64]; std::swprintf(text,64,L"%.9g",value);
    return WritePrivateProfileStringW(section,key,text,ini)!=0;
}
// 1 is on; anything else is off.
inline bool read_switch(const wchar_t* ini,const wchar_t* section,const wchar_t* key,bool fallback) {
    return GetPrivateProfileIntW(section,key,fallback?1:0,ini)==1;
}
inline bool write_switch(const wchar_t* ini,const wchar_t* section,const wchar_t* key,bool on) {
    return WritePrivateProfileStringW(section,key,on?L"1":L"0",ini)!=0;
}

// Log file next to the DLL. open() truncates it; line() appends one line and
// may be called from any thread. Each line opens and closes the file, so
// keep it to startup, menu changes and failures.
class Log {
public:
    bool open(HMODULE module) {
        if (!companion_path(module,L".log",path_)) return false;
        if (FILE* file=_wfopen(path_,L"w")) std::fclose(file);
        return true;
    }
    // For a log whose path was derived elsewhere (e.g. from the INI path).
    bool open_path(const wchar_t* ini_path) {
        if (std::wcslen(ini_path)>=path_capacity) return false;
        std::wcscpy(path_,ini_path);
        if (!replace_extension(path_,L".log")) return false;
        if (FILE* file=_wfopen(path_,L"w")) std::fclose(file);
        return true;
    }
    void line(const char* format,...) {
        if (!path_[0]) return;
        AcquireSRWLockExclusive(&lock_);
        if (FILE* file=_wfopen(path_,L"a")) {
            va_list args; va_start(args,format);
            std::vfprintf(file,format,args);
            va_end(args);
            std::fputc('\n',file);
            std::fclose(file);
        }
        ReleaseSRWLockExclusive(&lock_);
    }
private:
    wchar_t path_[path_capacity]{};
    SRWLOCK lock_=SRWLOCK_INIT;
};

// Checked in-process reads/writes, for pointers that may be stale (outside
// the game's own callbacks). A bad address fails instead of crashing.
inline bool read_bytes(uintptr_t address,void* out,size_t size) {
    SIZE_T done=0;
    return address && size<=UINTPTR_MAX-address &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&done) && done==size;
}
template<class T> bool read(uintptr_t address,T& value) { return read_bytes(address,&value,sizeof(value)); }
inline bool write_bytes(uintptr_t address,const void* data,size_t size) {
    SIZE_T done=0;
    return address && WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),data,size,&done) && done==size;
}
template<class T> bool write(uintptr_t address,const T& value) { return write_bytes(address,&value,sizeof(value)); }

// SoloParamRepository tables. The repository global comes from
// game_scan::param_repository_pattern(). Layout follows Erd-Tools-CPP's
// ParamEditor: 186 entries of 0x48 bytes from +0x80, each with a loaded flag
// and a resource (+0x18 name, +0x80 -> +0x80 PARAM header). The header has
// the row count at +0x0a, the type string offset at +0x10, the data offset
// at +0x30 and 0x18-byte row records from +0x40.
enum class Probe { waiting, invalid, found };
struct ParamTable { uintptr_t header=0; uint16_t count=0; uint32_t data_offset=0; uint64_t type_offset=0; uint32_t row_size=0; };
// waiting: params not loaded yet; invalid: the table's layout does not match
// (never write then). With two or more rows the stride must equal row_size;
// a single-row table must have at least row_size bytes of data.
inline Probe find_param_table(uintptr_t repository_global,const wchar_t* name,const char* type,
                              uint32_t row_size,ParamTable& table) {
    uintptr_t repository=0;
    if (!repository_global) return Probe::invalid;
    if (!read(repository_global,repository) || !repository) return Probe::waiting;
    const size_t name_size=(std::wcslen(name)+1)*sizeof(wchar_t),type_size=std::strlen(type)+1;
    if (name_size>64*sizeof(wchar_t) || type_size>64) return Probe::invalid;
    for (unsigned i=0;i<186;++i) {
        const uintptr_t entry=repository+0x80+i*0x48;
        uintptr_t resource=0,name_pointer=0,info=0,header=0;
        uint8_t loaded=0;
        if (!read(entry,loaded) || !loaded || !read(entry+8,resource) || !resource ||
            !read(resource+0x18,name_pointer) || !name_pointer) continue;
        wchar_t found[64]{};
        if (!read_bytes(name_pointer,found,name_size) || std::wcscmp(found,name)) continue;
        if (!read(resource+0x80,info) || !info || !read(info+0x80,header) || !header) return Probe::waiting;
        uint16_t count=0; uint64_t type_offset=0; uint32_t data_offset=0;
        if (!read(header+0x0a,count) || count<1 || count>60000 ||
            !read(header+0x10,type_offset) || !read(header+0x30,data_offset) ||
            type_offset<=data_offset || type_offset>64*1024*1024 ||
            data_offset<0x40+uint64_t(count)*0x18 ||
            type_offset-data_offset<uint64_t(count)*row_size) return Probe::invalid;
        char found_type[64]{};
        if (!read_bytes(header+type_offset,found_type,type_size) || std::strcmp(found_type,type)) return Probe::invalid;
        uint32_t first=0,second=0;
        if (!read(header+0x48,first) || first<data_offset) return Probe::invalid;
        if (count>=2 && (!read(header+0x60,second) || second-first!=row_size)) return Probe::invalid;
        table=ParamTable{header,count,data_offset,type_offset,row_size};
        return Probe::found;
    }
    return Probe::waiting;
}
// Address of row id, or 0. Copies the record table once (no syscall per row).
// Records are {u32 id, pad, u32 data offset, pad, u32 name offset, pad}.
inline uintptr_t param_row(const ParamTable& table,uint32_t id) {
    const size_t size=size_t(table.count)*0x18;
    auto* records=static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(),0,size));
    if (!records) return 0;
    uintptr_t result=0;
    if (read_bytes(table.header+0x40,records,size)) {
        for (uint32_t row=0;row<table.count;++row) {
            uint32_t row_id=0,offset=0;
            std::memcpy(&row_id,records+size_t(row)*0x18,4);
            std::memcpy(&offset,records+size_t(row)*0x18+8,4);
            if (row_id!=id) continue;
            if (offset>=table.data_offset && uint64_t(offset)+table.row_size<=table.type_offset) result=table.header+offset;
            break;
        }
    }
    HeapFree(GetProcessHeap(),0,records);
    return result;
}

// Optional Mod Settings Menu. wait_for_menu() checks once a second for up to
// timeout_ms and then gives up (the mod keeps its INI values). Each newer
// API table is offered only when the host reports exactly that version.
struct Menu {
    const MMS_API* v1=nullptr;
    const MMS_API2* v2=nullptr;
    const MMS_API3* v3=nullptr;
    const MMS_API4* v4=nullptr;
};
inline bool find_menu(Menu& menu) {
    menu=Menu{};
    HMODULE host=GetModuleHandleW(L"mod-settings-menu.dll");
    auto get=host?reinterpret_cast<MMS_GetAPI>(GetProcAddress(host,"ModSettings_GetAPI")):nullptr;
    if (!get) return false;
    const MMS_API* v1=get(MMS_API_VERSION);
    if (!v1 || v1->size!=sizeof(MMS_API) || v1->version!=MMS_API_VERSION) return false;
    menu.v1=v1;
    auto v2=reinterpret_cast<const MMS_API2*>(get(MMS_API_VERSION_2));
    if (v2 && v2->base.size==sizeof(MMS_API2) && v2->base.version==MMS_API_VERSION_2) menu.v2=v2;
    auto v3=reinterpret_cast<const MMS_API3*>(get(MMS_API_VERSION_3));
    if (v3 && v3->base.base.size==sizeof(MMS_API3) && v3->base.base.version==MMS_API_VERSION_3) menu.v3=v3;
    auto v4=reinterpret_cast<const MMS_API4*>(get(MMS_API_VERSION_4));
    if (v4 && v4->base.base.base.size==sizeof(MMS_API4) && v4->base.base.base.version==MMS_API_VERSION_4) menu.v4=v4;
    return true;
}
inline bool wait_for_menu(Menu& menu,DWORD timeout_ms=60000) {
    const ULONGLONG started=GetTickCount64();
    for (;;) {
        if (find_menu(menu)) return true;
        if (GetTickCount64()-started>=timeout_ms) return false;
        Sleep(1000);
    }
}
// declared=true registers through v4 (a mod that declares its layout should
// declare every setting); otherwise v3 for numeric entry, else v1, which is
// how the host has always laid these settings out.
struct Layout { bool declared=false; const char* page_id=nullptr; const wchar_t* page_title=nullptr;
                uint32_t flags=0; const wchar_t* minimum_label=nullptr; };
inline uint32_t register_setting(const Menu& menu,const MMS_Setting& setting,const Layout& layout={}) {
    if (!menu.v1) return 0;
    if (layout.declared && menu.v4) {
        MMS_Setting4 declared{{setting,layout.flags},layout.page_id,layout.page_title,layout.minimum_label};
        return menu.v4->register_setting4(&declared);
    }
    if (menu.v3 && (layout.flags&MMS_SETTING_NUMERIC_ENTRY)) {
        MMS_Setting3 numeric{setting,MMS_SETTING_NUMERIC_ENTRY};
        return menu.v3->register_setting3(&numeric);
    }
    return menu.v1->register_setting(&setting);
}
inline bool set_mod_title(const Menu& menu,const char* mod_id,const wchar_t* title) {
    return menu.v2 && menu.v2->set_mod_title(mod_id,title);
}
} // namespace mod
