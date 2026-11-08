#pragma once
// Version-independent address resolution for eldenring.exe.
// Header-only; shared by the -perf DLL mods. Author: Bloodaxis.
//
// Patterns are hex bytes separated by spaces; "??" is a wildcard. Every
// rel32 branch target and RIP-relative displacement is a wildcard, so only
// instruction encodings, struct offsets and constants have to stay the same.
// A pattern must match exactly once, otherwise resolution fails and the mod
// stays disabled instead of patching the wrong code.
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace game_scan {

struct Module { uintptr_t base=0,end=0; };

struct Pattern {
    unsigned char bytes[256]{};
    bool fixed[256]{};
    unsigned length=0;
    unsigned anchor=0; // first fixed byte
};

inline bool parse(const char* text,Pattern& p) {
    p=Pattern{};
    while (*text) {
        if (*text==' ') { ++text; continue; }
        if (p.length>=sizeof(p.bytes)) return false;
        if (text[0]=='?') { p.length++; text+=text[1]=='?'?2:1; continue; }
        auto nibble=[](char c)->int{
            if (c>='0'&&c<='9') return c-'0';
            if (c>='a'&&c<='f') return c-'a'+10;
            if (c>='A'&&c<='F') return c-'A'+10;
            return -1;
        };
        const int high=nibble(text[0]),low=high<0?-1:nibble(text[1]);
        if (low<0) return false;
        p.bytes[p.length]=static_cast<unsigned char>(high*16+low);
        p.fixed[p.length++]=true; text+=2;
    }
    while (p.anchor<p.length && !p.fixed[p.anchor]) ++p.anchor;
    return p.anchor<p.length;
}

inline bool readable(uintptr_t address,size_t size) {
    uintptr_t cursor=address;
    if (!cursor || size>UINTPTR_MAX-cursor) return false;
    while (cursor<address+size) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof(info)) ||
            info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        const uintptr_t next=uintptr_t(info.BaseAddress)+info.RegionSize;
        if (next<=cursor) return false;
        cursor=next;
    }
    return true;
}

inline bool executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) ||
        info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD)) return false;
    const DWORD p=info.Protect&0xff;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}

// Validates the PE headers only; no timestamp or size is required.
inline bool open(Module& m,uintptr_t base) {
    m=Module{};
    if (!base || !readable(base,sizeof(IMAGE_DOS_HEADER))) return false;
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x1000 ||
        !readable(base+dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64))) return false;
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections>96) return false;
    m.base=base; m.end=base+nt->OptionalHeader.SizeOfImage;
    return true;
}
inline bool open_game(Module& m) {
    return open(m,uintptr_t(GetModuleHandleW(L"eldenring.exe")));
}

template<class F> void for_each_section(const Module& m,DWORD required,DWORD excluded,F f) {
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(m.base);
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(m.base+dos->e_lfanew);
    auto section=IMAGE_FIRST_SECTION(nt);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        const DWORD c=section[i].Characteristics;
        if ((c&required)!=required || (c&excluded)) continue;
        const uintptr_t begin=m.base+section[i].VirtualAddress;
        const size_t size=section[i].Misc.VirtualSize;
        if (begin<m.base || begin>m.end || size>m.end-begin) continue;
        // Visit committed, readable runs only.
        uintptr_t cursor=begin;
        while (cursor<begin+size) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof(info))) break;
            uintptr_t stop=uintptr_t(info.BaseAddress)+info.RegionSize;
            if (stop<=cursor) break;
            if (stop>begin+size) stop=begin+size;
            if (info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))
                f(cursor,size_t(stop-cursor));
            cursor=stop;
        }
    }
}

// One request per pattern. After scan(), result is the unique match (the
// pattern's first byte) or 0; count is the number of matches (capped at 2).
struct Request {
    const char* pattern;
    uintptr_t result=0;
    unsigned count=0;
    Pattern parsed{};
    bool valid=false;
};

inline bool equal_at(const unsigned char* p,const Pattern& pattern) {
    for (unsigned k=0;k<pattern.length;++k)
        if (pattern.fixed[k] && p[k]!=pattern.bytes[k]) return false;
    return true;
}

