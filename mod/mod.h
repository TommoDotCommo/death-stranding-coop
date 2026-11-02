#pragma once
#include <stdint.h>
#if defined(MOD_INTERFACE_ONLY)
#define MOD_API
#elif defined(_WIN32)
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
/* Trusted proxy callback, versioned and checked again by the mod. Caller is the
   reviewed application return RVA, not an address supplied by a network peer.
   Returns 1=ticked, 0=skipped (busy/gated), -1=failure. Never unload the DLL while
   the proxy retains this callback; the proxy keeps its load for process lifetime. */
typedef struct mod_game_thread_context {
    uint32_t size, version, thread_id, depth;
    uint64_t caller_rva;
    uint32_t network_enforced, reserved;
} mod_game_thread_context;
MOD_API int mod_game_thread_tick(const mod_game_thread_context* context);
MOD_API void mod_shutdown(void);
/* Copies a NUL-terminated JSON status, returns required bytes including NUL. */
MOD_API unsigned mod_status(char* output, unsigned capacity);
MOD_API const char* mod_last_error(void);
#ifdef __cplusplus
}
#endif
