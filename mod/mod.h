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
/* Optional, every game frame (the tick runs at most every 50 ms): reads only, never calls the game, never waits; 0 when
   it did nothing. Lets the mod see one-frame values (the porter's animation pulses) the tick would miss. */
MOD_API int mod_game_thread_frame(const mod_game_thread_context* context);
MOD_API void mod_shutdown(void);
/* Scripted virtual gamepad for player 1 (XInput gamepad layout), polled by the proxy's XInputGetState wrapper
   from the game's input thread and by its raw-input feed from the game thread. Never blocks. Returns 1 while a pad script is active, 0 otherwise, -1 on a
   wrong size. Only when the config enables virtual_pad. */
typedef struct mod_pad_state {
    uint32_t size;
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t lx, ly, rx, ry;
    /* Scripted keyboard and mouse for the proxy's raw-input feed: virtual keys held, mouse buttons (1 L, 2 R, 4 M)
       and mouse speed in counts per second. */
    uint8_t keys[8];
    uint8_t key_count, mouse_buttons;
    int16_t mouse_x, mouse_y;
    /* Mouse wheel: notches to turn once (120 each), and which script step asked (it turns once per new value). */
    int8_t mouse_wheel;
    uint8_t wheel_seq;
} mod_pad_state;
MOD_API int mod_pad(mod_pad_state* state);
/* Copies a NUL-terminated JSON status, returns required bytes including NUL. */
MOD_API unsigned mod_status(char* output, unsigned capacity);
MOD_API const char* mod_last_error(void);
#ifdef __cplusplus
}
#endif
