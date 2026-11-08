#pragma once
#include <stdint.h>
#include <wchar.h>
#ifdef _WIN32
#define MMS_CALL __cdecl
#else
#define MMS_CALL
#endif
#define MMS_API_VERSION 1u
#define MMS_API_VERSION_2 2u
#define MMS_API_VERSION_3 3u
#define MMS_API_VERSION_4 4u
#define MMS_SETTING_NUMERIC_ENTRY 1u
// v4 layout flags (register_setting4 only).
#define MMS_SETTING_INDENTED 2u     // caption indented one level, as a sub-option
#define MMS_SETTING_BELOW_PAGES 4u  // main page only: listed after the page buttons
// Windows x64 C ABI. Call from a worker, NEVER from DllMain.
// apply runs on the host worker while Mod Settings is open (plus one final
// flush on close), must not throw, and returns 1 on success.
// The client owns validation, applying the value and persistence.
// Callback module is pinned; context must remain alive until process exit.
// Strings are copied at registration. No live-unload support in v1.
typedef int (MMS_CALL *MMS_Apply)(void* context, double value);
typedef struct MMS_Setting {
    uint32_t size;
    const char* mod_id;
    const char* setting_id;
    const wchar_t* label;
    const wchar_t* help;
    double minimum, maximum, step, initial;
    MMS_Apply apply;
    void* context;
} MMS_Setting;
typedef struct MMS_API {
    uint32_t size, version;
    // Returns stable nonzero handle, zero on invalid/duplicate/full registration.
    uint32_t (MMS_CALL *register_setting)(const MMS_Setting* setting);
    // Returns 1 and the last accepted value, 0 on invalid handle.
    int (MMS_CALL *get_value)(uint32_t handle,double* out);
    // Queue a change, including callback/persistence. Latest request wins.
    int (MMS_CALL *request_value)(uint32_t handle,double value);
} MMS_API;
// Optional extension. The v1 prefix remains unchanged for existing DLLs.
typedef struct MMS_API2 {
    MMS_API base;
    // Call after registering at least one setting for mod_id. Title is copied.
    int (MMS_CALL *set_mod_title)(const char* mod_id, const wchar_t* title);
} MMS_API2;
// Optional extension for sliders that also allow exact in-menu number entry.
// The slider itself remains limited to 255 positions; numeric entry does not
// round its value to a slider position. Existing v1/v2 clients are unaffected.
typedef struct MMS_Setting3 {
    MMS_Setting base;
    uint32_t flags;
} MMS_Setting3;
typedef struct MMS_API3 {
    MMS_API2 base;
    uint32_t (MMS_CALL *register_setting3)(const MMS_Setting3* setting);
    int (MMS_CALL *request_exact_value)(uint32_t handle,double value);
} MMS_API3;
// Optional extension: a setting declares where it is shown. Settings with a
// page_id are listed on that page, opened by a button on the mod's main page;
// buttons follow the order in which each page first appears. The first
// setting registered for a page supplies its title. minimum_label, when set,
// is shown instead of the number while the value equals the minimum (for
// example L"Use global"). Settings registered through v1-v3 keep the host's
// existing layout.
typedef struct MMS_Setting4 {
    MMS_Setting3 base;
    const char* page_id;          // NULL or "" for the main page; < 64 bytes
    const wchar_t* page_title;    // required with page_id; < 128 characters
    const wchar_t* minimum_label; // optional; < 32 characters
} MMS_Setting4;
typedef struct MMS_API4 {
    MMS_API3 base;
    uint32_t (MMS_CALL *register_setting4)(const MMS_Setting4* setting);
} MMS_API4;
typedef const MMS_API* (MMS_CALL *MMS_GetAPI)(uint32_t version);
#ifdef __cplusplus
extern "C" {
#endif
#ifdef MMS_EXPORTS
__declspec(dllexport)
#endif
const MMS_API* MMS_CALL ModSettings_GetAPI(uint32_t version);
#ifdef __cplusplus
}
#endif