// Searches [begin,begin+size) for every request in one pass.
inline void scan_range(uintptr_t begin,size_t size,Request* requests,size_t n) {
    // Bucket requests by their first fixed byte; each byte checks one bucket.
    int head[256]; int next[64];
    if (n>64) return;
    for (int& h:head) h=-1;
    for (size_t i=0;i<n;++i) {
        if (!requests[i].valid) continue;
        const unsigned char key=requests[i].parsed.bytes[requests[i].parsed.anchor];
        next[i]=head[key]; head[key]=int(i);
    }
    auto data=reinterpret_cast<const unsigned char*>(begin);
    if (n==1 && requests[0].valid) {
        // Single pattern: memchr between candidates of its first fixed byte.
        auto& r=requests[0]; const Pattern& p=r.parsed;
        if (size<p.length) return;
        size_t offset=p.anchor;
        const size_t last=size-p.length+p.anchor;
        while (offset<=last) {
            auto hit=static_cast<const unsigned char*>(std::memchr(data+offset,p.bytes[p.anchor],last-offset+1));
            if (!hit) break;
            offset=size_t(hit-data);
            if (equal_at(hit-p.anchor,p)) {
                if (r.count<2) ++r.count;
                r.result=r.count==1?begin+offset-p.anchor:0;
            }
            ++offset;
        }
        return;
    }
    for (size_t offset=0;offset<size;++offset) {
        for (int i=head[data[offset]];i>=0;i=next[i]) {
            auto& r=requests[i]; const Pattern& p=r.parsed;
            if (offset<p.anchor || size-(offset-p.anchor)<p.length) continue;
            if (!equal_at(data+offset-p.anchor,p)) continue;
            if (r.count<2) ++r.count;
            r.result=r.count==1?begin+offset-p.anchor:0;
        }
    }
}

// Scans every executable image section once for all requests.
inline void scan(const Module& m,Request* requests,size_t n) {
    for (size_t i=0;i<n;++i) {
        requests[i].result=0; requests[i].count=0;
        requests[i].valid=parse(requests[i].pattern,requests[i].parsed);
    }
    if (!m.base) return;
    for_each_section(m,IMAGE_SCN_MEM_EXECUTE,0,[&](uintptr_t begin,size_t size){
        scan_range(begin,size,requests,n);
    });
    for (size_t i=0;i<n;++i) if (requests[i].count!=1) requests[i].result=0;
}

inline uintptr_t unique(const Module& m,const char* pattern) {
    Request r{pattern}; scan(m,&r,1); return r.result;
}

// Calls f(address) for every match in the executable sections, for patterns
// that are only unique together with a further check (e.g. template twins).
template<class F> void for_each_match(const Module& m,const char* pattern,F f) {
    Pattern p;
    if (!m.base || !parse(pattern,p)) return;
    for_each_section(m,IMAGE_SCN_MEM_EXECUTE,0,[&](uintptr_t begin,size_t size){
        if (size<p.length) return;
        auto data=reinterpret_cast<const unsigned char*>(begin);
        size_t offset=p.anchor;
        const size_t last=size-p.length+p.anchor;
        while (offset<=last) {
            auto hit=static_cast<const unsigned char*>(std::memchr(data+offset,p.bytes[p.anchor],last-offset+1));
            if (!hit) break;
            offset=size_t(hit-data);
            if (equal_at(hit-p.anchor,p)) f(begin+offset-p.anchor);
            ++offset;
        }
    });
}

// Unique match inside one already resolved function or code range.
inline uintptr_t unique_in(uintptr_t begin,size_t size,const char* pattern) {
    Request r{pattern};
    r.valid=parse(pattern,r.parsed);
    if (!r.valid || !readable(begin,size)) return 0;
    scan_range(begin,size,&r,1);
    return r.count==1?r.result:0;
}

inline int32_t rel32(uintptr_t field) { int32_t v; std::memcpy(&v,reinterpret_cast<void*>(field),4); return v; }
// Target of a rel32 operand stored at field, for an instruction ending at end.
inline uintptr_t relative(uintptr_t field,uintptr_t end) { return field?end+rel32(field):0; }
// Target of an E8 call or E9 jmp at address.
inline uintptr_t call_target(uintptr_t address) {
    if (!address) return 0;
    const unsigned char op=*reinterpret_cast<const unsigned char*>(address);
    return op==0xe8||op==0xe9?relative(address+1,address+5):0;
}

// Destination of an existing entry detour written by another mod.
inline uintptr_t jump_destination(uintptr_t p) {
    if (!readable(p,16)) return 0;
    auto b=reinterpret_cast<const unsigned char*>(p);
    if (b[0]==0xe9) return relative(p+1,p+5);
    if (b[0]==0xff&&b[1]==0x25) {
        const uintptr_t slot=relative(p+2,p+6); uintptr_t v=0;
        if (readable(slot,8)) std::memcpy(&v,reinterpret_cast<void*>(slot),8);
        return v;
    }
    if (b[0]==0x48&&b[1]==0xb8&&b[10]==0xff&&b[11]==0xe0) { uintptr_t v; std::memcpy(&v,b+2,8); return v; }
    return 0;
}

