#pragma once
#include <stdint.h>
#ifdef _WIN32
#define MOD_API __declspec(dllexport)
#else
#define MOD_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Call init/shutdown outside the loader lock. tick belongs to ONE game thread.
   dt is seconds; the adapter uses a monotonic clock for wire timestamps.
   init takes an absolute config path. One mod instance per process. */
MOD_API int mod_init(const char* config_path);
MOD_API int mod_tick(double dt);
MOD_API void mod_shutdown(void);
/* Copies a NUL-terminated JSON status, returns required bytes including NUL. */
MOD_API unsigned mod_status(char* output, unsigned capacity);
MOD_API const char* mod_last_error(void);
#ifdef __cplusplus
}
#endif
