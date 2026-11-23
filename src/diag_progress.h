#pragma once

/*
 * OLIMEX_DIAG: on-screen progress marker.
 *
 * diag_progress(stage) paints a 16-pixel bar on framebuffer line 0:
 *   - pixels 0..stage-1 = index 15 (white, completed)
 *   - pixel  stage      = index (stage+8) & 0xF (distinct color per stage)
 *   - pixels stage+1..15 = index 0 (black, pending)
 *
 * Implemented in src/main.c. Without OLIMEX_DIAG every call site compiles to
 * a no-op, so diagnostics can be turned off with a pure build-config change.
 */

#if OLIMEX_DIAG
void diag_progress(int stage);
#else
static inline void diag_progress(int stage) { (void)stage; }
#endif