// Function lookup that tolerates another mod's entry detour: the pattern is
// tried as given, then with its first 16 and then 32 bytes as wildcards (hook
// libraries pad the rest of the instruction their jump overlaps), in which
// case the entry must jump to executable memory. Patterns that should survive
// a 32-byte detour need unique bytes after offset 32. One pass per stage.
struct Function {
    const char* pattern;
    uintptr_t address=0;
    bool detoured=false;
};
inline bool resolve_functions(const Module& m,Function* functions,size_t n,bool allow_detour) {
    if (n>32) return false;
    Request requests[32];
    for (size_t i=0;i<n;++i) requests[i]=Request{functions[i].pattern};
    scan(m,requests,n);
    size_t retry[32],retries=0;
    for (size_t i=0;i<n;++i) {
        functions[i].address=requests[i].result; functions[i].detoured=false;
        if (!functions[i].address) retry[retries++]=i;
    }
    static constexpr unsigned spans[]{16,32};
    for (unsigned span:spans) {
        if (!retries || !allow_detour) break;
        char masked[32][800];
        Request second[32];
        for (size_t k=0;k<retries;++k) {
            const char* text=functions[retry[k]].pattern; char* out=masked[k]; size_t w=0; unsigned tokens=0;
            while (*text && w+4<sizeof(masked[k])) {
                while (*text==' ') ++text;
                if (!*text) break;
                const char* start=text; while (*text && *text!=' ') ++text;
                if (w) out[w++]=' ';
                if (tokens<span) { out[w++]='?'; out[w++]='?'; }
                else for (const char* c=start;c<text && w+2<sizeof(masked[k]);++c) out[w++]=*c;
                ++tokens;
            }
            out[w]=0; second[k]=Request{out};
        }
        scan(m,second,retries);
        size_t remaining=0;
        for (size_t k=0;k<retries;++k) {
            const uintptr_t entry=second[k].result;
            const uintptr_t destination=entry?jump_destination(entry):0;
            if (destination && executable(destination)) {
                functions[retry[k]].address=entry; functions[retry[k]].detoured=true;
            } else retry[remaining++]=retry[k];
        }
        retries=remaining;
    }
    bool all=true;
    for (size_t i=0;i<n;++i) all=all&&functions[i].address;
    return all;
}

// Primary vtable of a class from its MSVC RTTI name, e.g. ".?AVCSThrowNode@CS@@".
inline uintptr_t rtti_vtable(const Module& m,const char* mangled) {
    const size_t length=std::strlen(mangled)+1;
    uintptr_t descriptor=0; unsigned descriptors=0;
    for_each_section(m,IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_EXECUTE,[&](uintptr_t begin,size_t size){
        auto data=reinterpret_cast<const unsigned char*>(begin);
        for (size_t o=16;o+length<=size;o+=8) {
            // TypeDescriptor: vftable, spare, then the name at +0x10 (8-aligned).
            if (data[o]=='.' && !std::memcmp(data+o,mangled,length)) { descriptor=begin+o-16; ++descriptors; }
        }
    });
    if (descriptors!=1) return 0;
    const uint32_t descriptor_rva=uint32_t(descriptor-m.base);
    uintptr_t locator=0; unsigned locators=0;
    for_each_section(m,IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_WRITE|IMAGE_SCN_MEM_EXECUTE,[&](uintptr_t begin,size_t size){
        for (size_t o=0;o+24<=size;o+=4) {
            uint32_t col[6]; std::memcpy(col,reinterpret_cast<void*>(begin+o),sizeof(col));
            // CompleteObjectLocator: signature 1, offset 0, type, hierarchy, self.
            if (col[0]==1 && col[1]==0 && col[3]==descriptor_rva && col[5]==uint32_t(begin+o-m.base)) {
                locator=begin+o; ++locators;
            }
        }
    });
    if (locators!=1) return 0;
    uintptr_t vtable=0; unsigned vtables=0;
    for_each_section(m,IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_WRITE|IMAGE_SCN_MEM_EXECUTE,[&](uintptr_t begin,size_t size){
        for (size_t o=0;o+16<=size;o+=8) {
            uintptr_t v; std::memcpy(&v,reinterpret_cast<void*>(begin+o),8);
            if (v==locator) { vtable=begin+o+8; ++vtables; }
        }
    });
    return vtables==1?vtable:0;
}

// WorldChrMan global and the local-player field offset, read from
//   mov rax,[rip+WorldChrMan]; test rax,rax; je; cmp [rax+player],rcx
struct WorldChr { uintptr_t global=0; uint32_t player_offset=0; };
inline const char* world_chr_pattern() { return "48 8b 05 ?? ?? ?? ?? 48 85 c0 74 0f 48 39 88 ?? ?? ?? ??"; }
inline bool world_chr_from(uintptr_t site,WorldChr& out) {
    out=WorldChr{};
    if (!site) return false;
    out.global=relative(site+3,site+7);
    int32_t offset=rel32(site+15);
    if (offset<=0 || offset>0x100000) return false;
    out.player_offset=uint32_t(offset);
    return out.global!=0;
}

// SoloParamRepository global:
//   mov rcx,[rip+repo]; test rcx,rcx; je; xor r8d,r8d; mov edx,0x90
inline const char* param_repository_pattern() {
    return "48 8b 0d ?? ?? ?? ?? 48 85 c9 0f 84 ?? ?? ?? ?? 45 33 c0 ba 90 00 00 00";
}
inline uintptr_t param_repository_from(uintptr_t site) { return site?relative(site+3,site+7):0; }

} // namespace game_scan
