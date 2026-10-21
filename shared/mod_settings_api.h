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
#define MMS_SETTING_NUMERIC_ENTRY 1u
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
    uint32_t (MMS_CALL *register_setting)(const MMS_Setting* setting);
    int (MMS_CALL *get_value)(uint32_t handle,double* out);
    int (MMS_CALL *request_value)(uint32_t handle,double value);
} MMS_API;
typedef struct MMS_API2 {
    MMS_API base;
    int (MMS_CALL *set_mod_title)(const char* mod_id, const wchar_t* title);
} MMS_API2;
typedef struct MMS_Setting3 {
    MMS_Setting base;
    uint32_t flags;
} MMS_Setting3;
typedef struct MMS_API3 {
    MMS_API2 base;
    uint32_t (MMS_CALL *register_setting3)(const MMS_Setting3* setting);
    int (MMS_CALL *request_exact_value)(uint32_t handle,double value);
} MMS_API3;
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
