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
